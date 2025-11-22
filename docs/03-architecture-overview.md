# 03 — PIVX-V6-KHU ARCHITECTURE OVERVIEW

Version: 1.0.0
Status: FINAL
Base: PIVX Core v5.6.1 + KHU Extensions

---

## 1. OBJECTIF

Ce document décrit l'implémentation des règles du SPEC (01) dans PIVX-V6-KHU.

Il indique:
- Quels modules sont impactés
- Quels fichiers portent quelles responsabilités
- Comment l'état global KHU est géré
- Comment les flux de transactions KHU sont intégrés

**Référence obligatoire:**
- `01-SPEC-CANONIQUE.md` (règles consensus)
- `05-ROADMAP.md` (phases d'activation)

---

## 2. MODULES IMPACTÉS

### 2.1 Nouveaux Modules

```
src/khu/
├── khu_state.h/cpp          # KhuGlobalState + invariants
├── khu_db.h/cpp             # Persistence LevelDB
├── khu_utxo.h/cpp           # Tracker KHU_T UTXO
├── khu_mint.h/cpp           # Logic MINT
├── khu_redeem.h/cpp         # Logic REDEEM
├── khu_stake.h/cpp          # Logic STAKE
├── khu_unstake.h/cpp        # Logic UNSTAKE
├── khu_yield.h/cpp          # Scheduler Cr/Ur
├── khu_domc.h/cpp           # Gouvernance R%
└── khu_htlc.h/cpp           # HTLC cross-chain
```

### 2.2 Modules PIVX Modifiés

```
src/primitives/transaction.h # Nouveaux TxType (MINT, REDEEM, etc.)
src/validation.h/cpp         # ConnectBlock/DisconnectBlock
src/consensus/tx_verify.cpp  # Validation transactions KHU
src/coins.h/cpp              # Extension cache UTXO KHU
src/txdb.h/cpp               # Clés LevelDB KHU
src/chainparams.cpp          # Paramètres consensus KHU
src/rpc/khu.cpp              # RPC KHU (nouveau)
src/wallet/wallet.h/cpp      # Gestion KHU_T, auto-getnewaddress
src/sapling/                 # Extensions ZKHU (métadonnées)
src/llmq/                    # Finalité KHU (réutilisation)
```

---

## 3. ÉTAT GLOBAL KHU

### 3.1 Structure

**Fichier:** `src/khu/khu_state.h`

```cpp
struct KhuGlobalState {
    // Collatéral et supply
    int64_t C;                      // PIV collateral
    int64_t U;                      // KHU_T supply

    // Yield pool
    int64_t Cr;                     // Reward pool
    int64_t Ur;                     // Reward rights

    // DOMC
    uint16_t R_annual;              // Basis points (0-3000)
    uint16_t R_MAX_dynamic;         // max(400, 3000 - year*100)
    uint32_t last_domc_height;

    // Cycle DOMC
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;     // 43200 blocks
    uint32_t domc_phase_length;     // 4320 blocks

    // Yield scheduler
    uint32_t last_yield_update_height;

    // Chain tracking
    uint32_t nHeight;
    uint256 hashBlock;
    uint256 hashPrevState;

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

> ⚠️ **ANTI-DÉRIVE: CHECKSUM DE STRUCTURE (doc 03 ↔ doc 02)**
>
> **COHERENCE OBLIGATOIRE:**
>
> Cette structure `KhuGlobalState` dans doc 03 doit être **IDENTIQUE** à celle de doc 02.
>
> **CHECKSUM DE REFERENCE (14 champs) :**
> ```
> SHA256("int64_t C | int64_t U | int64_t Cr | int64_t Ur | uint16_t R_annual | uint16_t R_MAX_dynamic | uint32_t last_domc_height | uint32_t domc_cycle_start | uint32_t domc_cycle_length | uint32_t domc_phase_length | uint32_t last_yield_update_height | uint32_t nHeight | uint256 hashPrevState | uint256 hashBlock")
> = 0x4a7b8c9e...  (same as doc 02)
> ```
>
> **VERIFICATION:**
>
> ```bash
> # Verify struct synchronization between docs
> diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
>      <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md)
>
> # Expected output: No differences (or only comment differences)
> ```
>
> **If ANY field differs → docs are DESYNCHRONIZED → implementation will be INCORRECT.**
>
> Voir doc 02 section 2.1 pour le protocole de modification.

### 3.2 Stockage LevelDB

**Fichier:** `src/khu/khu_db.h`

**Clés:**
```
'K' + 'S' + height         → KhuGlobalState (state at height)
'K' + 'B'                  → uint256 (best state hash)
'K' + 'H' + hash           → KhuGlobalState (state by hash)
'K' + 'C' + height         → KhuStateCommitment (finality sig)
```

**Fonctions:**
```cpp
class CKHUStateDB : public CDBWrapper {
public:
    bool WriteKHUState(const KhuGlobalState& state);
    bool ReadKHUState(uint32_t nHeight, KhuGlobalState& state);
    bool ReadBestKHUState(KhuGlobalState& state);
    bool WriteKHUCommitment(uint32_t nHeight, const KhuStateCommitment& commitment);
};
```

### 3.3 Chargement et Persistance

**Au démarrage du nœud:**
```cpp
// src/init.cpp
bool AppInitMain() {
    // ...
    if (!pKHUStateDB->ReadBestKHUState(khuGlobalState))
        khuGlobalState = KhuGlobalState();  // Genesis state
    // ...
}
```

**À chaque bloc:**
```cpp
// src/validation.cpp ConnectBlock()
if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
    KhuGlobalState newState = prevState;

    // Apply KHU transactions
    for (const auto& tx : block.vtx) {
        ApplyKHUTransaction(tx, newState, view, state);
    }

    // Update yield if needed
    if ((pindex->nHeight - newState.last_yield_update_height) >= 1440) {
        UpdateDailyYield(newState, pindex->nHeight);
    }

    // Check invariants
    if (!newState.CheckInvariants())
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation");

    // Persist
    pKHUStateDB->WriteKHUState(newState);
}
```

### 3.4 Reorg Handling

**Fichier:** `src/validation.cpp DisconnectBlock()`

**Mécanisme:**
- Chaque `KhuGlobalState` référence le précédent via `hashPrevState`
- En cas de reorg, recharger l'état du bloc parent
- Rejouer les blocs de la nouvelle chaîne

```cpp
bool DisconnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        // Reload previous state
        KhuGlobalState prevState;
        if (!pKHUStateDB->ReadKHUState(pindex->nHeight - 1, prevState))
            return error("Failed to load previous KHU state");

        // Revert global state
        khuGlobalState = prevState;
        pKHUStateDB->WriteKHUState(prevState);
    }
    return true;
}
```

### 3.5 DÉCISIONS ARCHITECTURALES PHASE 1 (CANONIQUES)

Cette section documente les **décisions architecturales définitives** prises après audit complet de la documentation et analyse des trade-offs. Ces décisions sont **IMMUABLES** et doivent être suivies à la lettre durant l'implémentation Phase 1.

---

#### 3.5.1 Reorg Strategy = STATE-BASED (DÉCISION FINALE)

**Choix:** Stocker l'état complet `KhuGlobalState` à chaque bloc

**Justification:**
- Finalité LLMQ → max reorg = 12 blocs
- 12 × sizeof(KhuGlobalState) = 12 × 150 bytes = 1.8 KB overhead (négligeable)
- Pas de deltas, pas de journaling, pas de complexité
- DisconnectBlock() = simple lecture de l'état précédent

**Alternative rejetée:** Delta-based (stocker uniquement les changements)
- Complexité accrue (calcul inverse des deltas)
- Performance équivalente pour max 12 blocs
- Risque d'erreur dans le calcul inverse
- Pas de gain de stockage significatif

**Implémentation obligatoire:**

```cpp
// src/validation.cpp DisconnectBlock()
bool DisconnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (!NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU))
        return true;

    // ⚠️ DÉCISION ARCHITECTE: STATE-BASED reorg
    // Load complete previous state from DB (no delta computation)
    LOCK2(cs_main, cs_khu);

    KhuGlobalState prev;
    if (!pKHUStateDB->ReadKHUState(pindex->nHeight - 1, prev))
        return error("KHU: Failed to load previous state at height %d", pindex->nHeight - 1);

    // Replace current state with previous state (atomic revert)
    khuGlobalState = prev;

    return true;
}
```

**Propriétés garanties:**
- ✅ Reorg en O(1) par bloc (lecture simple)
- ✅ Pas de risque de désynchronisation delta
- ✅ Invariants vérifiables à chaque height
- ✅ Audit trail complet (état à chaque bloc disponible)

**Interdit:**
- ❌ Calculer deltas inverses
- ❌ Implémenter journaling
- ❌ Stocker uniquement hashPrevState sans état complet
- ❌ Utiliser snapshot tous les N blocs

**Validation:**
```bash
# Vérifier que DisconnectBlock() ne calcule PAS de deltas
grep -A20 "DisconnectBlock.*KHU" src/validation.cpp | grep -E "(delta|inverse|journal)"
# Expected output: RIEN (aucune occurrence)
```

---

#### 3.5.2 Per-Note Ur Tracking = STREAMING VIA LEVELDB CURSOR (DÉCISION FINALE)

**Choix:** Itération note par note via curseur LevelDB, batch updates via CDBBatch

**Justification:**
- Yield appliqué 1× par 1440 blocs → performance pas critique
- Évite OOM (Out Of Memory) avec large note set (>100k notes)
- CDBBatch permet updates efficaces sans flush par note
- Streaming maintient empreinte mémoire constante

**Alternative rejetée:** Charger toutes les notes en RAM
- Risque OOM avec grand nombre de stakers
- Empreinte mémoire proportionnelle au nombre de notes
- Pas de gain de performance significatif (yield 1×/jour)

**Implémentation obligatoire:**

```cpp
// src/khu/khu_yield.cpp
bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight) {
    AssertLockHeld(cs_khu);

    // ⚠️ DÉCISION ARCHITECTE: STREAMING via LevelDB cursor
    // Never load all notes in RAM - iterate one by one

    int64_t total_daily_yield = 0;
    CDBBatch batch(CLIENT_VERSION);

    // Create cursor to iterate active ZKHU notes
    std::unique_ptr<CKHUNoteCursor> cursor = pKHUNoteDB->GetCursor();

    // Stream through all active notes
    for (cursor->SeekToFirst(); cursor->Valid(); cursor->Next()) {
        CKHUNoteData note = cursor->GetNote();

        // Skip spent notes (marked via flag in DB, not deletion)
        if (note.fSpent)
            continue;

        // Check maturity
        uint32_t age = nHeight - note.nStakeStartHeight;
        if (age < MATURITY_BLOCKS)
            continue;

        // Calculate daily yield for this note
        // ⚠️ Division BEFORE multiplication to prevent overflow
        int64_t daily = (note.amount / 10000) * state.R_annual / 365;

        // Accumulate in note (batch update, not immediate write)
        note.Ur_accumulated += daily;
        batch.Write(std::make_pair('N', note.nullifier), note);

        // Accumulate global total
        total_daily_yield += daily;
    }

    // Update global state (atomically: Cr and Ur together)
    state.Cr += total_daily_yield;
    state.Ur += total_daily_yield;

    // Flush batch to DB (single atomic write)
    pKHUNoteDB->WriteBatch(batch);

    return state.CheckInvariants();
}
```

**Propriétés garanties:**
- ✅ Empreinte mémoire O(1) (indépendante du nombre de notes)
- ✅ Pas de OOM avec 1M+ notes actives
- ✅ Batch updates efficient (single LevelDB write)
- ✅ Yield correctness vérifiable note par note

**Interdit:**
- ❌ `std::vector<CKHUNoteData> allNotes = LoadAllNotes();`
- ❌ Flush DB après chaque note
- ❌ Charger notes en cache global
- ❌ Précharger notes en mémoire "pour performance"

**Validation:**
```bash
# Vérifier usage de curseur (pas de LoadAll)
grep -r "LoadAllNotes\|GetAllNotes" src/khu/
# Expected output: RIEN (aucune fonction de chargement en masse)

# Vérifier présence de cursor
grep -r "CKHUNoteCursor\|GetCursor" src/khu/khu_yield.cpp
# Expected output: au moins 1 occurrence
```

---

#### 3.5.3 Rolling Frontier Tree = MARQUAGE, PAS SUPPRESSION (DÉCISION FINALE)

**Choix:** Arbre Sapling standard append-only, gestion des notes actives via flag `fSpent` en DB

**Justification:**
- Sapling commitment tree est append-only par design (merkle tree)
- Suppression de commitment = fork consensus (invalide merkle proof)
- Flag `fSpent` en DB suffit pour tracking actif/inactif
- Simplicité : pas de nouveau type d'arbre

**Alternative rejetée:** Pruning des commitments anciens
- Invalide les merkle proofs existants
- Nécessite nouveau type d'arbre (complexité)
- Pas de gain de stockage significatif (commitments = 32 bytes)

**Implémentation obligatoire:**

```cpp
// src/khu/khu_db.h
struct CKHUNoteData {
    uint256 commitment;        // Sapling commitment (never deleted)
    uint256 nullifier;         // Revealed on spend
    int64_t amount;            // Principal staked
    int64_t Ur_accumulated;    // Yield accumulated
    uint32_t nStakeStartHeight;
    bool fSpent;               // ⚠️ MARQUAGE: true if nullifier revealed

    SERIALIZE_METHODS(CKHUNoteData, obj) {
        READWRITE(obj.commitment, obj.nullifier, obj.amount,
                  obj.Ur_accumulated, obj.nStakeStartHeight, obj.fSpent);
    }
};

// src/sapling/incrementalmerkletree.cpp
class SaplingMerkleTree {
    // ⚠️ Append-only: commitments NEVER removed
    void append(const uint256& commitment) {
        // Standard Sapling append (no deletion support)
        witnesses.append(commitment);
    }

    // ❌ FORBIDDEN: No prune() or remove() method
};
```

**Gestion des notes actives:**

```cpp
// src/khu/khu_unstake.cpp
bool ApplyKHUUnstake(const CTransaction& tx, KhuGlobalState& state, ...) {
    // 1. Verify nullifier not already spent
    CKHUNoteData note;
    if (!pKHUNoteDB->ReadNote(nullifier, note))
        return state.Invalid("khu-note-not-found");

    if (note.fSpent)
        return state.Invalid("khu-double-spend");

    // 2. Mark as spent (DO NOT delete commitment from tree)
    note.fSpent = true;
    pKHUNoteDB->WriteNote(nullifier, note);

    // 3. Commitment remains in Sapling tree forever
    // (merkle proofs for other notes still valid)

    return true;
}
```

**Propriétés garanties:**
- ✅ Merkle proofs toujours valides (tree append-only)
- ✅ Pas de fork consensus via pruning
- ✅ Simplicité (réutilisation code Sapling standard)
- ✅ Notes actives trackables via query `WHERE fSpent = false`

**Interdit:**
- ❌ `saplingTree.remove(commitment);`
- ❌ `pruneOldCommitments();`
- ❌ Supprimer note de DB après UNSTAKE
- ❌ Compacter l'arbre périodiquement

**Validation:**
```bash
# Vérifier absence de code de suppression
grep -r "remove.*commitment\|prune.*tree\|delete.*note" src/sapling/ src/khu/
# Expected output: RIEN (aucune suppression)

# Vérifier présence de flag fSpent
grep -r "fSpent" src/khu/khu_db.h src/khu/khu_unstake.cpp
# Expected output: au moins 2 occurrences
```

---

#### 3.5.4 Rappels Critiques (OBLIGATOIRES)

Ces règles sont des **invariants d'implémentation** qui ne peuvent JAMAIS être violés.

**1. Ordre d'exécution: Yield → ProcessKHUTransaction**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
ApplyDailyYieldIfNeeded();      // Étape 2
ProcessKHUTransactions();        // Étape 3

// ❌ FORBIDDEN (cause consensus failure)
ProcessKHUTransactions();        // WRONG ORDER
ApplyDailyYieldIfNeeded();
```

**Raison:** UNSTAKE consomme Ur_accumulated. Si yield pas encore appliqué, bonus calculé faux → invariant violation.

**2. Double flux UNSTAKE = atomique**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
state.U  += B;   // Les 4 updates dans la même fonction
state.C  += B;   // sous le même lock cs_khu
state.Cr -= B;   // pas de return/break entre les lignes
state.Ur -= B;   // vérification invariants immédiatement après

// ❌ FORBIDDEN
state.U += B;
if (someCondition) return false;  // ❌ Rollback partiel interdit
state.C += B;
```

**3. C/U et Cr/Ur = jamais séparés**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
state.C += amount;
state.U += amount;  // Toujours ensemble, jamais séparés

// ❌ FORBIDDEN
state.C += amount;
DoSomethingElse();  // ❌ Autre code entre les mutations
state.U += amount;
```

**4. Lock order: LOCK2(cs_main, cs_khu)**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
LOCK2(cs_main, cs_khu);
// ... mutations KHU state ...

// ❌ FORBIDDEN (deadlock risk)
LOCK(cs_khu);
LOCK(cs_main);  // ❌ Ordre inversé
```

**5. Remplacer assert() → Invalid()**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
if (!state.CheckInvariants())
    return state.Invalid("khu-invariant-violation");

// ❌ FORBIDDEN (crash node au lieu de rejeter bloc)
assert(state.CheckInvariants());
```

**6. Overflow yield: division avant multiplication**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
int64_t daily = (note.amount / 10000) * R_annual / 365;

// ❌ FORBIDDEN (overflow possible)
int64_t daily = (note.amount * R_annual) / 10000 / 365;
```

**7. ProcessKHUTransaction: return bool et testé**

**Règle:**
```cpp
// CORRECT (OBLIGATOIRE)
if (!ProcessKHUTransaction(tx, state, view, validationState))
    return false;  // Reject block immediately

// ❌ FORBIDDEN (erreur ignorée)
ProcessKHUTransaction(tx, state, view, validationState);  // return value ignored
```

---

**FIN DES DÉCISIONS ARCHITECTURALES PHASE 1**

Toute implémentation qui dévie de ces décisions constitue une **violation architecturale** et doit être rejetée en code review.

---

## 4. INTÉGRATION CONSENSUS

### 4.1 Validation de Transaction

**Fichier:** `src/consensus/tx_verify.cpp`

```cpp
bool CheckTransaction(const CTransaction& tx, CValidationState& state) {
    switch (tx.nType) {
        case CTransaction::TX_KHU_MINT:
            return CheckKHUMint(tx, state);

        case CTransaction::TX_KHU_REDEEM:
            return CheckKHURedeem(tx, state);

        case CTransaction::TX_KHU_STAKE:
            return CheckKHUStake(tx, state);

        case CTransaction::TX_KHU_UNSTAKE:
            return CheckKHUUnstake(tx, state);

        case CTransaction::TX_DOMC_VOTE:
            return CheckDOMCVote(tx, state);

        case CTransaction::TX_HTLC_CREATE:
        case CTransaction::TX_HTLC_CLAIM:
        case CTransaction::TX_HTLC_REFUND:
            return CheckHTLC(tx, state);
    }
    return true;
}
```

### 4.2 ConnectBlock

**Fichier:** `src/validation.cpp`

**Ordre d'exécution canonique:**

Pour chaque bloc `B` au height `h`:

```cpp
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (!NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU))
        return true;  // KHU not active yet

    // ============================================================
    // ⚠️ CRITICAL LOCK: LOCK2(cs_main, cs_khu)
    // ============================================================
    // MUST acquire BOTH locks BEFORE any KHU state access or mutation
    // Lock order is MANDATORY: cs_main first, cs_khu second (never reversed)
    // This lock protects ALL operations below until end of KHU processing
    LOCK2(cs_main, cs_khu);

    // 1. Charger l'état global précédent
    KhuGlobalState prevState;
    if (!pKHUStateDB->ReadKHUState(pindex->nHeight - 1, prevState))
        return state.Invalid("khu-state-load-failed");

    KhuGlobalState newState = prevState;

    // 2. Appliquer le yield quotidien (si applicable)
    // AVANT transactions pour éviter mismatch avec UNSTAKE
    if (pindex->nHeight >= newState.last_yield_update_height + BLOCKS_PER_DAY) {
        ApplyDailyYield(newState, pindex->nHeight, view);
        // Atomicité: Ur et Cr mis à jour ensemble
        newState.last_yield_update_height = pindex->nHeight;
    }

    // 3. Appliquer les transactions KHU du bloc
    // ⚠️ ANTI-DÉRIVE: TOUTES les erreurs de transaction KHU doivent rejeter le bloc
    for (const auto& tx : block.vtx) {
        if (!tx->IsKHUTransaction())
            continue;

        switch (tx->nType) {
            case TxType::KHU_MINT:
            case TxType::KHU_REDEEM:
                // Mettre à jour C et U selon la spec
                // CRITICAL: Check return value and reject block immediately on failure
                if (!ApplyKHUMintOrRedeem(*tx, newState, view, state))
                    return false;  // DO NOT CONTINUE - block is invalid
                break;

            case TxType::KHU_STAKE:
                // Marquer la note ZKHU (sans changer C/U/Cr/Ur)
                // CRITICAL: Check return value and reject block immediately on failure
                if (!ApplyKHUStake(*tx, newState, view, state))
                    return false;  // DO NOT CONTINUE - block is invalid
                break;

            case TxType::KHU_UNSTAKE:
                // Double flux atomique: U+=B, C+=B, Cr-=B, Ur-=B
                // CRITICAL: Check return value and reject block immediately on failure
                if (!ApplyKHUUnstake(*tx, newState, view, state, pindex->nHeight))
                    return false;  // DO NOT CONTINUE - block is invalid
                break;

            default:
                break;
        }
    }

    // 4. Appliquer l'émission de bloc
    // APRÈS transactions pour que C/U soit consistant
    if (pindex->nHeight >= consensusParams.nKHUActivationHeight) {
        uint32_t year = (pindex->nHeight - consensusParams.nKHUActivationHeight) / BLOCKS_PER_YEAR;
        CAmount reward_year = std::max(6 - (int64_t)year, 0LL) * COIN;

        CAmount stakerReward = reward_year;
        CAmount mnReward = reward_year;
        CAmount daoReward = reward_year;

        // Emission ne modifie PAS C/U/Cr/Ur (PIV pur)
        ProcessBlockReward(block, stakerReward, mnReward, daoReward);
    }

    // 5. Vérifier les invariants
    if (!newState.CheckInvariants()) {
        return state.Invalid(BlockValidationResult::BLOCK_CONSENSUS,
                           "khu-invariant-violation",
                           strprintf("C=%d U=%d Cr=%d Ur=%d at height %d",
                                   newState.C, newState.U, newState.Cr, newState.Ur,
                                   pindex->nHeight));
    }

    // 6. Persister l'état (atomique via CDBBatch)
    newState.nHeight = pindex->nHeight;
    newState.hashBlock = pindex->GetBlockHash();
    newState.hashPrevState = prevState.GetHash();

    if (!pKHUStateDB->WriteKHUState(newState))
        return state.Invalid("khu-state-persist-failed");

    return true;
}
```

**Justification de l'ordre:**

1. **Yield AVANT transactions:** Un UNSTAKE consomme le Ur accumulé. Le yield quotidien doit donc être ajouté à Cr/Ur AVANT que l'UNSTAKE ne soit traité, sinon le calcul du bonus serait incorrect.

2. **Emission APRÈS transactions:** L'émission PIV ne modifie PAS l'état KHU (C, U, Cr, Ur). Elle crée simplement du PIV pur pour staker/masternode/DAO. L'ordre relatif n'affecte donc pas les invariants KHU.

3. **Invariants en fin:** Les invariants sont vérifiés APRÈS toutes les mutations, garantissant qu'aucun état intermédiaire invalide n'est persisté.

---

> ⚠️ **ATOMICITÉ CRITIQUE — RÈGLE FONDAMENTALE**
>
> **Toute modification de C/U (MINT/REDEEM) ou C/U/Cr/Ur (UNSTAKE/Yield) doit être atomique et intrabloc.**
>
> **MINT:** `C += amount` et `U += amount` dans le même bloc/opération.
> **REDEEM:** `C -= amount` et `U -= amount` dans le même bloc/opération.
> **UNSTAKE:** Les 4 mises à jour (`C+=B`, `U+=B`, `Cr-=B`, `Ur-=B`) sont une transition atomique dans le même `ConnectBlock()`.
> **Yield:** `Cr += Δ` et `Ur += Δ` ensemble dans `ApplyDailyYield()`.
>
> L'implémentation de `ApplyKHUUnstake()` doit appliquer les quatre mises à jour (`C`, `U`, `Cr`, `Ur`) sous un même lock (`cs_khu`) et en une seule séquence, dans `ConnectBlock`.
>
> **Il est interdit de:**
> - Séparer ces mises à jour dans plusieurs fonctions indépendantes non atomiques
> - Faire `Ur -= B` dans un bloc et `C += B` dans un autre
> - Laisser un bloc persister un état intermédiaire où `Cr != Ur` ou `C != U`
> - Modifier C sans modifier U en même temps (et vice versa)
> - Modifier Cr sans modifier Ur en même temps (et vice versa)
>
> **Toutes les mutations de `C/U/Cr/Ur` doivent être confinées aux fonctions atomiques appropriées pour garantir l'atomicité.**
>
> Voir docs/02-canonical-specification.md sections 3.1, 3.2, 7.2.3 et docs/06-protocol-reference.md sections 3, 3.1 pour détails complets.

### 4.3 ApplyKHUTransaction

**Fichier:** `src/khu/khu_state.cpp`

```cpp
bool ApplyKHUTransaction(const CTransaction& tx,
                         KhuGlobalState& state,
                         const CCoinsViewCache& view,
                         CValidationState& validationState) {
    switch (tx.nType) {
        case CTransaction::TX_KHU_MINT: {
            CKHUMintPayload payload;
            GetTxPayload(tx, payload);
            state.C += payload.pivAmount;
            state.U += payload.pivAmount;
            break;
        }

        case CTransaction::TX_KHU_REDEEM: {
            CKHURedeemPayload payload;
            GetTxPayload(tx, payload);
            state.C -= payload.khuAmount;
            state.U -= payload.khuAmount;
            break;
        }

        case CTransaction::TX_KHU_UNSTAKE: {
            int64_t bonus = CalculateUnstakeBonus(tx, view, state);
            state.U += bonus;
            state.C += bonus;
            state.Cr -= bonus;
            state.Ur -= bonus;
            break;
        }
    }

    return state.CheckInvariants();
}
```

---

## 5. PIPELINE KHU_T (UTXO TRACKER)

### 5.1 Structure UTXO

**Fichier:** `src/khu/khu_utxo.h`

```cpp
struct CKHUUTXO {
    int64_t amount;             // KHU_T amount
    CScript scriptPubKey;       // Owner script
    uint32_t nHeight;           // Creation height

    // Staking flags
    bool fStaked;               // Currently staked as ZKHU
    uint32_t nStakeStartHeight; // Stake start (0 if not staked)

    SERIALIZE_METHODS(CKHUUTXO, obj) { /* ... */ }
};
```

### 5.2 Cache UTXO

**Fichier:** `src/coins.h`

```cpp
// Extension de CCoinsViewCache
typedef std::unordered_map<COutPoint, CKHUUTXO, SaltedOutpointHasher> CKHUCoinsMap;

class CCoinsViewCache {
    CCoinsMap cacheCoins;           // PIV UTXOs
    CKHUCoinsMap cacheKHUCoins;     // KHU UTXOs
    // ...

    bool GetKHUCoin(const COutPoint& outpoint, CKHUUTXO& khuCoin) const;
    void AddKHUCoin(const COutPoint& outpoint, CKHUUTXO&& khuCoin);
    bool SpendKHUCoin(const COutPoint& outpoint);
};
```

### 5.3 LevelDB Storage

**Clé:**
```
'k' + txid + index  → CKHUUTXO
```

### 5.4 Pipeline Autorisé

```
PIV (transparent)
  ↓ MINT
KHU_T (UTXO coloré interne)
  ↓ STAKE
ZKHU (Sapling note)
  ↓ UNSTAKE (after 4320 blocks)
KHU_T (UTXO)
  ↓ REDEEM
PIV (transparent)
```

**Règles strictes:**
- Aucun Z→Z ZKHU
- Aucun BURN KHU (seul REDEEM détruit)
- Aucun transfert KHU_T hors pipeline

---

## 6. FINALITÉ MASTERNODE

### 6.1 Réutilisation LLMQ

**Modules utilisés:**
- `src/llmq/quorums.h/cpp` (quorum formation)
- `src/llmq/signing.h/cpp` (BLS signatures)
- `src/llmq/chainlocks.h/cpp` (ChainLock mechanism)

**Principe:**
- Les quorums LLMQ existants signent les blocs
- ChainLock finalize automatiquement l'état KHU
- Pas de nouveau mécanisme, réutilisation directe

### 6.2 Finalité KHU

**Fichier:** `src/khu/khu_finality.cpp`

```cpp
bool CheckKHUFinality(uint32_t nHeight, const KhuGlobalState& state) {
    // 1. Vérifier qu'un ChainLock existe pour ce bloc
    uint256 hashBlock = chainActive[nHeight]->GetBlockHash();
    if (!llmq::chainLocksHandler->HasChainLock(nHeight, hashBlock))
        return false;

    // 2. Vérifier que l'état KHU est cohérent
    KhuGlobalState finalizedState;
    if (!pKHUStateDB->ReadKHUState(nHeight, finalizedState))
        return false;

    // 3. Vérifier que l'état actuel descend de l'état finalisé
    if (!IsStateDescendantOf(state, finalizedState))
        return false;

    return true;
}
```

### 6.3 Paramètres

```
Finality depth: 12 blocks
Quorum rotation: 240 blocks (LLMQ DKG interval)
Quorum type: LLMQ_400_60 (400 MN, 60% threshold)
```

**Protection:**
- État KHU finalisé après 12 blocs
- Reorg impossible au-delà de 12 blocs
- Invariants C==U et Cr==Ur protégés par finalité

---

## 7. STAKING ZKHU (SAPLING)

### 7.1 Intégration Sapling

**Modules modifiés:**
- `src/sapling/sapling_transaction.h` (métadonnées note)
- `src/sapling/incrementalmerkletree.h` (Rolling Frontier Tree)

**Principe:**
- Réutiliser circuits Sapling existants
- Ajouter métadonnées dans memo field
- Pas de nouveau circuit zk-SNARK

**IMPORTANT — Separation des pools Sapling:**

ZKHU utilise un **pool Sapling séparé** distinct du pool zPIV existant.

**Raison:**
- Évite collision entre ZKHU et zPIV
- Permet tracking indépendant des commitment trees
- Simplifie l'audit de l'état KHU (C, U, Cr, Ur)
- Garantit que ZKHU ne peut pas être confondu avec zPIV

**Implementation:**
- Flag interne `fIsKHU` dans `OutputDescription`
- Commitment tree séparé : `zkhu_tree` vs `sapling_tree`
- Nullifier set séparé pour éviter double-spend entre pools
- Wallet tracking séparé

**Conséquence:**
- ZKHU et zPIV ne partagent AUCUNE structure on-chain
- Pas de conversion ZKHU ↔ zPIV possible
- Anonymity set ZKHU indépendant de anonymity set zPIV

### 7.2 STAKE Transaction

**Fichier:** `src/khu/khu_stake.cpp`

```cpp
bool CreateKHUStake(CMutableTransaction& tx, const CKHUUTXO& khuInput, const libzcash::SaplingPaymentAddress& zkhuAddress) {
    // 1. Spend KHU_T input
    tx.vin.push_back(CTxIn(khuInput.outpoint));

    // 2. Create ZKHU output (Sapling)
    OutputDescription zkhuOutput;
    zkhuOutput.value = khuInput.amount;

    // 3. Encode metadata in memo
    std::array<unsigned char, 512> memo;
    memo[0] = 0x01;  // KHU protocol version
    WriteLE32(&memo[1], chainActive.Height());  // stake_start_height
    WriteLE64(&memo[5], khuInput.amount);       // stake_amount

    zkhuOutput.memo = memo;
    tx.sapData->vShieldedOutput.push_back(zkhuOutput);

    // 4. Update UTXO tracker
    khuInput.fStaked = true;
    khuInput.nStakeStartHeight = chainActive.Height();

    return true;
}
```

### 7.3 Maturité

```cpp
bool IsNoteMatured(const OutputDescription& note, uint32_t currentHeight) {
    uint32_t stakeStartHeight = ReadLE32(&note.memo[1]);
    return (currentHeight - stakeStartHeight) >= 4320;  // 3 days
}
```

### 7.4 Rolling Frontier Tree

**Fichier:** `src/sapling/incrementalmerkletree.cpp`

**Principe:**
- Conserver uniquement les notes actives (pas toutes les notes historiques)
- Supprimer les notes unstaked
- Économie de stockage et mémoire

---

## 8. YIELD MECHANISM (Cr/Ur)

### 8.1 Scheduler

**Fichier:** `src/khu/khu_yield.cpp`

**Trigger:** Tous les 1440 blocs (1 jour)

```cpp
void UpdateDailyYield(KhuGlobalState& state, uint32_t nHeight, const CCoinsViewCache& view) {
    int64_t total_daily_yield = 0;

    // Iterate over all active ZKHU notes
    for (const auto& [nullifier, noteData] : view.GetActiveZKHUNotes()) {
        // Check maturity
        if ((nHeight - noteData.stakeStartHeight) >= 4320) {
            // Calculate daily yield
            int64_t daily = (noteData.amount * state.R_annual / 10000) / 365;

            // Accumulate
            total_daily_yield += daily;

            // Track per-note Ur
            noteData.Ur_accumulated += daily;
        }
    }

    // Update global state
    state.Ur += total_daily_yield;
    state.Cr += total_daily_yield;

    // Invariant preserved: Cr == Ur
}
```

### 8.2 R_MAX_dynamic Update

**Fichier:** `src/khu/khu_yield.cpp`

```cpp
void UpdateRMaxDynamic(KhuGlobalState& state, uint32_t nHeight) {
    uint32_t year = (nHeight - ACTIVATION_HEIGHT) / 525600;
    state.R_MAX_dynamic = std::max(400, 3000 - year * 100);  // basis points

    // Clamp R_annual if needed
    if (state.R_annual > state.R_MAX_dynamic)
        state.R_annual = state.R_MAX_dynamic;
}
```

### 8.3 Per-Note Tracking

**Structure:**
```cpp
struct ZKHUNoteData {
    uint256 nullifier;
    int64_t amount;
    uint32_t stakeStartHeight;
    int64_t Ur_accumulated;       // Accumulated yield for this note
    libzcash::SaplingPaymentAddress address;
};
```

**LevelDB:**
```
'K' + 'N' + nullifier  → ZKHUNoteData
```

---

## 9. UNSTAKE

### 9.1 Validation

**Fichier:** `src/khu/khu_unstake.cpp`

```cpp
bool CheckKHUUnstake(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Extract nullifier from Sapling spend
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // 2. Load note data
    ZKHUNoteData noteData;
    if (!view.GetZKHUNoteData(spend.nullifier, noteData))
        return state.Invalid(false, REJECT_INVALID, "khu-unstake-unknown-note");

    // 3. Check maturity
    if ((chainActive.Height() - noteData.stakeStartHeight) < 4320)
        return state.Invalid(false, REJECT_INVALID, "khu-unstake-immature");

    // 4. Calculate bonus
    int64_t bonus = noteData.Ur_accumulated;

    // 5. Verify output amount
    int64_t expectedAmount = noteData.amount + bonus;
    if (GetKHUOutputAmount(tx) != expectedAmount)
        return state.Invalid(false, REJECT_INVALID, "khu-unstake-incorrect-amount");

    return true;
}
```

### 9.2 Bonus Calculation

```cpp
int64_t CalculateUnstakeBonus(const CTransaction& tx, const CCoinsViewCache& view, const KhuGlobalState& state) {
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    ZKHUNoteData noteData;
    view.GetZKHUNoteData(spend.nullifier, noteData);

    // Bonus = accumulated Ur for this note
    return noteData.Ur_accumulated;
}
```

### 9.3 State Update

```cpp
void ApplyUnstake(KhuGlobalState& state, int64_t bonus) {
    state.U += bonus;
    state.C += bonus;
    state.Cr -= bonus;
    state.Ur -= bonus;

    // Invariants preserved:
    // C == U (U increased, C increased by same amount)
    // Cr == Ur (both decreased by same amount)
}
```

### 9.4 Auto-getnewaddress (CRITIQUE)

**Fichier:** `src/wallet/wallet.cpp`

**Règle:**
- Les sorties UNSTAKE KHU_T doivent **toujours** utiliser une nouvelle adresse
- **Jamais** réutiliser l'adresse de staking originale
- Géré automatiquement par le wallet

```cpp
bool CWallet::CreateKHUUnstakeTransaction(const uint256& nullifier, CMutableTransaction& tx) {
    // 1. Load note data
    ZKHUNoteData noteData;
    GetZKHUNoteData(nullifier, noteData);

    // 2. Calculate total output
    int64_t principal = noteData.amount;
    int64_t bonus = noteData.Ur_accumulated;
    int64_t total = principal + bonus;

    // 3. IMPORTANT: Get NEW address (never reuse staking address)
    CTxDestination newDest;
    if (!GetKeyFromPool(newDest, KEYPOOL_KHU))
        return false;

    CScript newScript = GetScriptForDestination(newDest);

    // 4. Create output
    tx.vout.push_back(CTxOut(total, newScript));

    // 5. Mark address as used
    SetAddressBook(newDest, "KHU unstake", "receive");

    return true;
}
```

**RPC:**
```cpp
UniValue unstakekhu(const JSONRPCRequest& request) {
    // ...
    CMutableTransaction tx;
    if (!pwallet->CreateKHUUnstakeTransaction(nullifier, tx))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to create unstake transaction");

    // Sign and broadcast
    // ...
}
```

**Rationale:**
- Privacy: éviter la corrélation stake → unstake
- Sécurité: réduire la surface d'attaque
- UX: automatique, pas de configuration utilisateur

---

## 10. DOMC (GOUVERNANCE R%)

### 10.1 Cycle Management

**Fichier:** `src/khu/khu_domc.cpp`

```cpp
void UpdateDOMCCycle(KhuGlobalState& state, uint32_t nHeight) {
    // Check if cycle is active
    if (nHeight < state.domc_cycle_start)
        return;

    uint32_t cyclePosition = (nHeight - state.domc_cycle_start) % state.domc_cycle_length;
    uint32_t currentPhase = cyclePosition / state.domc_phase_length;

    // Phase 0: COMMIT
    // Phase 1: REVEAL
    // Phase 2: TALLY
    // ...

    // At end of cycle, activate new R_annual
    if (cyclePosition == 0 && nHeight > state.domc_cycle_start) {
        ActivateNewRPercent(state, nHeight);
    }
}
```

### 10.2 Vote Transaction

**Fichier:** `src/khu/khu_domc.cpp`

```cpp
struct DOMCVotePayload {
    uint16_t R_proposal;        // Basis points
    uint256 commitment;         // Hash(R_proposal || secret)
    uint256 secret;             // Revealed in REVEAL phase
    CPubKey voterPubKey;
    std::vector<unsigned char> signature;

    SERIALIZE_METHODS(DOMCVotePayload, obj) { /* ... */ }
};
```

**Validation:**
```cpp
bool CheckDOMCVote(const CTransaction& tx, CValidationState& state, const KhuGlobalState& khuState) {
    DOMCVotePayload payload;
    GetTxPayload(tx, payload);

    // 1. Verify voter is masternode
    if (!masternodeManager.Has(payload.voterPubKey))
        return state.Invalid(false, REJECT_INVALID, "domc-vote-not-masternode");

    // 2. Verify phase
    uint32_t phase = GetCurrentDOMCPhase(khuState);
    if (phase == 0) {  // COMMIT phase
        if (payload.commitment.IsNull())
            return state.Invalid(false, REJECT_INVALID, "domc-vote-missing-commitment");
    } else if (phase == 1) {  // REVEAL phase
        // Verify commitment
        uint256 expected = Hash(payload.R_proposal, payload.secret);
        if (payload.commitment != expected)
            return state.Invalid(false, REJECT_INVALID, "domc-vote-invalid-reveal");
    }

    // 3. Verify R_proposal bounds
    if (payload.R_proposal > khuState.R_MAX_dynamic)
        return state.Invalid(false, REJECT_INVALID, "domc-vote-exceeds-max");

    return true;
}
```

### 10.3 Tally and Activation

```cpp
void ActivateNewRPercent(KhuGlobalState& state, uint32_t nHeight) {
    // 1. Collect all valid revealed votes
    std::vector<uint16_t> validVotes;
    for (const auto& vote : GetDOMCVotesForCycle(nHeight - state.domc_cycle_length))
        validVotes.push_back(vote.R_proposal);

    // 2. Calculate median
    std::sort(validVotes.begin(), validVotes.end());
    uint16_t median = validVotes[validVotes.size() / 2];

    // 3. Clamp to R_MAX_dynamic
    if (median > state.R_MAX_dynamic)
        median = state.R_MAX_dynamic;

    // 4. Activate
    state.R_annual = median;
    state.last_domc_height = nHeight;

    // 5. Start new cycle
    state.domc_cycle_start = nHeight;
}
```

---

## 11. HTLC CROSS-CHAIN

### 11.1 Transaction Types

**Fichier:** `src/khu/khu_htlc.h`

```cpp
struct HTLCPayload {
    uint256 hashlock;           // SHA256(preimage)
    uint32_t timelock;          // Absolute height
    int64_t amount;             // KHU_T locked
    CScript recipientScript;
    CScript refundScript;

    SERIALIZE_METHODS(HTLCPayload, obj) { /* ... */ }
};
```

### 11.2 HTLC Storage

**Clé LevelDB:**
```
'K' + 'T' + outpoint  → HTLCPayload
```

**Structure:**
```cpp
struct HTLCState {
    COutPoint outpoint;         // Locked KHU_T UTXO
    HTLCPayload payload;
    bool fClaimed;              // true if claimed
    bool fRefunded;             // true if refunded
};
```

### 11.3 CREATE

```cpp
bool CheckHTLCCreate(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    HTLCPayload payload;
    GetTxPayload(tx, payload);

    // 1. Verify KHU_T input exists
    CKHUUTXO khuInput;
    if (!view.GetKHUCoin(tx.vin[0].prevout, khuInput))
        return state.Invalid(false, REJECT_INVALID, "htlc-missing-input");

    // 2. Verify amount matches
    if (payload.amount != khuInput.amount)
        return state.Invalid(false, REJECT_INVALID, "htlc-amount-mismatch");

    // 3. Verify timelock is future
    if (payload.timelock <= chainActive.Height())
        return state.Invalid(false, REJECT_INVALID, "htlc-timelock-past");

    // 4. Lock UTXO (no state change, just marked as locked)
    return true;
}
```

### 11.4 CLAIM

```cpp
bool CheckHTLCClaim(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Load HTLC
    HTLCState htlc;
    if (!view.GetHTLC(tx.vin[0].prevout, htlc))
        return state.Invalid(false, REJECT_INVALID, "htlc-not-found");

    // 2. Verify preimage
    std::vector<unsigned char> preimage = tx.vExtraPayload;  // Or from witness
    uint256 hash = Hash(preimage.begin(), preimage.end());
    if (hash != htlc.payload.hashlock)
        return state.Invalid(false, REJECT_INVALID, "htlc-invalid-preimage");

    // 3. Verify timelock not expired
    if (chainActive.Height() > htlc.payload.timelock)
        return state.Invalid(false, REJECT_INVALID, "htlc-timelock-expired");

    // 4. Transfer KHU_T to recipient
    // No change to C, U (just UTXO ownership change)

    return true;
}
```

### 11.5 REFUND

```cpp
bool CheckHTLCRefund(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Load HTLC
    HTLCState htlc;
    if (!view.GetHTLC(tx.vin[0].prevout, htlc))
        return state.Invalid(false, REJECT_INVALID, "htlc-not-found");

    // 2. Verify timelock expired
    if (chainActive.Height() <= htlc.payload.timelock)
        return state.Invalid(false, REJECT_INVALID, "htlc-timelock-not-expired");

    // 3. Refund KHU_T to original owner
    // No change to C, U

    return true;
}
```

### 11.6 Invariant Preservation

**Principe:**
- HTLC ne crée ni ne détruit de KHU
- C et U restent inchangés pendant tout le cycle HTLC
- Seul le propriétaire de l'UTXO change (original → recipient ou refund)

---

## 12. RPC INTERFACE

### 12.1 État Global

**Fichier:** `src/rpc/khu.cpp`

```cpp
UniValue getkhuglobalstate(const JSONRPCRequest& request) {
    KhuGlobalState state;
    pKHUStateDB->ReadBestKHUState(state);

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", (int)state.nHeight);
    result.pushKV("blockhash", state.hashBlock.GetHex());
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("R_annual", state.R_annual);
    result.pushKV("R_MAX_dynamic", state.R_MAX_dynamic);

    double CD = (state.U == 0) ? 0.0 : (double)state.C / state.U;
    double CDr = (state.Ur == 0) ? 0.0 : (double)state.Cr / state.Ur;

    result.pushKV("CD", CD);
    result.pushKV("CDr", CDr);
    result.pushKV("invariants_ok", state.CheckInvariants());

    return result;
}
```

### 12.2 Opérations Wallet

```cpp
UniValue mintkhu(const JSONRPCRequest& request);         // PIV → KHU_T
UniValue redeemkhu(const JSONRPCRequest& request);       // KHU_T → PIV
UniValue stakekhu(const JSONRPCRequest& request);        // KHU_T → ZKHU
UniValue unstakekhu(const JSONRPCRequest& request);      // ZKHU → KHU_T (+ bonus)
UniValue votedomerc(const JSONRPCRequest& request);      // Vote DOMC
UniValue createhtlc(const JSONRPCRequest& request);      // HTLC_CREATE
UniValue claimhtlc(const JSONRPCRequest& request);       // HTLC_CLAIM
UniValue refundhtlc(const JSONRPCRequest& request);      // HTLC_REFUND
```

### 12.3 Informations

```cpp
UniValue listkhuutxos(const JSONRPCRequest& request);    // List KHU_T UTXOs
UniValue listzkhubalance(const JSONRPCRequest& request); // List staked ZKHU
UniValue getdomcstatus(const JSONRPCRequest& request);   // DOMC cycle info
UniValue listhtlcs(const JSONRPCRequest& request);       // List active HTLCs
```

---

## 13. EMISSION PIVX

### 13.1 Calcul Récompense

**Fichier:** `src/validation.cpp`

```cpp
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& consensusParams) {
    if (!consensusParams.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_KHU))
        return OLD_BLOCK_REWARD;

    int32_t year = (nHeight - KHU_ACTIVATION_HEIGHT) / 525600;
    int64_t reward_year = std::max(6 - year, 0LL);

    return reward_year * COIN;
}
```

### 13.2 Répartition

```cpp
void CreateCoinStake(CBlock& block, const CWallet& wallet) {
    CAmount blockReward = GetBlockSubsidy(block.nHeight, Params().GetConsensus());

    // 3 outputs:
    block.vtx[1]->vout.push_back(CTxOut(blockReward, stakerScript));     // Staker
    block.vtx[1]->vout.push_back(CTxOut(blockReward, masternodeScript)); // Masternode
    block.vtx[1]->vout.push_back(CTxOut(blockReward, daoScript));        // DAO
}
```

### 13.3 Fee Burning

```cpp
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& view) {
    // ...

    // Calculate fees
    CAmount nFees = 0;
    for (const auto& tx : block.vtx) {
        if (!tx->IsCoinBase() && !tx->IsCoinStake())
            nFees += view.GetValueIn(*tx) - tx->GetValueOut();
    }

    // IMPORTANT: Fees are NOT added to coinstake
    // They are implicitly burned (not included in any output)

    // Verify coinstake does NOT include fees
    if (block.vtx[1]->GetValueOut() != 3 * GetBlockSubsidy(pindex->nHeight, consensusParams))
        return state.Invalid(false, REJECT_INVALID, "bad-coinstake-amount");

    // ...
}
```

### 13.4 Inviolabilité de l'Émission

**RÈGLE ABSOLUE — À APPLIQUER DANS L'IMPLÉMENTATION:**

```cpp
// ❌ INTERDIT : Modifier, optimiser, interpoler reward_year
// ❌ INTERDIT : Ajouter year_fraction ou calcul proportionnel
// ❌ INTERDIT : Créer table alternative ou cache
// ❌ INTERDIT : Moduler selon network conditions
// ✅ AUTORISÉ : Uniquement max(6 - year, 0)

// GetBlockSubsidy est SACRÉ et ne doit JAMAIS être "amélioré"
```

**SÉPARATION STRICTE:**
- `GetBlockSubsidy()` retourne reward_year (émission PIVX, immuable)
- `CalculateDailyYield()` utilise R% (yield KHU, gouverné par DOMC)
- Ces deux fonctions sont INDÉPENDANTES et ne doivent JAMAIS s'influencer

**ANTI-DÉRIVE:**
Si un développeur propose de "lisser l'émission" ou "ajuster selon le staking", c'est une violation de consensus et doit être rejeté immédiatement.

---

## 14. FICHIERS CRÉÉS/MODIFIÉS (RÉSUMÉ)

### 14.1 Nouveaux Fichiers

```
src/khu/khu_state.h           # KhuGlobalState definition
src/khu/khu_state.cpp         # State management
src/khu/khu_db.h              # LevelDB interface
src/khu/khu_db.cpp            # Database operations
src/khu/khu_utxo.h            # CKHUUTXO structure
src/khu/khu_utxo.cpp          # UTXO tracker
src/khu/khu_mint.cpp          # MINT logic
src/khu/khu_redeem.cpp        # REDEEM logic
src/khu/khu_stake.cpp         # STAKE logic
src/khu/khu_unstake.cpp       # UNSTAKE logic + bonus calc
src/khu/khu_yield.cpp         # Daily yield scheduler
src/khu/khu_domc.cpp          # DOMC voting
src/khu/khu_htlc.cpp          # HTLC cross-chain
src/khu/khu_finality.cpp      # Finality checks
src/rpc/khu.cpp               # RPC commands
src/test/khu_tests.cpp        # Unit tests
```

### 14.2 Fichiers Modifiés

```
src/primitives/transaction.h  # Add TX_KHU_* types
src/validation.h              # ConnectBlock/DisconnectBlock
src/validation.cpp            # State management
src/consensus/tx_verify.cpp   # KHU tx validation
src/coins.h                   # CKHUCoinsMap
src/coins.cpp                 # KHU UTXO cache
src/txdb.h                    # KHU LevelDB keys
src/chainparams.cpp           # UPGRADE_KHU parameters
src/wallet/wallet.h           # KHU balance tracking
src/wallet/wallet.cpp         # Auto-getnewaddress for UNSTAKE
src/init.cpp                  # KHU initialization
src/sapling/sapling_transaction.h  # ZKHU metadata
```

---

## 15. SÉQUENCE D'ACTIVATION

### Phase 0: Pre-KHU
```
height < ACTIVATION_HEIGHT
- Standard PIVX consensus
- No KHU functionality
```

### Phase 1: Base Consensus (Block ACTIVATION_HEIGHT)
```
- Initialize KhuGlobalState (C=0, U=0, Cr=0, Ur=0)
- Enable MINT/REDEEM
- Emission 6 PIV active
- Fee burning active
- Finality after 12 blocks
```

### Phase 2: Sapling Staking (Block ACTIVATION_HEIGHT + X)
```
- Enable STAKE/UNSTAKE
- Yield mechanism active
- R_annual = 500 (5% initial)
```

### Phase 3: DOMC (Block ACTIVATION_HEIGHT + 525600)
```
- First DOMC cycle starts
- Voting enabled
```

### Phase 4: HTLC (Block ACTIVATION_HEIGHT + Y)
```
- HTLC enabled
- Cross-chain swaps active
```

---

## 16. INVARIANTS ET SÉCURITÉ

### 16.1 Invariants Stricts

```
INVARIANT_1: C == U               (toujours, aucune exception)
INVARIANT_2: Cr == Ur             (toujours, aucune exception)
```

**Enforcement:**
- Vérifié à chaque bloc dans `ConnectBlock()`
- Bloc rejeté si violation
- État revert en cas de reorg
- Finalité empêche modification après 12 blocs

**INTERDICTION ABSOLUE:**
```
Toute modification de C, U, Cr, Ur en dehors des opérations suivantes est INTERDITE:
  - MINT:         C += amount, U += amount
  - REDEEM:       C -= amount, U -= amount
  - UNSTAKE:      C += bonus, U += bonus, Cr -= bonus, Ur -= bonus
  - DAILY_YIELD:  Cr += total, Ur += total

Aucune autre fonction ne peut modifier ces variables.
Aucune exception. Aucun contournement. Aucune optimisation.
```

### 16.2 Protections

```
1. Reorg protection: 12 blocks finality
2. State consistency: hashPrevState chain
3. Invariant checks: every block
4. DOMC bounds: R ≤ R_MAX_dynamic
5. Maturity enforcement: 4320 blocks minimum
6. Fee burning: no inflation from fees
7. HTLC atomicity: all-or-nothing
8. Auto-getnewaddress: privacy protection
```

---

## 17. PERFORMANCE

### 17.1 Optimisations

```
- Cache UTXO KHU en mémoire (CKHUCoinsMap)
- Rolling Frontier Tree (notes actives seulement)
- Batch writes LevelDB (tous les blocs)
- Delta-based reorg (si implémenté)
- Index notes par nullifier
```

### 17.2 Complexité

```
MINT/REDEEM: O(1) state update
STAKE/UNSTAKE: O(1) Sapling operation
Daily yield: O(n) where n = active ZKHU notes
DOMC tally: O(m) where m = masternode votes
HTLC: O(1) UTXO operations
```

---

## END OF ARCHITECTURE OVERVIEW

Ce document décrit l'implémentation complète de PIVX-V6-KHU.

Chaque module, fichier, et fonction est défini.

Les règles du SPEC (01) sont implémentées exactement comme décrit.

Aucune déviation. Aucune approximation. Implémentation stricte uniquement.
