# CLAUDE.md — PIVX-V6-KHU Development Context

**Last Sync:** 2025-11-27
**Documents Synchronized:** SPEC.md, ARCHITECTURE.md, ROADMAP.md, IMPLEMENTATION.md, blueprints/*

---

## Role & Context

Tu es un **développeur C++ senior** travaillant sur le projet PIVX-V6-KHU.
Tu reçois les consignes de ChatGPT (architecte) et tu exécutes, mais tu restes **force de proposition en cas d'incertitude**.

**Organisation documentation:**
- Tous les documents sont dans `/docs` à la racine
- JAMAIS de fichiers dans le dossier `/PIVX/` (sauf code C++)

---

## 0. DÉFINITION KHU

**KHU = Knowledge Hedge Unit**

KHU est un **colored coin collatéralisé 1:1** par PIV.
- ❌ Ce n'est PAS un stablecoin (pas de peg USD/EUR)
- ✅ C'est une unité de couverture tokenisée avec staking privé et yield gouverné

---

## 1. INVARIANTS SACRÉS (IMMUABLES)

Ces invariants doivent être vérifiés **à chaque commit**:

**Z est stocké dans KhuGlobalState** et mis à jour atomiquement par STAKE/UNSTAKE.

```cpp
C == U + Z                // collateral == supply transparent + shielded
Cr == Ur                  // reward pool == reward rights (toujours)
T >= 0                    // DAO Treasury (Phase 6)
```

### Opérations Autorisées

| Opération | Effet sur C/U/Z | Effet sur Cr/Ur |
|-----------|-----------------|-----------------|
| MINT      | C+, U+          | -               |
| REDEEM    | C-, U-          | -               |
| STAKE     | U-, Z+          | -               |
| DAILY_YIELD | -             | Cr+, Ur+        |
| UNSTAKE   | C+, U+, Z-      | Cr-, Ur-        |

**UNSTAKE = Double Flux Atomique:**
```cpp
// Soit: P = principal (note.amount), Y = yield accumulé (note.accumulatedYield)
state.Z  -= P;       // (1) Principal retiré du shielded
state.U  += P + Y;   // (2) Principal + Yield vers transparent
state.C  += Y;       // (3) Yield ajoute au collateral (inflation)
state.Cr -= Y;       // (4) Yield consommé du pool
state.Ur -= Y;       // (5) Yield consommé des droits
// Ces 5 lignes doivent être adjacentes, AUCUN code entre elles
```

---

## 2. PIPELINE KHU

```
FLUX COMPLET (avec yield):
PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV

FLUX SIMPLE (sans yield):
PIV → MINT → KHU_T → [trade] → REDEEM → PIV
```

**Chaque opération est INDÉPENDANTE (pas de séquence obligatoire):**

- **MINT**: Lock PIV, créer KHU_T (1:1) — instantané
- **REDEEM**: Burn KHU_T, unlock PIV (1:1) — instantané
- **STAKE**: KHU_T → ZKHU (note Sapling) — optionnel, pour yield R%
- **UNSTAKE**: ZKHU → KHU_T + yield (Y) — après 4320 blocs maturity

> ⚠️ STAKE/UNSTAKE sont OPTIONNELS. On peut MINT/REDEEM en boucle sans jamais staker.

---

## 2.1 FRAIS DE TRANSACTION (RÈGLE CANONIQUE)

**Tous les frais KHU sont payés en PIV non-bloqué.**

```
✅ Système classique PIVX: fees en PIV → mineurs
✅ Aucun impact sur C/U/Cr/Ur (invariants préservés)
✅ PIV utilisé pour fees = PIV libre du wallet (pas le collateral)

❌ JAMAIS de fees en KHU
❌ JAMAIS de fees prélevés sur le collateral C
```

**Implémentation:**
- Chaque tx KHU (MINT, REDEEM, STAKE, UNSTAKE) inclut un input PIV séparé pour les fees
- Le change PIV retourne au wallet comme PIV normal
- Le change KHU retourne au wallet comme KHU_T

**Exemple STAKE:**
```
Inputs:  10 KHU_T + 0.001 PIV (pour fee)
Outputs: 5 ZKHU (staked) + 5 KHU_T (change) + ~0.00085 PIV (change)
Fee:     ~0.00015 PIV (au mineur)
```

---

## 3. ORDRE CONNECTBLOCK (CANONIQUE)

**L'ordre d'exécution dans ConnectBlock est IMMUABLE:**

```cpp
bool ConnectBlock(...) {
    LOCK2(cs_main, cs_khu);  // (0) Lock order: cs_main PUIS cs_khu

    KhuGlobalState prevState = LoadKhuState(pindex->pprev);
    KhuGlobalState newState = prevState;

    // (1) Apply daily updates (T accumulation + Yield) — UNIFIED
    ApplyDailyUpdatesIfNeeded(newState, nHeight);

    // (2) Process KHU transactions (MINT, REDEEM, STAKE, UNSTAKE, DOMC)
    for (const auto& tx : block.vtx) {
        if (!ProcessKHUTransaction(tx, newState, view, state))
            return false;  // Stop immediately
    }

    // (3) Apply block reward (emission)
    ApplyBlockReward(newState, nHeight);

    // (4) Check invariants (MANDATORY)
    if (!newState.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // (5) Persist state
    pKHUStateDB->WriteKHUState(newState);
}
```

**Note:** T accumulation (~5%/an at R=40%) et Yield (R%) sont unifiés dans `ApplyDailyUpdatesIfNeeded()` — tous les 1440 blocs.

**CRITIQUE: Daily updates AVANT transactions (jamais l'inverse)**

---

## 4. LOCK ORDER

```cpp
// CORRECT
LOCK2(cs_main, cs_khu);

// INTERDIT (deadlock)
LOCK2(cs_khu, cs_main);
```

Toutes les fonctions qui modifient C/U/Cr/Ur doivent:
```cpp
AssertLockHeld(cs_khu);  // En première ligne
```

---

## 5. TRANSACTION TYPES

```cpp
enum TxType : int16_t {
    // PIVX standard
    NORMAL = 0,
    PROREG = 1,
    PROUPSERV = 2,
    PROUPREG = 3,
    PROUPREV = 4,
    LLMQCOMM = 5,

    // KHU types (Phase 2-6)
    KHU_MINT = 6,           // PIV → KHU_T
    KHU_REDEEM = 7,         // KHU_T → PIV
    KHU_STAKE = 8,          // KHU_T → ZKHU
    KHU_UNSTAKE = 9,        // ZKHU → KHU_T + yield
    KHU_DOMC_COMMIT = 10,   // DOMC commit (Hash(R || salt))
    KHU_DOMC_REVEAL = 11,   // DOMC reveal (R + salt)
};
```

---

## 6. CONSTANTES CANONIQUES

```cpp
// Timing
const uint32_t BLOCKS_PER_DAY = 1440;           // ~1 minute blocks
const uint32_t BLOCKS_PER_YEAR = 525600;
const uint32_t MATURITY_BLOCKS = 4320;          // 3 jours
const uint32_t FINALITY_DEPTH = 12;             // LLMQ finality

// DOMC Cycle (Updated 2025-11-27)
const uint32_t DOMC_CYCLE_LENGTH = 172800;      // 4 mois (~120 jours)
const uint32_t DOMC_COMMIT_OFFSET = 132480;     // Début phase commit
const uint32_t DOMC_REVEAL_OFFSET = 152640;     // Début phase reveal
const uint32_t DOMC_PHASE_DURATION = 20160;     // Durée commit/reveal (~2 semaines)

// V6 Emission (FINAL - Updated 2025-11-27)
// Block reward = 0 après activation V6
// Toute l'économie est gouvernée par R%
const int64_t POST_V6_BLOCK_REWARD = 0;

// R% Parameters (FINAL - Updated 2025-11-27)
const uint16_t R_INITIAL = 4000;                // 40% initial (4000 basis points)
const uint16_t R_FLOOR = 700;                   // 7% plancher (700 basis points)
const uint16_t R_DECAY_PER_YEAR = 100;          // -1%/an (100 basis points)
const uint16_t R_YEARS_TO_FLOOR = 33;           // Atteint en 33 ans

// R_MAX_dynamic calculation
uint16_t R_MAX = std::max(700, 4000 - year * 100);  // Décroît 40%→7% sur 33 ans

// T (DAO Treasury) divisor
const uint16_t T_DIVISOR = 8;                   // T = U × R% / 8 / 365 (~5% at R=40%)
```

---

## 7. STRUCTURE KhuGlobalState

```cpp
struct KhuGlobalState {
    // Collateral and supply
    int64_t C;                      // PIV collateral (satoshis)
    int64_t U;                      // KHU_T supply transparent (satoshis)
    int64_t Z;                      // ZKHU supply shielded (satoshis) — mis à jour par STAKE/UNSTAKE

    // Reward pool
    int64_t Cr;                     // Reward pool (satoshis)
    int64_t Ur;                     // Reward rights (satoshis)

    // DAO Treasury (Phase 6)
    int64_t T;                      // DAO Treasury Pool (satoshis)

    // DOMC parameters (Updated 2025-11-27)
    uint16_t R_annual;              // Basis points (0-4000), INITIAL = 4000 (40%)
    uint16_t R_MAX_dynamic;         // max(700, 4000 - year*100) → 40%→7% sur 33 ans
    uint32_t last_domc_height;

    // DOMC cycle (Updated 2025-11-25)
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;          // 172800 blocks (4 mois)
    uint32_t domc_commit_phase_start;    // cycle_start + 132480
    uint32_t domc_reveal_deadline;       // cycle_start + 152640

    // Yield scheduler
    uint32_t last_yield_update_height;

    // Chain tracking
    uint32_t nHeight;
    uint256 hashBlock;
    uint256 hashPrevState;

    bool CheckInvariants() const {
        if (C < 0 || U < 0 || Z < 0 || Cr < 0 || Ur < 0 || T < 0)
            return false;
        bool cd_ok = (C == U + Z);           // INVARIANT_1
        bool cdr_ok = (Ur == 0 || Cr == Ur); // INVARIANT_2
        return cd_ok && cdr_ok;
    }
};
```

---

## 8. LEVELDB KEYS (Namespace KHU = 'K')

```cpp
// KHU State
'K' + 'S' + height      → KhuGlobalState at height
'K' + 'B'               → Best KHU state hash
'K' + 'H' + hash        → KhuGlobalState by hash

// ZKHU (namespace 'K' — séparé de Shield 'S')
'K' + 'A' + anchor      → ZKHU SaplingMerkleTree
'K' + 'N' + nullifier   → ZKHU nullifier spent flag
'K' + 'T' + note_id     → ZKHUNoteData

// Shield PIVX (namespace 'S' — NE PAS UTILISER pour KHU)
'S' + anchor            → Shield SaplingMerkleTree
's' + nullifier         → Shield nullifier spent flag
```

---

## 9. ZKHU — RÈGLES ABSOLUES

```
✅ Sapling cryptographie réutilisée (pas de modification)
✅ Namespace 'K' séparé de Shield 'S'
✅ Pipeline: KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T

❌ AUCUN Z→Z transfer (pas de ZKHU → ZKHU)
❌ AUCUN join-split
❌ AUCUNE référence Zerocoin/zPIV (MORT)
❌ AUCUN pool partagé avec Shield
```

---

## 10. HTLC — RÈGLES ABSOLUES

```
✅ HTLC standard Bitcoin (opcodes existants PIVX)
✅ Hashlock = SHA256(secret) uniquement
✅ Timelock = block height uniquement

❌ Pas de Z→Z HTLC
❌ Pas de timelock timestamp
❌ Pas d'oracle prix on-chain
❌ Pas de code HTLC spécial pour KHU
```

---

## 11. MODÈLE ÉCONOMIQUE V6 (FINAL - Updated 2025-11-27)

### Post-V6: Block Reward = 0

À l'activation V6, le block reward passe à **ZÉRO**. L'économie est entièrement gouvernée par R%.

```
┌────────────────────────────────────────────────────────────────────┐
│  POST-V6: ÉCONOMIE GOUVERNÉE PAR R%                                │
├────────────────────────────────────────────────────────────────────┤
│                                                                     │
│  Block Reward = 0 PIV (aucune émission)                           │
│                                                                     │
│  Yield (ZKHU):    Z × R% / 365 par jour                           │
│  Treasury (T):    U × R% / 8 / 365 par jour (~5% at R=40%)          │
│                                                                     │
│  R% voté par MN via DOMC (cycles 4 mois)                          │
│  R_MAX décroît: 40% → 7% sur 33 ans                               │
│                                                                     │
└────────────────────────────────────────────────────────────────────┘
```

### Timeline R_MAX

| Année | R_MAX   | Inflation max* |
|-------|---------|----------------|
| 0     | 40%     | ~10.5%         |
| 10    | 30%     | ~7.9%          |
| 20    | 20%     | ~5.3%          |
| 30    | 10%     | ~2.6%          |
| 33+   | 7%      | ~1.84%         |

*Inflation max calculée avec scénario 50/50 (KHU=50% PIV, ZKHU=50% KHU)

---

## 12. INTERDICTIONS ABSOLUES

### Code
```cpp
// ❌ Modifier C sans U (ou inverse)
state.C += amount;  // ❌ Violation invariant

// ❌ Code entre mutations atomiques
state.C += amount;
DoSomething();      // ❌ INTERDIT
state.U += amount;

// ❌ Float/double pour montants
double yield = amount * 0.05;  // ❌ Utiliser int64_t

// ❌ Compounding yield
daily = ((principal + accumulated) * R) / 10000 / 365;  // ❌

// ✅ CORRECT: Yield linéaire
daily = (principal * R_annual) / 10000 / 365;
```

### Architecture
```
❌ Pas d'interpolation émission (transition brutale entre années)
❌ Pas de yield avant maturity (4320 blocs)
❌ Pas de DOMC sur émission PIVX (DOMC gouverne R% uniquement)
❌ Pas de reorg > 12 blocs (finality LLMQ)
```

---

## 12.0 MATURITY PERIOD — RÈGLE CRITIQUE

> ⚠️ **AUCUN yield avant 4320 blocs (3 jours) de maturity.**

### Règle Canonique
```
STAKE à bloc N:
  - Blocs N à N+4319: MATURITY PERIOD (aucun yield)
  - À partir de bloc N+4320: Yield actif
```

### Timeline Exemple (12,000 PIV stakés à V6)
```
Jour 0 (bloc 0):      STAKE 12,000 → Maturity commence
Jour 1 (bloc 1440):   Maturity (pas de yield)
Jour 2 (bloc 2880):   Maturity (pas de yield)
Jour 3 (bloc 4320):   ✅ Note mature - Yield commence
Jour 4 (bloc 5760):   +12.16 PIV yield (R=37%, 12000×3700/10000/365)
Jour 5 (bloc 7200):   +12.16 PIV yield
...
```

### Calcul Yield (Post-Maturity uniquement)
```cpp
// Yield quotidien = (principal × R_annual) / 10000 / 365
CAmount dailyYield = (note.amount * R_annual) / 10000 / 365;

// Exemple: 12,000 PIV @ 37%
// dailyYield = (12000 × 3700) / 10000 / 365 = 12.16 PIV/jour
```

### Pourquoi 3 Jours?
- **Anti-arbitrage**: Empêche le flash-stake (stake→yield→unstake immédiat)
- **Engagement minimum**: Force un commitment temporel
- **Sécurité réseau**: Aligne incentives avec stabilité long-terme

---

## 12.1 CLARIFICATION R% — RÈGLE CRITIQUE (Updated 2025-11-27)

### Cycle DOMC (4 mois = 172800 blocs)

```
R% ACTIF PENDANT 4 MOIS COMPLETS (jamais interrompu)

0────────────132480────────152640────────172800
│              │              │              │
│              │    VOTE      │  ADAPTATION  │
│              │  (2 sem)     │   (2 sem)    │
│              │   commits    │              │
│              │   secrets    │  REVEAL      │
│              │              │  instantané  │
│              │              │  ↓           │
│              │              │  Futur R%    │
│              │              │  visible     │
│                                            │
├────────────────────────────────────────────┤
│     R% ACTIF PENDANT TOUT LE CYCLE         │
│              (4 mois)                      │
└────────────────────────────────────────────┴─────►
                                             │
                                   Nouveau R% activé
```

**Le processus:**
1. **R% actif 4 mois complets** (jamais interrompu)
2. **Mois 1-3**: R% actif, pas de vote
3. **Semaines 13-14**: VOTE (commits secrets), R% toujours actif
4. **Bloc 152640**: REVEAL instantané → futur R% visible
5. **Semaines 15-16**: ADAPTATION (tout le monde s'adapte au futur R%)
6. **Bloc 172800**: Nouveau R% activé automatiquement

### R% Uniforme pour Tous

> ⚠️ **R% est GLOBAL et IDENTIQUE pour TOUS les stakers à un instant T.**

```
À jour J avec R_annual = X:
  - Alice (stakée depuis 100 jours) → reçoit X%
  - Bob (staké depuis 10 jours)     → reçoit X%
  - Charlie (staké depuis 1 jour)   → reçoit X%
```

### R_MAX — Plafond qui Décroît

```
R_MAX = max(7%, 40% - année×1%)

Année 0:   R_MAX = 40%  → MN peuvent voter entre 0% et 40%
Année 10:  R_MAX = 30%  → MN peuvent voter entre 0% et 30%
Année 20:  R_MAX = 20%  → MN peuvent voter entre 0% et 20%
Année 33+: R_MAX = 7%   → MN peuvent voter entre 0% et 7% (plancher)
```

### Résumé

| Élément | Qui décide? | Comment? |
|---------|-------------|----------|
| **R%** (taux réel) | Masternodes | Vote tous les 4 mois |
| **R_MAX** (plafond) | Protocole | Décroît automatiquement |
| **Mise à jour R%** | Automatique | Appliqué à la fin du cycle |

```cpp
// Code source (khu_yield.cpp ligne 114):
// Le MÊME R_annual pour toutes les notes
CAmount dailyYield = CalculateDailyYieldForNote(note.amount, R_annual);
```

---

## 12.2 ACTIVATION V6 — RÈGLE CRITIQUE (Updated 2025-11-27)

> ⚠️ **À l'activation V6, l'économie passe entièrement sous gouvernance R%.**

### Modèle Simplifié Post-V6
```
Block Reward = 0 PIV (aucune émission traditionnelle)

Yield:    Z × R% / 365 par jour
Treasury: U × R% / 20 / 365 par jour

R% = seul levier économique, voté par MN via DOMC
```

### Paramètres R%
```
- R% initial = 40% (4000 basis points)
- Actif IMMÉDIATEMENT à l'activation V6
- Premier cycle DOMC démarre à V6 + 172800 blocs (4 mois)
- Après 4 mois, les MN votent pour ajuster R%
- R_MAX_dynamic = max(700, 4000 - year×100) // 40%→7% sur 33 ans
- R% RESTE ACTIF pendant les phases commit/reveal
```

### Timeline
```
V6 Activation:
  ├─ Block Reward: 0 PIV/bloc
  ├─ R%: 40% actif immédiatement
  ├─ Yield: Z × R% / 365
  └─ Treasury: U × R% / 20 / 365

V6 + 4 mois (172800 blocs):
  └─ DOMC: Premier vote MN pour R%

V6 + 33 ans:
  └─ R_MAX: 7% (plancher atteint)
```

**R% = levier unique de gouvernance économique.**

---

## 12.3 DAO TREASURY (T) — RÈGLE CRITIQUE (Updated 2025-11-27)

> ⚠️ **T s'accumule selon U × R% / 8, quotidiennement (même timing que Yield).**

### Formule T (FINAL)
```cpp
// T indexé sur activité économique (U) et R%
// Diviseur = 8 → donne ~5% de U par an quand R%=40%
T_daily = (U * R_annual) / 10000 / 8 / 365;

// Exemple: U=1M, R%=40% (4000 bp)
// T_daily = (1M × 4000) / 10000 / 8 / 365 = 1,369.86 PIV/jour
// T_annual = 1,369.86 × 365 = 500,000 PIV = 5% of U
```

### Timing
```
T et Yield sont unifiés dans ApplyDailyUpdatesIfNeeded():
  1. T += U × R% / 8 / 365       // DAO Treasury (GLOBAL, ~5% at R=40%)
  2. Pour chaque note: yield     // Staking Yield (INDIVIDUEL)

Trigger: tous les 1440 blocs (1 jour)
```

### T vs YIELD — Distinction Critique
```
┌──────────────┬─────────────────────────┬──────────────────────────┐
│ Métrique     │ T (DAO Treasury)        │ YIELD (Staking R%)       │
├──────────────┼─────────────────────────┼──────────────────────────┤
│ Calcul       │ GLOBAL (1×)             │ INDIVIDUEL (par note)    │
│ Base         │ U (transparent supply)  │ note.amount (ZKHU)       │
│ Formule      │ U × R% / 8 / 365        │ note.amt × R% / 365      │
│ Destination  │ state.T                 │ note.accumulated + Cr/Ur │
└──────────────┴─────────────────────────┴──────────────────────────┘
```

### Propriétés
- **T >= 0** (invariant)
- **T indexé sur U** (activité économique transparente)
- **T proportionnel à R%** (même levier que yield)
- **Diviseur 8** → T représente ~1/8ème du yield total (~5% at R=40%)
- **Post-V6**: T seule source de funding DAO (block reward = 0)

---

## 13. FICHIERS CLÉS

```
src/khu/khu_state.h/cpp       → KhuGlobalState
src/khu/khu_validation.h/cpp  → ProcessKHUTransaction
src/khu/khu_db.h/cpp          → LevelDB persistence
src/khu/khu_yield.h/cpp       → Daily yield calculation
src/khu/khu_domc.h/cpp        → DOMC commit-reveal
src/rpc/khu.cpp               → RPC commands
src/consensus/params.h        → UPGRADE_KHU activation
```

---

## 14. RPC COMMANDS (Phase 8)

### Implémentés
- `getkhustate` — État global KHU
- `getkhustatecommitment` — Hash state commitment
- `domccommit` / `domcreveal` — Votes DOMC

### À implémenter (BLOQUANT pour testnet)
- `mintkhu <amount>` — PIV → KHU_T
- `redeemkhu <amount>` — KHU_T → PIV
- `stakekhu <amount>` — KHU_T → ZKHU
- `unstakekhu <nullifier>` — ZKHU → KHU_T
- `sendkhu <address> <amount>` — Transfer KHU_T
- `getkhubalance` — Balance wallet
- `listkhuunspent` — Liste UTXOs KHU

---

## 15. TESTS OBLIGATOIRES

Avant chaque commit:
```bash
# Build
cd PIVX && make -j$(nproc)

# Tests unitaires
./src/test/test_pivx --run_test=khu*

# Tests fonctionnels (si disponibles)
test/functional/test_runner.py khu*
```

---

## 16. SERIALIZATION ORDER (CONSENSUS)

L'ordre de sérialisation de `GetHash()` est **IMMUABLE**:

```cpp
// Ces 17 champs dans CET ORDRE EXACT (Updated 2025-11-26)
ss << C;
ss << U;
ss << Z;                           // ZKHU supply shielded (stocké, pas calculé)
ss << Cr;
ss << Ur;
ss << T;                           // DAO Treasury (Phase 6.3)
ss << R_annual;
ss << R_MAX_dynamic;
ss << last_yield_update_height;
ss << last_yield_amount;           // For exact undo
ss << domc_cycle_start;
ss << domc_cycle_length;
ss << domc_commit_phase_start;
ss << domc_reveal_deadline;
ss << nHeight;
ss << hashBlock;
ss << hashPrevState;
```

Changer l'ordre = **HARD FORK IMMÉDIAT**

---

## 17. DOCUMENTATION (Structure Simplifiée)

### Structure
```
docs/
├── README.md           ← Index
├── SPEC.md             ← Spécification canonique (IMMUABLE)
├── ARCHITECTURE.md     ← Architecture technique (IMMUABLE)
├── ROADMAP.md          ← Phases et statut
├── IMPLEMENTATION.md   ← Guide implémentation
│
├── comprendre/         ← Pour les normies
├── blueprints/         ← Détails par feature
└── archive/            ← Documents historiques
```

### Documents Canoniques (IMMUABLES)

| Document | Rôle UNIQUE |
|----------|-------------|
| `docs/SPEC.md` | Règles MATHÉMATIQUES (formules, invariants, constantes) |
| `docs/ARCHITECTURE.md` | Structure CODE (fichiers, patterns, LevelDB) |
| `docs/blueprints/01-*.md` | Détails techniques par feature |

**Pas de redondance:** SPEC.md = QUOI (règles), ARCHITECTURE.md = OÙ (code).
**Modification interdite sans validation architecte + review complète.**

### Documents Évolutifs
- `docs/ROADMAP.md` — Statut phases
- `docs/IMPLEMENTATION.md` — Guide développeurs
- `docs/archive/` — Rapports historiques

---

## 19. CHECKLIST ANTI-DÉRIVE

Avant chaque commit, vérifier:

- [ ] `C == U + Z` préservé après toutes mutations (Z = state.Z)
- [ ] `Cr == Ur` préservé après toutes mutations
- [ ] `AssertLockHeld(cs_khu)` dans fonctions mutation
- [ ] `CheckInvariants()` appelé après mutations
- [ ] Pas de code entre mutations atomiques (C/U, Cr/Ur)
- [ ] Yield AVANT transactions dans ConnectBlock
- [ ] Formule émission `max(6-year,0)` respectée
- [ ] Tests passent: `make check`

---

## 20. CONTACT

**Architecte:** ChatGPT (via conversations)
**Développeur:** Claude (Senior C++)

En cas d'incertitude sur une implémentation:
1. Consulter les blueprints dans `/docs/blueprints/`
2. Consulter la spécification dans `/docs/SPEC.md`
3. Demander clarification à l'architecte avant d'implémenter

---

**Version:** 2.0
**Date:** 2025-11-27
**Status:** ACTIF - FINAL KHU V6 SPEC
**Changelog:**
- v2.0: FINAL KHU V6 - Block reward=0 post-V6, R%=40%→7% sur 33 ans, T=U×R%/20/365
- v1.7: TxType corrigés (6-11 au lieu de 10-17), DOMC_COMMIT/REVEAL séparés, alignement code/doc vérifié
- v1.6: Simplification structure docs (SPEC, ARCHITECTURE, ROADMAP, IMPLEMENTATION), dossier comprendre/ pour normies, archive/
- v1.5: Ajout section 12.0 MATURITY explicite (3 jours, aucun yield avant), Y (Yield) au lieu de B (Bonus), calcul yield détaillé
- v1.4: Z stocké dans KhuGlobalState (pas calculé dynamiquement), CheckInvariants() sans paramètre
- v1.3: Double-entry accounting clarification (a/y notation), UNSTAKE formula corrected, R% = 37% everywhere
- v1.2: T (DAO Treasury) daily accumulation 2%/year, DAO transition Year 1+, unified ApplyDailyUpdatesIfNeeded()
- v1.1: R% = 37% initial at V6 activation, DOMC cycle 172800 blocks (4 months)
