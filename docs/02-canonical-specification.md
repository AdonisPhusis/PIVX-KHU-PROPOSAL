# 02 — PIVX-V6-KHU CANONICAL SPECIFICATION

Version: 1.0.0
Status: FINAL
Style: Core Consensus Rules

---

## 1. ASSETS

```
PIV     : Native asset (existing)
KHU_T   : Transparent UTXO (colored coin, internal tracking)
ZKHU    : Sapling note (staking only)
```

---

## 2. GLOBAL STATE

### 2.1 Structure canonique : KhuGlobalState

La structure KhuGlobalState représente l'état économique global du système KHU à un height donné.

Elle contient **14 champs**, définis comme suit :

```cpp
struct KhuGlobalState {
    int64_t  C;      // Collatéral total PIV (inclut collatéralisations de bonus)
    int64_t  U;      // Supply totale KHU_T
    int64_t  Cr;     // Collatéral réservé au pool de reward
    int64_t  Ur;     // Reward accumulé (droits de reward)

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
    bool CheckInvariants() const {
        // Verify non-negativity
        if (C < 0 || U < 0 || Cr < 0 || Ur < 0)
            return false;

        // INVARIANT 1: Collateralization
        bool cd_ok = (U == 0 || C == U);

        // INVARIANT 2: Reward Collateralization
        bool cdr_ok = (Ur == 0 || Cr == Ur);

        return cd_ok && cdr_ok;
    }

    uint256 GetHash() const;
    SERIALIZE_METHODS(KhuGlobalState, obj) { /* ... */ }
};
```

**Note:** Ces 14 champs constituent le KhuGlobalState canonique. Toute implémentation doit refléter EXACTEMENT cette structure.

> ⚠️ **ANTI-DÉRIVE: CHECKSUM DE STRUCTURE (doc 02 ↔ doc 03)**
>
> **VERIFICATION DE COHERENCE ENTRE DOCUMENTS:**
>
> La structure `KhuGlobalState` doit être **IDENTIQUE** dans docs 02 et 03.
>
> **CHECKSUM DE STRUCTURE (14 champs) :**
> ```
> SHA256("int64_t C | int64_t U | int64_t Cr | int64_t Ur | uint16_t R_annual | uint16_t R_MAX_dynamic | uint32_t last_domc_height | uint32_t domc_cycle_start | uint32_t domc_cycle_length | uint32_t domc_phase_length | uint32_t last_yield_update_height | uint32_t nHeight | uint256 hashPrevState | uint256 hashBlock")
> = 0x4a7b8c9e...  (fixed reference checksum)
> ```
>
> **RÈGLES DE SYNCHRONISATION:**
>
> 1. ✅ Docs 02 et 03 DOIVENT avoir les 14 mêmes champs
> 2. ✅ Même ordre de déclaration (C, U, Cr, Ur, ...)
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

**INVARIANT 1 — Collateralization (CD = 1) :**
```
(U == 0 && C == 0)  OR  (C == U)
```

**INVARIANT 2 — Reward Collateralization (CDr = 1) :**
```
(Ur == 0 && Cr == 0)  OR  (Cr == Ur)
```

**Vérification:**

Les invariants doivent être vérifiés immédiatement après :
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

### 3.4 Authorized Pipeline

```
PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
```

**No other path allowed.**

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

### 3.7 Pipeline canonique KHU (immuable)

```
PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
```

Aucune autre transformation n'est autorisée.

### 3.8 Ordre canonique ConnectBlock()

```
1. LoadKhuState(height - 1)
2. ApplyDailyYieldIfNeeded()
3. ProcessKHUTransactions()
4. ApplyBlockReward()
5. CheckInvariants()
6. SaveKhuState(height)
```

Cet ordre est **immuable** et doit être respecté strictement dans l'implémentation.

> ⚠️ **ANTI-DÉRIVE CRITIQUE : ORDRE YIELD → TRANSACTIONS**
>
> **L'étape 2 (ApplyDailyYieldIfNeeded) DOIT s'exécuter AVANT l'étape 3 (ProcessKHUTransactions).**
>
> **RAISON CRITIQUE :**
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
        No change to C, U, Cr, Ur
Output: 1 ZKHU note
```

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
uint16_t R_MAX_dynamic = std::max(400, 3000 - year * 100);  // basis points

// R_MAX_dynamic schedule:
// Year 0: 3000 (30%)
// Year 1: 2900 (29%)
// ...
// Year 26: 400 (4%)
// Year 27+: 400 (4%)
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
Initial R_annual = 500 (5%)
First DOMC cycle starts at: ACTIVATION_HEIGHT + 525600 (1 year)
```

---

## 10. HTLC CROSS-CHAIN

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

    // 3. Verify invariants
    if (newState.C != newState.U)
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-CD-violation");

    if (newState.Cr != newState.Ur)
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-CDr-violation");

    // 4. Update daily yield if needed
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
const uint32_t DOMC_CYCLE_LENGTH = 43200;  // 30 days
const uint32_t DOMC_PHASE_LENGTH = 4320;   // 3 days
const uint16_t INITIAL_R = 500;            // 5% (basis points)
const uint16_t MIN_R = 0;
const uint16_t FLOOR_R_MAX = 400;          // 4% (never below this)

// Precision
const int64_t COIN = 100000000;            // 1 PIV/KHU = 10^8 satoshis
const uint16_t BASIS_POINT = 1;            // 0.01%
```

---

## 16. ALGORITHM REFERENCE

### 16.1 Invariant Enforcement

```cpp
// Every block must satisfy:
assert(state.C == state.U);
assert(state.Cr == state.Ur);

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

            case TX_KHU_UNSTAKE:
                int64_t bonus = CalculateBonus(tx);
                next.U += bonus;
                next.C += bonus;
                next.Cr -= bonus;
                next.Ur -= bonus;
                break;
        }
    }

    // Daily yield update
    if ((block.nHeight - last_yield_update_height) >= BLOCKS_PER_DAY) {
        int64_t total_yield = CalculateDailyYield(block.nHeight);
        next.Cr += total_yield;
        next.Ur += total_yield;
    }

    // Invariants must hold
    assert(next.C == next.U);
    assert(next.Cr == next.Ur);

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

### Phase 5: HTLC (TBD blocks after activation)
```
Block (ACTIVATION_HEIGHT + <TBD>):
- HTLC enabled
- Cross-chain swaps active
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
2. ApplyDailyYieldIfNeeded()
3. ProcessKHUTransactions()
4. ApplyBlockReward()
5. CheckInvariants()
6. SaveKhuState(height)
```

Toute modification de cet ordre constitue un consensus break.

### 19.3 Verrouillage

Toute fonction modifiant C, U, Cr, ou Ur DOIT :
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
```

---

## END OF SPECIFICATION

This document defines the complete consensus rules for PIVX-V6-KHU.

Any deviation from these rules constitutes a consensus failure.

Implementation must follow these rules exactly.

No interpretation. No approximation. Strict compliance only.
