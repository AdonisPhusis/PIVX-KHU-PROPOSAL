# 02 — KHU COLORED COIN BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit **KHU_T comme colored coin UTXO**.

**KHU_T est un token UTXO transparent avec structure IDENTIQUE à PIV.**

**RÈGLE ABSOLUE : KHU_T utilise le même modèle UTXO que PIVX, pas de burn direct, supply protégée par invariant C==U+Z.**

---

## 1. KHU_T = COLORED COIN (PAS NOUVEAU TOKEN)

### 1.1 Qu'est-ce qu'un Colored Coin ?

Un **colored coin** est un UTXO standard (comme PIV) avec metadata supplémentaire pour indiquer qu'il représente un actif différent.

```
┌─────────────────────────────────────────────┐
│ UTXO PIV (standard)                         │
├─────────────────────────────────────────────┤
│ • TxHash + vout                             │
│ • Amount (PIV)                              │
│ • scriptPubKey                              │
│ • nHeight                                   │
│ • Pas de metadata spéciale                  │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│ UTXO KHU_T (colored coin)                   │
├─────────────────────────────────────────────┤
│ • TxHash + vout                             │
│ • Amount (KHU)                              │
│ • scriptPubKey                              │
│ • nHeight                                   │
│ • fIsKHU = true ← Flag "coloring"           │
│ • fStaked (optionnel)                       │
│ • nStakeStartHeight (si staké)              │
└─────────────────────────────────────────────┘
```

**KHU_T = PIV UTXO + flag "fIsKHU"**

### 1.2 Avantages Colored Coin

```
✅ Réutilise infrastructure UTXO PIVX existante
✅ Pas besoin de nouveau modèle de stockage
✅ Compatible avec scripts Bitcoin standard
✅ HTLC fonctionne automatiquement
✅ Validation UTXO identique
✅ Pas de complexité cryptographique ajoutée
```

### 1.3 KHU_T vs PIV (Comparaison)

| Propriété | PIV | KHU_T |
|-----------|-----|-------|
| Modèle | UTXO | UTXO (identique) |
| Structure | CTxOut | CTxOut (identique) |
| Validation | CheckTransaction | CheckTransaction (identique) |
| Stockage | LevelDB coins | LevelDB coins (préfixe 'K') |
| Scripts | Bitcoin standard | Bitcoin standard (identique) |
| HTLC | Supporté | Supporté (identique) |
| Fees | PIV burn | PIV burn (identique) |
| Supply | Variable (émission) | Fixe (invariant C==U+Z) |
| Burn | Non | Non (sauf REDEEM) |

**La SEULE différence : KHU_T a un flag "fIsKHU" pour tracking.**

---

## 2. STRUCTURE CANONIQUE KHU_T UTXO

### 2.1 Fichier : src/khu/khu_coins.h

```cpp
#ifndef PIVX_KHU_COINS_H
#define PIVX_KHU_COINS_H

#include "coins.h"
#include "primitives/transaction.h"
#include "amount.h"
#include "script/script.h"

/**
 * KHU UTXO Structure
 *
 * ⚠️ IMPORTANT : Structure IDENTIQUE à Coin (PIVX UTXO standard)
 *                Seule différence : flags supplémentaires
 */
struct CKHUUTXO {
    //! Montant KHU (CAmount = int64_t, identique à PIV)
    CAmount amount;

    //! Script de sortie (identique à PIV)
    CScript scriptPubKey;

    //! Hauteur de création (identique à PIV)
    uint32_t nHeight;

    //! ═══════════════════════════════════════════
    //! FLAGS SPÉCIFIQUES KHU (metadata coloring)
    //! ═══════════════════════════════════════════

    //! Flag : est-ce un UTXO KHU ? (vs PIV)
    bool fIsKHU;

    //! Flag : KHU staké en ZKHU ?
    bool fStaked;

    //! Si staké : hauteur de début de stake
    uint32_t nStakeStartHeight;

    //! Constructeur par défaut
    CKHUUTXO() : amount(0), nHeight(0), fIsKHU(true), fStaked(false), nStakeStartHeight(0) {}

    //! Constructeur complet
    CKHUUTXO(CAmount amountIn, CScript scriptIn, uint32_t heightIn)
        : amount(amountIn), scriptPubKey(scriptIn), nHeight(heightIn),
          fIsKHU(true), fStaked(false), nStakeStartHeight(0) {}

    //! Sérialisation (pour LevelDB)
    SERIALIZE_METHODS(CKHUUTXO, obj) {
        READWRITE(obj.amount);
        READWRITE(obj.scriptPubKey);
        READWRITE(obj.nHeight);
        READWRITE(obj.fIsKHU);
        READWRITE(obj.fStaked);
        READWRITE(obj.nStakeStartHeight);
    }

    //! Est-ce dépensé ?
    bool IsSpent() const {
        return amount == -1;
    }

    //! Marquer comme dépensé
    void Clear() {
        amount = -1;
        scriptPubKey.clear();
    }

    //! Peut être dépensé ? (pas staké)
    bool IsSpendable() const {
        return !fStaked;
    }
};

#endif // PIVX_KHU_COINS_H
```

### 2.2 Comparaison avec Coin (PIV UTXO)

```cpp
// PIVX UTXO standard (coins.h)
class Coin {
    CTxOut out;           // Montant + scriptPubKey
    uint32_t nHeight;
    bool fCoinBase;
};

// KHU UTXO (khu_coins.h)
struct CKHUUTXO {
    CAmount amount;       // ← Même que out.nValue
    CScript scriptPubKey; // ← Même que out.scriptPubKey
    uint32_t nHeight;     // ← Identique

    // Flags supplémentaires (coloring)
    bool fIsKHU;          // ← SEULE différence
    bool fStaked;
    uint32_t nStakeStartHeight;
};
```

**Structure quasiment identique = Réutilisation maximale du code PIVX.**

---

## 3. PAS DE BURN DIRECT (SUPPLY PROTÉGÉE)

### 3.1 Règle Fondamentale

**KHU_T ne peut PAS être brûlé directement.**

```cpp
// ❌ INTERDIT : Burn direct
void BurnKHU(CAmount amount) {
    state.U -= amount;  // ❌ VIOLATION INVARIANT C==U+Z
}

// ✅ CORRECT : Destruction via REDEEM uniquement
void RedeemKHU(CAmount amount) {
    state.C -= amount;  // Atomique
    state.U -= amount;  // Atomique
    // Invariant C==U+Z préservé
}
```

### 3.2 Supply Inchangeable (Sauf MINT/REDEEM)

**L'invariant C==U+Z protège la supply KHU (transparent + shielded).**

```
┌────────────────────────────────────────────────┐
│ OPÉRATIONS AUTORISÉES (modifient U)           │
├────────────────────────────────────────────────┤
│ MINT:    C+, U+  (création KHU)                │
│ REDEEM:  C-, U-  (destruction KHU)             │
│ UNSTAKE: Z-, C+, U+, Cr-, Ur-  (yield Y)       │
└────────────────────────────────────────────────┘

┌────────────────────────────────────────────────┐
│ OPÉRATIONS INTERDITES (modifieraient U seul)  │
├────────────────────────────────────────────────┤
│ ❌ Burn direct (U- sans C-)                    │
│ ❌ Inflation (U+ sans C+)                      │
│ ❌ Confiscation (U- sans compensation)         │
│ ❌ Modification supply arbitraire              │
└────────────────────────────────────────────────┘
```

**L'invariant C==U+Z FORCE l'atomicité et empêche la manipulation de supply.**

### 3.3 Pourquoi Pas de Burn ?

**Raisons fondamentales :**

1. **Prévisibilité** : Supply KHU = f(MINT, REDEEM, UNSTAKE) uniquement
2. **Auditabilité** : Chaque KHU traçable depuis création (MINT)
3. **Sécurité** : Impossibilité de détruire KHU par erreur
4. **Invariant** : C==U+Z ne peut être maintenu avec burn arbitraire

**Exemple d'attaque évitée :**
```cpp
// Scénario d'attaque (si burn était permis)
if (malicious_condition) {
    BurnKHU(1000000 * COIN);  // ❌ Détruire 1M KHU
    // → C inchangé, U diminué
    // → C != U+Z (violation invariant)
    // → Système cassé
}

// Avec interdiction burn :
// ✅ Impossible de modifier U sans C
// ✅ Invariant toujours respecté
```

---

## 4. FEES EN PIV (BURN COMME CHAÎNE PRINCIPALE)

### 4.1 Règle des Fees

**TOUTES les transactions KHU ont des fees en PIV (PAS en KHU).**

**Les fees PIV sont BURN (comme les transactions PIV normales).**

```
┌─────────────────────────────────────────────────────┐
│ Transaction PIVX Standard                           │
├─────────────────────────────────────────────────────┤
│ Input:  100 PIV                                     │
│ Output: 99.99 PIV                                   │
│ Fee:    0.01 PIV → BURN ❌ (pas au mineur)          │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Transaction KHU (MINT, REDEEM, Transfer)            │
├─────────────────────────────────────────────────────┤
│ Input:  100 KHU_T + 1 PIV (pour fee)                │
│ Output: 100 KHU_T                                   │
│ Fee:    0.01 PIV → BURN ❌ (identique à PIVX)       │
└─────────────────────────────────────────────────────┘
```

**KHU_T transactions paient fees en PIV (burn), jamais en KHU.**

### 4.2 Pourquoi Fees en PIV ?

**Raisons techniques :**

1. **Cohérence** : Toutes les transactions PIVX paient fees en PIV
2. **Simplicité** : Pas besoin de dual-fee system
3. **Sécurité** : Évite spam via KHU
4. **Déflationniste** : Fees PIV burn = réduction supply PIV
5. **Invariant** : Fees en KHU compliqueraient C==U+Z

### 4.3 Exemples Transactions

#### Transaction MINT

```
┌─────────────────────────────────────────┐
│ MINT : 100 PIV → 100 KHU_T              │
├─────────────────────────────────────────┤
│ Inputs:                                 │
│   [0] 100.01 PIV                        │
│                                         │
│ Outputs:                                │
│   [0] 100 PIV → OP_RETURN (burn/lock)   │
│   [1] 100 KHU_T → recipient             │
│                                         │
│ Fee: 0.01 PIV → BURN                    │
└─────────────────────────────────────────┘

État global :
  C += 100  (collateral PIV verrouillé)
  U += 100  (KHU_T créé)
  Fee PIV burn (0.01 PIV détruit)
```

#### Transaction Transfer KHU_T

```
┌─────────────────────────────────────────┐
│ Transfer : 50 KHU_T → Alice             │
├─────────────────────────────────────────┤
│ Inputs:                                 │
│   [0] 100 KHU_T (sender)                │
│   [1] 1 PIV (pour fee)                  │
│                                         │
│ Outputs:                                │
│   [0] 50 KHU_T → Alice                  │
│   [1] 50 KHU_T → sender (change)        │
│   [2] 0.99 PIV → sender (change PIV)    │
│                                         │
│ Fee: 0.01 PIV → BURN                    │
└─────────────────────────────────────────┘

État global :
  C inchangé (pas de MINT/REDEEM)
  U inchangé (transfer interne)
  Fee PIV burn (0.01 PIV détruit)
```

#### Transaction REDEEM

```
┌─────────────────────────────────────────┐
│ REDEEM : 50 KHU_T → 50 PIV              │
├─────────────────────────────────────────┤
│ Inputs:                                 │
│   [0] 50 KHU_T                          │
│   [1] 1 PIV (pour fee)                  │
│                                         │
│ Outputs:                                │
│   [0] 50 PIV → recipient                │
│   [1] 0.99 PIV → sender (change fee)    │
│                                         │
│ Fee: 0.01 PIV → BURN                    │
└─────────────────────────────────────────┘

État global :
  C -= 50  (collateral PIV libéré)
  U -= 50  (KHU_T détruit)
  Fee PIV burn (0.01 PIV détruit)
```

### 4.4 Calcul Fee (Identique à PIVX)

```cpp
/**
 * Calcul fee transaction KHU
 * Identique au calcul fee PIVX standard
 */
CAmount CalculateKHUTransactionFee(const CTransaction& tx) {
    // Taille transaction en bytes
    size_t nTxSize = ::GetSerializeSize(tx, PROTOCOL_VERSION);

    // Fee = taille × taux (ex: 0.0001 PIV/byte)
    CAmount nFee = nTxSize * GetMinRelayFee();

    return nFee;
}

/**
 * Vérification fee suffisante
 */
bool CheckKHUTransactionFee(const CTransaction& tx, const CCoinsViewCache& view) {
    CAmount nInputsPIV = 0;
    CAmount nOutputsPIV = 0;

    // Calculer inputs PIV (pas KHU)
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (!coin.IsKHU())  // Seulement PIV
            nInputsPIV += coin.out.nValue;
    }

    // Calculer outputs PIV (pas KHU)
    for (const auto& out : tx.vout) {
        if (!IsKHUOutput(out))  // Seulement PIV
            nOutputsPIV += out.nValue;
    }

    // Fee = inputs PIV - outputs PIV
    CAmount nFee = nInputsPIV - nOutputsPIV;

    // Vérifier fee minimale
    CAmount nMinFee = CalculateKHUTransactionFee(tx);
    if (nFee < nMinFee)
        return false;

    // ✅ Fee sera BURN (comme PIVX standard)
    return true;
}
```

---

## 5. STOCKAGE LEVELDB (PRÉFIXE 'K')

### 5.1 Clés LevelDB KHU

**KHU UTXOs utilisent un namespace séparé (préfixe 'K') pour isolation.**

```cpp
// ═══════════════════════════════════════════════════════
// NAMESPACE PIVX (coins.cpp)
// ═══════════════════════════════════════════════════════
'C' + txid + vout  →  Coin (PIV UTXO)

// ═══════════════════════════════════════════════════════
// NAMESPACE KHU (khu_coins.cpp)
// ═══════════════════════════════════════════════════════
'K' + 'U' + txid + vout  →  CKHUUTXO (KHU_T UTXO)
'K' + 'S' + height        →  KhuGlobalState
'K' + 'B'                 →  Best KHU state hash
'K' + 'H' + hash          →  KHU state by hash

// ═══════════════════════════════════════════════════════
// NAMESPACE ZKHU (khu_sapling.cpp)
// ═══════════════════════════════════════════════════════
'K' + 'A' + anchor        →  ZKHU SaplingMerkleTree
'K' + 'N' + nullifier     →  ZKHU nullifier spent flag
'K' + 'T' + note_id       →  ZKHUNoteData
```

**Isolation complète : PIV ('C'), KHU ('K' + 'U'), ZKHU ('K' + 'A'/'N'/'T').**

### 5.2 Fichier : src/khu/khu_coins.cpp

```cpp
#include "khu/khu_coins.h"
#include "dbwrapper.h"

/**
 * Écrire UTXO KHU dans LevelDB
 */
bool WriteKHUCoin(CDBBatch& batch, const COutPoint& outpoint, const CKHUUTXO& coin) {
    // Clé : 'K' + 'U' + txid + vout
    std::vector<unsigned char> key;
    key.push_back('K');
    key.push_back('U');
    key.insert(key.end(), outpoint.hash.begin(), outpoint.hash.end());
    WriteLE32(key, outpoint.n);

    // Valeur : CKHUUTXO sérialisé
    batch.Write(key, coin);

    return true;
}

/**
 * Lire UTXO KHU depuis LevelDB
 */
bool ReadKHUCoin(CDBWrapper& db, const COutPoint& outpoint, CKHUUTXO& coin) {
    // Clé : 'K' + 'U' + txid + vout
    std::vector<unsigned char> key;
    key.push_back('K');
    key.push_back('U');
    key.insert(key.end(), outpoint.hash.begin(), outpoint.hash.end());
    WriteLE32(key, outpoint.n);

    // Lire depuis DB
    if (!db.Read(key, coin))
        return false;

    return true;
}

/**
 * Dépenser UTXO KHU (marquer comme spent)
 */
bool SpendKHUCoin(CDBBatch& batch, const COutPoint& outpoint) {
    // Lire coin existant
    CKHUUTXO coin;
    if (!ReadKHUCoin(db, outpoint, coin))
        return false;

    // Marquer comme spent
    coin.Clear();  // amount = -1

    // Écrire (ou effacer)
    batch.Erase(MakeKHUCoinKey(outpoint));

    return true;
}
```

---

## 6. VALIDATION TRANSACTIONS KHU

### 6.1 Vérifications Canoniques

**Toute transaction KHU doit passer les mêmes validations que PIV + checks KHU spécifiques.**

```cpp
bool CheckKHUTransaction(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // ═══════════════════════════════════════════════════
    // 1. VALIDATIONS STANDARD PIVX (réutilisées)
    // ═══════════════════════════════════════════════════

    // Vérifier taille
    if (::GetSerializeSize(tx, PROTOCOL_VERSION) > MAX_BLOCK_SIZE)
        return state.Invalid("khu-tx-oversize");

    // Vérifier inputs non-vides
    if (tx.vin.empty())
        return state.Invalid("khu-tx-no-inputs");

    // Vérifier outputs non-vides
    if (tx.vout.empty())
        return state.Invalid("khu-tx-no-outputs");

    // Vérifier montants positifs
    for (const auto& out : tx.vout) {
        if (out.nValue < 0)
            return state.Invalid("khu-tx-negative-output");
    }

    // ═══════════════════════════════════════════════════
    // 2. VALIDATIONS SPÉCIFIQUES KHU
    // ═══════════════════════════════════════════════════

    switch (tx.nType) {
        case TxType::KHU_MINT:
            return CheckKHUMint(tx, state, view);

        case TxType::KHU_REDEEM:
            return CheckKHURedeem(tx, state, view);

        case TxType::KHU_STAKE:
            return CheckKHUStake(tx, state, view);

        case TxType::KHU_UNSTAKE:
            return CheckKHUUnstake(tx, state, view);

        case TxType::KHU_TRANSFER:
            // Transfer standard (comme PIV)
            return CheckKHUTransfer(tx, state, view);

        default:
            return state.Invalid("khu-tx-unknown-type");
    }

    return true;
}
```

### 6.2 Transfer KHU_T Standard

```cpp
bool CheckKHUTransfer(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // Transfer KHU_T fonctionne EXACTEMENT comme transfer PIV

    CAmount nInputsKHU = 0;
    CAmount nOutputsKHU = 0;

    // Vérifier inputs KHU existent
    for (const auto& in : tx.vin) {
        CKHUUTXO khuCoin;
        if (!view.GetKHUCoin(in.prevout, khuCoin))
            return state.Invalid("khu-transfer-missing-input");

        // Vérifier pas staké
        if (khuCoin.fStaked)
            return state.Invalid("khu-transfer-staked-khu");

        nInputsKHU += khuCoin.amount;
    }

    // Vérifier outputs KHU
    for (const auto& out : tx.vout) {
        if (IsKHUOutput(out))
            nOutputsKHU += out.nValue;
    }

    // Vérifier conservation montant
    if (nInputsKHU != nOutputsKHU)
        return state.Invalid("khu-transfer-amount-mismatch");

    // ✅ Pas de modification C/U (transfer interne)
    return true;
}
```

---

## 7. INTERDICTIONS ABSOLUES

### 7.1 Code Interdit

```cpp
// ❌ INTERDIT : Burn direct
state.U -= amount;  // Sans state.C -= amount

// ❌ INTERDIT : Création KHU sans MINT
CKHUUTXO coin;
coin.amount = 1000000 * COIN;
WriteKHUCoin(coin);  // Violation C==U

// ❌ INTERDIT : Fees en KHU
CAmount fee_khu = 0.01 * COIN;  // ❌ Fees = PIV uniquement

// ❌ INTERDIT : Modifier supply sans atomicité C/U
state.U += Y;  // Sans state.C += Y (yield doit être atomique)

// ❌ INTERDIT : Structure CKHUUTXO différente de Coin
struct CKHUUTXO {
    std::string owner;  // ❌ Pas de string
    double amount;      // ❌ Pas de double (CAmount uniquement)
};
```

### 7.2 Opérations Interdites

```
❌ Burn KHU direct (pas via REDEEM)
❌ Inflation KHU arbitraire
❌ Fees en KHU (PIV uniquement)
❌ Modification supply sans C/U atomique
❌ UTXO KHU sans tracking proper
❌ Mélanger namespace PIV ('C') et KHU ('K')
❌ Dépenser KHU staké (fStaked = true)
❌ Créer KHU sans verrouillage PIV (MINT)
```

---

## 8. CHECKLIST IMPLÉMENTATION

### 8.1 Structures

- [ ] `CKHUUTXO` défini (src/khu/khu_coins.h)
- [ ] Structure identique à `Coin` (sauf flags)
- [ ] Flags : `fIsKHU`, `fStaked`, `nStakeStartHeight`
- [ ] Sérialisation compatible LevelDB

### 8.2 Stockage LevelDB

- [ ] Namespace 'K' + 'U' pour UTXOs KHU
- [ ] `WriteKHUCoin()` implémenté
- [ ] `ReadKHUCoin()` implémenté
- [ ] `SpendKHUCoin()` implémenté
- [ ] Isolation complète PIV/KHU

### 8.3 Validation

- [ ] `CheckKHUTransaction()` implémenté
- [ ] `CheckKHUTransfer()` implémenté
- [ ] Vérifications standard PIVX réutilisées
- [ ] Fee PIV vérifiée (pas KHU)
- [ ] Pas de dépense KHU staké

### 8.4 Fees

- [ ] Fees calculées en PIV (CalculateKHUTransactionFee)
- [ ] Fees burn (comme PIVX standard)
- [ ] Pas de fees en KHU

### 8.5 Tests

- [ ] `khu_colored_coin_tests.cpp` (unit tests)
- [ ] Test structure CKHUUTXO
- [ ] Test storage/retrieval LevelDB
- [ ] Test validation transfer
- [ ] Test fees PIV burn
- [ ] Test interdiction burn direct

---

## 9. TESTS UNITAIRES

### 9.1 Fichier : src/test/khu_colored_coin_tests.cpp

```cpp
#include <boost/test/unit_test.hpp>
#include "khu/khu_coins.h"

BOOST_AUTO_TEST_SUITE(khu_colored_coin_tests)

/**
 * Test 1 : Structure CKHUUTXO
 */
BOOST_AUTO_TEST_CASE(test_ckhuutxo_structure)
{
    CKHUUTXO coin;
    coin.amount = 100 * COIN;
    coin.scriptPubKey = GetScriptForDestination(dest);
    coin.nHeight = 1000;
    coin.fIsKHU = true;
    coin.fStaked = false;

    // Vérifier structure
    BOOST_CHECK_EQUAL(coin.amount, 100 * COIN);
    BOOST_CHECK_EQUAL(coin.nHeight, 1000);
    BOOST_CHECK(coin.fIsKHU);
    BOOST_CHECK(!coin.fStaked);
    BOOST_CHECK(coin.IsSpendable());
}

/**
 * Test 2 : KHU staké non dépensable
 */
BOOST_AUTO_TEST_CASE(test_staked_khu_not_spendable)
{
    CKHUUTXO coin;
    coin.amount = 100 * COIN;
    coin.fIsKHU = true;
    coin.fStaked = true;  // Staké

    // Vérifier non-dépensable
    BOOST_CHECK(!coin.IsSpendable());
}

/**
 * Test 3 : Interdiction burn direct
 */
BOOST_AUTO_TEST_CASE(test_no_direct_burn)
{
    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // Tentative burn direct (violation invariant)
    state.U -= 10 * COIN;  // C inchangé

    // Invariant doit échouer
    BOOST_CHECK(!state.CheckInvariants());
}

BOOST_AUTO_TEST_SUITE_END()
```

---

## 10. RÉFÉRENCES

**Blueprints liés :**
- `02-MINT-REDEEM.md` — Opérations MINT/REDEEM
- `07-ZKHU-STAKE-UNSTAKE.md` — Staking KHU_T → ZKHU

**Documents liés :**
- `01-blueprint-master-flow.md` — Flow général
- `SPEC.md` — Spécifications canoniques
- `06-protocol-reference.md` — Référence protocole

---

## 11. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **KHU_T = Colored Coin** : Structure UTXO identique à PIV
2. **Pas de burn direct** : Destruction uniquement via REDEEM
3. **Supply protégée** : Invariant C==U+Z empêche manipulation
4. **Fees en PIV** : Fees burn comme chaîne principale PIVX
5. **Namespace séparé** : 'K' + 'U' pour isolation LevelDB

Toute modification doit :
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date

**Statut :** ACTIF pour implémentation Phase 1-2

---

**FIN DU BLUEPRINT KHU COLORED COIN**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
