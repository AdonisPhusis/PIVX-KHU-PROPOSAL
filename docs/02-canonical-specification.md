# 02 — PIVX-V6-KHU CANONICAL SPECIFICATION

Version: 1.4.0
Status: FINAL
Style: Core Consensus Rules

---

## 1. ASSETS

### 1.1 Définition

**KHU = Knowledge Hedge Unit**

KHU est un **colored coin collatéralisé 1:1** par PIV. Ce n'est PAS un stablecoin (pas de peg USD/EUR).
C'est une unité de couverture de connaissance — une représentation tokenisée de PIV avec des propriétés étendues (staking privé, yield gouverné).

### 1.2 Types d'actifs

```
PIV     : Native asset (existing)
KHU_T   : Knowledge Hedge Unit - Transparent UTXO (colored coin, internal tracking)
ZKHU    : Knowledge Hedge Unit - Sapling note (staking only, private)
```

---

## 2. GLOBAL STATE

### 2.1 Structure canonique : KhuGlobalState

La structure KhuGlobalState représente l'état économique global du système KHU à un height donné.

Elle contient **15 champs**, définis comme suit :

```cpp
struct KhuGlobalState {
    int64_t  C;      // Collatéral total PIV (inclut collatéralisations de bonus)
    int64_t  U;      // Supply totale KHU_T
    int64_t  Cr;     // Collatéral réservé au pool de reward
    int64_t  Ur;     // Reward accumulé (droits de reward)
    int64_t  T;      // DAO Treasury Pool (Phase 6: accumulation automatique)

    uint16_t R_annual;        // Paramètre DOMC (basis points, ex: 500 = 5.00%)
    uint16_t R_MAX_dynamic;   // Limite supérieure votable dynamique

    uint32_t last_domc_height;        // Dernière mise à jour DOMC
    uint32_t domc_cycle_start;        // Début du cycle DOMC actif
    uint32_t domc_cycle_length;       // 43200 blocs (cycle)
    uint32_t domc_phase_length;       // 4320 blocs (commit/reveal)

    uint32_t last_yield_update_height; // Dernière application yield

    uint32_t nHeight;       // Hauteur de cet état
    uint256  hashPrevState; // Hash de l'état précédent
    uint256  hashBlock;     // Hash du bloc ayant produit cet état

    // Invariant verification
    // NOTE: Z (total ZKHU staked) is computed via GetTotalStakedZKHU()
    bool CheckInvariants(int64_t Z) const {
        // Verify non-negativity
        if (C < 0 || U < 0 || Cr < 0 || Ur < 0 || T < 0 || Z < 0)
            return false;

        // INVARIANT 1: Total Collateralization (C == U + Z)
        // C = PIV collateral, U = transparent KHU, Z = shielded ZKHU
        bool cd_ok = (C == U + Z);

        // INVARIANT 2: Reward Collateralization
        bool cdr_ok = (Ur == 0 || Cr == Ur);

        // INVARIANT 3: Treasury Non-Negative (T can grow independently)
        // T >= 0 (verified above)

        return cd_ok && cdr_ok;
    }

    uint256 GetHash() const;
    SERIALIZE_METHODS(KhuGlobalState, obj) { /* ... */ }
};
```

**Note:** Ces 15 champs constituent le KhuGlobalState canonique. Toute implémentation doit refléter EXACTEMENT cette structure.

> ⚠️ **ANTI-DÉRIVE: CHECKSUM DE STRUCTURE (doc 02 ↔ doc 03)**
>
> **VERIFICATION DE COHERENCE ENTRE DOCUMENTS:**
>
> La structure `KhuGlobalState` doit être **IDENTIQUE** dans docs 02 et 03.
>
> **CHECKSUM DE STRUCTURE (15 champs) :**
> ```
> SHA256("int64_t C | int64_t U | int64_t Cr | int64_t Ur | int64_t T | uint16_t R_annual | uint16_t R_MAX_dynamic | uint32_t last_domc_height | uint32_t domc_cycle_start | uint32_t domc_cycle_length | uint32_t domc_phase_length | uint32_t last_yield_update_height | uint32_t nHeight | uint256 hashPrevState | uint256 hashBlock")
> = 0x7d3e2a1f...  (fixed reference checksum - UPDATED for Phase 6)
> ```
>
> **RÈGLES DE SYNCHRONISATION:**
>
> 1. ✅ Docs 02 et 03 DOIVENT avoir les 15 mêmes champs
> 2. ✅ Même ordre de déclaration (C, U, Cr, Ur, T, ...)
> 3. ✅ Mêmes types exacts (int64_t, uint16_t, uint32_t, uint256)
> 4. ✅ Même méthode CheckInvariants() avec logique identique
> 5. ✅ Même signature SERIALIZE_METHODS
>
> **VERIFICATION AVANT IMPLEMENTATION:**
>
> ```bash
> # Extract struct from doc 02
> grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md > /tmp/struct_02.txt
>
> # Extract struct from doc 03
> grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md > /tmp/struct_03.txt
>
> # Compare - MUST be identical (ignoring comments)
> diff /tmp/struct_02.txt /tmp/struct_03.txt
> ```
>
> **If diff shows ANY difference in field names, types, or order → STOP and fix synchronization.**
>
> **MODIFICATION PROTOCOL:**
>
> If KhuGlobalState needs to be modified:
> 1. Update doc 02 first (canonical source)
> 2. Immediately update doc 03 to match EXACTLY
> 3. Update checksum in both documents
> 4. Run diff verification
> 5. NEVER modify only one document

### 2.2 Invariants (version canonique)

Les invariants doivent être vrais **à chaque fin de ConnectBlock()** :

**Soit `Z = Σ(active ZKHU notes)` la somme des montants ZKHU stakés.**

**INVARIANT 1 — Total Collateralization :**
```
C == U + Z

où:
  C = PIV collateral total
  U = KHU_T en circulation (transparent)
  Z = ZKHU stakés (shielded, calculé via GetTotalStakedZKHU())
```

**INVARIANT 2 — Reward Collateralization (CDr = 1) :**
```
(Ur == 0 && Cr == 0)  OR  (Cr == Ur)
```

**Vérification:**

Les invariants doivent être vérifiés immédiatement après :
- AccumulateDaoTreasury (Phase 6)
- ApplyDailyYield
- ApplyKHUTransactions
- ApplyKhuUnstake
- ApplyBlockReward

Tout bloc pour lequel l'un de ces invariants est violé doit être rejeté avec code `BLOCK_CONSENSUS`.

**Atomicité:**

Toute opération qui modifie C, U, Cr, ou Ur doit garantir que les invariants restent valides **après l'opération complète**. Les états intermédiaires qui violent les invariants ne doivent jamais être observables (voir section 7.2.3 pour UNSTAKE).

---

## 3. CONSENSUS OPERATIONS

### 3.1 MINT

```
Input:  amount PIV (burned)
Effect: C  += amount  (atomique)
        U  += amount  (atomique)
Output: amount KHU_T (created)
```

**Rules:**
- PIV must be provably burned (unspendable output).
- KHU_T created in single UTXO to recipient.
- INVARIANT_1 preserved.

**Atomicity:**
C and U must be updated **in the same ConnectBlock execution**.
No intermediate state where C is modified but U is not (or vice versa).
Both mutations confined to single function call under global state lock.

### 3.2 REDEEM

```
Input:  amount KHU_T (spent)
Effect: C  -= amount  (atomique)
        U  -= amount  (atomique)
Output: amount PIV (created from collateral)
```

**Rules:**
- KHU_T UTXOs must exist and be spendable.
- PIV created to recipient.
- INVARIANT_1 preserved.

**Atomicity:**
C and U must be updated **in the same ConnectBlock execution**.
No intermediate state where C is decremented but U is not (or vice versa).
Both mutations confined to single function call under global state lock.

### 3.3 BURN

**Prohibited.** Only REDEEM destroys KHU.

### 3.4 Authorized Operations

```
OPÉRATIONS INDÉPENDANTES (pas de séquence obligatoire):

  MINT:    PIV → KHU_T       (instantané)
  REDEEM:  KHU_T → PIV       (instantané)
  STAKE:   KHU_T → ZKHU      (optionnel, pour yield R%)
  UNSTAKE: ZKHU → KHU_T      (après maturity 4320 blocs)

FLUX POSSIBLES:

  Simple:   PIV → MINT → KHU_T → REDEEM → PIV
  Complet:  PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
  Mixte:    MINT/REDEEM en boucle + STAKE partiel pour yield
```

**STAKE/UNSTAKE sont OPTIONNELS.** On peut MINT/REDEEM sans jamais staker.

### 3.4.1 Operations State Table (Canonical)

**Soit Z = Σ(active ZKHU notes) calculé dynamiquement via GetTotalStakedZKHU().**

| Opération | C | U | Z | Cr | Ur | Invariants |
|-----------|---|---|---|----|----|------------|
| **MINT** | +a | +a | - | - | - | C==U+Z ✓ |
| **REDEEM** | -a | -a | - | - | - | C==U+Z ✓ |
| **STAKE** | - | **-a** | +a | - | - | C==U+Z ✓ |
| **YIELD** | - | - | - | **+y** | **+y** | Cr==Ur ✓ |
| **UNSTAKE** | +y | +(a+y) | -a | -y | -y | Both ✓ |

**Légende:**
- `a` = montant de l'opération (amount / principal)
- `y` = yield accumulé (pour UNSTAKE: Ur_note accumulé)
- `-` = pas de changement
- Z est calculé dynamiquement (pas stocké dans state)

### 3.5 Atomicité du Double Flux (semantics canonique)

Les opérations suivantes constituent une *transition atomique unique* :

**1. Yield quotidien (après maturité 3 jours et toutes les 1440 blocs) :**
```
Ur  += Δ
Cr  += Δ
```

**2. UNSTAKE d'une note ZKHU :**
```
U   += B
C   += B
Cr  -= B
Ur  -= B
```
(où B = Ur accumulé pour cette note)

**Règles d'atomicité:**

Ces mises à jour doivent être appliquées :
- dans le même bloc
- dans la même exécution de ConnectBlock()
- sous LOCK(cs_khu)
- sans état intermédiaire persistant

Toute exécution partielle DOIT provoquer:
```
reject_block("khu-invariant-violation")
```

### 3.6 Atomicité MINT & REDEEM

**MINT :**
```
C += amount
U += amount
```

**REDEEM :**
```
C -= amount
U -= amount
```

Ces opérations sont atomiques : C et U NE DOIVENT JAMAIS être modifiés séparément.

### 3.7 Opérations canoniques KHU (immuables)

```
Opérations autorisées (INDÉPENDANTES, pas de séquence obligatoire):
  • MINT:    PIV → KHU_T
  • REDEEM:  KHU_T → PIV
  • STAKE:   KHU_T → ZKHU (optionnel)
  • UNSTAKE: ZKHU → KHU_T + bonus (optionnel)
```

Aucune autre transformation n'est autorisée. STAKE/UNSTAKE sont optionnels.

### 3.8 Ordre canonique ConnectBlock()

```
1. LoadKhuState(height - 1)
2. ApplyDailyUpdatesIfNeeded()  // Phase 6: T += 2%/365 × (U+Ur) + Yield (même trigger daily)
3. ProcessKHUTransactions()
4. ApplyBlockReward()
5. CheckInvariants()
6. SaveKhuState(height)
```

**Note:** T accumulation et Yield sont appliqués ensemble tous les 1440 blocs (1 jour).

Cet ordre est **immuable** et doit être respecté strictement dans l'implémentation.

> ⚠️ **ANTI-DÉRIVE CRITIQUE : ORDRE DAILY_UPDATES → TRANSACTIONS**
>
> **L'étape 2 (ApplyDailyUpdatesIfNeeded) DOIT s'exécuter EN PREMIER (avant transactions).**
>
> **ARCHITECTURE SIMPLIFIÉE (v1.3):**
>
> T accumulation et Yield sont maintenant **unifiés** dans la même fonction `ApplyDailyUpdatesIfNeeded()`:
>
> ```cpp
> void ApplyDailyUpdatesIfNeeded(KhuGlobalState& state, uint32_t nHeight) {
>     if ((nHeight - state.last_yield_update_height) < BLOCKS_PER_DAY)
>         return;  // Not a daily update block
>
>     // 1. DAO Treasury accumulation (2% annual) — GLOBAL sur total
>     //    T = calcul unique sur (U + Ur) global
>     CAmount T_daily = (state.U + state.Ur) / 182500;
>     state.T += T_daily;
>
>     // 2. Yield accumulation (R% annual) — INDIVIDUEL par note
>     //    Chaque note stakée reçoit son yield basé sur note.amount
>     CAmount totalYield = 0;
>     for (auto& note : GetAllStakedNotes()) {
>         CAmount noteYield = (note.amount * R_annual) / 10000 / 365;
>         note.accumulatedYield += noteYield;
>         totalYield += noteYield;
>     }
>     state.Cr += totalYield;
>     state.Ur += totalYield;
>
>     state.last_yield_update_height = nHeight;
> }
> ```
>
> **DISTINCTION CRITIQUE T vs YIELD:**
>
> | Métrique | Calcul | Base | Destination |
> |----------|--------|------|-------------|
> | **T** | GLOBAL (1 calcul) | `(U + Ur) / 182500` | `state.T` |
> | **Yield** | INDIVIDUEL (par note) | `note.amount × R / 10000 / 365` | `note.accumulatedYield` + `state.Cr/Ur` |
>
> **L'ordre T → Yield est maintenu atomiquement.**
>
> **RAISON CRITIQUE POUR DAILY_UPDATES AVANT TRANSACTIONS:**
>
> Si un bloc contient à la fois :
> - Un update du yield quotidien (tous les 1440 blocs)
> - Une transaction UNSTAKE d'une note mature
>
> Et que l'ordre est INVERSÉ (transactions → yield), voici ce qui se passe :
>
> ```
> // SCÉNARIO D'ERREUR (ordre inversé)
> 1. ProcessKHUTransactions() exécuté en PREMIER
>    → UNSTAKE utilise note.Ur_accumulated AVANT que le yield du jour soit ajouté
>    → Le bonus calculé est INCORRECT (manque le yield d'aujourd'hui)
>    → state.Ur -= B (où B est FAUX, trop petit)
>
> 2. ApplyDailyYieldIfNeeded() exécuté EN SECOND
>    → state.Ur += daily_yield
>    → Mais le Ur de la note unstaked a DÉJÀ été soustrait avec un montant faux
>
> 3. Résultat : INVARIANT VIOLATION
>    → state.Cr != state.Ur
>    → Bloc rejeté
>    → Consensus failure
> ```
>
> **ORDRE CORRECT (yield → transactions) :**
>
> ```
> 1. ApplyDailyYieldIfNeeded() exécuté en PREMIER
>    → note.Ur_accumulated += daily_yield
>    → state.Ur += total_daily_yield
>    → state.Cr += total_daily_yield
>
> 2. ProcessKHUTransactions() exécuté EN SECOND
>    → UNSTAKE utilise note.Ur_accumulated AVEC le yield d'aujourd'hui inclus
>    → B = note.Ur_accumulated (valeur CORRECTE)
>    → state.Ur -= B (montant CORRECT)
>    → state.Cr -= B (montant CORRECT)
>
> 3. Résultat : INVARIANTS OK
>    → state.Cr == state.Ur (strict equality)
>    → Bloc accepté
> ```
>
> **RÈGLE ABSOLUE :**
>
> L'ordre `ApplyDailyYieldIfNeeded() → ProcessKHUTransactions()` est **OBLIGATOIRE**.
>
> Cette règle ne peut JAMAIS être changée, optimisée, ou réordonnée pour quelque raison que ce soit.
>
> Violation = consensus failure immédiat.

---

## 4. TRANSACTION TYPES

```cpp
enum TxType {
    TX_NORMAL          = 0,
    // ... existing types ...
    TX_KHU_MINT        = 10,
    TX_KHU_REDEEM      = 11,
    TX_KHU_STAKE       = 12,
    TX_KHU_UNSTAKE     = 13,
    TX_DOMC_VOTE       = 14,
    TX_HTLC_CREATE     = 15,
    TX_HTLC_CLAIM      = 16,
    TX_HTLC_REFUND     = 17,
};
```

---

## 5. BLOCK REWARD

### 5.1 Emission Formula

```cpp
const uint32_t BLOCKS_PER_YEAR = 525600;
const uint32_t ACTIVATION_HEIGHT = <TBD>;

uint32_t year = (nHeight - ACTIVATION_HEIGHT) / BLOCKS_PER_YEAR;
int64_t reward_year = std::max(6 - (int64_t)year, 0LL) * COIN;
```

### 5.2 Distribution

```
staker_reward = reward_year
mn_reward     = reward_year
dao_reward    = reward_year

Total emission per block = 3 * reward_year
```

### 5.3 Emission Schedule

| Year | Height Range | reward_year | Per Block | Per Year |
|------|-------------|-------------|-----------|----------|
| 0 | 0-525599 | 6 PIV | 18 PIV | 9,460,800 PIV |
| 1 | 525600-1051199 | 5 PIV | 15 PIV | 7,884,000 PIV |
| 2 | 1051200-1576799 | 4 PIV | 12 PIV | 6,307,200 PIV |
| 3 | 1576800-2102399 | 3 PIV | 9 PIV | 4,730,400 PIV |
| 4 | 2102400-2627999 | 2 PIV | 6 PIV | 3,153,600 PIV |
| 5 | 2628000-3153599 | 1 PIV | 3 PIV | 1,576,800 PIV |
| 6+ | 3153600+ | 0 PIV | 0 PIV | 0 PIV |

**Total cap: 33,112,800 PIV (if starting from genesis).**

### 5.4 Fees

```
All PIV transaction fees: BURNED (not given to block producer).
KHU transaction fees: PIV (burned, same as PIV transactions).
```

**Note importante :**
- Les opérations KHU (MINT, REDEEM, STAKE, UNSTAKE, Transfer) paient des **fees en PIV**
- Les fees PIV sont **BURN** (pas donnés au producteur de bloc)
- **Aucun fee en KHU** : Impossible de payer fees avec KHU
- Fees identiques aux transactions PIVX standard

### 5.5 Inviolabilité de l'Émission

**RÈGLE ABSOLUE:**

L'émission PIVX définie ci-dessus est **IMMUABLE** et **NON-GOUVERNABLE**.

- ❌ DOMC ne peut PAS modifier reward_year
- ❌ Masternodes ne peuvent PAS voter sur l'émission
- ❌ Aucun paramètre runtime ne peut ajuster la formule
- ❌ Aucun soft fork ne peut changer le schedule
- ✅ Seul un hard fork avec consensus communautaire peut modifier cette règle

**SÉPARATION STRICTE:**

```
Émission PIVX (reward_year) ≠ Yield KHU (R%)
```

- **reward_year** contrôle l'offre totale PIV (déflationnaire, fixed schedule)
- **R%** contrôle les rewards de staking KHU (gouverné par DOMC, borné par R_MAX_dynamic)

Ces deux systèmes sont **INDÉPENDANTS** et ne doivent JAMAIS être confondus.

**ANTI-DÉRIVE:**

Interdictions absolues dans l'implémentation:
- Pas d'interpolation ou transition douce entre années
- Pas de year_fraction ou calcul proportionnel
- Pas de table alternative ou cache pré-calculé
- Pas de modulation selon hashrate/staking/network
- La formule `max(6 - year, 0)` est sacrée et inchangeable

### 5.6 DAO Treasury Accumulation (Phase 6)

**Règle:**

**Tous les jours** (1440 blocs), le pool interne T s'incrémente automatiquement:

```
T_daily = (U + Ur) × 2% / 365
T_daily = (U + Ur) × 200 / 10000 / 365
T_daily = (U + Ur) / 182500

T += T_daily
```

**Taux annuel: 2% maximum d'inflation vers T**

**Propriétés:**

- **Automatique:** Aucun vote, aucune gouvernance pour l'accumulation
- **Déterministe:** Formule fixe, basée sur état début de bloc
- **Perpétuel:** Continue indéfiniment (même après année 6)
- **Internal Pool:** T est un champ dans KhuGlobalState (pas une address externe)
- **Indépendant:** N'affecte PAS C, U, Cr, Ur (pas de création monétaire immédiate)
- **Daily:** Accumulation quotidienne alignée avec le yield R%

#### 5.6.1 Transition Timeline — AUCUN GAP de Financement

> ⚠️ **CLARIFICATION IMPORTANTE — Financement DAO**
>
> **Il n'y a AUCUN gap de financement entre la fin des block rewards et T.**
>
> T accumule dès l'Année 0 (activation V6), AVANT que les block rewards ne cessent.
>
> ```
> TIMELINE DE FINANCEMENT DAO:
>
> Année 0-5:  Block rewards (6→1 PIV/bloc) + T accumule
>             ├─ DAO reçoit PIV via block rewards (budget système existant)
>             └─ T accumule 2%/an de (U+Ur) EN PARALLÈLE
>
> Année 6:    Block rewards = 0 + T disponible
>             ├─ Les block rewards s'arrêtent (max(6-6,0) = 0)
>             └─ T DÉJÀ accumulé depuis 6 ans est disponible
>
> Année 7+:   T continue d'accumuler
>             └─ DAO financée uniquement par T
> ```
>
> **Exemple numérique (hypothétique):**
> ```
> Si à l'Année 6, (U + Ur) = 10M KHU:
>   T_accumulé = ~1.2M KHU sur 6 ans (2%/an × 6 ans, approximatif)
>   T_annuel   = 200k KHU/an
>
> La DAO dispose d'un budget IMMÉDIATEMENT à la fin des block rewards.
> ```
>
> **Il n'y a donc JAMAIS de période sans financement DAO.**

**Trigger (même timing que yield):**

```cpp
// T accumulates daily, same trigger as yield
bool IsDaoAccumulationDay(uint32_t nHeight, uint32_t last_yield_height) {
    return (nHeight - last_yield_height) >= BLOCKS_PER_DAY;  // 1440 blocks
}
```

**Budget Calculation:**

```cpp
// 2% annual = 200 basis points / 365 days
const int64_t T_ANNUAL_RATE = 200;  // 2% in basis points

CAmount CalculateDailyDAOBudget(const KhuGlobalState& state) {
    __int128 total = (__int128)state.U + (__int128)state.Ur;
    __int128 budget = (total * T_ANNUAL_RATE) / 10000 / 365;
    // Simplified: budget = total / 182500

    if (budget < 0 || budget > MAX_MONEY) {
        return 0;  // Overflow protection
    }

    return (CAmount)budget;
}
```

**Accumulation:**

```cpp
void AccumulateDaoTreasuryDaily(KhuGlobalState& state) {
    CAmount budget = CalculateDailyDAOBudget(state);
    state.T += budget;

    // T overflow protection
    if (state.T < 0 || state.T > MAX_MONEY) {
        reject_block("dao-treasury-overflow");
    }
}
```

**Phase 7 (Future):**

Les propositions DAO pourront dépenser depuis T (avec vote MN approval). Phase 6 implémente uniquement l'accumulation.

### 5.7 DAO Funding Transition (Année 1+)

**Règle de transition:**

À partir de l'**Année 1** (V6 + 525600 blocs), le système DAO existant commence à utiliser T au lieu des block rewards.

```
Année 0:     DAO = 6 PIV/bloc (emission) + T accumule 2%/an
Année 1:     DAO = 5 PIV/bloc (emission) + T disponible pour proposals
Année 2-5:   DAO emission décroît, T devient source principale
Année 6+:    DAO = 0 PIV/bloc → T est la SEULE source de funding
```

**Système Dual → Transition:**

```
Phase 1 (Année 0):
  - Block reward DAO = 6 PIV/bloc
  - T accumule 2%/an (reserve building)
  - DAO proposals NON actives

Phase 2 (Année 1+):
  - Block reward DAO = 5→0 PIV/bloc (décroissant)
  - T disponible pour DAO proposals (vote MN)
  - Transition progressive vers T-only

Phase 3 (Année 6+):
  - Block reward DAO = 0 PIV/bloc
  - T = SEULE source de funding DAO
  - Système autosuffisant via 2% inflation T
```

**Cette transition est AUTOMATIQUE et NE NÉCESSITE PAS de hard fork.**

---

## 6. MASTERNODE FINALITY

### 6.1 Finality Rule

```
finalized_height = tip_height - 12
```

**Block at `finalized_height` must have valid BLS quorum signature.**

### 6.2 Quorum Signature

```cpp
struct KHUStateCommitment {
    uint32_t nHeight;
    uint256 hashKHUState;      // Hash(KHUGlobalState)
    uint256 hashBlock;
    CBLSSignature sig;         // Aggregated BLS signature
};
```

**Validation:**
- Quorum size: LLMQ_400_60 or equivalent.
- Signature must verify against quorum public key.
- `hashKHUState` must match actual state at `nHeight`.

### 6.3 Reorg Protection

```
if (reorg_depth > 12) → REJECT
```

### 6.4 Quorum Rotation

```
rotation_interval = 240 blocks
```

New quorum selected deterministically every 240 blocks via LLMQ DKG.

---

## 7. SAPLING INTEGRATION

### 7.1 STAKE (KHU_T → ZKHU)

```
Input:  amount KHU_T (transparent UTXO)
Effect: KHU_T spent
        ZKHU note created
        U  -= amount  (KHU_T supply decreases)
        // C unchanged (collateral still backs U + Z)
        // Z increases via note creation (computed dynamically)
Output: 1 ZKHU note
```

**State Changes:**
- `U -= amount` (transparent supply decreases)
- Z increases by `amount` (shielded supply increases)
- Net effect on `C == U + Z`: preserved (U decreases, Z increases by same amount)

**Rules:**
- Exactly 1 note per STAKE transaction.
- Note memo encodes: `stake_start_height`, `stake_amount`.
- No Z→Z transfers allowed.
- Rolling Frontier Tree for note commitments.

### 7.2 UNSTAKE (ZKHU → KHU_T)

```
Input:  ZKHU note (nullifier revealed)
Effect: ZKHU note spent
        Yield calculated if mature
        KHU_T created (original + bonus)
Output: (stake_amount + bonus) KHU_T
```

**Maturity:**
```
maturity_blocks = 4320  // 3 days
mature = (current_height - stake_start_height) >= maturity_blocks
```

**If mature:**
```
days_staked = (current_height - stake_start_height) / 1440
Ur_accumulated = (stake_amount * R_annual / 100 / 365) * days_staked

bonus = Ur_accumulated
U  += bonus
C  += bonus
Cr -= bonus
Ur -= bonus
```

**If immature:**
```
bonus = 0
```

**Output address:**
```
UNSTAKE must create output to NEW address (never reuse staking address).
Auto-getnewaddress enforced by wallet.
Privacy protection mandatory.
```

**Invariants preserved.**

### 7.2.3 Atomicité du double flux (UNSTAKE)

#### Principe fondamental

Lors d'un UNSTAKE, le protocole applique un **double flux atomique** sur l'état global:

1. **Flux descendant (reward → staker)**: Cr → Ur → KHU_T (bonus accumulé B)
2. **Flux ascendant (principal → supply)**: ZKHU → KHU_T (principal P)

Ces deux flux s'exécutent **atomiquement** dans un seul `ConnectBlock`. Échec partiel = rejet du bloc entier (pas de rollback partiel).

#### Définition du double flux

Soit `B` le montant de bonus (Ur accumulé pour la note stakée):

```
U'  = U  + B   (création de B unités de KHU_T pour le staker)
C'  = C  + B   (augmentation de la collatéralisation de B)
Cr' = Cr - B   (consommation du pool de rendement à hauteur de B)
Ur' = Ur - B   (suppression des droits de rendement consommés)
```

**Note critique:** Le principal `P` ne modifie PAS l'état global (C, U, Cr, Ur) car il était déjà compté dans U au moment du STAKE initial. Seul le bonus `B` crée un double flux.

#### Ordre d'exécution atomique

1. **Vérifications préalables** (AVANT toute mutation):
   - Nullifier non dépensé
   - Maturité (≥ 4320 blocs)
   - Cr >= B et Ur >= B

2. **Double flux atomique** (ne peut être interrompu):
   ```cpp
   state.U  += B;   // Flux ascendant (création KHU_T)
   state.C  += B;   // Flux ascendant (collatéralisation)
   state.Cr -= B;   // Flux descendant (consommation pool)
   state.Ur -= B;   // Flux descendant (consommation droits)
   ```

3. **Vérification invariants IMMÉDIATE**:
   - Si `!state.CheckInvariants()` → bloc rejeté
   - Pas de rollback partiel autorisé

4. **Création UTXO** (principal + bonus):
   - `CreateKHUUTXO(P + B, recipientScript)`

#### Règle d'atomicité

Ces quatre mutations (étape 2) doivent s'exécuter:
- **Sans interruption possible**
- Dans le même appel de fonction
- Sous le même verrou `cs_khu`
- Dans le même `ConnectBlock`

Toute exécution partielle viole les invariants et entraîne le rejet du bloc par `CheckInvariants()`.

#### Interdictions absolues

**1. Pas de rollback partiel:**

❌ INTERDIT:
```cpp
state.U += B;
state.C += B;
if (state.Cr < B) {
    state.U -= B;  // ❌ Rollback interdit
    state.C -= B;
    return false;
}
```

✅ CORRECT:
```cpp
// Vérification AVANT mutation
if (state.Cr < B || state.Ur < B)
    return false;  // Rejet avant toute mutation

// Mutation atomique (pas de rollback possible)
state.U  += B;
state.C  += B;
state.Cr -= B;
state.Ur -= B;
```

**2. Pas de mutation hors ConnectBlock:**

❌ INTERDIT: Mutation dans `AcceptToMemoryPool`

✅ CORRECT: Validation seule dans mempool, mutation uniquement dans `ConnectBlock`

**3. Pas de yield compounding:**

Le yield accumulé (`Ur_accumulated`) d'une note ZKHU **ne génère PAS lui-même de yield**.

❌ INTERDIT: `daily = ((note.amount + note.Ur_accumulated) * R_annual) / 10000 / 365`

✅ CORRECT: `daily = (note.amount * R_annual) / 10000 / 365`

**4. Pas de funding externe du reward pool — AXIOME CANONIQUE:**

```cpp
// ✅ AXIOME IMMUABLE
const int64_t KHUPoolInjection = 0;
```

**Il n'existe AUCUNE injection externe ou interne vers Cr ou Ur.**

Cr et Ur évoluent EXCLUSIVEMENT via:
1. YIELD quotidien: `Cr += Δ`, `Ur += Δ`
2. UNSTAKE: `Cr -= B`, `Ur -= B`

Toute autre mutation (y compris depuis émission PIVX, fees, DAO, MN rewards) est **INTERDITE** et doit provoquer un rejet de bloc.

#### Propriétés garanties

- **Atomicité**: Les 4 mutations réussissent toutes ou le bloc est rejeté (XOR logique)
- **Conservation**: Cr == Ur maintenu avant et après UNSTAKE
- **Pas de création de valeur**: Seul le reward pool finance le bonus B
- **Ordre canonique**: Yield update AVANT UNSTAKE dans le même bloc (voir section 11.1)

### 7.3 Prohibited

```
Z→Z KHU transfers: FORBIDDEN
Multiple notes per STAKE: FORBIDDEN
Partial UNSTAKE: FORBIDDEN (must unstake entire note)
```

---

## 8. YIELD MECHANISM (Cr/Ur)

### 8.1 R% Bounds

```cpp
uint32_t year = (nHeight - ACTIVATION_HEIGHT) / BLOCKS_PER_YEAR;
uint16_t R_MAX_dynamic = std::max(400, 3700 - year * 100);  // basis points (37%→4% over 33 years)

// R_MAX_dynamic schedule:
// Year 0:  3700 (37%)
// Year 10: 2700 (27%)
// Year 20: 1700 (17%)
// Year 33: 400  (4%) - minimum
// Year 34+: 400 (4%)
```

**Constraint:**
```
0 <= R_annual <= R_MAX_dynamic
```

### 8.2 Daily Update (1440 blocks)

```cpp
if ((nHeight - last_yield_update_height) >= 1440) {
    for each active ZKHU note:
        if (nHeight - note.stake_start_height >= 4320) {
            Ur_daily = (note.amount * R_annual / 10000) / 365;
            Ur += Ur_daily;
            Cr += Ur_daily;
        }

    last_yield_update_height = nHeight;
}
```

**INVARIANT_2 preserved: Cr == Ur always.**

### 8.2.1 R% Global et Uniforme (RÈGLE CRITIQUE)

> ⚠️ **CLARIFICATION IMPORTANTE — ANTI-CONFUSION**
>
> **R% est GLOBAL et IDENTIQUE pour TOUS les stakers à un instant T.**
>
> ```
> À jour J avec R_annual = X:
>   - Alice (stakée depuis 100 jours) → reçoit X%
>   - Bob (staké depuis 10 jours)     → reçoit X%
>   - Charlie (staké depuis 1 jour)   → reçoit X%
> ```
>
> **Ce n'est PAS une "dilution des stakers".**
> **C'est une "dilution des arbitrageurs tardifs":**
>
> - R% diminue au fil du temps (37% → 4% sur 33 ans via DOMC/R_MAX)
> - Un arbitrageur qui arrive TARD reçoit un R% plus faible
> - Mais à un instant donné, TOUS les stakers actifs reçoivent le MÊME taux
>
> **Exemple temporel:**
> ```
> Année 0: R_MAX=30%, DOMC vote R=25%
>   → Alice stake → reçoit 25%
>   → Bob stake   → reçoit 25% (même que Alice)
>
> Année 5: R_MAX=25%, DOMC vote R=20%
>   → Alice (toujours stakée) → reçoit 20%
>   → Bob (toujours staké)    → reçoit 20%
>   → Charlie arrive          → reçoit 20% (même que Alice et Bob)
> ```
>
> **Code source (khu_yield.cpp ligne 114):**
> ```cpp
> // Le MÊME R_annual pour toutes les notes
> CAmount dailyYield = CalculateDailyYieldForNote(note.amount, R_annual);
> ```

### 8.3 Yield Restrictions

```
- No yield for immature notes (< 4320 blocks).
- No compounding (Ur not staked automatically).
- Linear accumulation only (daily addition, never exponential).
- Ur only realized on UNSTAKE.
- No early withdrawal of yield.
```

---

## 9. DOMC (GOVERNANCE OF R%)

### 9.1 Vote Cycle

```cpp
const uint32_t DOMC_CYCLE_LENGTH = 43200;   // 30 days
const uint32_t DOMC_PHASE_LENGTH = 4320;    // 3 days

Phase 1: COMMIT   (blocks 0-4319)
Phase 2: REVEAL   (blocks 4320-8639)
Phase 3: TALLY    (blocks 8640-12959)
Phase 4: FINALIZE (blocks 12960-17279)
... (up to 10 phases total)
```

### 9.2 Vote Transaction

```cpp
struct DOMCVotePayload {
    uint16_t R_proposal;        // Proposed R% (basis points)
    uint256 commitment;         // Hash(R_proposal || secret) [COMMIT phase]
    uint256 secret;             // Revealed in REVEAL phase
    CPubKey voterPubKey;        // Masternode key
    std::vector<unsigned char> signature;
};
```

**Rules:**
- Only masternodes can vote.
- One vote per masternode per cycle.
- COMMIT: submit `commitment = Hash(R_proposal || secret)`.
- REVEAL: submit `R_proposal` and `secret`, verify commitment.
- Invalid reveals are discarded.

### 9.3 Tally

```cpp
median_R = median(all_valid_R_proposals);

if (median_R > R_MAX_dynamic)
    median_R = R_MAX_dynamic;

R_annual = median_R;
```

**Activation:**
```
R_annual takes effect at: domc_cycle_start + DOMC_CYCLE_LENGTH
```

### 9.4 Bootstrap

```
Initial R_annual = 3700 (37%)
R% is active IMMEDIATELY at V6 activation (alongside 6/6/6 emission)
First DOMC cycle starts at: ACTIVATION_HEIGHT + 172800 (4 months)
```

**Important:** R% = 37% for the first 4 months provides strong incentive for early stakers. After the first DOMC cycle, masternodes vote to adjust R_annual (bounded by R_MAX_dynamic).

### 9.5 R% Active During Commit/Reveal (RÈGLE CRITIQUE)

> ⚠️ **CLARIFICATION IMPORTANTE**
>
> **R% RESTE ACTIF pendant les phases commit et reveal du cycle DOMC.**
>
> Le yield quotidien continue de s'accumuler à l'ancien taux R_annual
> jusqu'à ce que le nouveau taux soit activé au début du cycle suivant.
>
> ```
> Cycle N (172800 blocs = 4 mois):
> ├─ Blocs 0 - 132479:       R_annual(N-1) actif, pas de vote
> ├─ Blocs 132480 - 152639:  R_annual(N-1) actif, phase COMMIT
> ├─ Blocs 152640 - 172799:  R_annual(N-1) actif, phase REVEAL
> └─ Bloc 172800 (= Cycle N+1 bloc 0): Nouveau R_annual(N) activé
>
> AUCUNE interruption du yield pendant le vote.
> ```
>
> **Justification:** Le yield est un engagement contractuel envers les stakers.
> Interrompre le yield pendant le vote créerait de l'incertitude et découragerait le staking.

---

## 10. HTLC (OPTIONAL)

### 10.1 HTLC_CREATE

```cpp
struct HTLCPayload {
    uint256 hashlock;           // SHA256(preimage)
    uint32_t timelock;          // Absolute block height
    CAmount amount;             // KHU_T amount locked
    CScript recipientScript;    // Recipient if claimed
    CScript refundScript;       // Refund if timeout
};
```

**Effect:**
```
KHU_T locked (not spent, not transferable).
C, U unchanged (funds still in system).
```

### 10.2 HTLC_CLAIM

```
Input:  HTLC outpoint, preimage
Verify: SHA256(preimage) == hashlock
        nHeight <= timelock
Effect: KHU_T transferred to recipientScript
```

### 10.3 HTLC_REFUND

```
Input:  HTLC outpoint
Verify: nHeight > timelock
Effect: KHU_T refunded to refundScript
```

**Invariant preservation:**
- HTLC does not create or destroy KHU.
- C and U remain unchanged throughout.

---

## 11. VALIDATION RULES

### 11.1 Block Validation

```cpp
bool CheckBlock(const CBlock& block, CValidationState& state) {
    // 1. Validate transactions
    for (const auto& tx : block.vtx) {
        if (!CheckTransaction(tx, state))
            return false;
    }

    // 2. Compute new KHU state
    KHUGlobalState newState = prevState;
    for (const auto& tx : block.vtx) {
        if (!ApplyKHUTransaction(tx, newState, state))
            return false;
    }

    // 3. Verify invariants (Z = total ZKHU staked)
    int64_t Z = GetTotalStakedZKHU();
    if (newState.C != newState.U + Z)
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-CD-violation");

    if (newState.Cr != newState.Ur)
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-CDr-violation");

    // 4. Update daily yield if needed (NOTE: Should be BEFORE transactions in canonical order)
    if ((block.nHeight - last_yield_update_height) >= 1440) {
        UpdateDailyYield(newState, block.nHeight);
    }

    // 5. Check finality
    if (!CheckFinality(block, newState))
        return false;

    return true;
}
```

### 11.2 Transaction Validation

```cpp
bool CheckTransaction(const CTransaction& tx, CValidationState& state) {
    switch (tx.nType) {
        case TX_KHU_MINT:
            return CheckKHUMint(tx, state);
        case TX_KHU_REDEEM:
            return CheckKHURedeem(tx, state);
        case TX_KHU_STAKE:
            return CheckKHUStake(tx, state);
        case TX_KHU_UNSTAKE:
            return CheckKHUUnstake(tx, state);
        case TX_DOMC_VOTE:
            return CheckDOMCVote(tx, state);
        case TX_HTLC_CREATE:
        case TX_HTLC_CLAIM:
        case TX_HTLC_REFUND:
            return CheckHTLC(tx, state);
        default:
            return CheckStandardTransaction(tx, state);
    }
}
```

---

## 12. STORAGE

### 12.1 LevelDB Keys

```
'K' + 'S' + height         → KHUGlobalState (state at height)
'K' + 'B'                  → Best KHU state hash
'K' + 'H' + hash           → KHUGlobalState (state by hash)
'K' + 'C' + height         → KHUStateCommitment (finality signature)
'K' + 'U' + outpoint       → CKHUUTXO (KHU UTXO)
'K' + 'N' + nullifier      → ZKHU note data
'K' + 'D' + cycle          → DOMC vote data
'K' + 'T' + outpoint       → HTLC data
```

### 12.2 UTXO Structure

```cpp
struct CKHUUTXO {
    int64_t amount;
    CScript scriptPubKey;
    uint32_t nHeight;

    // Flags
    bool fStaked;              // true if currently ZKHU
    uint32_t nStakeStartHeight; // if staked
};
```

---

## 13. NETWORK UPGRADE

### 13.1 Activation

```cpp
const Consensus::UpgradeIndex UPGRADE_KHU = 15;

// Activation height (TBD, set before mainnet)
vUpgrades[UPGRADE_KHU].nActivationHeight = <ACTIVATION_HEIGHT>;
vUpgrades[UPGRADE_KHU].nProtocolVersion = 70023;
```

### 13.2 Activation Requirements

```
- Testnet fully operational for >= 525600 blocks (1 year).
- All invariants verified.
- No critical bugs.
- Community approval via DOMC or equivalent governance.
```

---

## 14. ABSOLUTE PROHIBITIONS

```
1.  KHU burn outside REDEEM                    → FORBIDDEN
2.  Z→Z ZKHU transfers                         → FORBIDDEN
3.  Automatic Cr/Ur funding (outside DOMC)     → FORBIDDEN
4.  Epsilon tolerance on invariants            → FORBIDDEN
5.  External yield sources                     → FORBIDDEN
6.  R% modification outside DOMC               → FORBIDDEN
7.  Floating point arithmetic                  → FORBIDDEN (use int64_t only)
8.  Multiple notes per STAKE                   → FORBIDDEN
9.  Partial UNSTAKE                            → FORBIDDEN
10. Feeless KHU operations (no PIV fees)       → FORBIDDEN
11. Reorg beyond finalized height              → FORBIDDEN
12. MINT without PIV burn                      → FORBIDDEN
13. REDEEM without KHU_T destruction           → FORBIDDEN
14. Yield before 4320 block maturity           → FORBIDDEN
15. R_annual > R_MAX_dynamic                   → FORBIDDEN
```

---

## 15. CONSTANTS

```cpp
// Time
const uint32_t BLOCKS_PER_YEAR = 525600;
const uint32_t BLOCKS_PER_DAY = 1440;
const uint32_t MATURITY_BLOCKS = 4320;  // 3 days

// Finality
const uint32_t FINALITY_DEPTH = 12;
const uint32_t QUORUM_ROTATION = 240;

// Emission
const int64_t MAX_REWARD_YEAR = 6 * COIN;
const int64_t EMISSION_YEARS = 6;

// DOMC
const uint32_t DOMC_CYCLE_LENGTH = 172800; // 4 months (~120 days)
const uint32_t DOMC_PHASE_LENGTH = 20160;  // ~2 weeks
const uint16_t INITIAL_R = 3700;           // 37% (basis points) - active at V6 activation
const uint16_t MIN_R = 0;
const uint16_t FLOOR_R_MAX = 400;          // 4% (never below this)

// DAO Treasury (T)
const int64_t T_ANNUAL_RATE = 200;         // 2% annual (basis points)
const int64_t T_DAILY_DIVISOR = 182500;    // (U+Ur) / 182500 = daily T accumulation

// Precision
const int64_t COIN = 100000000;            // 1 PIV/KHU = 10^8 satoshis
const uint16_t BASIS_POINT = 1;            // 0.01%
```

---

## 16. ALGORITHM REFERENCE

### 16.1 Invariant Enforcement

```cpp
// Every block must satisfy (where Z = GetTotalStakedZKHU()):
int64_t Z = GetTotalStakedZKHU();
assert(state.C == state.U + Z);  // INVARIANT 1: Total Collateralization
assert(state.Cr == state.Ur);    // INVARIANT 2: Reward Collateralization

// Or reject block.
```

### 16.2 State Transition

```cpp
KHUGlobalState ApplyBlock(const CBlock& block, const KHUGlobalState& prev) {
    KHUGlobalState next = prev;

    for (const auto& tx : block.vtx) {
        switch (tx.nType) {
            case TX_KHU_MINT:
                next.C += tx.amount;
                next.U += tx.amount;
                break;

            case TX_KHU_REDEEM:
                next.C -= tx.amount;
                next.U -= tx.amount;
                break;

            case TX_KHU_STAKE:
                next.U -= tx.amount;  // KHU_T → ZKHU (U decreases, Z increases)
                break;

            case TX_KHU_UNSTAKE:
                int64_t bonus = CalculateBonus(tx);
                int64_t principal = GetStakeAmount(tx);
                // Double-flux atomique:
                next.U  += principal + bonus;  // Create KHU_T for principal + bonus
                next.C  += bonus;              // Only bonus adds to collateral
                next.Cr -= bonus;              // Consume reward pool
                next.Ur -= bonus;              // Consume reward rights
                break;
        }
    }

    // Daily yield update (BEFORE transactions in canonical order, shown here for clarity)
    if ((block.nHeight - last_yield_update_height) >= BLOCKS_PER_DAY) {
        int64_t total_yield = CalculateDailyYield(block.nHeight);
        next.Cr += total_yield;
        next.Ur += total_yield;
    }

    // Invariants must hold (Z = total ZKHU staked, computed dynamically)
    int64_t Z = GetTotalStakedZKHU();
    assert(next.C == next.U + Z);  // INVARIANT 1: Total Collateralization
    assert(next.Cr == next.Ur);    // INVARIANT 2: Reward Collateralization

    return next;
}
```

### 16.3 Yield Calculation

```cpp
int64_t CalculateDailyYield(uint32_t nHeight) {
    int64_t total = 0;

    for (const auto& note : active_zkhu_notes) {
        if (nHeight - note.stake_start_height >= MATURITY_BLOCKS) {
            int64_t daily = (note.amount * R_annual / 10000) / 365;
            total += daily;
        }
    }

    return total;
}
```

### 16.4 UNSTAKE Bonus

```cpp
int64_t CalculateBonus(const CTransaction& unstake_tx) {
    uint32_t stake_start = GetStakeStartHeight(unstake_tx);
    int64_t stake_amount = GetStakeAmount(unstake_tx);
    uint32_t current_height = chainActive.Height();

    if (current_height - stake_start < MATURITY_BLOCKS)
        return 0;

    uint32_t days_staked = (current_height - stake_start) / BLOCKS_PER_DAY;
    int64_t bonus = (stake_amount * R_annual / 10000 / 365) * days_staked;

    return bonus;
}
```

---

## 17. ERROR CODES

```cpp
enum KHURejectCode {
    REJECT_KHU_INVARIANT_CD = 0x60,
    REJECT_KHU_INVARIANT_CDR = 0x61,
    REJECT_KHU_INVALID_MINT = 0x62,
    REJECT_KHU_INVALID_REDEEM = 0x63,
    REJECT_KHU_INVALID_STAKE = 0x64,
    REJECT_KHU_INVALID_UNSTAKE = 0x65,
    REJECT_KHU_FINALITY_VIOLATION = 0x66,
    REJECT_KHU_IMMATURE_YIELD = 0x67,
    REJECT_KHU_INVALID_R = 0x68,
    REJECT_KHU_FORBIDDEN_BURN = 0x69,
    REJECT_KHU_FORBIDDEN_TRANSFER = 0x6A,
    REJECT_KHU_HTLC_INVALID = 0x6B,
};
```

---

## 18. ACTIVATION SEQUENCE

### Phase 0: Pre-Activation
```
Block 0 - (ACTIVATION_HEIGHT - 1):
- Standard PIVX consensus
- No KHU functionality
```

### Phase 1: Activation
```
Block ACTIVATION_HEIGHT:
- Initialize KHUGlobalState: C=0, U=0, Cr=0, Ur=0
- Enable MINT and REDEEM
- Emission formula active
- Fee burning active
```

### Phase 2: Finality (12 blocks after activation)
```
Block (ACTIVATION_HEIGHT + 12):
- Masternode finality enforced
- State commitments required
```

### Phase 3: Sapling (TBD blocks after activation)
```
Block (ACTIVATION_HEIGHT + <TBD>):
- STAKE/UNSTAKE enabled
- ZKHU notes active
- Yield mechanism begins
```

### Phase 4: DOMC (1 year after activation)
```
Block (ACTIVATION_HEIGHT + 525600):
- First DOMC cycle begins
- Voting enabled
```

### Phase 5: HTLC (Optional, TBD)
```
Block (ACTIVATION_HEIGHT + <TBD>):
- HTLC transactions enabled (optional)
```

---

## 19. NOTES DE CONSENSUS (OBLIGATOIRES)

Les règles suivantes sont **absolues** et constituent le fondement du consensus KHU :

### 19.1 Atomicité des mutations d'état

- C/U et Cr/Ur ne doivent **jamais** être modifiés séparément
- Toutes les mutations C/U/Cr/Ur doivent être atomiques
- Toute séparation de ces mutations constitue une violation de consensus

### 19.2 Ordre d'exécution

L'ordre ConnectBlock() est **immuable** :

```
1. LoadKhuState(height - 1)
2. ApplyDailyUpdatesIfNeeded()  // T += 2%/365 × (U+Ur) + Yield (même trigger daily)
3. ProcessKHUTransactions()
4. ApplyBlockReward()
5. CheckInvariants()
6. SaveKhuState(height)
```

**Note:** T accumulation et Yield sont unifiés dans ApplyDailyUpdatesIfNeeded (tous les 1440 blocs).

Toute modification de cet ordre constitue un consensus break.

### 19.3 Verrouillage

Toute fonction modifiant C, U, Cr, Ur, ou T DOIT :
- Acquérir le lock `cs_khu` avant toute mutation
- Maintenir ce lock pendant toutes les mutations atomiques
- Appeler `CheckInvariants()` avant de relâcher le lock

### 19.4 Rejet de bloc

Toute déviation des règles ci-dessus DOIT provoquer :
```
reject_block("khu-invariant-violation")
```

Aucune correction automatique. Aucune tolérance. Rejet immédiat.

---

## 20. VERSION HISTORY

```
1.0.0 - Initial canonical specification
1.1.0 - Phase 6: Added T (DAO Treasury Pool) as 15th field in KhuGlobalState
        - T accumulates 0.5% × (U+Ur) every 172800 blocks (4 months)
        - Updated ConnectBlock order: AccumulateDaoTreasuryIfNeeded() added as step 2
        - Updated anti-drift checksum for 15 fields
1.2.0 - R% Bootstrap Update:
        - Changed INITIAL_R from 500 (5%) to 3000 (30%)
        - R% is active immediately at V6 activation (alongside 6/6/6 emission)
        - First DOMC cycle starts at ACTIVATION_HEIGHT + 172800 (4 months)
        - Updated DOMC_CYCLE_LENGTH to 172800 blocks (4 months)
        - Updated DOMC_PHASE_LENGTH to 20160 blocks (~2 weeks)
1.3.0 - T Daily Accumulation & DAO Transition:
        - Changed T accumulation from 0.5%/4months to 2%/year daily
        - T_daily = (U + Ur) / 182500 (every 1440 blocks)
        - T and Yield now unified in ApplyDailyUpdatesIfNeeded()
        - Added Section 5.7: DAO Funding Transition (Year 1+)
        - Year 1+: DAO starts using T instead of emission
        - Year 6+: T becomes sole DAO funding source
        - Added T_ANNUAL_RATE and T_DAILY_DIVISOR constants
1.4.0 - Double-Entry Accounting Clarification & STAKE Fix:
        - CRITICAL: STAKE now modifies U (U -= amount)
        - Fixed invariant to C == U + Z (not C == U)
        - Z = GetTotalStakedZKHU() computed dynamically
        - Added Operations State Table (Section 3.4.1)
        - Updated Section 7.1 STAKE with state changes
        - Updated Section 11.1 CheckBlock invariant verification
        - Updated Section 16.1 Invariant Enforcement
        - Updated Section 16.2 State Transition with STAKE case
        - Fixed INITIAL_R from 3000 (30%) to 3700 (37%)
```

---

## END OF SPECIFICATION

This document defines the complete consensus rules for PIVX-V6-KHU.

Any deviation from these rules constitutes a consensus failure.

Implementation must follow these rules exactly.

No interpretation. No approximation. Strict compliance only.
