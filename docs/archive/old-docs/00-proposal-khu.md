# 00 — PIVX V6 KHU Proposal

Version: 2.1.0
Status: PROPOSAL
Date: 2025-11-26

---

## 1. DEFINITION KHU

**KHU = Knowledge Hedge Unit**

KHU est un **colored coin collatéralisé 1:1** par PIV intégré directement au consensus PIVX V6.

| Propriété | Description |
|-----------|-------------|
| Type | Colored coin natif (pas un token externe) |
| Collatéral | 1 KHU = 1 PIV locked (TOUJOURS) |
| Staking | Staking privé via ZKHU (Sapling) |
| Yield | Yield gouverné par DOMC (R%) |

---

## 2. TYPES D'ACTIFS

```
PIV     : Actif natif PIVX (existant, inchangé)
KHU_T   : Knowledge Hedge Unit Transparent (colored UTXO)
ZKHU    : Knowledge Hedge Unit Shielded (Sapling note, staking only)
```

### Pipeline KHU Complet

```
FLUX COMPLET (avec yield):
PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV

FLUX SIMPLE (sans yield):
PIV → MINT → KHU_T → [trade] → REDEEM → PIV
```

**Chaque opération est INDÉPENDANTE** — pas de séquence obligatoire.

---

## 3. COMPTABILITÉ À DOUBLE ENTRÉE

### 3.1 Principe Fondamental

Le système KHU utilise une **comptabilité à double entrée** pour garantir l'auditabilité totale:

```
┌─────────────────────────────────────────────────────────────────┐
│                 COMPTABILITÉ KHU (Double Entrée)                │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   SYSTÈME PRINCIPAL              SYSTÈME REWARD                 │
│   (Circulation)                  (Accumulation Yield)           │
│                                                                 │
│   ┌─────────┐                    ┌─────────┐                    │
│   │    C    │ ◄── PIV Collat.    │   Cr    │ ◄── PIV Reward    │
│   │ (actif) │                    │ (actif) │    Collateral      │
│   └────┬────┘                    └────┬────┘                    │
│        │                              │                         │
│        │ backs 1:1                    │ backs 1:1               │
│        │                              │                         │
│   ┌────▼────┐                    ┌────▼────┐                    │
│   │  U + Z  │ ◄── KHU Supply     │   Ur    │ ◄── Reward Rights │
│   │(passif) │    (T + Shielded)  │(passif) │    (droits yield) │
│   └─────────┘                    └─────────┘                    │
│                                                                 │
│   INVARIANT 1: C == U + Z        INVARIANT 2: Cr == Ur          │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 3.2 Nomenclature Canonique

| Variable | Nom Complet | Description | Type |
|----------|-------------|-------------|------|
| **C** | Collateral | PIV locked pour backing (U + Z) | int64_t |
| **U** | Supply Transparent | KHU_T en circulation | int64_t |
| **Z** | Supply Shielded | ZKHU staké (mis à jour par STAKE/UNSTAKE) | int64_t |
| **Cr** | Collateral Reward | PIV réservé pour backing Ur | int64_t |
| **Ur** | Unstake Rights | Droits yield accumulés (non-matérialisés) | int64_t |
| **T** | Treasury | DAO Treasury pool | int64_t |

**Note:** Z est **stocké dans KhuGlobalState** et mis à jour atomiquement:
- STAKE: `Z += amount`
- UNSTAKE: `Z -= amount`

Ceci permet l'auditabilité totale sans scanner les notes shielded (dont les montants individuels restent privés).

### 3.3 Les Deux Invariants Sacrés

```cpp
// INVARIANT 1: Système Principal — Collatéralisation totale
C == U + Z
// Chaque KHU (transparent ou shielded) est backé 1:1 par PIV

// INVARIANT 2: Système Reward — Droits backés
Cr == Ur
// Chaque droit de yield est backé par du collateral reward

// AUDIT GLOBAL (vérification externe):
// Total Collateral = C + Cr
// Total Supply     = U + Z + Ur
// Vérifiable: (C + Cr) == (U + Z + Ur) ✓
```

---

## 4. ÉTAT GLOBAL — KhuGlobalState

### 4.1 Structure (16 champs)

```cpp
struct KhuGlobalState {
    // ═══════════════════════════════════════════════════════
    // SYSTÈME PRINCIPAL (Circulation)
    // ═══════════════════════════════════════════════════════
    int64_t C;      // Collateral PIV total (satoshis)
    int64_t U;      // KHU_T supply transparente (satoshis)
    int64_t Z;      // ZKHU supply shielded (satoshis) — mis à jour par STAKE/UNSTAKE

    // ═══════════════════════════════════════════════════════
    // SYSTÈME REWARD (Accumulation Yield)
    // ═══════════════════════════════════════════════════════
    int64_t Cr;     // Collateral Reward (PIV réservé) (satoshis)
    int64_t Ur;     // Unstake Rights (droits accumulés) (satoshis)

    // ═══════════════════════════════════════════════════════
    // DAO TREASURY
    // ═══════════════════════════════════════════════════════
    int64_t T;      // DAO Treasury Pool (satoshis)

    // ═══════════════════════════════════════════════════════
    // PARAMÈTRES DOMC
    // ═══════════════════════════════════════════════════════
    uint16_t R_annual;        // Taux annuel (basis points: 3700 = 37%)
    uint16_t R_MAX_dynamic;   // Plafond dynamique votable

    // ═══════════════════════════════════════════════════════
    // CYCLE DOMC
    // ═══════════════════════════════════════════════════════
    uint32_t last_domc_height;
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;    // 172800 blocs (4 mois)
    uint32_t domc_phase_length;    // 20160 blocs (2 semaines)

    // ═══════════════════════════════════════════════════════
    // YIELD SCHEDULER
    // ═══════════════════════════════════════════════════════
    uint32_t last_yield_update_height;

    // ═══════════════════════════════════════════════════════
    // CHAIN TRACKING
    // ═══════════════════════════════════════════════════════
    uint32_t nHeight;
    uint256  hashPrevState;
    uint256  hashBlock;

    // ═══════════════════════════════════════════════════════
    // VÉRIFICATION INVARIANTS
    // ═══════════════════════════════════════════════════════
    bool CheckInvariants() const {
        // Non-négativité
        if (C < 0 || U < 0 || Z < 0 || Cr < 0 || Ur < 0 || T < 0)
            return false;

        // INVARIANT 1: Collatéralisation principale
        bool principal_ok = (C == U + Z);

        // INVARIANT 2: Collatéralisation reward
        bool reward_ok = (Cr == Ur);

        return principal_ok && reward_ok;
    }
};
```

---

## 5. OPÉRATIONS KHU

### 5.1 MINT — PIV → KHU_T

```cpp
// Entrée: amount PIV
// Sortie: amount KHU_T

// Effet sur SYSTÈME PRINCIPAL:
C += amount;    // Lock PIV collateral
U += amount;    // Create KHU_T supply
// Z inchangé

// Invariant préservé:
// Avant: C == U + Z
// Après: (C + a) == (U + a) + Z ✓
```

**Instantané.** Lock des PIV, création de KHU_T 1:1.

### 5.2 REDEEM — KHU_T → PIV

```cpp
// Entrée: amount KHU_T
// Sortie: amount PIV

// Effet sur SYSTÈME PRINCIPAL:
C -= amount;    // Unlock PIV collateral
U -= amount;    // Burn KHU_T supply
// Z inchangé

// Invariant préservé:
// Avant: C == U + Z
// Après: (C - a) == (U - a) + Z ✓
```

**Instantané.** Burn des KHU_T, unlock des PIV 1:1.

### 5.3 STAKE — KHU_T → ZKHU

```cpp
// Entrée: amount KHU_T
// Sortie: ZKHU note (Sapling)

// Effet sur SYSTÈME PRINCIPAL (ATOMIQUE):
U -= amount;    // Burn KHU_T (transparent supply ↓)
Z += amount;    // ZKHU créé (shielded supply ↑)
// C inchangé   // Le collateral reste locked

// Invariant préservé:
// Avant: C == U + Z
// Après: C == (U - a) + (Z + a) == U + Z ✓
```

**Conversion forme.** Le total (U + Z) reste constant, C inchangé.

### 5.4 YIELD — Accumulation Quotidienne

```cpp
// Trigger: Tous les 1440 blocs (1 jour)
// Pour chaque note ZKHU mature (age >= 4320 blocs):

daily_yield = (note.amount × R_annual / 10000) / 365;

// Effet sur SYSTÈME REWARD (création monétaire auditée):
Cr += daily_yield;    // Créer collateral reward (backing)
Ur += daily_yield;    // Créer droit correspondant

// Invariant préservé:
// Avant: Cr == Ur
// Après: (Cr + y) == (Ur + y) ✓

// Note: Yield est de la CRÉATION MONÉTAIRE DIFFÉRÉE
// Les droits (Ur) ne sont pas encore du KHU réel
// Ils seront matérialisés lors de l'UNSTAKE
```

### 5.5 UNSTAKE — ZKHU → KHU_T + Bonus

```cpp
// Pré-condition: nHeight - note.stake_start >= 4320 (maturity)
// Soit P = principal (note.amount)
// Soit B = bonus = note.Ur_accumulated (yield accumulé)

// ═══════════════════════════════════════════════════════════════
// DOUBLE FLUX ATOMIQUE (les 4 lignes DOIVENT être adjacentes)
// ═══════════════════════════════════════════════════════════════

// FLUX 1: Reward → Principal (matérialisation du bonus)
Cr -= B;    // Libère collateral reward
C  += B;    // Devient collateral principal
Ur -= B;    // Consume droits reward
U  += B;    // Crée KHU_T (bonus)

// FLUX 2: Z → U (retour du principal)
Z  -= P;    // Note ZKHU détruite
U  += P;    // Crée KHU_T (principal)

// Sortie totale: (P + B) KHU_T

// Vérification INVARIANT 1 (après FLUX 1):
// C' = C + B, U' = U + B, Z' = Z
// C' == U' + Z' ⟺ C + B == U + B + Z ⟺ C == U + Z ✓

// Vérification INVARIANT 2 (après FLUX 1):
// Cr' = Cr - B, Ur' = Ur - B
// Cr' == Ur' ⟺ Cr - B == Ur - B ⟺ Cr == Ur ✓

// Vérification après FLUX 2:
// Z' = Z - P, U' = U + P + B
// C' == U' + Z' ⟺ C + B == (U + P + B) + (Z - P) == U + Z + B ✓
```

**Double flux atomique.** Matérialise les droits (Ur) en KHU réel (U) avec son backing (Cr→C).

---

## 6. TABLEAU RÉCAPITULATIF DES OPÉRATIONS

| Opération | C | U | Z | Cr | Ur | Invariants |
|-----------|---|---|---|----|----|------------|
| **MINT** | +a | +a | - | - | - | C==U+Z ✓ |
| **REDEEM** | -a | -a | - | - | - | C==U+Z ✓ |
| **STAKE** | - | **-a** | +a | - | - | C==U+Z ✓ |
| **YIELD** | - | - | - | **+y** | **+y** | Cr==Ur ✓ |
| **UNSTAKE** | +y | +(a+y) | -a | -y | -y | Both ✓ |

**Légende:**
- `a` = amount / principal (montant de la transaction ou staké)
- `y` = yield accumulé (pour UNSTAKE: Ur_note = note.accumulatedYield)
- `-` = pas de changement

---

## 7. YIELD MECHANISM (R%)

### 7.1 Calcul Quotidien (INDIVIDUEL par note)

```cpp
// Tous les 1440 blocs (1 jour):
if ((nHeight - last_yield_update_height) >= 1440) {
    for each active ZKHU note:
        if (nHeight - note.stake_start >= 4320) {  // Mature
            // Yield INDIVIDUEL par note
            daily_yield = (note.amount * R_annual / 10000) / 365;

            // Accumulation dans la note
            note.Ur_accumulated += daily_yield;

            // Mise à jour globale (ATOMIQUE: Cr et Ur ensemble)
            Cr += daily_yield;
            Ur += daily_yield;
        }
    last_yield_update_height = nHeight;
}
```

### 7.2 R% Bounds — Décroissance sur 33 ans

```cpp
// R_MAX_dynamic décroît sur 33 ans:
uint16_t R_MAX = std::max(400, 3700 - year * 100);

// Schedule:
// Année 0:  3700 bp (37.00%)
// Année 10: 2700 bp (27.00%)
// Année 20: 1700 bp (17.00%)
// Année 33: 400 bp  (4.00%) - minimum garanti
// Année 34+: 400 bp (4.00%)

// Contrainte:
0 <= R_annual <= R_MAX_dynamic
```

### 7.3 R% Global et Uniforme

> **R% est IDENTIQUE pour TOUS les stakers à un instant T.**

```
À jour J avec R_annual = 25%:
  - Alice (stakée depuis 100 jours) → reçoit 25%
  - Bob (staké depuis 10 jours)     → reçoit 25%
  - Charlie (staké depuis 1 jour)   → reçoit 25%
```

La "dilution" s'applique aux arbitrageurs TARDIFS (R% baisse avec le temps), pas aux stakers actifs.

---

## 8. DAO TREASURY (T)

### 8.1 Accumulation Quotidienne (GLOBAL sur total)

```cpp
// Tous les 1440 blocs (même timing que yield):
// T calculé sur le TOTAL (U + Ur), pas par note
T_daily = (U + Ur) / 182500    // 2% annuel

T += T_daily
```

### 8.2 Distinction T vs Yield

| Aspect | T (DAO Treasury) | Yield R% |
|--------|------------------|----------|
| Calcul | GLOBAL sur (U + Ur) | INDIVIDUEL par note ZKHU |
| Base | Total KHU supply + droits | Montant de chaque note |
| Bénéficiaire | DAO Treasury Pool | Chaque staker individuellement |
| Affecte Cr/Ur | NON | OUI |

### 8.3 Transition — AUCUN GAP

```
Année 0-5:  Block rewards (6→1 PIV) + T accumule en parallèle
Année 6:    Block rewards = 0, T déjà disponible
Année 7+:   DAO financée uniquement par T

Il n'y a JAMAIS de période sans financement DAO.
```

---

## 9. DOMC — Governance (R%)

### 9.1 Cycle DOMC

```
Cycle: 172800 blocs (~4 mois)

├─ Blocs 0 - 132479:       Normal (R% actif)
├─ Blocs 132480 - 152639:  Phase COMMIT (R% actif)
├─ Blocs 152640 - 172799:  Phase REVEAL (R% actif)
└─ Bloc 172800:            Nouveau R% activé

R% RESTE ACTIF pendant commit/reveal — aucune interruption.
```

### 9.2 Mécanisme de Vote

```cpp
// COMMIT: Hash(R_proposed || salt)
// REVEAL: R_proposed + salt
// TALLY: Médiane des votes valides
R_annual = median(revealed_votes)
```

**Seuls les masternodes votent.** Quorum: 50% des MN actifs.

### 9.3 Bootstrap

```
À l'activation V6:
  R_annual = 3700 (37%)
  Premier cycle DOMC démarre à: ACTIVATION_HEIGHT + 172800
```

---

## 10. ÉMISSION PIVX (Planifiée V6)

### 10.1 Formule Canonique

```cpp
int64_t reward_per_role = std::max(6 - (int64_t)year, 0) * COIN;
// 3 rôles: MN, Staker, DAO
int64_t block_reward = reward_per_role * 3;
```

### 10.2 Schedule

| Année | Par rôle | Par bloc | Total annuel |
|-------|----------|----------|--------------|
| 0     | 6 PIV    | 18 PIV   | 9,460,800 PIV |
| 1     | 5 PIV    | 15 PIV   | 7,884,000 PIV |
| 2     | 4 PIV    | 12 PIV   | 6,307,200 PIV |
| 3     | 3 PIV    | 9 PIV    | 4,730,400 PIV |
| 4     | 2 PIV    | 6 PIV    | 3,153,600 PIV |
| 5     | 1 PIV    | 3 PIV    | 1,576,800 PIV |
| 6+    | 0 PIV    | 0 PIV    | 0 PIV |

**Émission totale sur 6 ans: ~33M PIV** puis arrêt définitif.

---

## 11. ZKHU — Staking Privé

### 11.1 Architecture

```
ZKHU utilise la cryptographie Sapling PIVX existante mais avec:
- Namespace LevelDB séparé: 'K' (vs 'S' pour Shield PIVX)
- Arbres Merkle séparés
- Nullifiers séparés
```

### 11.2 Règles Absolues

| Autorisé | Interdit |
|----------|----------|
| KHU_T → ZKHU (STAKE) | ZKHU → ZKHU transfers |
| ZKHU → KHU_T (UNSTAKE) | Join-splits |
| Une note par stake | Pool partagé avec Shield |
| Sapling crypto réutilisée | Modification crypto |

### 11.3 Maturity

```cpp
const uint32_t MATURITY_BLOCKS = 4320;  // 3 jours

// Une note ne génère du yield qu'après maturity
bool IsMature(const ZKHUNote& note, uint32_t nHeight) {
    return (nHeight - note.stake_start_height) >= MATURITY_BLOCKS;
}
```

---

## 12. HTLC — Atomic Swaps (Optionnel)

| Type | Action | Condition |
|------|--------|-----------|
| HTLC_CREATE | Lock KHU_T | - |
| HTLC_CLAIM | Unlock vers recipient | SHA256(preimage) == hashlock |
| HTLC_REFUND | Refund vers sender | nHeight > timelock |

---

## 13. FRAIS DE TRANSACTION — PIV BURN

> **TOUS les frais du système KHU sont payés en PIV et BRÛLÉS.**

| Opération | Frais | Devise | Destination |
|-----------|-------|--------|-------------|
| MINT | Standard tx fee | PIV | BURN |
| REDEEM | Standard tx fee | PIV | BURN |
| STAKE | Standard tx fee | PIV | BURN |
| UNSTAKE | Standard tx fee | PIV | BURN |
| Transfer KHU_T | Standard tx fee | PIV | BURN |

**Les frais N'AFFECTENT PAS les invariants KHU (C, U, Cr, Ur, T).**

---

## 14. ORDRE CONNECTBLOCK (Canonique)

```cpp
bool ConnectBlock(...) {
    LOCK2(cs_main, cs_khu);  // Lock order IMMUABLE

    // (1) DOMC cycle boundary (finalize + initialize)
    ProcessDomcCycleBoundary(state, nHeight);

    // (2) DAO Treasury accumulation (T += daily)
    AccumulateDaoTreasuryIfNeeded(state, nHeight);

    // (3) Daily yield AVANT transactions (Cr += y, Ur += y)
    ApplyDailyYieldIfNeeded(state, nHeight);

    // (4) Process KHU transactions (MINT, REDEEM, STAKE, UNSTAKE)
    for (const auto& tx : block.vtx) {
        ProcessKHUTransaction(tx, state);
    }

    // (5) Block reward
    ApplyBlockReward(state, nHeight);

    // (6) Check invariants (OBLIGATOIRE)
    if (!state.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // (7) Persist
    pKHUStateDB->WriteKHUState(state);
}
```

**CRITIQUE:** DOMC → Treasury → Yield → Transactions → Invariants.

---

## 15. TYPES DE TRANSACTIONS

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
    DOMC_VOTE = 14,     // Vote R%
    HTLC_CREATE = 15,
    HTLC_CLAIM = 16,
    HTLC_REFUND = 17,
};
```

---

## 16. CONSTANTES SYSTÈME

```cpp
// Timing
const uint32_t BLOCKS_PER_DAY = 1440;
const uint32_t BLOCKS_PER_YEAR = 525600;
const uint32_t MATURITY_BLOCKS = 4320;      // 3 jours
const uint32_t FINALITY_DEPTH = 12;         // LLMQ finality

// DOMC
const uint32_t DOMC_CYCLE_LENGTH = 172800;  // 4 mois
const uint32_t DOMC_COMMIT_OFFSET = 132480;
const uint32_t DOMC_REVEAL_OFFSET = 152640;
const uint32_t DOMC_PHASE_DURATION = 20160; // 2 semaines

// R% Limits
const uint16_t R_MIN = 0;
const uint16_t R_MAX = 5000;        // Absolute max 50%
const uint16_t R_DEFAULT = 3700;    // 37% at V6
```

---

## 17. SÉCURITÉ

### 17.1 Protections Implémentées

| Vecteur | Protection |
|---------|------------|
| Overflow | `__int128` pour calculs intermédiaires |
| Reorg > 12 | Finalité LLMQ + limite hard |
| Double-spend ZKHU | Nullifiers séparés |
| Invariant violation | CheckInvariants() obligatoire |
| Lock order deadlock | `LOCK2(cs_main, cs_khu)` |

### 17.2 CVE Corrigés

- **CVE-KHU-2025-002**: Validation invariants au chargement DB
- **VULN-KHU-2025-001**: Protection overflow avant mutations

---

## 18. COMMANDES RPC

| Commande | Description |
|----------|-------------|
| `getkhustate` | État global KHU |
| `khumint <amount>` | PIV → KHU_T |
| `khuredeem <amount>` | KHU_T → PIV |
| `khustake <amount>` | KHU_T → ZKHU |
| `khuunstake <nullifier>` | ZKHU → KHU_T |
| `khubalance` | Balance wallet KHU |
| `khuliststaked` | Liste notes ZKHU |
| `domccommit <R> <salt>` | Vote commit |
| `domcreveal <R> <salt>` | Vote reveal |

---

## 19. FICHIERS CLÉS

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

## 20. SCHÉMA RÉCAPITULATIF

```
            ┌─────────────────────────────────────────────────────────┐
            │              PIVX V6 KHU — DOUBLE ENTRÉE                │
            └─────────────────────────────────────────────────────────┘

  SYSTÈME PRINCIPAL                           SYSTÈME REWARD
  ═════════════════                           ══════════════

    ┌───────────┐                               ┌───────────┐
    │     C     │ PIV Collateral                │    Cr     │ Reward Collateral
    │  (actif)  │                               │  (actif)  │
    └─────┬─────┘                               └─────┬─────┘
          │ backs 1:1                                 │ backs 1:1
          ▼                                           ▼
    ┌───────────┐                               ┌───────────┐
    │   U + Z   │ KHU Supply                    │    Ur     │ Unstake Rights
    │ (passif)  │ (Transparent + Shielded)      │ (passif)  │ (droits yield)
    └─────┬─────┘                               └─────┬─────┘
          │                                           │
          │                                           │
    INVARIANT 1: C == U + Z                     INVARIANT 2: Cr == Ur


    OPÉRATIONS:
    ───────────
    MINT:    C += a,  U += a                    (System Principal)
    REDEEM:  C -= a,  U -= a                    (System Principal)
    STAKE:   U -= a,  Z += a                    (System Principal, C inchangé)
    YIELD:                                      Cr += y,  Ur += y  (System Reward)
    UNSTAKE: C += B,  U += (P+B),  Z -= P       Cr -= B,  Ur -= B  (Double Flux)


    ┌─────────────────────────────────────────────────────────────────────┐
    │  AUDIT GLOBAL: (C + Cr) == (U + Z + Ur)                             │
    │  Tout est backé. Création monétaire traçable. Système fermé.        │
    └─────────────────────────────────────────────────────────────────────┘
```

---

## 21. VERSION HISTORY

```
v1.0.0 - 2025-11-26 - Initial proposal
v2.0.0 - 2025-11-26 - Clarification nomenclature:
  - Ajout système à double entrée comptable
  - Clarification C == U + Z (pas C == U)
  - Clarification STAKE modifie U (U -= amount)
  - Clarification YIELD modifie Cr ET Ur
  - Ajout Z comme variable calculée dynamiquement
  - Schéma récapitulatif double flux
v2.1.0 - 2025-11-26 - Z stocké dans state:
  - Z est maintenant STOCKÉ dans KhuGlobalState (pas calculé)
  - STAKE: Z += amount (atomique avec U -= amount)
  - UNSTAKE: Z -= amount (atomique)
  - Suppression GetTotalStakedZKHU() - plus nécessaire
  - Privacy: montants individuels restent cachés, seul total Z est public
```

---

**Document Version:** 2.1.0
**Date:** 2025-11-26
**Source:** Canonical documentation consensus
