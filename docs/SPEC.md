# PIVX-V6-KHU — CANONICAL SPECIFICATION

Version: 2.1.0
Status: FINAL
Last Update: 2025-11-27

---

## RÔLE DE CE DOCUMENT

> **SPEC.md = RÈGLES MATHÉMATIQUES ET CONSENSUS**
>
> Ce document est la **SOURCE UNIQUE** pour:
> - Structure KhuGlobalState (17 champs)
> - Invariants mathématiques (C == U + Z, Cr == Ur)
> - Formules canoniques (yield, émission, DOMC)
> - Effets des opérations sur l'état
> - Constantes du protocole
>
> **Ce document NE décrit PAS:**
> - Organisation des fichiers/modules → voir ARCHITECTURE.md
> - Patterns d'implémentation → voir ARCHITECTURE.md
> - Détails RPC/Wallet → voir IMPLEMENTATION.md
>
> **Règle:** Si SPEC.md et un autre document divergent sur une formule ou règle mathématique, **SPEC.md a raison**.

---

## 1. DÉFINITION KHU

**KHU = Knowledge Hedge Unit**

KHU est un **colored coin collatéralisé 1:1** par PIV.

- ❌ Ce n'est PAS un stablecoin (pas de peg USD/EUR)
- ✅ C'est une unité de couverture tokenisée avec staking privé et yield gouverné

**Types d'actifs:**

| Type | Description |
|------|-------------|
| PIV | Asset natif PIVX |
| KHU_T | KHU transparent (colored UTXO) |
| ZKHU | KHU shielded (Sapling note, staking uniquement) |

---

## 2. ÉTAT GLOBAL — KhuGlobalState

### 2.1 Structure canonique (18 champs)

```cpp
struct KhuGlobalState {
    // === ÉCONOMIE ===
    int64_t  C;                  // Collatéral total PIV (satoshis)
    int64_t  U;                  // Supply KHU_T transparent (satoshis)
    int64_t  Z;                  // Supply ZKHU shielded (satoshis)
    int64_t  Cr;                 // Reward pool (satoshis)
    int64_t  Ur;                 // Reward rights (satoshis)
    int64_t  T;                  // DAO Treasury Pool (satoshis)

    // === DOMC ===
    uint32_t R_annual;           // Current yield rate (basis points 0-4000)
    uint32_t R_next;             // Next R% after REVEAL (visible during ADAPTATION, 0 if not set)
    uint32_t R_MAX_dynamic;      // Plafond R% dynamique (max(700, 4000 - year×100))

    // === YIELD SCHEDULER ===
    uint32_t last_yield_update_height;
    int64_t  last_yield_amount;

    // === DOMC CYCLE ===
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;         // 172800 blocs (4 mois)
    uint32_t domc_commit_phase_start;   // cycle_start + 132480
    uint32_t domc_reveal_deadline;      // cycle_start + 152640

    // === CHAIN TRACKING ===
    uint32_t nHeight;
    uint256  hashBlock;
    uint256  hashPrevState;

    // Vérification invariants
    bool CheckInvariants() const {
        if (C < 0 || U < 0 || Z < 0 || Cr < 0 || Ur < 0 || T < 0)
            return false;
        return (C == U + Z) && (Ur == 0 || Cr == Ur);
    }
};
```

> ⚠️ **Ces 18 champs sont IMMUABLES.** Toute modification = hard fork.

### 2.2 Ordre de sérialisation (consensus)

L'ordre de sérialisation pour `GetHash()` est **SACRÉ**:

```
C → U → Z → Cr → Ur → T → R_annual → R_next → R_MAX_dynamic →
last_yield_update_height → last_yield_amount →
domc_cycle_start → domc_cycle_length → domc_commit_phase_start →
domc_reveal_deadline → nHeight → hashBlock → hashPrevState
```

Changer l'ordre = **HARD FORK IMMÉDIAT**.

---

## 3. INVARIANTS

### 3.1 INVARIANT_1 — Collatéralisation Totale

```
C == U + Z

où:
  C = PIV collatéral total
  U = KHU_T en circulation (transparent)
  Z = ZKHU stakés (shielded)
```

### 3.2 INVARIANT_2 — Collatéralisation Reward

```
(Cr == 0 && Ur == 0) OR (Cr == Ur)
```

### 3.3 Vérification

Les invariants DOIVENT être vérifiés:
- Après CHAQUE opération KHU (MINT, REDEEM, STAKE, UNSTAKE)
- Après CHAQUE yield quotidien
- À la FIN de chaque ConnectBlock()

Violation = bloc rejeté avec `"khu-invariant-violation"`.

---

## 4. OPÉRATIONS

### 4.1 Table des effets sur l'état

| Opération | C | U | Z | Cr | Ur | Invariants |
|-----------|---|---|---|----|----|------------|
| **MINT** | +P | +P | - | - | - | C==U+Z ✓ |
| **REDEEM** | -P | -P | - | - | - | C==U+Z ✓ |
| **STAKE** | - | -P | +P | - | - | C==U+Z ✓ |
| **YIELD** | - | - | - | +Y | +Y | Cr==Ur ✓ |
| **UNSTAKE** | +Y | +(P+Y) | -P | -Y | -Y | Both ✓ |

**Légende:** P = principal, Y = yield accumulé

### 4.2 MINT (PIV → KHU_T)

```
Input:  P PIV
Effect: C += P
        U += P
Output: P KHU_T
```

### 4.3 REDEEM (KHU_T → PIV)

```
Input:  P KHU_T
Effect: C -= P
        U -= P
Output: P PIV
```

### 4.4 STAKE (KHU_T → ZKHU)

```
Input:  P KHU_T
Effect: U -= P
        Z += P
Output: 1 ZKHU note (amount = P)
```

### 4.5 UNSTAKE (ZKHU → KHU_T) — Double Flux Atomique

```
Input:  1 ZKHU note (P = principal, Y = yield accumulé)
Effect: Z  -= P         // Principal retiré du shielded
        U  += P + Y     // Principal + Yield vers transparent
        C  += Y         // Yield ajoute au collateral
        Cr -= Y         // Yield consommé du reward pool
        Ur -= Y         // Yield consommé des droits
Output: (P + Y) KHU_T
```

**ATOMICITÉ:** Ces 5 mutations sont UNE transition atomique. Aucun état intermédiaire observable.

### 4.6 YIELD (quotidien)

```
Trigger: tous les 1440 blocs (1 jour)

Pour chaque note ZKHU mature (age >= 4320 blocs):
  daily = (note.amount × R_annual) / 10000 / 365
  note.accumulated += daily

Effet global:
  Cr += Σ(daily)
  Ur += Σ(daily)
```

---

## 5. MATURITY

### 5.1 Règle de maturity

```
MATURITY_BLOCKS = 4320  // 3 jours
mature = (current_height - stake_start_height) >= MATURITY_BLOCKS
```

### 5.2 Conséquences

- **Si immature (< 4320 blocs):** Y = 0 (AUCUN yield)
- **Si mature (>= 4320 blocs):** Y = accumulated yield

---

## 6. YIELD — FORMULE R%

### 6.1 Formule quotidienne

```
daily_yield = (principal × R_annual) / 10000 / 365
```

où `R_annual` est en basis points (4000 = 40%).

### 6.2 R_MAX_dynamic (plafond)

```
year = (nHeight - ACTIVATION_HEIGHT) / 525600
R_MAX_dynamic = max(700, 4000 - year × 100)

Schedule:
  Year 0:   4000 (40%)
  Year 10:  3000 (30%)
  Year 20:  2000 (20%)
  Year 33:  700  (7%) — minimum permanent
```

### 6.3 Contrainte

```
0 <= R_annual <= R_MAX_dynamic
```

### 6.4 Règle R% uniforme

> ⚠️ **R% est GLOBAL et IDENTIQUE pour TOUS les stakers à un instant T.**
>
> À jour J avec R_annual = X:
> - Alice (stakée depuis 100 jours) → reçoit X%
> - Bob (staké depuis 10 jours) → reçoit X%
> - Charlie (staké depuis 1 jour) → reçoit X%

---

## 7. ÉMISSION PIVX — TRANSITION V6

### 7.1 Avant V6 (PIVX V5.6 actuel)

```
Staker:     6 PIV/bloc
Masternode: 4 PIV/bloc
DAO:       10 PIV/bloc
─────────────────────────
Total:     20 PIV/bloc
```

### 7.2 À l'activation V6 — ZÉRO IMMÉDIAT

```cpp
// À V6_ACTIVATION_HEIGHT:
staker_reward = 0 PIV
mn_reward     = 0 PIV
dao_reward    = 0 PIV

Total = 0 PIV/bloc  // IMMÉDIATEMENT, pas de transition progressive
```

**RÈGLE ABSOLUE:** Dès le bloc V6, l'émission PIVX passe à **ZÉRO**.
Pas de schedule 6→5→4→...→0. C'est **0 immédiatement**.

### 7.3 Nouvelle économie post-V6

L'économie est entièrement gouvernée par:
- **R%** = Yield pour stakers ZKHU (voté par MN, 40%→7% sur 33 ans)
- **T** = DAO Treasury (accumule ~5%/an de U à R=40%)

### 7.4 Fees

```
Tous les fees: payés en PIV (pas de burn, va aux mineurs/stakers)
```

---

## 8. DAO TREASURY (T)

### 8.1 Accumulation quotidienne

```
T_daily = (U × R_annual) / 10000 / 8 / 365    // ~5% annuel avec T_DIVISOR=8

Trigger: tous les 1440 blocs (même timing que yield)
```

### 8.2 Post-V6

```
Dès V6: Block reward = 0 PIV
        T = seule source de financement DAO
        T accumule selon U × R% / 8 / 365
```

---

## 9. DOMC — GOUVERNANCE R%

### 9.1 Principe

```
R%     = Taux de yield annuel, VOTÉ par les masternodes
R_MAX  = Plafond du vote, DÉCROÎT AUTOMATIQUEMENT (-1%/an)

Les MN votent pour R% entre 0% et R_MAX
Après le vote → R% MIS À JOUR AUTOMATIQUEMENT
```

### 9.2 Cycle (4 mois)

```
DOMC_CYCLE_LENGTH = 172800        // 4 mois (~120 jours)
DOMC_COMMIT_OFFSET = 132480       // Début phase vote (commits secrets)
DOMC_REVEAL_OFFSET = 152640       // REVEAL instantané + début adaptation
DOMC_PHASE_DURATION = 20160       // ~2 semaines par phase

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

Timeline:
  [0 - 132479]:       R% actif, pas de vote
  [132480 - 152639]:  VOTE (commits secrets), R% toujours actif
  [152640]:           REVEAL instantané → futur R% visible
  [152640 - 172799]:  ADAPTATION (2 semaines pour s'adapter), R% toujours actif
  [172800]:           → Nouveau R% ACTIVÉ AUTOMATIQUEMENT
```

### 9.3 Calcul du nouveau R%

```
1. Collecter tous les votes révélés valides
2. median_R = median(all_valid_R_proposals)
3. if (median_R > R_MAX_dynamic) → median_R = R_MAX_dynamic
4. R_annual = median_R  // MISE À JOUR AUTOMATIQUE
```

### 9.4 R_MAX_dynamic — Plafond qui décroît

```
R_MAX_dynamic = max(700, 4000 - year × 100)

Année 0:   R_MAX = 40%  → MN peuvent voter entre 0% et 40%
Année 10:  R_MAX = 30%  → MN peuvent voter entre 0% et 30%
Année 20:  R_MAX = 20%  → MN peuvent voter entre 0% et 20%
Année 33+: R_MAX = 7%   → MN peuvent voter entre 0% et 7% (plancher)
```

### 9.5 Bootstrap

```
Initial R_annual = 4000 (40%)
R% actif IMMÉDIATEMENT à l'activation V6
Premier vote DOMC: ACTIVATION_HEIGHT + 172800 blocs (après 4 mois)
```

### 9.6 Résumé

| Élément | Qui décide? | Comment? |
|---------|-------------|----------|
| **R%** (taux réel) | Masternodes | Vote tous les 4 mois |
| **R_MAX** (plafond) | Protocole | Décroît automatiquement |
| **Mise à jour R%** | Automatique | Appliqué à la fin du cycle |

---

## 10. MASTERNODE FINALITY

### 10.1 Règle de finalité

```
finalized_height = tip_height - 12
```

### 10.2 Protection reorg

```
if (reorg_depth > 12) → REJECT
```

---

## 11. CONSTANTES

```cpp
// Timing
BLOCKS_PER_YEAR = 525600
BLOCKS_PER_DAY = 1440
MATURITY_BLOCKS = 4320          // 3 jours
FINALITY_DEPTH = 12

// DOMC
DOMC_CYCLE_LENGTH = 172800      // 4 mois
INITIAL_R = 4000                // 40%
FLOOR_R_MAX = 700               // 7%

// DAO Treasury
T_DIVISOR = 8                   // Treasury = 1/8 of yield rate (~5% at R=40%)

// Precision
COIN = 100000000                // 1 PIV/KHU = 10^8 satoshis
BASIS_POINT = 1                 // 0.01%
```

---

## 12. TRANSACTION TYPES

```cpp
enum TxType : int16_t {
    // PIVX standard
    NORMAL      = 0,
    PROREG      = 1,
    PROUPSERV   = 2,
    PROUPREG    = 3,
    PROUPREV    = 4,
    LLMQCOMM    = 5,

    // KHU types (Phase 2-6)
    KHU_MINT        = 6,    // PIV → KHU_T
    KHU_REDEEM      = 7,    // KHU_T → PIV
    KHU_STAKE       = 8,    // KHU_T → ZKHU
    KHU_UNSTAKE     = 9,    // ZKHU → KHU_T + yield
    KHU_DOMC_COMMIT = 10,   // DOMC commit (Hash(R || salt))
    KHU_DOMC_REVEAL = 11,   // DOMC reveal (R + salt)
};
```

---

## 13. ORDRE ConnectBlock (CANONIQUE)

```
1. LoadKhuState(height - 1)
2. ApplyDailyUpdatesIfNeeded()   // T + Yield (tous les 1440 blocs)
3. ProcessKHUTransactions()       // MINT, REDEEM, STAKE, UNSTAKE
4. ApplyBlockReward()             // Émission 6/6/6
5. CheckInvariants()
6. SaveKhuState(height)
```

> ⚠️ **Daily updates (étape 2) AVANT transactions (étape 3). TOUJOURS.**

---

## 14. INTERDICTIONS ABSOLUES

| # | Interdit | Raison |
|---|----------|--------|
| 1 | Modifier C sans U (ou inverse) | Viole INVARIANT_1 |
| 2 | Modifier Cr sans Ur (ou inverse) | Viole INVARIANT_2 |
| 3 | Yield avant maturity (4320 blocs) | Règle de maturity |
| 4 | R_annual > R_MAX_dynamic | Contrainte DOMC |
| 5 | Z→Z ZKHU transfers | Pas de privacy mixing |
| 6 | Burn KHU (hors REDEEM) | Conservation KHU |
| 7 | Float/double pour montants | Précision int64_t |
| 8 | Yield compounding | Yield linéaire uniquement |
| 9 | Transactions AVANT daily updates | Ordre ConnectBlock |
| 10 | Reorg > 12 blocs | Finalité MN |

---

## 15. HTLC (OPTIONNEL)

### 15.1 Structure

```cpp
struct HTLCPayload {
    uint256 hashlock;       // SHA256(preimage)
    uint32_t timelock;      // Block height
    int64_t amount;         // KHU_T
    CScript recipientScript;
    CScript refundScript;
};
```

### 15.2 Règles

- **CREATE:** Lock KHU_T, C/U inchangés
- **CLAIM:** Prouver preimage, before timelock
- **REFUND:** After timelock

### 15.3 Invariant preservation

HTLC ne crée ni ne détruit de KHU. C et U restent inchangés.

---

## VERSION HISTORY

```
2.0.0 - Refactoring complet:
        - Recentré sur règles MATHÉMATIQUES uniquement
        - Supprimé toute référence à l'organisation du code
        - Simplifié la structure (15 sections vs 20)
        - SPEC.md = source unique pour formules et invariants
1.6.1 - 17 champs, 172800 DOMC cycle, Y (yield) notation
```

---

## END OF SPECIFICATION

Ce document définit les règles de consensus PIVX-V6-KHU.

Pour l'organisation du code et les patterns d'implémentation, voir **ARCHITECTURE.md**.
