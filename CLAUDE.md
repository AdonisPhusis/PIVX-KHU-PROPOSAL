# CLAUDE.md — PIVX-V6-KHU Development Context

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

```cpp
C == U                    // collateral == supply (toujours)
Cr == Ur                  // reward pool == reward rights (toujours)
T >= 0                    // DAO Treasury (Phase 6)
```

### Opérations Autorisées

| Opération | Effet sur C/U | Effet sur Cr/Ur |
|-----------|---------------|-----------------|
| MINT      | C+, U+        | -               |
| REDEEM    | C-, U-        | -               |
| DAILY_YIELD | -           | Cr+, Ur+        |
| UNSTAKE   | C+, U+        | Cr-, Ur-        |

**UNSTAKE = Double Flux Atomique:**
```cpp
state.U  += B;   // (1) Create B units of KHU_T
state.C  += B;   // (2) Increase collateral by B
state.Cr -= B;   // (3) Consume reward pool
state.Ur -= B;   // (4) Consume reward rights
// Ces 4 lignes doivent être adjacentes, AUCUN code entre elles
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
- **UNSTAKE**: ZKHU → KHU_T + bonus — après 4320 blocs maturity

> ⚠️ STAKE/UNSTAKE sont OPTIONNELS. On peut MINT/REDEEM en boucle sans jamais staker.

---

## 3. ORDRE CONNECTBLOCK (CANONIQUE)

**L'ordre d'exécution dans ConnectBlock est IMMUABLE:**

```cpp
bool ConnectBlock(...) {
    LOCK2(cs_main, cs_khu);  // (0) Lock order: cs_main PUIS cs_khu

    KhuGlobalState prevState = LoadKhuState(pindex->pprev);
    KhuGlobalState newState = prevState;

    // (1) DAO Treasury accumulation (Phase 6)
    AccumulateDaoTreasuryIfNeeded(newState, nHeight);

    // (2) Apply daily yield AVANT transactions
    ApplyDailyYieldIfNeeded(newState, nHeight);

    // (3) Process KHU transactions (MINT, REDEEM, STAKE, UNSTAKE, DOMC)
    for (const auto& tx : block.vtx) {
        if (!ProcessKHUTransaction(tx, newState, view, state))
            return false;  // Stop immediately
    }

    // (4) Apply block reward (emission)
    ApplyBlockReward(newState, nHeight);

    // (5) Check invariants (MANDATORY)
    if (!newState.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // (6) Persist state
    pKHUStateDB->WriteKHUState(newState);
}
```

**CRITIQUE: Yield AVANT transactions (jamais l'inverse)**

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
    // ... (2-5)

    // KHU types
    KHU_MINT = 10,      // PIV → KHU_T
    KHU_REDEEM = 11,    // KHU_T → PIV
    KHU_STAKE = 12,     // KHU_T → ZKHU
    KHU_UNSTAKE = 13,   // ZKHU → KHU_T
    DOMC_VOTE = 14,     // DOMC R% vote
    HTLC_CREATE = 15,   // Create HTLC
    HTLC_CLAIM = 16,    // Claim HTLC
    HTLC_REFUND = 17,   // Refund HTLC
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

// DOMC Cycle
const uint32_t DOMC_CYCLE_LENGTH = 43200;       // 30 jours
const uint32_t DOMC_PHASE_LENGTH = 4320;        // 3 jours

// Emission (formule sacrée)
int64_t reward_year = std::max(6 - (int64_t)year, 0LL) * COIN;
// Cette formule est IMMUABLE - pas d'interpolation, pas de table

// R_MAX_dynamic
uint16_t R_MAX = std::max(400, 3000 - year * 100);  // Décroît 30%→4%
```

---

## 7. STRUCTURE KhuGlobalState

```cpp
struct KhuGlobalState {
    // Collateral and supply
    int64_t C;                      // PIV collateral (satoshis)
    int64_t U;                      // KHU_T supply (satoshis)

    // Reward pool
    int64_t Cr;                     // Reward pool (satoshis)
    int64_t Ur;                     // Reward rights (satoshis)

    // DAO Treasury (Phase 6)
    int64_t T;                      // DAO Treasury Pool (satoshis)

    // DOMC parameters
    uint16_t R_annual;              // Basis points (0-3000)
    uint16_t R_MAX_dynamic;         // max(400, 3000 - year*100)
    uint32_t last_domc_height;

    // DOMC cycle
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;     // 43200 blocks (30 days)
    uint32_t domc_phase_length;     // 4320 blocks (3 days)

    // Yield scheduler
    uint32_t last_yield_update_height;

    // Chain tracking
    uint32_t nHeight;
    uint256 hashBlock;
    uint256 hashPrevState;

    bool CheckInvariants() const {
        if (C < 0 || U < 0 || Cr < 0 || Ur < 0 || T < 0)
            return false;
        bool cd_ok = (U == 0 || C == U);
        bool cdr_ok = (Ur == 0 || Cr == Ur);
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

## 11. EMISSION PIVX (-1/an)

| Année | reward_year | Par bloc (3 rôles) |
|-------|-------------|-------------------|
| 0     | 6 PIV       | 18 PIV           |
| 1     | 5 PIV       | 15 PIV           |
| 2     | 4 PIV       | 12 PIV           |
| 3     | 3 PIV       | 9 PIV            |
| 4     | 2 PIV       | 6 PIV            |
| 5     | 1 PIV       | 3 PIV            |
| 6+    | 0 PIV       | 0 PIV            |

**Formule:** `max(6 - year, 0)` — IMMUABLE, pas d'interpolation

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

## 12.1 CLARIFICATION R% — RÈGLE CRITIQUE

> ⚠️ **R% est GLOBAL et IDENTIQUE pour TOUS les stakers à un instant T.**

```
À jour J avec R_annual = X:
  - Alice (stakée depuis 100 jours) → reçoit X%
  - Bob (staké depuis 10 jours)     → reçoit X%
  - Charlie (staké depuis 1 jour)   → reçoit X%
```

**Ce n'est PAS une "dilution des stakers".**
**C'est une "dilution des arbitrageurs tardifs":**

- R% diminue au fil du temps (30% → 4% sur 26 ans via DOMC/R_MAX)
- Un arbitrageur qui arrive TARD reçoit un R% plus faible que s'il était arrivé tôt
- Mais à un instant donné, TOUS les stakers actifs reçoivent le MÊME taux

```cpp
// Code source (khu_yield.cpp ligne 114):
// Le MÊME R_annual pour toutes les notes
CAmount dailyYield = CalculateDailyYieldForNote(note.amount, R_annual);
```

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
// Ces 14 champs dans CET ORDRE EXACT
ss << C;
ss << U;
ss << Cr;
ss << Ur;
ss << R_annual;
ss << R_MAX_dynamic;
ss << last_domc_height;
ss << domc_cycle_start;
ss << domc_cycle_length;
ss << domc_phase_length;
ss << last_yield_update_height;
ss << nHeight;
ss << hashBlock;
ss << hashPrevState;
```

Changer l'ordre = **HARD FORK IMMÉDIAT**

---

## 17. DOCUMENTS CANONIQUES (IMMUABLES)

Ces documents définissent le système et **NE DOIVENT JAMAIS ÊTRE MODIFIÉS**:

- `docs/02-canonical-specification.md` — Spécification mathématique
- `docs/03-architecture-overview.md` — Architecture technique
- `docs/04-economics.md` — Propriétés économiques
- `docs/blueprints/01-PIVX-INFLATION-DIMINUTION.md` — Émission canonique

**Modification interdite sans validation architecte + review complète.**

---

## 18. DOCUMENTS MODIFIABLES

Ces documents évoluent au fur et à mesure:

- `docs/05-roadmap.md` — Statut phases et progression
- `docs/06-protocol-reference.md` — Référence implémentation
- `docs/reports/*.md` — Rapports de phase

---

## 19. CHECKLIST ANTI-DÉRIVE

Avant chaque commit, vérifier:

- [ ] `C == U` préservé après toutes mutations
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
2. Consulter la spécification dans `/docs/02-canonical-specification.md`
3. Demander clarification à l'architecte avant d'implémenter

---

**Version:** 1.0
**Date:** 2025-11-24
**Status:** ACTIF
