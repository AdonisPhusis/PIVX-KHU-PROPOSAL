# PHASE 3 PLAN — FINALITÉ MASTERNODE KHU

**Date:** 2025-11-23
**Branch:** `khu-phase3-finality`
**Base:** Phase 2 FREEZE
**Status:** 🚀 **PLANNING**

---

## 1. OBJECTIF PHASE 3

**Phase 3 = FINALITÉ MASTERNODE (LLMQ ChainLocks pour KHU)**

### Problème à Résoudre

Actuellement (Phase 2) :
- ✅ KHU state (C, U) stocké par bloc
- ✅ Reorg protection basique (depth check)
- ❌ **MAIS**: Pas de garantie cryptographique de finalité
- ❌ **RISQUE**: Nodes peuvent diverger sur l'état KHU
- ❌ **RISQUE**: Reorg profonds peuvent casser la cohérence

### Solution Phase 3

**StateCommitment signé par LLMQ masternodes**

```cpp
hashState = SHA256(C, U, Cr, Ur, height)
CLSIG-KHU(height, hashState)  // Signature quorum
```

**Garanties:**
1. État KHU cryptographiquement finalisé
2. Consensus masternode sur hashState
3. Reorg > 12 blocs impossible si signatures divergent
4. Détection immédiate de divergence d'état

---

## 2. SCOPE STRICT PHASE 3

### ✅ CE QUI EST INCLUS

- **StateCommitment** structure et hash
- **LLMQ integration** pour signatures
- **DB storage** (LevelDB prefix "K/C")
- **Validation hooks** (FinalizeKHUStateIfQuorum)
- **Reorg protection** cryptographique
- **RPC read-only** (getkhustatecommitment)
- **Tests unitaires** complets

### ❌ CE QUI EST EXCLU (autres phases)

- ❌ **Pas de R%** (yield rate - Phase 5)
- ❌ **Pas de Daily Yield** (Phase 5)
- ❌ **Pas de Cr/Ur mutations** (Phase 5)
- ❌ **Pas de KHU_STAKE** (Phase 6+)
- ❌ **Pas de ZKHU** (Phase 7+)
- ❌ **Pas de HTLC** (Phase 8+)
- ❌ **Pas de DOMC** (Phase 9+)
- ❌ **Pas de Zerocoin/zPIV**

**RÈGLE:** Phase 3 touche UNIQUEMENT à la finalité cryptographique de l'état existant (C, U, Cr, Ur).

---

## 3. ARCHITECTURE PHASE 3

### 3.1 Nouveaux Fichiers

```
src/khu/khu_commitment.h          # StateCommitment structure
src/khu/khu_commitment.cpp        # Create/Verify functions
src/khu/khu_commitmentdb.h        # LevelDB wrapper
src/khu/khu_commitmentdb.cpp      # DB implementation
src/test/khu_phase3_tests.cpp     # Unit tests Phase 3
```

### 3.2 Fichiers Modifiés

```
src/validation.cpp                # Hook FinalizeKHUStateIfQuorum
src/khu/khu_validation.cpp        # Reorg protection
src/rpc/blockchain.cpp            # RPC getkhustatecommitment
src/Makefile.am                   # Add new files
src/Makefile.test.include         # Add phase3 tests
```

### 3.3 Structure KhuStateCommitment

```cpp
struct KhuStateCommitment {
    uint32_t nHeight;              // Block height
    uint256 hashState;             // SHA256(C, U, Cr, Ur, height)

    // LLMQ signature data
    uint256 quorumHash;            // LLMQ quorum identifier
    CBLSSignature sig;             // Aggregate BLS signature
    std::vector<bool> signers;     // Bitfield of signers

    // Validation
    bool IsValid() const;
    bool HasQuorum() const;        // >= 60% signatures
    uint256 GetHash() const;       // For consensus

    SERIALIZE_METHODS(KhuStateCommitment, obj) {
        READWRITE(obj.nHeight);
        READWRITE(obj.hashState);
        READWRITE(obj.quorumHash);
        READWRITE(obj.sig);
        READWRITE(obj.signers);
    }
};
```

### 3.4 Calcul hashState

```cpp
uint256 ComputeKHUStateHash(const KhuGlobalState& state)
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << state.C;
    ss << state.U;
    ss << state.Cr;
    ss << state.Ur;
    ss << state.nHeight;
    return ss.GetHash();
}
```

---

## 4. IMPLÉMENTATION DÉTAILLÉE

### 4.1 khu_commitment.h/cpp

**Fonctions principales:**

```cpp
// Create commitment for current state
KhuStateCommitment CreateKHUStateCommitment(
    const KhuGlobalState& state,
    const uint256& quorumHash
);

// Verify commitment signature
bool VerifyKHUStateCommitment(
    const KhuStateCommitment& commitment,
    const llmq::CQuorum& quorum
);

// Check if commitment has quorum (>= 60%)
bool HasQuorumSignatures(
    const KhuStateCommitment& commitment,
    const llmq::CQuorum& quorum
);

// Compute state hash
uint256 ComputeKHUStateHash(const KhuGlobalState& state);
```

### 4.2 khu_commitmentdb.h/cpp

**Database operations:**

```cpp
class CKHUCommitmentDB {
private:
    std::unique_ptr<CDBWrapper> db;

public:
    CKHUCommitmentDB(size_t nCacheSize, bool fMemory, bool fWipe);

    // Write commitment
    bool WriteCommitment(
        uint32_t nHeight,
        const KhuStateCommitment& commitment
    );

    // Read commitment
    bool ReadCommitment(
        uint32_t nHeight,
        KhuStateCommitment& commitment
    );

    // Check if commitment exists
    bool HaveCommitment(uint32_t nHeight);

    // Erase commitment (for reorg)
    bool EraseCommitment(uint32_t nHeight);

    // Get latest finalized height
    uint32_t GetLatestFinalizedHeight();
};
```

**LevelDB prefix:** `"K/C" + height → commitment`

### 4.3 Validation Hook

**Dans validation.cpp - ConnectBlock():**

```cpp
// After ProcessKHUBlock succeeds
if (NetworkUpgradeActive(pindex->nHeight, consensusParams, Consensus::UPGRADE_V6_0)) {
    KhuGlobalState khuState;
    if (!GetKHUStateForBlock(pindex->nHeight, khuState)) {
        return state.Error("khu-state-missing");
    }

    // Try to finalize state if quorum reached
    if (!FinalizeKHUStateIfQuorum(pindex->nHeight, khuState, state)) {
        return state.Error("khu-finality-failed");
    }
}
```

**Fonction FinalizeKHUStateIfQuorum:**

```cpp
bool FinalizeKHUStateIfQuorum(
    uint32_t nHeight,
    const KhuGlobalState& state,
    CValidationState& validationState
)
{
    // 1. Compute state hash
    uint256 hashState = ComputeKHUStateHash(state);

    // 2. Check if commitment already exists
    KhuStateCommitment existingCommit;
    if (GetKHUCommitmentDB()->ReadCommitment(nHeight, existingCommit)) {
        // Verify match
        if (existingCommit.hashState != hashState) {
            return validationState.Error("khu-statecommit-mismatch");
        }
        return true; // Already finalized
    }

    // 3. Get active quorum
    auto quorum = llmq::GetActiveQuorumForKHU(nHeight);
    if (!quorum) {
        return true; // No quorum yet, wait
    }

    // 4. Request signatures (async)
    llmq::RequestKHUStateSignature(nHeight, hashState, *quorum);

    // 5. Check if we have enough signatures
    auto commitment = CollectKHUStateCommitment(nHeight, hashState, *quorum);
    if (!commitment || !commitment->HasQuorum()) {
        return true; // Not enough sigs yet
    }

    // 6. Verify and store
    if (!VerifyKHUStateCommitment(*commitment, *quorum)) {
        return validationState.Error("khu-commit-invalid-sig");
    }

    if (!GetKHUCommitmentDB()->WriteCommitment(nHeight, *commitment)) {
        return validationState.Error("khu-commit-db-write-failed");
    }

    LogPrintf("KHU state finalized at height %d: %s\n", nHeight, hashState.ToString());
    return true;
}
```

### 4.4 Reorg Protection

**Dans khu_validation.cpp - DisconnectKHUBlock:**

```cpp
bool DisconnectKHUBlock(
    const CBlock& block,
    CBlockIndex* pindex,
    CValidationState& state
)
{
    // Existing code...

    // NEW: Check finality
    uint32_t finalizedHeight = GetKHUCommitmentDB()->GetLatestFinalizedHeight();

    if (pindex->nHeight <= finalizedHeight) {
        // Cannot reorg finalized blocks
        return state.Error(
            "khu-reorg-finalized",
            strprintf("Cannot reorg block %d (finalized at %d)",
                      pindex->nHeight, finalizedHeight)
        );
    }

    // Check depth limit
    int reorgDepth = chainActive.Height() - pindex->nHeight;
    if (reorgDepth > 12) {
        return state.Error(
            "khu-reorg-too-deep",
            strprintf("KHU reorg too deep: %d blocks", reorgDepth)
        );
    }

    // Existing undo logic...
}
```

### 4.5 RPC Implementation

**Dans rpc/blockchain.cpp:**

```cpp
static UniValue getkhustatecommitment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getkhustatecommitment height\n"
            "\nReturns KHU state commitment for a given block height.\n"
            "\nArguments:\n"
            "1. height    (numeric, required) The block height\n"
            "\nResult:\n"
            "{\n"
            "  \"height\": n,           (numeric) Block height\n"
            "  \"hashState\": \"hex\",    (string) State hash\n"
            "  \"quorumHash\": \"hex\",   (string) Quorum hash\n"
            "  \"signature\": \"hex\",    (string) BLS signature\n"
            "  \"signers\": n,          (numeric) Number of signers\n"
            "  \"finalized\": bool      (boolean) Has quorum\n"
            "}\n"
        );
    }

    uint32_t nHeight = request.params[0].get_int();

    KhuStateCommitment commitment;
    if (!GetKHUCommitmentDB()->ReadCommitment(nHeight, commitment)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "No commitment at this height");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", (int)commitment.nHeight);
    result.pushKV("hashState", commitment.hashState.GetHex());
    result.pushKV("quorumHash", commitment.quorumHash.GetHex());
    result.pushKV("signature", commitment.sig.ToString());

    int signerCount = 0;
    for (bool signer : commitment.signers) {
        if (signer) signerCount++;
    }
    result.pushKV("signers", signerCount);
    result.pushKV("finalized", commitment.HasQuorum());

    return result;
}

// Register RPC
static const CRPCCommand commands[] = {
    { "blockchain", "getkhustatecommitment", &getkhustatecommitment, {"height"} },
};
```

---

## 5. TESTS PHASE 3

### 5.1 Test Suite

**Fichier:** `src/test/khu_phase3_tests.cpp`

**Tests requis:**

1. **test_statecommit_consistency**
   - Créer état KHU (C=100, U=100)
   - Calculer hashState
   - Vérifier: même état → même hash
   - Vérifier: état différent → hash différent

2. **test_statecommit_creation**
   - Créer commitment
   - Vérifier structure
   - Vérifier serialization

3. **test_statecommit_signed**
   - Mock quorum
   - Créer commitment avec signatures
   - Vérifier quorum (>= 60%)
   - Vérifier validation

4. **test_statecommit_invalid**
   - Tenter commitment avec mauvais hash
   - Vérifier: rejeté

5. **test_finality_blocks_locked**
   - Finaliser bloc N
   - Tenter reorg bloc N
   - Vérifier: rejeté ("khu-reorg-finalized")

6. **test_reorg_depth_limit**
   - Tenter reorg > 12 blocs
   - Vérifier: rejeté ("khu-reorg-too-deep")

7. **test_commitment_db**
   - Write commitment
   - Read commitment
   - Verify persistence
   - Erase commitment

8. **test_state_hash_deterministic**
   - Même état → toujours même hash
   - Ordre sérialization correct

---

## 6. INTÉGRATION LLMQ

### 6.1 Quorum Selection

**Pour KHU finality, utiliser quorum type:**
- `LLMQ_TYPE_50_60` (50 masternodes, 60% threshold)
- Ou créer nouveau type `LLMQ_TYPE_KHU`

### 6.2 Signature Message

```cpp
// Message to sign
struct KHUStateSignatureMessage {
    uint32_t nHeight;
    uint256 hashState;

    SERIALIZE_METHODS(KHUStateSignatureMessage, obj) {
        READWRITE(obj.nHeight);
        READWRITE(obj.hashState);
    }
};
```

### 6.3 Signature Collection

**Utiliser infrastructure LLMQ existante:**
- `llmq::CSigningManager::SignShare()`
- `llmq::CSigningManager::CollectSignatures()`
- `llmq::CQuorum::VerifyRecoveredSig()`

---

## 7. MÉTRIQUES ATTENDUES

### 7.1 Performance

| Opération | Temps Max |
|-----------|-----------|
| ComputeKHUStateHash | < 1ms |
| CreateCommitment | < 10ms |
| VerifyCommitment | < 50ms |
| DB Write | < 5ms |
| DB Read | < 2ms |

### 7.2 Storage

| Item | Size |
|------|------|
| Commitment struct | ~200 bytes |
| DB per block | ~200 bytes |
| DB per year | ~10 MB |

---

## 8. LIVRAISON PHASE 3

### 8.1 Code

- ✅ `src/khu/khu_commitment.{h,cpp}` (créé)
- ✅ `src/khu/khu_commitmentdb.{h,cpp}` (créé)
- ✅ Modifications `validation.cpp` (hook)
- ✅ Modifications `khu_validation.cpp` (reorg)
- ✅ RPC `getkhustatecommitment` (créé)

### 8.2 Tests

- ✅ `src/test/khu_phase3_tests.cpp` (8 tests)
- ✅ Tous tests passent
- ✅ Integration avec make check

### 8.3 Documentation

- ✅ `docs/reports/RAPPORT_PHASE3_FINALITE.md`
- ✅ Code comments complets
- ✅ RPC documentation

---

## 9. VALIDATION

### 9.1 Checklist

- [ ] Code compile sans erreurs
- [ ] Tous tests passent (21 Phase1+2 + 8 Phase3 = 29 total)
- [ ] Pas de warnings KHU
- [ ] RPC fonctionne
- [ ] DB persistence OK
- [ ] Reorg protection OK
- [ ] Documentation complète

### 9.2 Critères de Succès

1. ✅ StateCommitment créé et vérifié
2. ✅ LLMQ signatures validées
3. ✅ Reorg > 12 blocs bloqué
4. ✅ State divergence détectée
5. ✅ Tests 100% PASS

---

## 10. NEXT STEPS

**Phase 4:** TBD
**Phase 5:** Daily Yield (Cr/Ur mutations)
**Phase 6+:** STAKE/UNSTAKE, ZKHU, HTLC, DOMC

---

**FIN PLAN PHASE 3**

**Status:** 📋 **PLANNING COMPLETE**
**Next:** 🚀 **IMPLEMENTATION**
