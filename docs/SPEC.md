# PIVX-V6-KHU — CANONICAL SPECIFICATION

Version: 2.0.0
Status: FINAL
Last Update: 2025-11-26

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

### 2.1 Structure canonique (17 champs)

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
    uint16_t R_annual;           // Yield rate (basis points 0-3700)
    uint16_t R_MAX_dynamic;      // Plafond R% dynamique

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

> ⚠️ **Ces 17 champs sont IMMUABLES.** Toute modification = hard fork.

### 2.2 Ordre de sérialisation (consensus)

L'ordre de sérialisation pour `GetHash()` est **SACRÉ**:

```
C → U → Z → Cr → Ur → T → R_annual → R_MAX_dynamic →
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

où `R_annual` est en basis points (3700 = 37%).

### 6.2 R_MAX_dynamic (plafond)

```
year = (nHeight - ACTIVATION_HEIGHT) / 525600
R_MAX_dynamic = max(400, 3700 - year × 100)

Schedule:
  Year 0:   3700 (37%)
  Year 10:  2700 (27%)
  Year 20:  1700 (17%)
  Year 33:  400  (4%) — minimum permanent
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

## 7. ÉMISSION PIVX

### 7.1 Formule sacrée

```cpp
year = (nHeight - ACTIVATION_HEIGHT) / BLOCKS_PER_YEAR;
reward_year = max(6 - year, 0) × COIN;
```

**Cette formule est IMMUABLE. Pas d'interpolation, pas de table.**

### 7.2 Distribution par bloc

```
staker_reward = reward_year
mn_reward     = reward_year
dao_reward    = reward_year

Total = 3 × reward_year
```

### 7.3 Schedule

| Année | reward_year | Par bloc | Par an |
|-------|-------------|----------|--------|
| 0 | 6 PIV | 18 PIV | 9,460,800 PIV |
| 1 | 5 PIV | 15 PIV | 7,884,000 PIV |
| 2 | 4 PIV | 12 PIV | 6,307,200 PIV |
| 3 | 3 PIV | 9 PIV | 4,730,400 PIV |
| 4 | 2 PIV | 6 PIV | 3,153,600 PIV |
| 5 | 1 PIV | 3 PIV | 1,576,800 PIV |
| 6+ | 0 PIV | 0 PIV | 0 PIV |

### 7.4 Fees

```
Tous les fees: payés en PIV, BURN (pas au producteur de bloc)
```

---

## 8. DAO TREASURY (T)

### 8.1 Accumulation quotidienne

```
T_daily = (U + Ur) / 182500    // 2% annuel / 365 jours

Trigger: tous les 1440 blocs (même timing que yield)
```

### 8.2 Timeline transition

```
Année 0:   DAO = 6 PIV/bloc + T accumule
Année 1+:  DAO = emission + T disponible pour proposals
Année 6+:  DAO = 0 PIV/bloc → T seule source
```

---

## 9. DOMC — GOUVERNANCE R%

### 9.1 Cycle (4 mois)

```
DOMC_CYCLE_LENGTH = 172800        // 4 mois (~120 jours)
DOMC_COMMIT_OFFSET = 132480       // Début phase commit
DOMC_REVEAL_OFFSET = 152640       // Début phase reveal
DOMC_PHASE_DURATION = 20160       // ~2 semaines par phase

Timeline:
  [0 - 132479]:       Normal operation, R_annual actif
  [132480 - 152639]:  COMMIT phase (MN submit votes)
  [152640 - 172799]:  REVEAL phase (votes revealed)
  [172800]:           Nouveau R_annual activé
```

### 9.2 Calcul du nouveau R%

```
1. Collecter tous les votes révélés valides
2. median_R = median(all_valid_R_proposals)
3. if (median_R > R_MAX_dynamic) median_R = R_MAX_dynamic
4. R_annual = median_R
```

### 9.3 Bootstrap

```
Initial R_annual = 3700 (37%)
R% actif IMMÉDIATEMENT à l'activation V6
Premier cycle DOMC: ACTIVATION_HEIGHT + 172800
```

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
INITIAL_R = 3700                // 37%
FLOOR_R_MAX = 400               // 4%

// DAO Treasury
T_ANNUAL_RATE = 200             // 2%
T_DAILY_DIVISOR = 182500

// Precision
COIN = 100000000                // 1 PIV/KHU = 10^8 satoshis
BASIS_POINT = 1                 // 0.01%
```

---

## 12. TRANSACTION TYPES

```cpp
enum TxType {
    TX_NORMAL        = 0,
    TX_KHU_MINT      = 10,
    TX_KHU_REDEEM    = 11,
    TX_KHU_STAKE     = 12,
    TX_KHU_UNSTAKE   = 13,
    TX_DOMC_VOTE     = 14,
    TX_HTLC_CREATE   = 15,
    TX_HTLC_CLAIM    = 16,
    TX_HTLC_REFUND   = 17,
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
