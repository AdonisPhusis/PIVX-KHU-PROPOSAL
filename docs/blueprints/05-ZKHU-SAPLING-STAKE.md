# 07 — ZKHU SAPLING STAKE/UNSTAKE

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Décrire précisément l'utilisation minimale de Sapling pour :
- Convertir KHU_T → ZKHU (STAKE)
- Conserver un stake privé
- Récupérer KHU_T avec bonus (UNSTAKE)

**RÈGLE FONDAMENTALE:** ZKHU utilise la cryptographie Sapling existante de PIVX (Shield) avec un namespace LevelDB séparé.

---

## ⚠️ PHASE 4 vs PHASE 5 : BONUS YIELD

**PHASE 4 (Staking sans yield) :**
- R% = 0 (pas de yield actif)
- Ur_accumulated = 0 pour toutes les notes
- Bonus B = 0 pour tous les UNSTAKE
- **Effet net : C/U numériquement inchangés** (mais code applique déjà double flux C+=B, U+=B avec B=0)
- **Structure code déjà prête pour Phase 5**

**PHASE 5+ (Yield R% actif) :**
- R% > 0 (voté par DOMC)
- Ur_accumulated croît quotidiennement (1440 blocs)
- Bonus B > 0 pour UNSTAKE après maturity
- **Effet net : C/U augmentent, Cr/Ur diminuent** (double flux actif)

**Ce blueprint décrit la structure GÉNÉRALE (B>=0), Phase 4 est le cas particulier B=0.**

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
| UNSTAKE (Z→T) | 3 | Appel ValidateSaplingSpend() existant + bonus calc |
| Namespace 'K' setup | 1 | Clés LevelDB 'K'+'A/N/T' (simple) |
| Tests maturity + bonus | 1 | Tests unitaires/fonctionnels |
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

    // ✅ Pas de changement C, U, Cr, Ur
    // STAKE ne modifie QUE la forme (T→Z)

    return true;
}
```

**Invariants STAKE:**
```cpp
C_after == C_before   // Aucun changement collateral
U_after == U_before   // Aucun changement supply
Cr_after == Cr_before // Aucun changement reward pool
Ur_after == Ur_before // Aucun changement rights
```

### 4.2 Opération UNSTAKE (Z→T)

**Entrée:** Note ZKHU (shielded)
**Sortie:** UTXO KHU_T (transparent) + bonus KHU

```cpp
bool ProcessUNSTAKE(const CTransaction& tx, KhuGlobalState& state) {
    // 1. Vérifier note ZKHU dépensée
    ZKHUNote zkhu_note = GetZKHUNote(tx);

    // 2. Vérifier nullifier unique
    if (IsNullifierSpent(zkhu_note.nullifier)) {
        return error("ZKHU note already spent");
    }

    // 3. Vérifier maturity (4320 blocs minimum)
    uint32_t stake_duration = nHeight - zkhu_note.nStakeStartHeight;
    if (stake_duration < MATURITY_BLOCKS) {
        return state.Invalid(false, REJECT_INVALID,
                           "unstake-before-maturity",
                           "UNSTAKE requires 4320 blocks maturity");
    }

    // 4. ✅ CORRIGÉ: Calculer bonus (per-note Ur_accumulated)
    int64_t bonus = zkhu_note.Ur_accumulated;  // Phase 4: =0, Phase 5+: >0
    if (bonus < 0) {
        return error("Negative bonus");
    }

    // 5. Créer output KHU_T avec bonus
    int64_t total_output = zkhu_note.amount + bonus;
    CreateKHUUTXO(total_output, new_address);  // getnewaddress

    // 6. ✅ CORRIGÉ: DOUBLE FLUX (canonique)
    // Supply et collateral augmentent (création KHU + PIV)
    state.U += bonus;    // Supply KHU increases
    state.C += bonus;    // Collateral PIV increases

    // Pool et rights diminuent (consommation reward)
    state.Cr -= bonus;   // Consume reward pool
    state.Ur -= bonus;   // Consume rights

    // 7. Marquer nullifier comme dépensé
    MarkNullifierSpent(zkhu_note.nullifier);

    // 8. ✅ Vérifier invariants (CRITICAL)
    if (!state.CheckInvariants()) {
        return error("Invariants violated after UNSTAKE");
    }

    return true;
}
```

**Invariants UNSTAKE (CORRIGÉ):**
```cpp
// Avant UNSTAKE:
C == U               // Collateralization
Cr == Ur             // Pool cohérent

// Après UNSTAKE (bonus B):
U_after  == U_before  + B   // ✅ Supply increases
C_after  == C_before  + B   // ✅ Collateral increases
Cr_after == Cr_before - B   // ✅ Pool decreases
Ur_after == Ur_before - B   // ✅ Rights decrease

// Invariants préservés:
C_after == U_after          // ✅ (C+B) == (U+B)
Cr_after == Ur_after        // ✅ (Cr-B) == (Ur-B)

// Phase 4: B=0 → effet net nul, mais structure déjà correcte
// Phase 5+: B>0 → double flux actif
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
Atomicité UNSTAKE:
1. Vérification note ZKHU valide
2. Vérification nullifier non dépensé
3. Vérification maturity >= 4320 blocs
4. Calcul bonus = Ur_now - Ur_at_stake
5. Création UTXO KHU_T (amount + bonus)
6. Mise à jour Cr -= bonus, Ur -= bonus
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
    // Setup: Note ZKHU staké depuis 5000 blocs
    ZKHUNote note;
    note.amount = 50 * COIN;
    note.nStakeStartHeight = 1000;
    note.Ur_at_stake = 10 * COIN;

    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Cr = 15 * COIN;  // Cr a augmenté (yield)
    state.Ur = 15 * COIN;

    uint32_t current_height = 6000;  // 5000 blocs après STAKE

    // UNSTAKE
    int64_t bonus = state.Ur - note.Ur_at_stake;  // 15 - 10 = 5 KHU
    ProcessUNSTAKE(note, state, current_height);

    // Vérifier Cr, Ur diminués de bonus
    BOOST_CHECK_EQUAL(state.Cr, 10 * COIN);  // 15 - 5
    BOOST_CHECK_EQUAL(state.Ur, 10 * COIN);  // 15 - 5
    BOOST_CHECK(state.CheckInvariants());

    // Vérifier C, U inchangés
    BOOST_CHECK_EQUAL(state.C, 100 * COIN);
    BOOST_CHECK_EQUAL(state.U, 100 * COIN);

    // Vérifier output KHU_T = amount + bonus
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

// ❌ INTERDIT : Bonus négatif
if (bonus < 0) {
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
5. **Bonus = Ur_now - Ur_at_stake** : Formule exacte
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
