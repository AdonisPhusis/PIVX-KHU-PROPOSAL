# 03 — MINT / REDEEM BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit les opérations **MINT** et **REDEEM** pour KHU.

**MINT** : Créer KHU en verrouillant PIV (1:1)
**REDEEM** : Détruire KHU et récupérer PIV (1:1)

**RÈGLE ABSOLUE : MINT et REDEEM sont les SEULES opérations qui modifient C et U.**

---

## 1. RÈGLES FONDAMENTALES MINT/REDEEM

### 1.1 Pipeline Canonique KHU

```
PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
```

**MINT et REDEEM sont les portes d'entrée/sortie du système KHU.**

### 1.2 Invariant Sacré : C == U

```cpp
// AVANT toute opération :
assert(state.C == state.U);

// MINT : C+, U+ (atomique)
state.C += amount;
state.U += amount;

// APRÈS MINT :
assert(state.C == state.U);

// REDEEM : C-, U- (atomique)
state.C -= amount;
state.U -= amount;

// APRÈS REDEEM :
assert(state.C == state.U);
```

**INTERDICTION ABSOLUE : C et U ne peuvent JAMAIS être modifiés séparément.**

### 1.3 Ratio 1:1 (Immuable)

```
1 PIV = 1 KHU_T (toujours)
1 KHU_T = 1 PIV (toujours)

Fees en PIV (burn, comme PIVX standard)
Pas de slippage
Pas de taux de change
```

---

## 2. OPÉRATION MINT (PIV → KHU_T)

### 2.1 Description

**MINT** convertit PIV en KHU_T en ratio 1:1.

**Flux :**
1. Utilisateur possède PIV (native coin)
2. Transaction MINT consomme PIV input
3. PIV est **verrouillé** (provably unspendable output)
4. KHU_T UTXO est **créé** (colored coin)
5. État global : `C += amount`, `U += amount` (atomique)

### 2.2 Transaction MINT

**TxType :** `TxType::KHU_MINT`

**Structure :**
```cpp
struct CMintKHUPayload {
    CAmount amount;          // Montant PIV à verrouiller
    CTxDestination dest;     // Destinataire KHU_T

    SERIALIZE_METHODS(CMintKHUPayload, obj) {
        READWRITE(obj.amount, obj.dest);
    }
};
```

**Exemple transaction :**
```
Inputs:
  [0] 100 PIV (standard UTXO)

Outputs:
  [0] 100 PIV → OP_RETURN <proof-of-burn> (verrouillage)
  [1] 100 KHU_T → <recipient_address> (création KHU)
```

### 2.3 Validation MINT

**Fichier :** `src/khu/khu_mint.cpp`

```cpp
bool CheckKHUMint(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Vérifier TxType
    if (tx.nType != TxType::KHU_MINT)
        return state.Invalid("khu-mint-invalid-type");

    // 2. Vérifier payload
    CMintKHUPayload payload;
    if (!GetMintKHUPayload(tx, payload))
        return state.Invalid("khu-mint-missing-payload");

    // 3. Vérifier montant > 0
    if (payload.amount <= 0)
        return state.Invalid("khu-mint-invalid-amount");

    // 4. Vérifier inputs PIV suffisants
    CAmount total_input = 0;
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent())
            return state.Invalid("khu-mint-missing-input");
        total_input += coin.out.nValue;
    }

    if (total_input < payload.amount)
        return state.Invalid("khu-mint-insufficient-funds");

    // 5. Vérifier output 0 = proof-of-burn
    if (!tx.vout[0].scriptPubKey.IsUnspendable())
        return state.Invalid("khu-mint-invalid-burn");

    // 6. Vérifier output 1 = KHU_T
    if (tx.vout[1].nValue != payload.amount)
        return state.Invalid("khu-mint-amount-mismatch");

    // 7. Vérifier destination valide
    if (!IsValidDestination(payload.dest))
        return state.Invalid("khu-mint-invalid-destination");

    return true;
}
```

### 2.4 Application MINT

**Fichier :** `src/khu/khu_mint.cpp`

```cpp
bool ApplyKHUMint(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight) {
    // ⚠️ LOCK: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CMintKHUPayload payload;
    GetMintKHUPayload(tx, payload);

    CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants())
        return error("MINT: pre-invariant violation");

    // 3. DOUBLE MUTATION ATOMIQUE (C et U ensemble)
    // ⚠️ CRITICAL: Ces deux lignes doivent être adjacentes
    state.C += amount;  // Augmenter collateral
    state.U += amount;  // Augmenter supply

    // 4. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants())
        return error("MINT: post-invariant violation");

    // 5. Créer UTXO KHU_T
    CKHUUTXO newCoin;
    newCoin.amount = amount;
    newCoin.scriptPubKey = GetScriptForDestination(payload.dest);
    newCoin.nHeight = nHeight;
    newCoin.fStaked = false;
    newCoin.nStakeStartHeight = 0;

    view.AddKHUCoin(tx.GetHash(), 1, newCoin);  // Output index 1

    // 6. Log
    LogPrint(BCLog::KHU, "ApplyKHUMint: amount=%d C=%d U=%d height=%d\n",
             amount, state.C, state.U, nHeight);

    return true;
}
```

### 2.5 Impact sur État Global

```cpp
// AVANT MINT:
C = 0, U = 0, Cr = 0, Ur = 0

// User fait MINT de 100 PIV:
ApplyKHUMint(100 * COIN)

// APRÈS MINT:
C = 100, U = 100, Cr = 0, Ur = 0

// Invariants:
assert(C == U);  // ✅ 100 == 100
assert(Cr == Ur); // ✅ 0 == 0
```

---

## 3. OPÉRATION REDEEM (KHU_T → PIV)

### 3.1 Description

**REDEEM** convertit KHU_T en PIV en ratio 1:1.

**Flux :**
1. Utilisateur possède KHU_T (colored coin UTXO)
2. Transaction REDEEM consomme KHU_T input
3. KHU_T est **détruit** (spent)
4. PIV UTXO est **créé** (depuis collateral)
5. État global : `C -= amount`, `U -= amount` (atomique)

### 3.2 Transaction REDEEM

**TxType :** `TxType::KHU_REDEEM`

**Structure :**
```cpp
struct CRedeemKHUPayload {
    CAmount amount;          // Montant KHU_T à détruire
    CTxDestination dest;     // Destinataire PIV

    SERIALIZE_METHODS(CRedeemKHUPayload, obj) {
        READWRITE(obj.amount, obj.dest);
    }
};
```

**Exemple transaction :**
```
Inputs:
  [0] 100 KHU_T (colored coin UTXO)

Outputs:
  [0] 100 PIV → <recipient_address> (récupération PIV)
```

### 3.3 Validation REDEEM

**Fichier :** `src/khu/khu_redeem.cpp`

```cpp
bool CheckKHURedeem(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Vérifier TxType
    if (tx.nType != TxType::KHU_REDEEM)
        return state.Invalid("khu-redeem-invalid-type");

    // 2. Vérifier payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload))
        return state.Invalid("khu-redeem-missing-payload");

    // 3. Vérifier montant > 0
    if (payload.amount <= 0)
        return state.Invalid("khu-redeem-invalid-amount");

    // 4. Vérifier inputs KHU_T suffisants
    CAmount total_khu = 0;
    for (const auto& in : tx.vin) {
        CKHUUTXO khuCoin;
        if (!view.GetKHUCoin(in.prevout, khuCoin))
            return state.Invalid("khu-redeem-missing-input");

        // Vérifier pas staké
        if (khuCoin.fStaked)
            return state.Invalid("khu-redeem-staked-khu", "Cannot redeem staked KHU");

        total_khu += khuCoin.amount;
    }

    if (total_khu < payload.amount)
        return state.Invalid("khu-redeem-insufficient-khu");

    // 5. Vérifier output PIV
    if (tx.vout[0].nValue != payload.amount)
        return state.Invalid("khu-redeem-amount-mismatch");

    // 6. Vérifier destination valide
    if (!IsValidDestination(payload.dest))
        return state.Invalid("khu-redeem-invalid-destination");

    // 7. Vérifier collateral disponible
    if (state.C < payload.amount)
        return state.Invalid("khu-redeem-insufficient-collateral");

    return true;
}
```

### 3.4 Application REDEEM

**Fichier :** `src/khu/khu_redeem.cpp`

```cpp
bool ApplyKHURedeem(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight) {
    // ⚠️ LOCK: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CRedeemKHUPayload payload;
    GetRedeemKHUPayload(tx, payload);

    CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants())
        return error("REDEEM: pre-invariant violation");

    // 3. Vérifier collateral suffisant
    if (state.C < amount || state.U < amount)
        return error("REDEEM: insufficient C/U");

    // 4. DOUBLE MUTATION ATOMIQUE (C et U ensemble)
    // ⚠️ CRITICAL: Ces deux lignes doivent être adjacentes
    state.C -= amount;  // Diminuer collateral
    state.U -= amount;  // Diminuer supply

    // 5. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants())
        return error("REDEEM: post-invariant violation");

    // 6. Dépenser UTXO KHU_T
    for (const auto& in : tx.vin) {
        view.SpendKHUCoin(in.prevout);
    }

    // 7. Log
    LogPrint(BCLog::KHU, "ApplyKHURedeem: amount=%d C=%d U=%d height=%d\n",
             amount, state.C, state.U, nHeight);

    return true;
}
```

### 3.5 Impact sur État Global

```cpp
// AVANT REDEEM:
C = 100, U = 100, Cr = 0, Ur = 0

// User fait REDEEM de 50 KHU:
ApplyKHURedeem(50 * COIN)

// APRÈS REDEEM:
C = 50, U = 50, Cr = 0, Ur = 0

// Invariants:
assert(C == U);  // ✅ 50 == 50
assert(Cr == Ur); // ✅ 0 == 0
```

---

## 4. ATOMICITÉ CRITIQUE

### 4.1 Règle d'Atomicité

**Les mutations C et U doivent être atomiques :**

```cpp
// ✅ CORRECT : Atomique
state.C += amount;
state.U += amount;
// Pas d'instructions entre les deux

// ❌ INTERDIT : Non-atomique
state.C += amount;
SomeFunctionCall();  // ❌ Interruption interdite
state.U += amount;
```

### 4.2 Pas de Rollback Partiel

```cpp
// ❌ INTERDIT : Rollback partiel
state.C += amount;
if (some_condition) {
    state.C -= amount;  // ❌ Rollback interdit
    return false;
}
state.U += amount;

// ✅ CORRECT : Vérification AVANT mutation
if (!some_condition)
    return false;  // Reject avant mutation

state.C += amount;
state.U += amount;  // Mutation atomique
```

### 4.3 Verrouillage Obligatoire

```cpp
bool ApplyKHUMint(...) {
    // ⚠️ cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // Mutations C/U sous lock
    state.C += amount;
    state.U += amount;

    // Vérification invariants sous lock
    if (!state.CheckInvariants())
        return false;

    return true;
}
```

---

## 5. INTERDICTIONS ABSOLUES

### 5.1 Code Interdit

```cpp
// ❌ INTERDIT : Modifier C sans U
state.C += amount;  // ❌ Violation invariant

// ❌ INTERDIT : Modifier U sans C
state.U += amount;  // ❌ Violation invariant

// ❌ INTERDIT : Ratio différent de 1:1
state.C += amount;
state.U += amount * 2;  // ❌ C != U

// ❌ INTERDIT : Déduire frais des changements d'état C/U
CAmount fee = amount * 0.01;
state.C += (amount - fee);  // ❌ C et U doivent changer du montant complet (1:1)
state.U += (amount - fee);  // Les fees PIV sont payés séparément (burn), pas déduits de C/U

// ❌ INTERDIT : Brûler KHU en dehors de REDEEM
if (some_condition) {
    state.U -= amount;  // ❌ Seul REDEEM peut brûler
}

// ❌ INTERDIT : MINT sans verrouillage PIV
state.C += amount;  // ❌ PIV doit être provably locked
state.U += amount;

// ❌ INTERDIT : REDEEM sans destruction KHU_T
state.C -= amount;  // ❌ KHU_T doit être dépensé
state.U -= amount;
```

### 5.2 Scénarios Interdits

```
❌ MINT sans PIV input
❌ MINT sans proof-of-burn PIV
❌ REDEEM sans KHU_T input
❌ REDEEM de KHU_T staké (fStaked = true)
❌ MINT/REDEEM avec frais
❌ MINT/REDEEM avec slippage
❌ MINT/REDEEM avec ratio ≠ 1:1
❌ BURN KHU sans REDEEM
❌ Modifier C/U en dehors MINT/REDEEM
```

---

## 6. TESTS

### 6.1 Tests Unitaires : src/test/khu_mint_redeem_tests.cpp

```cpp
#include <boost/test/unit_test.hpp>
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"

BOOST_AUTO_TEST_SUITE(khu_mint_redeem_tests)

/**
 * Test 1 : MINT basique
 */
BOOST_AUTO_TEST_CASE(test_mint_basic)
{
    KhuGlobalState state;
    state.C = 0;
    state.U = 0;

    CAmount amount = 100 * COIN;

    // Apply MINT
    state.C += amount;
    state.U += amount;

    // Vérifier invariants
    BOOST_CHECK_EQUAL(state.C, 100 * COIN);
    BOOST_CHECK_EQUAL(state.U, 100 * COIN);
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 2 : REDEEM basique
 */
BOOST_AUTO_TEST_CASE(test_redeem_basic)
{
    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    CAmount amount = 50 * COIN;

    // Apply REDEEM
    state.C -= amount;
    state.U -= amount;

    // Vérifier invariants
    BOOST_CHECK_EQUAL(state.C, 50 * COIN);
    BOOST_CHECK_EQUAL(state.U, 50 * COIN);
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 3 : MINT puis REDEEM (round-trip)
 */
BOOST_AUTO_TEST_CASE(test_mint_redeem_roundtrip)
{
    KhuGlobalState state;
    state.C = 0;
    state.U = 0;

    CAmount amount = 100 * COIN;

    // MINT
    state.C += amount;
    state.U += amount;
    BOOST_CHECK(state.CheckInvariants());

    // REDEEM
    state.C -= amount;
    state.U -= amount;
    BOOST_CHECK(state.CheckInvariants());

    // Retour à zéro
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
}

/**
 * Test 4 : Violation invariant (C modifié seul)
 */
BOOST_AUTO_TEST_CASE(test_invariant_violation)
{
    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    // Modifier C seul (violation)
    state.C += 10 * COIN;

    // Invariant doit échouer
    BOOST_CHECK(!state.CheckInvariants());
}

/**
 * Test 5 : REDEEM insuffisant
 */
BOOST_AUTO_TEST_CASE(test_redeem_insufficient)
{
    KhuGlobalState state;
    state.C = 50 * COIN;
    state.U = 50 * COIN;

    CAmount amount = 100 * COIN;  // Plus que disponible

    // Vérifier avant REDEEM
    bool can_redeem = (state.C >= amount && state.U >= amount);
    BOOST_CHECK(!can_redeem);  // Doit échouer
}

BOOST_AUTO_TEST_SUITE_END()
```

### 6.2 Tests Fonctionnels : test/functional/khu_mint_redeem.py

```python
#!/usr/bin/env python3
"""
Test KHU MINT and REDEEM operations
"""

from test_framework.test_framework import PivxTestFramework
from test_framework.util import assert_equal, assert_raises_rpc_error

class KHUMintRedeemTest(PivxTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        # Generate blocks to get PIV
        node.generate(101)

        # Initial state
        state = node.getkhustate()
        assert_equal(state['C'], 0)
        assert_equal(state['U'], 0)

        # Test 1: MINT 100 PIV → 100 KHU
        self.log.info("Testing MINT...")
        tx_mint = node.mintkhu(100)
        node.generate(1)

        state = node.getkhustate()
        assert_equal(state['C'], 100)
        assert_equal(state['U'], 100)
        assert_equal(state['C'], state['U'])  # Invariant

        balance = node.getkhubalance()
        assert_equal(balance, 100)

        # Test 2: REDEEM 50 KHU → 50 PIV
        self.log.info("Testing REDEEM...")
        tx_redeem = node.redeemkhu(50)
        node.generate(1)

        state = node.getkhustate()
        assert_equal(state['C'], 50)
        assert_equal(state['U'], 50)
        assert_equal(state['C'], state['U'])  # Invariant

        balance = node.getkhubalance()
        assert_equal(balance, 50)

        # Test 3: REDEEM trop (doit échouer)
        self.log.info("Testing REDEEM insufficient...")
        assert_raises_rpc_error(-6, "Insufficient KHU balance",
                              node.redeemkhu, 100)

        self.log.info("MINT/REDEEM tests passed ✅")

if __name__ == '__main__':
    KHUMintRedeemTest().main()
```

---

## 7. CHECKLIST IMPLÉMENTATION PHASE 2

### 7.1 Code

- [ ] `khu_mint.h` / `khu_mint.cpp` implémentés
- [ ] `khu_redeem.h` / `khu_redeem.cpp` implémentés
- [ ] `CMintKHUPayload` structure définie
- [ ] `CRedeemKHUPayload` structure définie
- [ ] `CheckKHUMint()` validation complète
- [ ] `CheckKHURedeem()` validation complète
- [ ] `ApplyKHUMint()` mutation atomique C/U
- [ ] `ApplyKHURedeem()` mutation atomique C/U
- [ ] Verrouillage `cs_khu` dans toutes mutations
- [ ] `CheckInvariants()` appelé après chaque mutation

### 7.2 RPC

- [ ] `mintkhu <amount>` implémenté
- [ ] `redeemkhu <amount>` implémenté
- [ ] `getkhubalance` implémenté
- [ ] Gestion erreurs (insufficient funds, etc.)

### 7.3 Tests

- [ ] `khu_mint_redeem_tests.cpp` (unit tests)
- [ ] `khu_mint_redeem.py` (functional tests)
- [ ] Test MINT basique
- [ ] Test REDEEM basique
- [ ] Test round-trip (MINT → REDEEM)
- [ ] Test invariant violation
- [ ] Test REDEEM insuffisant
- [ ] Test REDEEM KHU staké (doit échouer)

### 7.4 Vérifications Anti-Dérive

```bash
# Vérifier atomicité C/U
grep -A1 "state.C +=" src/khu/khu_mint.cpp | grep "state.U +="
grep -A1 "state.C -=" src/khu/khu_redeem.cpp | grep "state.U -="

# Si pas de match adjacent → STOP et corriger

# Vérifier AssertLockHeld
grep "AssertLockHeld(cs_khu)" src/khu/khu_mint.cpp
grep "AssertLockHeld(cs_khu)" src/khu/khu_redeem.cpp

# Si absent → STOP et corriger

# Vérifier CheckInvariants
grep "CheckInvariants()" src/khu/khu_mint.cpp
grep "CheckInvariants()" src/khu/khu_redeem.cpp

# Si absent → STOP et corriger
```

---

## 8. RÉFÉRENCES

**Documents liés :**
- `01-blueprint-master-flow.md` — Section 3.2 (Phase 2 MINT/REDEEM)
- `02-canonical-specification.md` — Sections 3.1, 3.2 (MINT/REDEEM spec)
- `03-architecture-overview.md` — Section 4 (KHU operations)
- `06-protocol-reference.md` — Section 14 (code C++ MINT/REDEEM)

---

## 9. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **Atomicité C/U** : Mutations toujours ensemble (jamais séparées)
2. **Ratio 1:1** : 1 PIV = 1 KHU_T (immuable)
3. **Fees en PIV** : Transactions paient fees PIV (burn), mais C/U changent 1:1 sans déduction
4. **Proof-of-burn** : PIV doit être provably locked (MINT)
5. **Destruction KHU** : KHU_T doit être dépensé (REDEEM)
6. **Pas de REDEEM staké** : fStaked = true interdit

Toute modification doit :
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date

**Statut :** ACTIF pour implémentation Phase 2 (après Phase 1 validée)

---

**FIN DU BLUEPRINT MINT / REDEEM**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
