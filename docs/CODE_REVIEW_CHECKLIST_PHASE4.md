# CODE REVIEW CHECKLIST — PHASE 4 (ZKHU SAPLING STAKING)

**Version:** 1.0
**Date:** 2025-11-23
**Status:** CANONIQUE
**Phases couvertes:** Phase 4 (STAKE/UNSTAKE)

---

## 🎯 OBJECTIF

Cette checklist garantit que l'implémentation Phase 4 respecte strictement :
- Les spécifications canoniques (02, 03, 06)
- Le blueprint ZKHU (05-ZKHU-SAPLING-STAKE.md)
- Les corrections critiques (double flux, Ur_accumulated per-note, Phase 4 B=0)

**Aucune PR Phase 4 ne doit être mergée sans validation complète de cette checklist.**

---

## ✅ SECTION 1 : STRUCTURES DE DONNÉES

### 1.1 ZKHUNoteData (CRITIQUE)

- [ ] **Structure contient exactement 5 champs :**
  ```cpp
  int64_t amount;
  uint32_t nStakeStartHeight;
  int64_t Ur_accumulated;  // ✅ PAS Ur_at_stake !
  uint256 nullifier;
  uint256 cm;
  ```

- [ ] **SERIALIZE_METHODS inclut Ur_accumulated** (pas Ur_at_stake)

- [ ] **Ur_accumulated initialisé à 0 en Phase 4** (STAKE operation)

- [ ] **Commentaire explicite :** "Phase 4: Ur_accumulated=0, Phase 5: incremented by yield engine"

### 1.2 ZKHUMemo (512 bytes)

- [ ] **Magic = "ZKHU"** (4 bytes, offset 0)

- [ ] **Version = 1** (1 byte, offset 4)

- [ ] **nStakeStartHeight** (4 bytes LE, offset 5)

- [ ] **amount** (8 bytes LE, offset 9)

- [ ] **Ur_accumulated** (8 bytes LE, offset 17) — ✅ PAS Ur_at_stake !

- [ ] **Padding = 487 bytes zeros** (offset 25)

- [ ] **Serialization/Deserialization fonctionnelle et testée**

---

## ✅ SECTION 2 : OPÉRATION STAKE (T→Z)

### 2.1 Validation (CheckKHUStake)

- [ ] **TxType == KHU_STAKE** vérifié

- [ ] **Input KHU_T UTXO exists** vérifié

- [ ] **Input amount > 0 && <= MAX_MONEY** vérifié

- [ ] **Input KHU_T not already staked** (fStaked == false) vérifié

- [ ] **Sapling commitment valid** vérifié

- [ ] **Ephemeral key valid** vérifié

- [ ] **Memo magic == "ZKHU"** vérifié

- [ ] **Height >= V6_ACTIVATION_HEIGHT** vérifié

### 2.2 Application (ApplyKHUStake)

- [ ] **Consume input KHU_T UTXO** (SpendKHUCoin)

- [ ] **Create ZKHUNoteData avec Ur_accumulated = 0** (Phase 4)

- [ ] **Store note in DB** (namespace 'K')

- [ ] **Append commitment to zkhuTree**

- [ ] **✅ CRITICAL: NO state change** (C, U, Cr, Ur unchanged)

- [ ] **Return true on success**

### 2.3 Reorg (UndoKHUStake)

- [ ] **Restore input KHU_T UTXO** (AddKHUCoin)

- [ ] **Remove ZKHU note from DB**

- [ ] **Remove commitment from zkhuTree** (if applicable)

- [ ] **State unchanged** (C, U, Cr, Ur same as before)

---

## ✅ SECTION 3 : OPÉRATION UNSTAKE (Z→T) — CRITIQUE

### 3.1 Validation (CheckKHUUnstake)

- [ ] **TxType == KHU_UNSTAKE** vérifié

- [ ] **Nullifier valid && not spent** vérifié

- [ ] **Anchor exists in zkhuTree** vérifié

- [ ] **zk-SNARK proof valid** (Sapling verification) vérifié

- [ ] **Memo magic == "ZKHU"** vérifié

- [ ] **⚠️ MATURITY: (nHeight - nStakeStartHeight) >= 4320** vérifié

- [ ] **Bonus = note.Ur_accumulated >= 0** vérifié

- [ ] **Cr >= bonus** (sufficient pool) vérifié

- [ ] **Ur >= bonus** (sufficient pool) vérifié

- [ ] **Output amount == amount + bonus** vérifié

- [ ] **SafeAdd checks** (no overflow C, U) vérifié

### 3.2 Application (ApplyKHUUnstake) — DOUBLE FLUX

- [ ] **Extract bonus from note:** `CAmount bonus = noteData.Ur_accumulated;`

- [ ] **✅ CRITICAL: Phase 4 bonus = 0** (vérifier que Ur_accumulated est bien 0)

- [ ] **✅ DOUBLE FLUX appliqué dans l'ordre :**
  ```cpp
  state.U += bonus;   // Supply increases
  state.C += bonus;   // Collateral increases
  state.Cr -= bonus;  // Pool decreases
  state.Ur -= bonus;  // Rights decrease
  ```

- [ ] **SafeAdd utilisé pour C et U** (pas d'overflow)

- [ ] **Create output KHU_T UTXO** (amount + bonus)

- [ ] **Mark nullifier as spent**

- [ ] **✅ CRITICAL: Verify invariants AFTER mutations:**
  ```cpp
  if (!state.CheckInvariants()) {
      return error("Invariant violation");
  }
  ```

- [ ] **Commentaire explicite :** "Phase 4: bonus=0 → net effect zero, but structure ready for Phase 5"

### 3.3 Reorg (UndoKHUUnstake) — SYMÉTRIE CRITIQUE

- [ ] **Extract same bonus:** `CAmount bonus = noteData.Ur_accumulated;`

- [ ] **✅ REVERSE double flux (même ordre inverse) :**
  ```cpp
  state.U -= bonus;   // Supply decreases
  state.C -= bonus;   // Collateral decreases
  state.Cr += bonus;  // Pool increases
  state.Ur += bonus;  // Rights increase
  ```

- [ ] **SafeAdd utilisé pour Cr et Ur** (pas d'overflow sur restoration)

- [ ] **Remove output KHU_T UTXO** (SpendKHUCoin)

- [ ] **Unspend nullifier** (EraseZKHUNullifier)

- [ ] **✅ CRITICAL: Verify invariants AFTER undo:**
  ```cpp
  if (!state.CheckInvariants()) {
      return error("Invariant violation after undo");
  }
  ```

- [ ] **État exactement restauré** (C, U, Cr, Ur identiques à avant ApplyKHUUnstake)

---

## ✅ SECTION 4 : DATABASE (Namespace 'K')

### 4.1 Clés LevelDB

- [ ] **ZKHU Anchor:** `'K' + 'A' + anchor → SaplingMerkleTree`

- [ ] **ZKHU Nullifier:** `'K' + 'N' + nullifier → bool (spent)`

- [ ] **ZKHU Note:** `'K' + 'T' + note_id → ZKHUNoteData`

- [ ] **Best anchor:** `'K' + 'B' → uint256` (optionnel Phase 4)

### 4.2 Isolation Shield vs ZKHU

- [ ] **Shield uses:** `'S'/'s'` (existing)

- [ ] **ZKHU uses:** `'K'` (nouveau)

- [ ] **Aucun chevauchement de clés**

- [ ] **Aucun mélange de données Shield et ZKHU**

### 4.3 Opérations DB

- [ ] **WriteZKHUNote / ReadZKHUNote** fonctionnelles

- [ ] **WriteZKHUNullifier / IsZKHUNullifierSpent** fonctionnelles

- [ ] **WriteZKHUAnchor / ReadZKHUAnchor** fonctionnelles

- [ ] **BatchWriteZKHU** (optionnel Phase 4, mais préparer)

---

## ✅ SECTION 5 : INTÉGRATION CONSENSUS

### 5.1 ProcessKHUBlock (ordre CRITIQUE)

- [ ] **Ordre d'exécution respecté :**
  ```cpp
  1. ApplyDailyYieldIfNeeded()   // no-op Phase 4
  2. ProcessKHUTransactions()    // MINT/REDEEM
  3. ProcessKHUStake()           // KHU_STAKE
  4. ProcessKHUUnstake()         // KHU_UNSTAKE
  5. ApplyBlockReward()
  6. CheckInvariants()           // ✅ CRITICAL
  7. PersistState()
  ```

- [ ] **ApplyDailyYield est no-op en Phase 4** (ou absent, flag désactivé)

- [ ] **Invariants vérifiés APRÈS toutes mutations**

- [ ] **Pas de return early avant CheckInvariants**

### 5.2 CheckTransaction

- [ ] **TxType KHU_STAKE détecté → CheckKHUStake() appelé**

- [ ] **TxType KHU_UNSTAKE détecté → CheckKHUUnstake() appelé**

- [ ] **Validation échoue correctement** (CValidationState set)

### 5.3 DisconnectBlock (Reorg)

- [ ] **UndoKHUStake appelé pour chaque KHU_STAKE**

- [ ] **UndoKHUUnstake appelé pour chaque KHU_UNSTAKE**

- [ ] **Ordre inverse de ConnectBlock**

- [ ] **État restauré exactement**

---

## ✅ SECTION 6 : TESTS UNITAIRES (7 tests minimum)

### 6.1 Test STAKE

- [ ] **test_stake_basic** : Créer note, vérifier state unchanged

- [ ] **test_stake_memo_format** : Vérifier memo serialization

- [ ] **test_reorg_stake** : UndoKHUStake restore exactement

### 6.2 Test UNSTAKE

- [ ] **test_unstake_basic** : B=0 Phase 4, double flux structure correcte

- [ ] **test_unstake_maturity** : Reject si < 4320 blocs

- [ ] **test_unstake_with_bonus_phase5_ready** : Simuler B>0, vérifier double flux actif

- [ ] **test_reorg_unstake** : UndoKHUUnstake restore exactement

### 6.3 Test Isolation

- [ ] **test_pool_separation** : ZKHU vs Shield namespaces séparés

- [ ] **test_multiple_unstake_isolation** : Per-note bonus indépendants

### 6.4 Test Invariants

- [ ] **test_invariants_after_unstake** : C==U, Cr==Ur après chaque UNSTAKE

- [ ] **Tous les tests PASS** (100%)

---

## ✅ SECTION 7 : ANTI-DRIFT (INTERDICTIONS)

### 7.1 Interdictions Absolues

- [ ] **❌ JAMAIS:** UNSTAKE sans modifier C/U (FAUX si présent)

- [ ] **❌ JAMAIS:** Bonus = Ur_now - Ur_at_stake (FAUX si présent)

- [ ] **❌ JAMAIS:** Ur_at_stake dans structures (FAUX si présent)

- [ ] **❌ JAMAIS:** Tests "C/U unchanged" sans contexte B=0 (ambigu si présent)

- [ ] **❌ JAMAIS:** Mélanger Shield et ZKHU (namespaces différents)

- [ ] **❌ JAMAIS:** UNSTAKE avant 4320 blocs maturity

- [ ] **❌ JAMAIS:** Z→Z transfers (pas de ZKHU → ZKHU)

- [ ] **❌ JAMAIS:** Join-split sur ZKHU

### 7.2 Obligations Strictes

- [ ] **✅ TOUJOURS:** UNSTAKE applique double flux (C+=B, U+=B, Cr-=B, Ur-=B)

- [ ] **✅ TOUJOURS:** Bonus = note.Ur_accumulated (per-note)

- [ ] **✅ TOUJOURS:** ZKHUNoteData.Ur_accumulated (pas Ur_at_stake)

- [ ] **✅ TOUJOURS:** Phase 4 B=0, mais code prêt pour B>0

- [ ] **✅ TOUJOURS:** Vérifier invariants APRÈS chaque mutation

- [ ] **✅ TOUJOURS:** Namespace 'K' pour ZKHU (jamais 'S')

- [ ] **✅ TOUJOURS:** Maturity 4320 blocs enforced

---

## ✅ SECTION 8 : COMPILATION & PERFORMANCE

### 8.1 Compilation

- [ ] **make -j$(nproc)** exit code 0

- [ ] **0 errors**

- [ ] **0 warnings KHU-related**

- [ ] **Binaries créés:** pivxd, pivx-cli, test_pivx

### 8.2 Tests Runtime

- [ ] **./test_pivx --run_test=khu_phase4_tests** → 100% PASS

- [ ] **Temps d'exécution raisonnable** (< 5 secondes pour 7 tests)

- [ ] **Valgrind clean** (0 memory leaks)

### 8.3 Makefile

- [ ] **khu_stake.h/cpp ajoutés** à libbitcoin_common_a_SOURCES

- [ ] **khu_unstake.h/cpp ajoutés** à libbitcoin_common_a_SOURCES

- [ ] **zkhu_db.h/cpp ajoutés** à libbitcoin_common_a_SOURCES

- [ ] **khu_phase4_tests.cpp ajouté** (si fichier test séparé)

---

## ✅ SECTION 9 : DOCUMENTATION

### 9.1 Code Comments

- [ ] **Chaque fonction publique a un comment explicatif**

- [ ] **Sections critiques (double flux, maturity) ont des comments WARNING**

- [ ] **Phase 4 vs Phase 5 explicité dans comments**

### 9.2 Rapport Phase 4

- [ ] **RAPPORT_PHASE4_IMPL_CPP.md créé**

- [ ] **Contient:** Executive summary, files created, test results, security analysis

- [ ] **Contient:** Section "Double flux UNSTAKE" avec explication B=0 Phase 4

- [ ] **Contient:** Section "Ur_accumulated per-note" avec justification

---

## ✅ SECTION 10 : REVUE FINALE (SIGN-OFF)

### 10.1 Validation Technique

- [ ] **Architecte C++ Senior:** APPROUVÉ ✅

- [ ] **Lead Developer:** APPROUVÉ ✅

- [ ] **Security Reviewer:** APPROUVÉ ✅

### 10.2 Validation Canonique

- [ ] **Conforme à 02-canonical-specification.md** ✅

- [ ] **Conforme à 03-architecture-overview.md** ✅

- [ ] **Conforme à 05-ZKHU-SAPLING-STAKE.md** ✅

- [ ] **Conforme à 06-protocol-reference.md** ✅

### 10.3 Ready for Merge

- [ ] **Toutes les sections de cette checklist validées** ✅

- [ ] **Aucune exception / waiver non documentée** ✅

- [ ] **Branch à jour avec main** ✅

- [ ] **CI/CD pipeline PASS** ✅

---

## 📋 UTILISATION DE CETTE CHECKLIST

### Pour le Développeur

1. Cocher chaque item au fur et à mesure de l'implémentation
2. Auto-review avant de soumettre PR
3. Corriger tout item non coché AVANT PR

### Pour le Reviewer

1. Vérifier CHAQUE item de la checklist
2. Ne pas approuver si un item critique est non coché
3. Demander corrections explicites pour items manquants

### Items Critiques (BLOQUANTS)

Ces items **DOIVENT** être cochés pour merge :

- ✅ Section 1.1 : ZKHUNoteData avec Ur_accumulated
- ✅ Section 3.2 : ApplyKHUUnstake double flux
- ✅ Section 3.3 : UndoKHUUnstake symétrie
- ✅ Section 5.1 : Ordre ProcessKHUBlock + invariants
- ✅ Section 6 : Tous tests PASS
- ✅ Section 7 : Aucune interdiction violée

---

**VERSION:** 1.0
**STATUS:** CANONIQUE
**MAINTENANCE:** Mettre à jour cette checklist si blueprints changent

**FIN CODE REVIEW CHECKLIST PHASE 4**
