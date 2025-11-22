# 07 — ZKHU STAKE/UNSTAKE BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit l'architecture **ZKHU** (staking anonyme via Sapling) et les opérations **STAKE** et **UNSTAKE**.

**ZKHU est strictement limité au STAKING. Pas de privacy spending, pas de Z→Z transactions.**

---

## 1. RÈGLES FONDAMENTALES ZKHU

### 1.1 ZKHU ≠ zPIV (INTERDICTION ZEROCOIN)

```
❌ ZKHU ne doit JAMAIS utiliser quoi que ce soit de Zerocoin ou zPIV
❌ ZKHU ne doit JAMAIS mentionner, imiter, ou s'inspirer de Zerocoin
❌ Zerocoin est MORT et ne doit JAMAIS contaminer KHU
```

### 1.2 ZKHU = Staking Only (Pas Z→Z)

```
✅ ZKHU est pour STAKING uniquement
❌ Pas de privacy spending
❌ Pas de transactions Z→Z (ZKHU → ZKHU)
❌ Pas de transactions ZKHU → Shield
❌ Pas de transactions Shield → ZKHU
❌ Pas de join-split
❌ Pas de pool mixing avec Shield
```

### 1.3 Pipeline Strict KHU_T ↔ ZKHU

```
Pipeline autorisé:
  PIV → KHU_T → ZKHU → KHU_T → PIV

Opérations autorisées:
  MINT:    PIV    → KHU_T  (Phase 2)
  STAKE:   KHU_T  → ZKHU   (Phase 4 — ce blueprint)
  UNSTAKE: ZKHU   → KHU_T  (Phase 4 — ce blueprint)
  REDEEM:  KHU_T  → PIV    (Phase 2)

INTERDIT:
  ❌ ZKHU → ZKHU (pas de Z→Z)
  ❌ ZKHU → Shield
  ❌ Shield → ZKHU
  ❌ ZKHU → PIV direct
```

---

## 2. ARCHITECTURE SAPLING

### 2.1 Cryptographie Sapling PARTAGÉE

**ZKHU utilise les primitives cryptographiques Sapling STANDARD de PIVX:**

```
✅ Circuits zk-SNARK Sapling (AUCUNE modification)
✅ Format OutputDescription (512-byte memo standard)
✅ Algorithmes de commitment/nullifier (standard Sapling)
✅ Format de proof zk-SNARK (standard Sapling)
```

**Raison:** Réutilisation de la cryptographie éprouvée de PIVX Shield. Pas de nouveau circuit, pas de fork Sapling.

### 2.2 Stockage LevelDB SÉPARÉ

**ZKHU maintient ses propres structures de stockage, DISTINCTES de Shield:**

```cpp
// ZKHU (namespace 'K' — OBLIGATOIRE)
'K' + 'A' + anchor_khu     → SaplingMerkleTree zkhuTree
'K' + 'N' + nullifier_khu  → bool (spent flag)
'K' + 'T' + note_id        → ZKHUNoteData

// Shield (PIVX Sapling public — namespace 'S'/'s')
'S' + anchor      → SaplingMerkleTree saplingTree
's' + nullifier   → bool (spent flag)
```

**⚠️ CRITICAL:**
- zkhuTree ≠ saplingTree (merkle trees DISTINCTS)
- zkhuNullifierSet ≠ nullifierSet (nullifier sets DISTINCTS)
- Anonymity sets SÉPARÉS (pas de pool mixing)

### 2.3 Structures de Données

**Fichier:** `src/khu/khu_sapling.h`

```cpp
// ZKHU note data (stored in LevelDB)
struct ZKHUNoteData {
    uint256 nullifier;              // Unique nullifier
    int64_t amount;                 // Stake amount (principal)
    uint32_t stakeStartHeight;      // Stake start height
    int64_t Ur_accumulated;         // Accumulated yield
    libzcash::SaplingPaymentAddress address;  // Owner address

    SERIALIZE_METHODS(ZKHUNoteData, obj) {
        READWRITE(obj.nullifier, obj.amount, obj.stakeStartHeight);
        READWRITE(obj.Ur_accumulated, obj.address);
    }
};

// ZKHU Merkle Tree (separate from Shield)
class CKHUSaplingTree {
    SaplingMerkleTree zkhuTree;  // ⚠️ DISTINCT from saplingTree

    bool AppendCommitment(const uint256& commitment);
    uint256 GetRoot() const;
    bool GetWitness(const uint256& commitment, SaplingWitness& witness) const;
};
```

### 2.4 Memo Format (512 bytes)

**ZKHU encode metadata spécifique dans memo Sapling standard:**

```cpp
std::array<unsigned char, 512> memo;
memo.fill(0);

// Layout canonique:
memcpy(memo.data(), "ZKHU", 4);               // Magic (bytes 0-3)
WriteLE32(memo.data() + 4, 1);                // Version (bytes 4-7)
WriteLE32(memo.data() + 8, stakeStartHeight);  // Height (bytes 8-11)
WriteLE64(memo.data() + 12, amount);          // Amount (bytes 12-19)
WriteLE64(memo.data() + 20, Ur_accumulated);  // Yield (bytes 20-27)
// Bytes 28-511: reserved (zeros)
```

**Usage:**
- Permet de distinguer ZKHU de Shield (magic "ZKHU")
- Stocke metadata nécessaire pour UNSTAKE (height, amount, Ur)
- Compatible avec format Sapling standard (512 bytes)

---

## 3. OPÉRATION STAKE (KHU_T → ZKHU)

### 3.1 Transaction STAKE

**TxType:** `TxType::KHU_STAKE`

**Flux:**
1. Utilisateur possède KHU_T (UTXO transparent coloré)
2. Transaction STAKE consomme KHU_T input
3. Transaction STAKE crée ZKHU output (Sapling shielded)
4. Commitment ajouté à zkhuTree (PAS saplingTree)
5. KHU_T UTXO marqué comme "staked"

### 3.2 Validation STAKE

**Fichier:** `src/khu/khu_stake.cpp`

```cpp
bool CheckKHUStake(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Verify TxType
    if (tx.nType != TxType::KHU_STAKE)
        return state.Invalid("khu-stake-invalid-type");

    // 2. Verify KHU_T input exists
    CKHUUTXO khuInput;
    if (!view.GetKHUCoin(tx.vin[0].prevout, khuInput))
        return state.Invalid("khu-stake-missing-input");

    // 3. Verify not already staked
    if (khuInput.fStaked)
        return state.Invalid("khu-stake-already-staked");

    // 4. Verify Sapling shielded output
    if (tx.sapData->vShieldedOutput.size() != 1)
        return state.Invalid("khu-stake-invalid-output-count");

    const OutputDescription& output = tx.sapData->vShieldedOutput[0];

    // 5. Verify amount matches
    if (output.value != khuInput.amount)
        return state.Invalid("khu-stake-amount-mismatch");

    // 6. Verify memo format
    if (!IsValidZKHUMemo(output.memo))
        return state.Invalid("khu-stake-invalid-memo");

    return true;
}
```

### 3.3 Application STAKE

**Fichier:** `src/khu/khu_stake.cpp`

```cpp
bool ApplyKHUStake(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view) {
    // ⚠️ LOCK: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract ZKHU output
    const OutputDescription& output = tx.sapData->vShieldedOutput[0];
    uint256 commitment = output.cmu;

    // 2. Add commitment to ZKHU tree (NOT Shield tree)
    SaplingMerkleTree zkhuTree;
    if (!view.GetKHUAnchor(zkhuTree))
        return false;

    zkhuTree.append(commitment);
    view.PushKHUAnchor(zkhuTree);  // Writes to 'K' + 'A' + anchor

    // 3. Store ZKHU note data
    ZKHUNoteData noteData;
    noteData.nullifier = /* derived from spend authority */;
    noteData.amount = output.value;
    noteData.stakeStartHeight = chainActive.Height();
    noteData.Ur_accumulated = 0;  // Initial yield = 0
    noteData.address = /* from output */;

    view.WriteKHUNoteData(noteData.nullifier, noteData);  // 'K' + 'N' + nullifier

    // 4. Mark KHU_T UTXO as staked
    CKHUUTXO khuUtxo;
    view.GetKHUCoin(tx.vin[0].prevout, khuUtxo);
    khuUtxo.fStaked = true;
    khuUtxo.nStakeStartHeight = chainActive.Height();
    view.SpendKHUCoin(tx.vin[0].prevout);  // Remove from UTXO set

    // ⚠️ IMPORTANT: STAKE does NOT modify C/U/Cr/Ur
    // Only state transition: KHU_T → ZKHU (internal pipeline)

    return true;
}
```

### 3.4 Impact sur État Global

**STAKE ne modifie PAS KhuGlobalState (C, U, Cr, Ur):**

```cpp
// AVANT STAKE:
C = 1000, U = 1000, Cr = 50, Ur = 50

// APRÈS STAKE de 100 KHU_T:
C = 1000, U = 1000, Cr = 50, Ur = 50  // INCHANGÉ

// Explication:
// STAKE est une transformation interne KHU_T → ZKHU
// Supply total KHU (U) ne change pas
// Collateral total (C) ne change pas
// Seulement la FORME change (transparent → shielded)
```

---

## 4. OPÉRATION UNSTAKE (ZKHU → KHU_T)

### 4.1 Transaction UNSTAKE

**TxType:** `TxType::KHU_UNSTAKE`

**Flux:**
1. Utilisateur possède ZKHU note (shielded)
2. Transaction UNSTAKE consomme ZKHU input (reveal nullifier)
3. Transaction UNSTAKE crée KHU_T output (principal + bonus)
4. Nullifier ajouté à zkhuNullifierSet (PAS nullifierSet)
5. Double flux: Cr-=B, Ur-=B, U+=B, C+=B (Phase future)

### 4.2 Validation UNSTAKE

**Fichier:** `src/khu/khu_unstake.cpp`

```cpp
bool CheckKHUUnstake(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, uint32_t nHeight) {
    // 1. Verify TxType
    if (tx.nType != TxType::KHU_UNSTAKE)
        return state.Invalid("khu-unstake-invalid-type");

    // 2. Verify Sapling spend
    if (tx.sapData->vShieldedSpend.size() != 1)
        return state.Invalid("khu-unstake-invalid-spend-count");

    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // 3. Verify nullifier not already spent (in ZKHU set, NOT Shield set)
    if (view.GetKHUNullifier(spend.nullifier))  // ⚠️ 'K' + 'N', NOT 's'
        return state.Invalid("khu-unstake-double-spend");

    // 4. Verify anchor matches ZKHU tree (NOT Shield tree)
    SaplingMerkleTree zkhuTree;
    if (!view.GetKHUAnchorAt(spend.anchor, zkhuTree))  // ⚠️ 'K' + 'A', NOT 'S'
        return state.Invalid("khu-unstake-invalid-anchor");

    // 5. Load ZKHU note data
    ZKHUNoteData noteData;
    if (!view.GetKHUNoteData(spend.nullifier, noteData))
        return state.Invalid("khu-unstake-note-not-found");

    // 6. Verify maturity (4320 blocks minimum)
    uint32_t stakeAge = nHeight - noteData.stakeStartHeight;
    if (stakeAge < KHU_MATURITY_BLOCKS)
        return state.Invalid("khu-unstake-immature",
                           strprintf("Age %d < %d", stakeAge, KHU_MATURITY_BLOCKS));

    // 7. Verify output amount (principal + bonus)
    int64_t expectedAmount = noteData.amount + noteData.Ur_accumulated;
    if (GetKHUOutputAmount(tx) != expectedAmount)
        return state.Invalid("khu-unstake-amount-mismatch");

    return true;
}
```

### 4.3 Application UNSTAKE (Phase Future)

**⚠️ PHASE 4 — Double Flux Atomique:**

```cpp
bool ApplyKHUUnstake(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight) {
    // ⚠️ LOCK: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract spend
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // 2. Load ZKHU note data
    ZKHUNoteData noteData;
    view.GetKHUNoteData(spend.nullifier, noteData);

    int64_t principal = noteData.amount;
    int64_t bonus = noteData.Ur_accumulated;

    // 3. Mark nullifier as spent in ZKHU set (NOT Shield set)
    view.SetKHUNullifier(spend.nullifier);  // 'K' + 'N' + nullifier

    // 4. DOUBLE FLUX ATOMIQUE (4-way mutation)
    state.U  += bonus;   // (1) Create bonus KHU_T
    state.C  += bonus;   // (2) Increase collateral
    state.Cr -= bonus;   // (3) Consume reward pool
    state.Ur -= bonus;   // (4) Consume reward rights

    // 5. Create KHU_T UTXO (principal + bonus)
    CKHUUTXO newCoin;
    newCoin.amount = principal + bonus;
    newCoin.scriptPubKey = tx.vout[0].scriptPubKey;  // ⚠️ NEW address (privacy)
    newCoin.nHeight = nHeight;
    newCoin.fStaked = false;
    newCoin.nStakeStartHeight = 0;

    view.AddKHUCoin(tx.GetHash(), 0, newCoin);

    // 6. Verify invariants
    if (!state.CheckInvariants())
        return error("UNSTAKE: invariant violation");

    return true;
}
```

### 4.4 Impact sur État Global (Phase Future)

```cpp
// AVANT UNSTAKE:
C = 1000, U = 1000, Cr = 50, Ur = 50
// Note ZKHU: principal=100, Ur_accumulated=5

// APRÈS UNSTAKE:
C = 1005, U = 1005, Cr = 45, Ur = 45

// Explication:
// Bonus B = 5 (Ur_accumulated)
// U += 5 (bonus KHU_T créé)
// C += 5 (collateral augmenté)
// Cr -= 5 (pool reward consommé)
// Ur -= 5 (reward rights consommés)
// Invariants: C==U (1005==1005), Cr==Ur (45==45) ✅
```

---

## 5. INTERDICTIONS ABSOLUES

### 5.1 Code Interdit

```cpp
// ❌ JAMAIS utiliser clés Shield pour ZKHU
view.PushSaplingAnchor(commitment);  // ❌ INTERDIT (écrit 'S' + anchor)
view.GetNullifier(nullifier, SAPLING);  // ❌ INTERDIT (lit 's' + nullifier)

// ✅ CORRECT: Utiliser API ZKHU
view.PushKHUAnchor(zkhuTree);  // ✅ Écrit 'K' + 'A' + anchor
view.GetKHUNullifier(nullifier);  // ✅ Lit 'K' + 'N' + nullifier
```

```cpp
// ❌ JAMAIS partager trees/nullifier sets
saplingTree.append(zkhu_commitment);  // ❌ INTERDIT (pollution pool)
nullifierSet.insert(zkhu_nullifier);  // ❌ INTERDIT (mixing ZKHU/Shield)

// ✅ CORRECT: Utiliser structures ZKHU séparées
zkhuTree.append(zkhu_commitment);  // ✅ Tree ZKHU distinct
zkhuNullifierSet.insert(zkhu_nullifier);  // ✅ Set ZKHU distinct
```

### 5.2 Transactions Interdites

```
❌ ZKHU → ZKHU (Z→Z transaction)
❌ ZKHU → Shield (conversion ZKHU vers privacy)
❌ Shield → ZKHU (conversion privacy vers staking)
❌ ZKHU → PIV direct (skip pipeline)
❌ Join-split avec ZKHU
❌ Pool mixing ZKHU/Shield
```

### 5.3 Architecture Interdite

```
❌ Modifier circuits zk-SNARK Sapling
❌ Créer nouveau circuit pour ZKHU
❌ Fork Sapling protocol
❌ Réutiliser code Zerocoin/zPIV
❌ Anonymity set partagé ZKHU/Shield
❌ Compounding yield dans ZKHU
```

---

## 6. CHECKLIST IMPLÉMENTATION PHASE 4

### 6.1 Structures de Données

- [ ] `ZKHUNoteData` structure définie (src/khu/khu_sapling.h)
- [ ] `CKHUSaplingTree` class définie (separate from Shield)
- [ ] LevelDB keys avec préfixe 'K' implémentées
- [ ] AUCUNE clé Shield ('S', 's') utilisée pour ZKHU

### 6.2 API CCoinsViewCache

- [ ] `view.GetKHUAnchor()` implémentée
- [ ] `view.PushKHUAnchor()` implémentée
- [ ] `view.GetKHUNullifier()` implémentée
- [ ] `view.SetKHUNullifier()` implémentée
- [ ] `view.GetKHUNoteData()` implémentée
- [ ] `view.WriteKHUNoteData()` implémentée

### 6.3 Validation

- [ ] `CheckKHUStake()` implémentée
- [ ] `CheckKHUUnstake()` implémentée
- [ ] Maturity 4320 blocs enforced
- [ ] Memo format ZKHU validé
- [ ] TxType KHU_STAKE/KHU_UNSTAKE vérifiés

### 6.4 Tests

- [ ] test_khu_sapling.cpp (unit tests)
  - [ ] ZKHU tree distinct de Shield tree
  - [ ] ZKHU nullifiers distincts de Shield nullifiers
  - [ ] Memo format ZKHU
  - [ ] STAKE ne modifie pas C/U/Cr/Ur
  - [ ] UNSTAKE double flux atomique

- [ ] khu_sapling.py (functional tests)
  - [ ] STAKE KHU_T → ZKHU
  - [ ] UNSTAKE ZKHU → KHU_T (après maturity)
  - [ ] UNSTAKE avant maturity (doit échouer)
  - [ ] Isolation ZKHU/Shield (pas de Z→Z)

### 6.5 Vérifications Finales

```bash
# Vérifier présence de structures ZKHU
grep -r "zkhuTree" src/khu/          # DOIT avoir occurrences
grep -r "zkhuNullifierSet" src/khu/  # DOIT avoir occurrences
grep -r "'K' + 'A'" src/khu/         # DOIT avoir occurrences
grep -r "'K' + 'N'" src/khu/         # DOIT avoir occurrences

# Vérifier absence de contamination Shield
grep -r "'S' + anchor" src/khu/      # DOIT être vide
grep -r "'s' + nullifier" src/khu/   # DOIT être vide
grep -r "PushSaplingAnchor.*KHU" src/  # DOIT être vide
grep -r "GetNullifier.*SAPLING.*khu" src/  # DOIT être vide
```

---

## 7. RÉFÉRENCES

**Documents liés:**
- `01-blueprint-master-flow.md` — Section 1.3.7 (règles ZKHU canoniques)
- `02-canonical-specification.md` — Sections 3.x (STAKE/UNSTAKE spec)
- `03-architecture-overview.md` — Section 7 (Sapling integration)
- `06-protocol-reference.md` — Section 18 (code C++ ZKHU)

**Sous-blueprints futurs:**
- `08-ZKHU-YIELD-ACCUMULATION.md` (Phase 3 - calcul Ur)
- `09-ZKHU-MATURITY-ENFORCEMENT.md` (Phase 4 - 4320 blocs)

---

## 8. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

Toute modification doit:
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date

**Statut:** ACTIF pour implémentation Phase 4 (après Phase 1-3 validées)

---

**FIN DU BLUEPRINT ZKHU STAKE/UNSTAKE**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
