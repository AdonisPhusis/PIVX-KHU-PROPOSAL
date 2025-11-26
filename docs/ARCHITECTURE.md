# PIVX-V6-KHU — ARCHITECTURE

Version: 2.0.0
Status: FINAL
Base: PIVX Core v5.6.1 + KHU Extensions
Last Update: 2025-11-26

---

## RÔLE DE CE DOCUMENT

> **ARCHITECTURE.md = STRUCTURE DU CODE**
>
> Ce document décrit **comment le code est organisé** pour implémenter les règles de SPEC.md:
> - Organisation des modules et fichiers
> - Patterns d'implémentation (locks, atomicité)
> - Schéma LevelDB
> - Flux de données
> - Décisions architecturales
>
> **Ce document NE répète PAS:**
> - Les formules mathématiques → voir SPEC.md
> - Les valeurs des constantes → voir SPEC.md
> - Les règles d'invariants → voir SPEC.md
>
> **Règle:** Pour les formules et règles de consensus, toujours référencer SPEC.md.

---

## 1. MODULES

### 1.1 Nouveaux modules (src/khu/)

```
src/khu/
├── khu_state.h/cpp          # KhuGlobalState (voir SPEC.md §2)
├── khu_db.h/cpp             # Persistence LevelDB
├── khu_utxo.h/cpp           # Tracker KHU_T UTXO
├── khu_validation.h/cpp     # Validation transactions KHU
├── khu_mint.h/cpp           # Logic MINT
├── khu_redeem.h/cpp         # Logic REDEEM
├── khu_stake.h/cpp          # Logic STAKE
├── khu_unstake.h/cpp        # Logic UNSTAKE
├── khu_yield.h/cpp          # Scheduler Cr/Ur
├── khu_domc.h/cpp           # Gouvernance R%
├── khu_dao.h/cpp            # DAO Treasury
└── khu_htlc.h/cpp           # HTLC cross-chain
```

### 1.2 Modules PIVX modifiés

```
src/primitives/transaction.h # Nouveaux TxType
src/validation.h/cpp         # ConnectBlock/DisconnectBlock
src/consensus/tx_verify.cpp  # Validation transactions KHU
src/coins.h/cpp              # Extension cache UTXO KHU
src/txdb.h/cpp               # Clés LevelDB KHU
src/chainparams.cpp          # UPGRADE_KHU parameters
src/rpc/khu.cpp              # RPC KHU (nouveau)
src/wallet/wallet.h/cpp      # Gestion KHU_T
src/wallet/khu_wallet.cpp    # Wallet KHU spécialisé
src/sapling/                 # Extensions ZKHU
src/llmq/                    # Finalité (réutilisation)
```

---

## 2. LEVELDB SCHEMA

### 2.1 Namespace 'K' (KHU)

```
'K' + 'S' + height         → KhuGlobalState (state at height)
'K' + 'B'                  → uint256 (best state hash)
'K' + 'H' + hash           → KhuGlobalState (state by hash)
'K' + 'C' + height         → KhuStateCommitment (finality sig)
'K' + 'U' + outpoint       → CKHUUTXO
'K' + 'N' + nullifier      → ZKHUNoteData
'K' + 'D' + cycle          → DOMC vote data
'K' + 'T' + outpoint       → HTLC data
```

### 2.2 ZKHU (séparé de Shield)

```
ZKHU:   'K' + 'A' + anchor    → ZKHU SaplingMerkleTree
        'K' + 'N' + nullifier → ZKHU nullifier spent flag

Shield: 'S' + anchor          → Shield SaplingMerkleTree
        's' + nullifier       → Shield nullifier spent flag
```

> ⚠️ **ZKHU et Shield utilisent des namespaces DISTINCTS. Pas de pool partagé.**

---

## 3. CONNECTBLOCK — IMPLÉMENTATION

### 3.1 Ordre d'exécution

```cpp
// src/validation.cpp
bool ConnectBlock(const CBlock& block, ...) {
    // (0) Lock order OBLIGATOIRE
    LOCK2(cs_main, cs_khu);

    // (1) Load previous state
    KhuGlobalState prevState, newState;
    pKHUStateDB->ReadKHUState(pindex->nHeight - 1, prevState);
    newState = prevState;

    // (2) Daily updates (T + Yield) — AVANT transactions
    ApplyDailyUpdatesIfNeeded(newState, pindex->nHeight);

    // (3) Process KHU transactions
    for (const auto& tx : block.vtx) {
        if (!ProcessKHUTransaction(tx, newState, view, state))
            return false;  // REJECT immediately
    }

    // (4) Block reward (emission)
    ApplyBlockReward(newState, pindex->nHeight);

    // (5) Check invariants
    if (!newState.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // (6) Persist
    pKHUStateDB->WriteKHUState(newState);
    return true;
}
```

> **Référence:** Voir SPEC.md §13 pour l'ordre canonique.

### 3.2 DisconnectBlock (STATE-BASED)

```cpp
// DÉCISION: State-based reorg (pas de deltas)
bool DisconnectBlock(const CBlock& block, CBlockIndex* pindex, ...) {
    LOCK2(cs_main, cs_khu);

    // Load complete previous state (no delta computation)
    KhuGlobalState prev;
    pKHUStateDB->ReadKHUState(pindex->nHeight - 1, prev);

    // Atomic revert
    khuGlobalState = prev;
    return true;
}
```

**Justification:** Finalité LLMQ → max 12 blocs de reorg → overhead négligeable.

---

## 4. LOCK PROTOCOL

### 4.1 Ordre des locks (OBLIGATOIRE)

```cpp
// CORRECT
LOCK2(cs_main, cs_khu);

// INTERDIT (deadlock)
LOCK(cs_khu);
LOCK(cs_main);
```

### 4.2 Assertion dans fonctions de mutation

```cpp
void ModifyKhuState(...) {
    AssertLockHeld(cs_khu);  // PREMIÈRE LIGNE
    // ... mutations ...
}
```

---

## 5. PATTERNS D'ATOMICITÉ

### 5.1 Mutations C/U (MINT, REDEEM)

```cpp
// MINT — Toujours ensemble
state.C += amount;
state.U += amount;
// AUCUN code entre ces deux lignes

// REDEEM — Toujours ensemble
state.C -= amount;
state.U -= amount;
// AUCUN code entre ces deux lignes
```

### 5.2 Double Flux UNSTAKE (5 mutations)

```cpp
bool ApplyKHUUnstake(const CTransaction& tx, KhuGlobalState& state, ...) {
    AssertLockHeld(cs_khu);

    // Vérifications AVANT mutations
    int64_t P = GetPrincipal(tx);
    int64_t Y = CalculateYield(tx);  // Y = 0 if immature
    if (state.Cr < Y || state.Ur < Y)
        return state.Invalid("insufficient-reward-pool");

    // 5 mutations ATOMIQUES — aucun code entre
    state.Z  -= P;
    state.U  += P + Y;
    state.C  += Y;
    state.Cr -= Y;
    state.Ur -= Y;

    return state.CheckInvariants();
}
```

> **Référence:** Voir SPEC.md §4.5 pour les effets mathématiques.

### 5.3 Pattern interdit: Rollback partiel

```cpp
// ❌ INTERDIT
state.Z -= P;
state.U += P + Y;
if (someError) {
    state.Z += P;    // Rollback interdit
    state.U -= P + Y;
    return false;
}

// ✅ CORRECT: Vérifier AVANT de muter
if (!CanUnstake(tx, state))
    return false;  // Pas de mutation
// Puis muter atomiquement
```

---

## 6. YIELD STREAMING

### 6.1 Décision: LevelDB Cursor (pas RAM)

```cpp
bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight) {
    AssertLockHeld(cs_khu);

    int64_t totalYield = 0;
    CDBBatch batch(CLIENT_VERSION);

    // Streaming via cursor (mémoire O(1))
    auto cursor = pKHUNoteDB->GetCursor();
    for (cursor->SeekToFirst(); cursor->Valid(); cursor->Next()) {
        CKHUNoteData note = cursor->GetNote();
        if (note.fSpent) continue;
        if (!IsMature(note, nHeight)) continue;

        int64_t daily = CalculateDailyYieldForNote(note, state.R_annual);
        note.Ur_accumulated += daily;
        batch.Write(note.nullifier, note);
        totalYield += daily;
    }

    // Global state update
    state.Cr += totalYield;
    state.Ur += totalYield;

    // Single atomic write
    pKHUNoteDB->WriteBatch(batch);
    return true;
}
```

**Justification:** Évite OOM avec large note set (>100k notes).

> **Référence:** Voir SPEC.md §6 pour la formule de yield.

---

## 7. SAPLING INTEGRATION

### 7.1 Principe

- **Réutiliser** circuits Sapling existants (pas de modification)
- **Séparer** storage ZKHU de Shield (namespaces distincts)
- **Encoder** metadata dans memo field (512 bytes)

### 7.2 Rolling Frontier Tree

```cpp
// Notes ZKHU = append-only tree
// Marquage via fSpent, pas suppression
struct CKHUNoteData {
    uint256 commitment;      // Never deleted
    uint256 nullifier;
    int64_t amount;
    int64_t Ur_accumulated;
    uint32_t nStakeStartHeight;
    bool fSpent;             // Marking, not deletion
};
```

**Règle:** Commitment JAMAIS supprimé (merkle proofs restent valides).

---

## 8. VALIDATION TRANSACTIONS

### 8.1 Switch principal

```cpp
// src/consensus/tx_verify.cpp
bool CheckTransaction(const CTransaction& tx, CValidationState& state) {
    switch (tx.nType) {
        case TX_KHU_MINT:    return CheckKHUMint(tx, state);
        case TX_KHU_REDEEM:  return CheckKHURedeem(tx, state);
        case TX_KHU_STAKE:   return CheckKHUStake(tx, state);
        case TX_KHU_UNSTAKE: return CheckKHUUnstake(tx, state);
        case TX_DOMC_VOTE:   return CheckDOMCVote(tx, state);
        case TX_HTLC_CREATE:
        case TX_HTLC_CLAIM:
        case TX_HTLC_REFUND: return CheckHTLC(tx, state);
        default:             return CheckStandardTransaction(tx, state);
    }
}
```

### 8.2 Règle de rejet

```cpp
// Dans ConnectBlock
if (!ProcessKHUTransaction(tx, state, view, validationState))
    return false;  // REJECT BLOC IMMÉDIATEMENT

// JAMAIS ignorer return value
```

---

## 9. FINALITY (LLMQ)

### 9.1 Réutilisation ChainLock

```cpp
bool CheckKHUFinality(uint32_t nHeight, const KhuGlobalState& state) {
    uint256 hashBlock = chainActive[nHeight]->GetBlockHash();

    // Réutiliser ChainLock existant
    if (!llmq::chainLocksHandler->HasChainLock(nHeight, hashBlock))
        return false;

    return true;
}
```

### 9.2 Paramètres

- Finality depth: 12 blocs
- Quorum rotation: 240 blocs
- Quorum type: LLMQ_400_60

---

## 10. RPC INTERFACE

### 10.1 État global

```cpp
// src/rpc/khu.cpp
UniValue getkhustate(const JSONRPCRequest& request);
UniValue getkhustatecommitment(const JSONRPCRequest& request);
```

### 10.2 Opérations wallet

```cpp
UniValue mintkhu(const JSONRPCRequest& request);      // PIV → KHU_T
UniValue redeemkhu(const JSONRPCRequest& request);    // KHU_T → PIV
UniValue stakekhu(const JSONRPCRequest& request);     // KHU_T → ZKHU
UniValue unstakekhu(const JSONRPCRequest& request);   // ZKHU → KHU_T
UniValue sendkhu(const JSONRPCRequest& request);      // Transfer KHU_T
UniValue getkhubalance(const JSONRPCRequest& request);
UniValue listkhuunspent(const JSONRPCRequest& request);
```

### 10.3 DOMC

```cpp
UniValue domccommit(const JSONRPCRequest& request);
UniValue domcreveal(const JSONRPCRequest& request);
UniValue getdomcstatus(const JSONRPCRequest& request);
```

### 10.4 ZKHU

```cpp
UniValue listzkhubalance(const JSONRPCRequest& request);
UniValue getzkhuinfo(const JSONRPCRequest& request);
```

---

## 11. AUTO-GETNEWADDRESS (UNSTAKE)

### 11.1 Règle de privacy

```cpp
bool CWallet::CreateKHUUnstakeTransaction(const uint256& nullifier, CMutableTransaction& tx) {
    // ...

    // IMPORTANT: Get NEW address (never reuse staking address)
    CTxDestination newDest;
    if (!GetKeyFromPool(newDest, KEYPOOL_KHU))
        return false;

    CScript newScript = GetScriptForDestination(newDest);
    tx.vout.push_back(CTxOut(P + Y, newScript));

    return true;
}
```

**Justification:** Privacy protection — éviter corrélation stake → unstake.

---

## 12. OVERFLOW PROTECTION

### 12.1 Yield calculation

```cpp
// Division AVANT multiplication pour éviter overflow
int64_t daily = (note.amount / 10000) * R_annual / 365;

// ❌ INTERDIT (overflow possible)
// int64_t daily = (note.amount * R_annual) / 10000 / 365;
```

### 12.2 Treasury

```cpp
void AccumulateDaoTreasury(KhuGlobalState& state) {
    // Utiliser __int128 pour calculs intermédiaires
    __int128 total = (__int128)state.U + (__int128)state.Ur;
    __int128 budget = total / 182500;

    if (budget > MAX_MONEY) budget = MAX_MONEY;
    state.T += (CAmount)budget;
}
```

---

## 13. ERROR HANDLING

### 13.1 Pattern: Invalid() au lieu de assert()

```cpp
// ✅ CORRECT
if (!state.CheckInvariants())
    return validationState.Invalid("khu-invariant-violation");

// ❌ INTERDIT (crash node)
assert(state.CheckInvariants());
```

### 13.2 Error codes

```cpp
enum KHURejectCode {
    REJECT_KHU_INVARIANT_CD  = 0x60,
    REJECT_KHU_INVARIANT_CDR = 0x61,
    REJECT_KHU_INVALID_MINT  = 0x62,
    REJECT_KHU_INVALID_REDEEM = 0x63,
    REJECT_KHU_INVALID_STAKE = 0x64,
    REJECT_KHU_INVALID_UNSTAKE = 0x65,
    REJECT_KHU_IMMATURE_YIELD = 0x67,
};
```

---

## 14. FICHIERS RÉSUMÉ

### 14.1 Nouveaux fichiers

```
src/khu/*.h/cpp              # Modules KHU
src/rpc/khu.cpp              # RPC commands
src/wallet/khu_wallet.cpp    # Wallet KHU
src/test/khu_tests.cpp       # Unit tests
```

### 14.2 Fichiers modifiés

```
src/primitives/transaction.h  # TX_KHU_* types
src/validation.h/cpp          # ConnectBlock
src/consensus/tx_verify.cpp   # KHU validation
src/coins.h/cpp               # KHU UTXO cache
src/txdb.h/cpp                # LevelDB keys
src/chainparams.cpp           # UPGRADE_KHU
src/init.cpp                  # KHU init
src/wallet/wallet.h/cpp       # KHU balance
```

---

## VERSION HISTORY

```
2.0.0 - Refactoring complet:
        - Recentré sur STRUCTURE DU CODE uniquement
        - Supprimé formules/constantes (→ SPEC.md)
        - Ajout patterns d'atomicité explicites
        - Ajout overflow protection patterns
        - Simplifié (14 sections vs 18)
1.4.1 - 17 champs, Y (yield) notation
```

---

## END OF ARCHITECTURE

Ce document décrit l'organisation du code PIVX-V6-KHU.

Pour les règles mathématiques et valeurs canoniques, voir **SPEC.md**.
