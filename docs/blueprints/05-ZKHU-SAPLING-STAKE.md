# 05 — ZKHU SAPLING STAKE/UNSTAKE

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Décrire précisément l'utilisation minimale de Sapling pour :
- Convertir KHU_T → ZKHU (STAKE)
- Conserver un stake privé
- Récupérer KHU_T avec yield Y (UNSTAKE)

**RÈGLE FONDAMENTALE:** ZKHU utilise la cryptographie Sapling existante de PIVX (Shield) avec un namespace LevelDB séparé.

---

## ⚠️ PHASE 4 vs PHASE 5 : YIELD

**PHASE 4 (Staking sans yield) :**
- R% = 0 (pas de yield actif)
- Ur_accumulated = 0 pour toutes les notes
- Y = 0 pour tous les UNSTAKE
- **Effet net : C/U numériquement inchangés** (mais code applique déjà double flux)
- **Structure code déjà prête pour Phase 5**

**PHASE 5+ (Yield R% actif) :**
- R% > 0 (voté par DOMC)
- Ur_accumulated croît quotidiennement (1440 blocs)
- Y > 0 pour UNSTAKE après maturity (4320 blocs = 3 jours)
- **Effet net : C augmente de Y, U augmente de P+Y** (double flux actif)

**MATURITY CRITIQUE:** AUCUN yield avant 4320 blocs (3 jours)

**Ce blueprint décrit la structure GÉNÉRALE (Y>=0), Phase 4 est le cas particulier Y=0.**

---

## 1. PRIMITIVES SAPLING UTILISÉES

ZKHU réutilise les primitives Sapling **SANS MODIFICATION** :

```cpp
✅ Note Sapling standard (OutputDescription)
✅ Nullifier standard (SHA256 commitments)
✅ Witness path standard (merkle tree authentification)
✅ Merkle tree standard (append-only)
✅ Memo 512 bytes standard
✅ Circuits zk-SNARK Sapling (inchangés)
✅ Format proof standard
```

**Aucune modification cryptographique. Réutilisation totale.**

---

## 2. ESTIMATION COMPLEXITÉ IMPLÉMENTATION

**COMPLEXITÉ: FAIBLE-MOYENNE** (8 jours-homme)

ZKHU réutilise INTÉGRALEMENT le code Sapling existant de PIVX. L'implémentation consiste principalement à **appeler les fonctions Sapling existantes** avec un namespace LevelDB différent.

### 2.1 Breakdown Effort

| Tâche | Jours | Raison |
|-------|-------|--------|
| STAKE (T→Z) | 3 | Appel CreateSaplingNote() existant + namespace 'K' |
| UNSTAKE (Z→T) | 3 | Appel ValidateSaplingSpend() existant + yield calc |
| Namespace 'K' setup | 1 | Clés LevelDB 'K'+'A/N/T' (simple) |
| Tests maturity + yield | 1 | Tests unitaires/fonctionnels |
| **TOTAL** | **8 jours** | Pas de cryptographie nouvelle |

### 2.2 Code Existant Réutilisé (PIVX Shield)

```cpp
✅ libsapling/sapling_core.cpp        // Circuits zk-SNARK (inchangés)
✅ sapling/saplingscriptpubkeyman.cpp // Note creation (réutilisé)
✅ sapling/transaction_builder.cpp    // Proof generation (réutilisé)
✅ validation.cpp (Sapling validation) // Spend validation (réutilisé)
```

**Nouveaux fichiers ZKHU (~1500 lignes total):**

```cpp
src/khu/khu_stake.h/cpp      ~600 lignes  // Wrapper STAKE T→Z
src/khu/khu_unstake.h/cpp    ~500 lignes  // Wrapper UNSTAKE Z→T
src/khu/zkhu_db.h/cpp        ~400 lignes  // Namespace 'K' LevelDB
```

### 2.3 Pas de Nouveau Circuit

```
❌ Aucune modification cryptographique
❌ Aucun nouveau circuit zk-SNARK
❌ Aucune preuve à recalculer
✅ Appel SaplingNote::Create() existant
✅ Appel SaplingSpendDescription::Verify() existant
✅ Juste namespace 'K' différent de 'S'
```

**Conclusion:** ZKHU n'est PAS une implémentation cryptographique complexe. C'est un **wrapper léger** autour de Sapling PIVX avec isolation namespace.

---

## 3. INTERDICTIONS

```
❌ Aucun Z→Z (pas de transfert ZKHU → ZKHU)
❌ Aucun join-split
❌ Aucun shielding de paiement
❌ Aucune référence Zerocoin/zPIV (MORT — ne jamais mentionner)
❌ Aucune utilisation des clés Shield ('S', 's')
```

**ZKHU = Staking privé uniquement. Pas de mécanisme de privacy général.**

---

## 3. STOCKAGE LEVELDB

ZKHU utilise un namespace séparé avec préfixe `'K'` :

```cpp
// ZKHU (namespace 'K' — OBLIGATOIRE)
'K' + 'A' + anchor_khu     → ZKHU SaplingMerkleTree
'K' + 'N' + nullifier_khu  → ZKHU nullifier spent flag
'K' + 'T' + note_id        → ZKHUNoteData

// Shield (PIVX Sapling public — namespace 'S'/'s')
'S' + anchor      → Shield SaplingMerkleTree
's' + nullifier   → Shield nullifier spent flag

// ✅ Aucun chevauchement — isolation complète
```

**Structure ZKHUNoteData (CORRIGÉ):**

```cpp
struct ZKHUNoteData {
    int64_t amount;              // KHU amount (satoshis)
    uint32_t nStakeStartHeight;  // Stake start height
    int64_t Ur_accumulated;      // ✅ CORRIGÉ: Accumulated reward (per-note)
    uint256 nullifier;           // Nullifier (dépense unique)
    uint256 cm;                  // Commitment

    SERIALIZE_METHODS(ZKHUNoteData, obj) {
        READWRITE(obj.amount, obj.nStakeStartHeight, obj.Ur_accumulated);
        READWRITE(obj.nullifier, obj.cm);
    }
};
```

---

## 4. PIPELINE STAKING

### 4.1 Opération STAKE (T→Z)

**Entrée:** UTXO KHU_T (transparent)
**Sortie:** Note ZKHU (shielded)

```cpp
bool ProcessSTAKE(const CTransaction& tx, KhuGlobalState& state) {
    // 1. Vérifier input KHU_T UTXO
    CKHUUTXO input_utxo = GetKHUUTXO(tx.vin[0]);
    if (input_utxo.fStaked) {
        return error("KHU already staked");
    }

    // 2. Créer note ZKHU
    ZKHUNote zkhu_note;
    zkhu_note.amount = input_utxo.amount;
    zkhu_note.nStakeStartHeight = nHeight;
    zkhu_note.Ur_accumulated = 0;  // ✅ Phase 4: no yield yet (Phase 5: will increment)

    // 3. Générer commitment et nullifier (Sapling standard)
    zkhu_note.cm = ComputeCommitment(zkhu_note);
    zkhu_note.nullifier = ComputeNullifier(zkhu_note);

    // 4. Ajouter à merkle tree ZKHU
    zkhuTree.append(zkhu_note.cm);

    // 5. Stocker note en DB (clé 'K')
    WriteZKHUNote(zkhu_note);

    // 6. ✅ MISE À JOUR: U et Z changent (Z stocké dans state)
    int64_t amount = input_utxo.amount;
    state.U -= amount;  // KHU_T supply decreases
    state.Z += amount;  // ZKHU supply increases

    // 7. Vérifier invariants
    if (!state.CheckInvariants()) {
        return error("Invariants violated after STAKE");
    }

    return true;
}
```

**Invariants STAKE:**
```cpp
C_after == C_before            // Aucun changement collateral
U_after == U_before - amount   // KHU_T supply decreases
Z_after == Z_before + amount   // ZKHU supply increases (stocké dans state)
Cr_after == Cr_before          // Aucun changement reward pool
Ur_after == Ur_before          // Aucun changement rights

// Invariant C == U + Z préservé:
// C = U_before + Z_before
// C = (U_before - amount) + (Z_before + amount) = U_after + Z_after ✅
```

### 4.2 Opération UNSTAKE (Z→T)

**Entrée:** Note ZKHU (shielded)
**Sortie:** UTXO KHU_T (transparent) avec principal P + yield Y

```cpp
bool ProcessUNSTAKE(const CTransaction& tx, KhuGlobalState& state) {
    // 1. Vérifier note ZKHU dépensée
    ZKHUNote zkhu_note = GetZKHUNote(tx);

    // 2. Vérifier nullifier unique
    if (IsNullifierSpent(zkhu_note.nullifier)) {
        return error("ZKHU note already spent");
    }

    // 3. Vérifier maturity (4320 blocs = 3 jours minimum)
    uint32_t stake_duration = nHeight - zkhu_note.nStakeStartHeight;
    if (stake_duration < MATURITY_BLOCKS) {
        return state.Invalid(false, REJECT_INVALID,
                           "unstake-before-maturity",
                           "UNSTAKE requires 4320 blocks (3 days) maturity");
    }

    // 4. ✅ CORRIGÉ: Calculer yield Y (per-note Ur_accumulated)
    // Y = 0 si note immature (< 4320 blocs), Y > 0 si mature
    int64_t Y = zkhu_note.Ur_accumulated;  // Phase 4: =0, Phase 5+: >0
    if (Y < 0) {
        return error("Negative yield");
    }

    // 5. Créer output KHU_T avec principal + yield
    int64_t P = zkhu_note.amount;  // P = principal
    int64_t total_output = P + Y;
    CreateKHUUTXO(total_output, new_address);  // getnewaddress

    // 6. ✅ Double flux atomique (P = principal, Y = yield)
    state.Z  -= P;       // Principal retiré du shielded
    state.U  += P + Y;   // Principal + Yield vers transparent
    state.C  += Y;       // Yield ajoute au collateral (inflation)
    state.Cr -= Y;       // Yield consommé du pool
    state.Ur -= Y;       // Yield consommé des droits

    // 7. Marquer nullifier comme dépensé
    MarkNullifierSpent(zkhu_note.nullifier);

    // 8. ✅ Vérifier invariants (CRITICAL)
    if (!state.CheckInvariants()) {
        return error("Invariants violated after UNSTAKE");
    }

    return true;
}
```

**Invariants UNSTAKE (P = principal, Y = yield):**
```cpp
// Avant UNSTAKE:
C == U + Z           // Collateralization (Z stocké dans state)
Cr == Ur             // Pool cohérent

// Après UNSTAKE:
Z_after  == Z_before - P           // ✅ Principal retiré du shielded
U_after  == U_before + P + Y       // ✅ Principal + Yield vers transparent
C_after  == C_before + Y           // ✅ Yield ajoute au collateral
Cr_after == Cr_before - Y          // ✅ Yield consommé du pool
Ur_after == Ur_before - Y          // ✅ Yield consommé des droits

// Invariant C == U + Z préservé:
// C_after = C_before + Y
// U_after + Z_after = (U_before + P + Y) + (Z_before - P) = U_before + Z_before + Y = C_before + Y = C_after ✅

// Phase 4: Y=0 → Z change, mais invariant préservé
// Phase 5+: Y>0 → double flux actif (inflation contrôlée)
```

---

## 5. METADATA (MEMO 512 BYTES)

Chaque note ZKHU contient metadata dans le memo Sapling standard (512 bytes) :

```cpp
struct ZKHUMemo {
    char magic[4];               // "ZKHU"
    uint8_t version;             // 1
    uint32_t nStakeStartHeight;  // Stake start height
    int64_t amount;              // KHU amount (satoshis)
    int64_t Ur_accumulated;      // ✅ CORRIGÉ: Accumulated reward (per-note)
    uint8_t padding[487];        // Zeros (reserved)
};
```

**Serialization:**
```cpp
std::array<unsigned char, 512> SerializeZKHUMemo(const ZKHUNote& note) {
    std::array<unsigned char, 512> memo;
    memo.fill(0);  // Zero padding

    // Magic
    memcpy(&memo[0], "ZKHU", 4);

    // Version
    memo[4] = 1;

    // Height (4 bytes)
    WriteLE32(&memo[5], note.nStakeStartHeight);

    // Amount (8 bytes)
    WriteLE64(&memo[9], note.amount);

    // ✅ CORRIGÉ: Ur_accumulated (per-note, NOT global snapshot)
    WriteLE64(&memo[17], note.Ur_accumulated);

    return memo;
}
```

---

## 6. ATOMICITÉ

### 6.1 STAKE (T→Z)

```
Atomicité STAKE:
1. Dépense UTXO KHU_T
2. Création note ZKHU
3. Append merkle tree ZKHU
4. Write DB 'K' + 'T' + note_id

✅ Soit tout réussit, soit rollback complet
✅ Pas de changement C, U, Cr, Ur
```

### 6.2 UNSTAKE (Z→T)

```
Atomicité UNSTAKE (P = principal, Y = yield):
1. Vérification note ZKHU valide
2. Vérification nullifier non dépensé
3. Vérification maturity >= 4320 blocs (3 jours)
4. Calcul Y = Ur_accumulated (0 si immature)
5. Création UTXO KHU_T (P + Y)
6. Double flux atomique:
   - Z -= P
   - U += P + Y
   - C += Y
   - Cr -= Y
   - Ur -= Y
7. Marquer nullifier dépensé

✅ Soit tout réussit, soit rollback complet
✅ Invariants vérifiés APRÈS mise à jour
```

---

## 7. TESTS OBLIGATOIRES

### 7.1 Test unitaire : STAKE T→Z

```cpp
BOOST_AUTO_TEST_CASE(test_stake_t_to_z)
{
    // Setup
    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Cr = 10 * COIN;
    state.Ur = 10 * COIN;

    // STAKE 50 KHU_T → ZKHU
    CKHUUTXO utxo;
    utxo.amount = 50 * COIN;
    utxo.fStaked = false;

    ProcessSTAKE(utxo, state);

    // Vérifier aucun changement C, U, Cr, Ur
    BOOST_CHECK_EQUAL(state.C, 100 * COIN);
    BOOST_CHECK_EQUAL(state.U, 100 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 10 * COIN);
    BOOST_CHECK_EQUAL(state.Ur, 10 * COIN);
    BOOST_CHECK(state.CheckInvariants());

    // Vérifier note ZKHU créée
    ZKHUNote note = GetZKHUNote(...);
    BOOST_CHECK_EQUAL(note.amount, 50 * COIN);
    BOOST_CHECK_EQUAL(note.nStakeStartHeight, nHeight);
    BOOST_CHECK_EQUAL(note.Ur_at_stake, 10 * COIN);
}
```

### 7.2 Test unitaire : UNSTAKE Z→T

```cpp
BOOST_AUTO_TEST_CASE(test_unstake_z_to_t)
{
    // Setup: Note ZKHU staké depuis 5000 blocs (>4320 = mature)
    ZKHUNote note;
    note.amount = 50 * COIN;  // P = principal
    note.nStakeStartHeight = 1000;
    note.Ur_accumulated = 5 * COIN;  // Y = yield accumulé

    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 50 * COIN;
    state.Z = 50 * COIN;   // Note ZKHU stakée
    state.Cr = 5 * COIN;
    state.Ur = 5 * COIN;

    uint32_t current_height = 6000;  // 5000 blocs après STAKE (mature)

    // UNSTAKE (P = 50, Y = 5)
    ProcessUNSTAKE(note, state, current_height);

    // Vérifier double flux atomique
    BOOST_CHECK_EQUAL(state.Z, 0);           // Z -= P
    BOOST_CHECK_EQUAL(state.U, 105 * COIN);  // U += P + Y (50 + 50 + 5)
    BOOST_CHECK_EQUAL(state.C, 105 * COIN);  // C += Y (100 + 5)
    BOOST_CHECK_EQUAL(state.Cr, 0);          // Cr -= Y (5 - 5)
    BOOST_CHECK_EQUAL(state.Ur, 0);          // Ur -= Y (5 - 5)
    BOOST_CHECK(state.CheckInvariants());    // C == U + Z

    // Vérifier output KHU_T = P + Y
    CKHUUTXO output = GetKHUUTXO(...);
    BOOST_CHECK_EQUAL(output.amount, 55 * COIN);  // 50 + 5
}
```

### 7.3 Test fonctionnel : Maturity enforcement

```python
#!/usr/bin/env python3
"""Test ZKHU maturity (4320 blocs)"""

from test_framework.test_framework import PivxTestFramework
from test_framework.util import assert_raises_rpc_error

MATURITY_BLOCKS = 4320

class ZKHUMaturityTest(PivxTestFramework):
    def run_test(self):
        node = self.nodes[0]

        # STAKE 100 KHU_T → ZKHU
        stake_tx = node.stakekhu(100)
        stake_height = node.getblockcount()

        # Essayer UNSTAKE immédiatement (devrait échouer)
        assert_raises_rpc_error(
            -26,  # REJECT_INVALID
            "unstake-before-maturity",
            node.unstakekhu,
            stake_tx
        )

        # Générer 4319 blocs (1 bloc avant maturity)
        node.generate(MATURITY_BLOCKS - 1)

        # Essayer UNSTAKE (devrait encore échouer)
        assert_raises_rpc_error(
            -26,
            "unstake-before-maturity",
            node.unstakekhu,
            stake_tx
        )

        # Générer 1 bloc de plus (maturity atteinte)
        node.generate(1)

        # UNSTAKE devrait réussir
        unstake_tx = node.unstakekhu(stake_tx)
        assert unstake_tx  # Success

        self.log.info("ZKHU maturity enforcement: PASSED ✅")

if __name__ == '__main__':
    ZKHUMaturityTest().main()
```

---

## 8. INTERDICTIONS TECHNIQUES

```cpp
// ❌ INTERDIT : Utiliser clés Shield pour ZKHU
zkhuTree = saplingTree;  // ❌ Doit être séparé

// ❌ INTERDIT : Z→Z transfer
CreateZKHUToZKHUTransaction();  // ❌ Pas de Z→Z

// ❌ INTERDIT : Join-split
JoinSplitZKHU();  // ❌ Pas de join-split

// ❌ INTERDIT : UNSTAKE avant maturity
if (nHeight - nStakeStartHeight < MATURITY_BLOCKS) {
    // ❌ DOIT rejeter
}

// ❌ INTERDIT : Yield négatif
if (yield < 0) {
    // ❌ DOIT rejeter (Ur a diminué)
}
```

---

## 9. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **Sapling uniquement** : Aucune modification cryptographique
2. **Namespace 'K'** : Isolation complète de Shield ('S', 's')
3. **Pipeline T→Z→T** : Pas de Z→Z, pas de join-split
4. **Maturity 4320 blocs** : Obligatoire avant UNSTAKE
5. **Yield (Y) = Ur_now - Ur_at_stake** : Formule exacte
6. **Invariants** : Cr == Ur avant et après UNSTAKE
7. **Atomicité** : Tout ou rien (pas d'état partiel)

Toute modification doit :
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date

**Statut :** ACTIF pour implémentation Phase 7

---

**FIN DU BLUEPRINT ZKHU SAPLING STAKE/UNSTAKE**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
