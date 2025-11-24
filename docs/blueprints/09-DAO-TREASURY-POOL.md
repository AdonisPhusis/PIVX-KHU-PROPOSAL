# 09 — DAO TREASURY POOL BLUEPRINT

**Date:** 2025-11-24
**Version:** 1.0
**Status:** CANONIQUE

---

## TABLE DES MATIÈRES

1. [Vision & Objectifs](#1-vision--objectifs)
2. [Architecture Système](#2-architecture-système)
3. [Spécifications Techniques](#3-spécifications-techniques)
4. [Implémentation Code](#4-implémentation-code)
5. [Tests & Validation](#5-tests--validation)
6. [Roadmap & Déploiement](#6-roadmap--déploiement)
7. [FAQ](#7-faq)

---

## 1. VISION & OBJECTIFS

### 1.1 Le Problème

**Après l'année 6, l'émission PIVX tombe à 0 PIV/bloc.**

```
Année 0-6:  Block reward DAO = 6→5→4→3→2→1→0 PIV/bloc
Année 6+:   Block reward DAO = 0 PIV/bloc ❌

→ Comment financer la DAO après l'année 6?
```

### 1.2 La Solution: Pool Interne T

**Créer un "pot commun" DAO qui s'auto-alimente perpétuellement.**

```
┌────────────────────────────────────────────────┐
│ T = Treasury Pool (Pool Interne)              │
├────────────────────────────────────────────────┤
│ • Variable dans KhuGlobalState                 │
│ • S'incrémente automatiquement tous les 4 mois │
│ • T += 0.5% × (U + Ur)                         │
│ • Accumulation perpétuelle (pas de burn)       │
│ • Propositions DAO peuvent dépenser depuis T   │
└────────────────────────────────────────────────┘
```

### 1.3 Principes Fondamentaux

```
✅ AUTOMATIQUE:  T s'incrémente sans intervention humaine
✅ MATHÉMATIQUE: Formule fixe 0.5% × (U + Ur)
✅ TRANSPARENT:  Visible dans KhuGlobalState
✅ GOUVERNÉ:     Dépenses approuvées par vote MN
✅ PERPÉTUEL:    Continue indéfiniment (après année 6)
✅ SIMPLE:       ~30 lignes de code core
```

---

## 2. ARCHITECTURE SYSTÈME

### 2.1 Vue d'Ensemble

```
╔═══════════════════════════════════════════════════════════╗
║                   POOL T - FLUX COMPLET                   ║
╚═══════════════════════════════════════════════════════════╝

    ENTRÉES                T (Pool)              SORTIES
┌──────────────┐      ┌────────────┐      ┌──────────────┐
│ Tous les     │      │            │      │ Proposition  │
│ 172800 blocs │ ───> │     T      │ ───> │ votée YES    │
│              │      │            │      │ par MN       │
│ +0.5%(U+Ur)  │      │ Accumule   │      │              │
└──────────────┘      │ perpétuel  │      │ T -= montant │
  Automatique         └────────────┘      └──────────────┘
                                            Gouverné (MN)
```

### 2.2 Deux Phases

#### **Phase 6.3 (MAINTENANT): Accumulation**
```cpp
// Tous les 172800 blocs:
if (IsDaoCycleBoundary(nHeight)) {
    delta = (U + Ur) × 0.5%;
    T += delta;  // Accumulation automatique
}
```

**Implémenté:** Création de T + incrémentation automatique
**Non implémenté:** Système de propositions (Phase 7)

#### **Phase 7 (FUTUR): Dépenses Gouvernées**
```cpp
// Quand une proposition est approuvée (vote MN):
if (proposal.IsApproved()) {
    T -= proposal.amount;  // Dépense depuis le pool
    // Payer la proposition...
}
```

**À implémenter plus tard:** Propositions + votes MN + dépenses

### 2.3 KhuGlobalState v3.0

```cpp
struct KhuGlobalState {
    // Stable pipeline
    int64_t C;   // collateral backing
    int64_t U;   // supply

    // Reward pipeline
    int64_t Cr;  // reward collateral
    int64_t Ur;  // reward rights

    // DAO treasury (NOUVEAU - Phase 6)
    int64_t T;   // DAO pool accumulator

    // Governance
    uint16_t R_annual;        // Current annual rate
    uint16_t R_MAX_dynamic;   // Dynamic maximum

    // ... metadata ...
};
```

### 2.4 Invariants

```cpp
Invariants existants (inchangés):
  C == U     ✅ (stable pipeline)
  Cr == Ur   ✅ (reward pipeline)

Nouvel invariant:
  T >= 0     ✅ (pool non-négatif)
```

**Note:** T peut diminuer si propositions dépensent (Phase 7), mais ne peut jamais être négatif.

### 2.5 Timeline Complète

```
┌─────────────────────────────────────────────────────┐
│ Année 0-6: DOUBLE financement DAO                   │
├─────────────────────────────────────────────────────┤
│ Source 1: Block reward DAO (6→0 PIV/bloc)           │
│   • Via émission PIVX (Blueprint 01)                │
│   • Payé chaque bloc                                │
│   • Termine à l'année 6                             │
│                                                     │
│ Source 2: Pool T accumulation                       │
│   • +0.5% × (U+Ur) / 4 mois                         │
│   • Pool interne (pas de paiement)                  │
│   • Commence dès activation                         │
│   • Continue perpétuellement                        │
│                                                     │
│ Total DAO = Source 1 + accumulation T               │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Année 6+: UN SEUL système DAO                       │
├─────────────────────────────────────────────────────┤
│ Source 1: Block reward = 0 PIV ❌                   │
│                                                     │
│ Source 2: Pool T                                    │
│   • Accumulation: +0.5% × (U+Ur) / 4 mois          │
│   • Dépenses: Propositions votées (Phase 7)        │
│   • Seul mécanisme de financement DAO              │
│                                                     │
│ Total DAO = Pool T uniquement                       │
└─────────────────────────────────────────────────────┘
```

### 2.6 Synchronisation avec DOMC

```
Cycle DOMC (R% governance): 172800 blocs (4 mois)
Cycle DAO (T increment):    172800 blocs (4 mois)

Parfaitement alignés:

Bloc 0       → DOMC Cycle 0 + T initial = 0
Bloc 172800  → DOMC Cycle 1 + T += delta₁
Bloc 345600  → DOMC Cycle 2 + T += delta₂
Bloc 518400  → DOMC Cycle 3 + T += delta₃
...
```

**Avantage:** Cohérence des cycles, simplification du code.

---

## 3. SPÉCIFICATIONS TECHNIQUES

### 3.1 Constantes

```cpp
// khu_dao.h
static const uint32_t DAO_CYCLE_LENGTH = 172800;  // 4 mois (identique DOMC)
```

### 3.2 Fonctions Core

#### **A) IsDaoCycleBoundary()**

```cpp
/**
 * Vérifier si on est à un cycle boundary DAO
 *
 * @param nHeight Height du bloc actuel
 * @param nActivationHeight Height activation KHU
 * @return true si c'est la fin d'un cycle DAO (172800, 345600, ...)
 */
bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight);
```

**Implémentation:**
```cpp
bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}
```

#### **B) CalculateDaoTreasuryDelta()**

```cpp
/**
 * Calculer delta à ajouter à T
 *
 * Formule: (U + Ur) × 0.5%
 *
 * @param U Supply KHU
 * @param Ur Reward rights KHU
 * @return Delta = (U + Ur) × 5 / 1000
 *
 * OVERFLOW PROTECTION: Utilise __int128
 */
int64_t CalculateDaoTreasuryDelta(int64_t U, int64_t Ur);
```

**Implémentation:**
```cpp
int64_t CalculateDaoTreasuryDelta(int64_t U, int64_t Ur)
{
    // Delta = (U + Ur) × 0.5% = (U + Ur) × 5 / 1000

    __int128 total = (__int128)U + (__int128)Ur;
    __int128 delta = (total * 5) / 1000;

    // Overflow protection
    if (delta < 0 || delta > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDelta overflow (U=%lld, Ur=%lld)\n", U, Ur);
        return 0;
    }

    return (int64_t)delta;
}
```

### 3.3 Sécurité & Protection

```cpp
Protections implémentées:
✅ Overflow (__int128 arithmetic)
✅ Underflow (delta < 0 check)
✅ MAX_MONEY validation
✅ Invariant T >= 0
✅ Consensus-driven (pas de clé privée)

Attaques impossibles:
❌ Double-spend T (consensus state)
❌ Falsifier T (consensus rules)
❌ Manipuler delta (formule fixe)
❌ Skip cycles (cycle detection automatique)
```

### 3.4 Exemples Numériques

```
Exemple 1: Activation (année 0)
  U = 1,000,000 KHU
  Ur = 500,000 KHU
  delta = (1,000,000 + 500,000) × 0.005 = 7,500 KHU
  T: 0 → 7,500

Exemple 2: Cycle 2 (année 1)
  U = 3,000,000 KHU
  Ur = 1,500,000 KHU
  delta = (3,000,000 + 1,500,000) × 0.005 = 22,500 KHU
  T: 7,500 → 30,000

Exemple 3: Année 10
  U = 10,000,000 KHU
  Ur = 5,000,000 KHU
  delta = (10,000,000 + 5,000,000) × 0.005 = 75,000 KHU
  T: (accumulé) → T + 75,000
```

**Note:** Les valeurs U/Ur évoluent avec le temps (MINT/REDEEM/STAKE/UNSTAKE), donc T s'adapte automatiquement.

---

## 4. IMPLÉMENTATION CODE

### 4.1 Fichiers à Créer

```
src/khu/
├── khu_dao.h        (~20 lignes)
└── khu_dao.cpp      (~30 lignes)
```

### 4.2 Fichiers à Modifier

```
src/khu/khu_state.h       (ajouter T, +10 lignes)
src/validation.cpp        (intégrer T update, +10 lignes)
```

**Total: ~70 lignes de code ajoutées!**

### 4.3 Implémentation Détaillée

#### **A) khu_state.h** - Ajouter T

```cpp
struct KhuGlobalState {
    int64_t C;
    int64_t U;
    int64_t Cr;
    int64_t Ur;
    int64_t T;   // ← NOUVEAU

    uint16_t R_annual;
    uint16_t R_MAX_dynamic;

    uint32_t last_domc_height;
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;
    uint32_t domc_phase_length;

    uint32_t last_yield_update_height;

    uint32_t nHeight;
    uint256 hashPrevState;
    uint256 hashBlock;

    // Constructeur
    KhuGlobalState() :
        C(0), U(0), Cr(0), Ur(0), T(0),  // ← Initialiser T
        R_annual(0), R_MAX_dynamic(0),
        last_domc_height(0), domc_cycle_start(0),
        domc_cycle_length(172800), domc_phase_length(20160),
        last_yield_update_height(0),
        nHeight(0), hashPrevState(), hashBlock() {}

    // Sérialisation
    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(C);
        READWRITE(U);
        READWRITE(Cr);
        READWRITE(Ur);
        READWRITE(T);  // ← NOUVEAU

        READWRITE(R_annual);
        READWRITE(R_MAX_dynamic);

        READWRITE(last_domc_height);
        READWRITE(domc_cycle_start);
        READWRITE(domc_cycle_length);
        READWRITE(domc_phase_length);

        READWRITE(last_yield_update_height);

        READWRITE(nHeight);
        READWRITE(hashPrevState);
        READWRITE(hashBlock);
    }

    // Invariants
    bool CheckInvariants() const {
        if (C != U) {
            LogPrintf("ERROR: Invariant C==U broken (C=%lld, U=%lld)\n", C, U);
            return false;
        }
        if (Cr != Ur) {
            LogPrintf("ERROR: Invariant Cr==Ur broken (Cr=%lld, Ur=%lld)\n", Cr, Ur);
            return false;
        }
        if (T < 0) {  // ← NOUVEAU
            LogPrintf("ERROR: Invariant T>=0 broken (T=%lld)\n", T);
            return false;
        }
        return true;
    }
};
```

#### **B) khu_dao.h** - Header

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#ifndef PIVX_KHU_DAO_H
#define PIVX_KHU_DAO_H

#include <cstdint>

/**
 * KHU DAO TREASURY POOL (T) — Phase 6
 *
 * T = pool interne qui accumule les "PIV DAO" créés automatiquement
 *
 * RÈGLES:
 *   Tous les 172800 blocs (4 mois):
 *     - Calculer: delta = 0.5% × (U + Ur)
 *     - Incrémenter: T += delta
 *     - Pas de burn, accumulation perpétuelle
 *
 * PHASE 6.3 (maintenant): Création T + accumulation automatique
 * PHASE 7 (futur):        Propositions DAO peuvent dépenser depuis T (vote MN)
 *
 * T est une variable dans KhuGlobalState.
 * AUCUNE adresse, AUCUNE transaction, AUCUN paiement externe.
 */

// Constantes
static const uint32_t DAO_CYCLE_LENGTH = 172800;  // 4 mois (aligné DOMC)

/**
 * Vérifier si c'est un bloc "cycle boundary" DAO
 *
 * @param nHeight Height du bloc actuel
 * @param nActivationHeight Height activation KHU
 * @return true si c'est la fin d'un cycle DAO
 */
bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * Calculer delta à ajouter à T
 *
 * Formule: (U + Ur) × 0.5%
 *
 * OVERFLOW PROTECTION: Utilise __int128
 *
 * @param U Supply KHU
 * @param Ur Reward rights KHU
 * @return Delta = (U + Ur) × 5 / 1000
 */
int64_t CalculateDaoTreasuryDelta(int64_t U, int64_t Ur);

#endif // PIVX_KHU_DAO_H
```

#### **C) khu_dao.cpp** - Implémentation

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#include "khu/khu_dao.h"
#include "amount.h"
#include "logging.h"

bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}

int64_t CalculateDaoTreasuryDelta(int64_t U, int64_t Ur)
{
    // Delta = (U + Ur) × 0.5% = (U + Ur) × 5 / 1000

    __int128 total = (__int128)U + (__int128)Ur;
    __int128 delta = (total * 5) / 1000;

    // Overflow protection
    if (delta < 0 || delta > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDelta overflow (U=%lld, Ur=%lld)\n", U, Ur);
        return 0;
    }

    return (int64_t)delta;
}
```

#### **D) validation.cpp (ConnectBlock)** - Intégration

```cpp
// Après ligne ~1700 (après ProcessKHUTransactions, avant FinalizeDOMCCycle)

// ────────────────────────────────────────────────────────────
// 5. DAO TREASURY UPDATE (Phase 6 - tous les 172800 blocs)
// ────────────────────────────────────────────────────────────
if (IsDaoCycleBoundary(nHeight, consensus.nKhuActivationHeight)) {
    int64_t delta = CalculateDaoTreasuryDelta(newState.U, newState.Ur);

    if (delta > 0) {
        newState.T += delta;

        LogPrint(BCLog::KHU,
                 "DAO Treasury update @ height %u: delta = %lld, total T = %lld\n",
                 nHeight, delta, newState.T);
    }
}
```

### 4.4 Ordre Canonique ConnectBlock

```cpp
bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, ...)
{
    // ... (code existant)

    if (IsKHUActive(nHeight)) {

        // 1. Load prevState
        KhuGlobalState prevState;
        if (!LoadKhuState(pindex->pprev->GetBlockHash(), prevState)) {
            return state.Invalid(REJECT_INVALID, "khu-state-missing");
        }

        KhuGlobalState newState = prevState;
        newState.nHeight = nHeight;
        newState.hashBlock = block.GetHash();

        // 2. APPLY DAILY YIELD
        if (!ApplyDailyYield(newState, nHeight, view)) {
            return state.Invalid(REJECT_INVALID, "khu-yield-failed");
        }

        // 3. APPLY TX KHU (MINT/REDEEM/STAKE/UNSTAKE)
        for (const auto& tx : block.vtx) {
            if (!ProcessKHUTransaction(tx, newState, view)) {
                return state.Invalid(REJECT_INVALID, "khu-tx-failed");
            }
        }

        // 4. APPLY BLOCK REWARD (staker + MN + DAO from emission)
        // (code existant - émission PIVX)

        // 5. DAO TREASURY UPDATE ← NOUVEAU
        if (IsDaoCycleBoundary(nHeight, consensus.nKhuActivationHeight)) {
            int64_t delta = CalculateDaoTreasuryDelta(newState.U, newState.Ur);
            if (delta > 0) {
                newState.T += delta;
                LogPrint(BCLog::KHU, "DAO Treasury: T += %lld (total = %lld)\n",
                         delta, newState.T);
            }
        }

        // 6. FINALIZE DOMC CYCLE (R% update)
        if (ShouldFinalizeDOMCCycle(newState, nHeight)) {
            if (!FinalizeDOMCCycle(newState, nHeight)) {
                return state.Invalid(REJECT_INVALID, "khu-domc-failed");
            }
        }

        // 7. CHECK INVARIANTS (C==U, Cr==Ur, T>=0)
        if (!newState.CheckInvariants()) {
            return state.Invalid(REJECT_INVALID, "khu-invariants-broken");
        }

        // 8. PERSIST STATE
        if (!PersistKhuState(newState)) {
            return state.Invalid(REJECT_INVALID, "khu-persist-failed");
        }
    }

    // ... (reste du code)
}
```

---

## 5. TESTS & VALIDATION

### 5.1 Tests Unitaires (5 tests)

```cpp
// src/test/khu_dao_tests.cpp

#include <boost/test/unit_test.hpp>
#include "khu/khu_dao.h"
#include "khu/khu_state.h"

BOOST_AUTO_TEST_SUITE(khu_dao_tests)

/**
 * Test 1: Détection cycle boundary
 */
BOOST_AUTO_TEST_CASE(dao_cycle_boundary)
{
    // Vérifier détection correcte des cycles
    BOOST_CHECK(IsDaoCycleBoundary(172800, 0));
    BOOST_CHECK(IsDaoCycleBoundary(345600, 0));
    BOOST_CHECK(IsDaoCycleBoundary(518400, 0));

    // Vérifier que les autres blocs ne sont pas détectés
    BOOST_CHECK(!IsDaoCycleBoundary(0, 0));
    BOOST_CHECK(!IsDaoCycleBoundary(100, 0));
    BOOST_CHECK(!IsDaoCycleBoundary(172799, 0));
    BOOST_CHECK(!IsDaoCycleBoundary(172801, 0));
}

/**
 * Test 2: Calcul delta correct
 */
BOOST_AUTO_TEST_CASE(dao_delta_calculation)
{
    int64_t U = 1000000 * COIN;
    int64_t Ur = 500000 * COIN;

    // Delta = 0.5% × (1M + 0.5M) = 0.5% × 1.5M = 7500 KHU
    int64_t expected = (1500000 * COIN * 5) / 1000;
    int64_t actual = CalculateDaoTreasuryDelta(U, Ur);

    BOOST_CHECK_EQUAL(actual, expected);
}

/**
 * Test 3: Accumulation T sur plusieurs cycles
 */
BOOST_AUTO_TEST_CASE(dao_t_accumulation)
{
    KhuGlobalState state;
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    state.T = 0;

    // Cycle 1
    int64_t delta1 = CalculateDaoTreasuryDelta(state.U, state.Ur);
    state.T += delta1;
    int64_t t_after_cycle1 = state.T;

    BOOST_CHECK_EQUAL(state.T, delta1);
    BOOST_CHECK(state.CheckInvariants());  // T >= 0

    // Cycle 2 (U/Ur changent)
    state.U = 1200000 * COIN;
    state.Ur = 600000 * COIN;
    int64_t delta2 = CalculateDaoTreasuryDelta(state.U, state.Ur);
    state.T += delta2;

    BOOST_CHECK_EQUAL(state.T, delta1 + delta2);
    BOOST_CHECK(state.T > t_after_cycle1);  // T augmente
    BOOST_CHECK(state.CheckInvariants());

    // Cycle 3
    state.U = 2000000 * COIN;
    state.Ur = 1000000 * COIN;
    int64_t delta3 = CalculateDaoTreasuryDelta(state.U, state.Ur);
    state.T += delta3;

    BOOST_CHECK_EQUAL(state.T, delta1 + delta2 + delta3);
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 4: Protection overflow
 */
BOOST_AUTO_TEST_CASE(dao_overflow_protection)
{
    // Cas overflow U + Ur
    int64_t U = MAX_MONEY;
    int64_t Ur = MAX_MONEY;

    int64_t delta = CalculateDaoTreasuryDelta(U, Ur);

    // Should return 0 (overflow detected)
    BOOST_CHECK_EQUAL(delta, 0);
}

/**
 * Test 5: Supply zéro
 */
BOOST_AUTO_TEST_CASE(dao_zero_supply)
{
    int64_t delta = CalculateDaoTreasuryDelta(0, 0);

    BOOST_CHECK_EQUAL(delta, 0);

    // État avec T=0 devrait être valide
    KhuGlobalState state;
    state.T = 0;
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_SUITE_END()
```

### 5.2 Tests Fonctionnels

```python
#!/usr/bin/env python3
# test/functional/khu_dao_treasury.py
"""
Test KHU DAO Treasury Pool (T)
"""

from test_framework.test_framework import PivxTestFramework
from test_framework.util import assert_equal, assert_greater_than

DAO_CYCLE_LENGTH = 172800

class KhuDaoTreasuryTest(PivxTestFramework):
    def set_test_params(self):
        self.num_nodes = 1
        self.setup_clean_chain = True

    def run_test(self):
        node = self.nodes[0]

        # Test 1: T initial = 0
        self.log.info("Test T initial...")
        state = node.getkhustate()
        assert_equal(state['T'], 0)

        # Test 2: T s'incrémente au premier cycle
        self.log.info("Test premier cycle...")
        node.generate(DAO_CYCLE_LENGTH)

        state = node.getkhustate()
        U = state['U']
        Ur = state['Ur']
        T = state['T']

        expected_delta = (U + Ur) * 0.005
        assert_greater_than(T, 0)
        assert_equal(T, expected_delta)  # (approximativement)

        # Test 3: T continue d'augmenter
        self.log.info("Test deuxième cycle...")
        t_before = T

        node.generate(DAO_CYCLE_LENGTH)

        state = node.getkhustate()
        T_after = state['T']

        assert_greater_than(T_after, t_before)

        # Test 4: T ne diminue jamais (Phase 6)
        self.log.info("Test T ne diminue pas...")
        for _ in range(3):
            t_before = node.getkhustate()['T']
            node.generate(DAO_CYCLE_LENGTH)
            t_after = node.getkhustate()['T']
            assert_greater_than(t_after, t_before)

        self.log.info("DAO Treasury tests passed ✅")

if __name__ == '__main__':
    KhuDaoTreasuryTest().main()
```

### 5.3 Checklist Validation

```
Avant merge Phase 6.3:
□ Tests unitaires passent (5/5)
□ Tests fonctionnels passent
□ T s'incrémente correctement tous les 172800 blocs
□ Formule 0.5% × (U+Ur) correcte
□ Protection overflow testée
□ Invariant T >= 0 vérifié
□ Sérialisation T fonctionne (state persistence)
□ Pas de régression autres fonctionnalités KHU
□ Code review par architecte
□ Documentation complète
```

---

## 6. ROADMAP & DÉPLOIEMENT

### 6.1 Phase 6.3: Accumulation T (MAINTENANT)

```
Semaine 1: Implémentation
  □ Modifier khu_state.h (ajouter T)
  □ Créer khu_dao.h/cpp (2 fonctions)
  □ Intégrer ConnectBlock (update T)

Semaine 2: Tests
  □ Tests unitaires (5 tests)
  □ Tests fonctionnels (4 tests)
  □ Tests regtest complets

Semaine 3: Review & Merge
  □ Code review
  □ Documentation
  □ Merge branch
```

**Durée estimée: 3 semaines**

### 6.2 Phase 7: Dépenses DAO (FUTUR)

```
À implémenter plus tard:

1. Système de Propositions
   - CKhuDAOProposal (name, description, payee, amount, cycle)
   - Soumission propositions (RPC)
   - Stockage propositions (mapProposals)

2. Système de Votes MN
   - CKhuDAOVote (proposalHash, direction YES/NO, signature MN)
   - Vote MN (RPC)
   - Comptage votes

3. Dépenses depuis T
   - GetWinningProposal() (plus de votes YES)
   - SpendFromTreasury(proposal)
   - T -= proposal.amount
   - Paiement propositions approuvées

4. Tests
   - Propositions création/validation
   - Votes MN
   - Dépenses T
   - Cas edge (T insuffisant, etc.)
```

**Note:** Phase 7 sera détaillée dans un blueprint séparé.

### 6.3 Critères de Succès Phase 6.3

```
✅ T créé dans KhuGlobalState
✅ T s'incrémente automatiquement tous les 172800 blocs
✅ Formule correcte: delta = 0.5% × (U + Ur)
✅ Invariant T >= 0 respecté
✅ Pas de transaction, pas d'adresse (pool interne)
✅ Synchronisé avec DOMC (cycles alignés)
✅ Tests 100% green
✅ Documentation complète
```

---

## 7. FAQ

### 7.1 Questions Générales

**Q: Pourquoi T et pas T/Tr?**
A: Simplicité. T suffit comme compteur. Tr serait redondant (T==Tr toujours dans Phase 6).

**Q: T est-il de l'argent "magique" créé de rien?**
A: Oui et non. T représente des PIV "réservés" pour la DAO, mais ils n'existent pas encore comme UTXO. C'est une réserve mathématique, comme la FED qui "imprime" mais ici transparent et gouverné.

**Q: T augmente la supply PIV?**
A: Non, dans Phase 6. T est juste un compteur. Quand des propositions dépenseront depuis T (Phase 7), là oui, des PIV seront créés (ou débloqués).

**Q: Peut-on auditer T?**
A: Oui! T est dans KhuGlobalState, visible par tous les nœuds. Chacun peut vérifier:
```bash
pivx-cli getkhustate
# {
#   "C": ...,
#   "U": ...,
#   "T": 123456789,  ← Visible
#   ...
# }
```

**Q: T peut-il diminuer?**
A: Dans Phase 6, non (accumulation uniquement). Dans Phase 7, oui, si propositions dépensent depuis T.

**Q: Que se passe-t-il si T devient négatif?**
A: Impossible. L'invariant `T >= 0` est vérifié dans CheckInvariants(). Une proposition ne peut pas dépenser plus que T disponible (Phase 7).

### 7.2 Questions Techniques

**Q: Pourquoi 0.5% et pas un autre pourcentage?**
A: Compromis entre financement suffisant et inflation contrôlée. 0.5% tous les 4 mois = ~1.5% par an (comparable inflation ciblée économies stables).

**Q: Pourquoi synchronisé avec DOMC (172800 blocs)?**
A: Cohérence des cycles, simplification du code. Les deux systèmes (R% governance + DAO) utilisent le même cycle de 4 mois.

**Q: Peut-on changer la formule 0.5%?**
A: Oui, mais nécessite un hard fork (consensus change). La formule est codée en dur dans CalculateDaoTreasuryDelta().

**Q: T est-il sérialisé (persistence)?**
A: Oui! T est dans KhuGlobalState qui est sérialisé dans la blockchain. Si un nœud redémarre, T est récupéré depuis l'état persisté.

**Q: Que se passe-t-il en cas de reorg?**
A: T est recalculé lors de la reconnexion des blocs (comme C/U/Cr/Ur). Si un bloc DAO cycle est reorg, T est ajusté correctement.

### 7.3 Questions Économiques

**Q: T ne crée-t-il pas une inflation infinie?**
A: Dans Phase 6, T est juste un compteur (pas d'inflation réelle). Dans Phase 7, les dépenses depuis T créeront de l'inflation, mais:
- Contrôlée (vote MN requis)
- Transparente (on-chain)
- Prévisible (formule fixe 0.5%)

**Q: Comment les propositions seront-elles votées (Phase 7)?**
A: Par les masternodes (comme système Budget PIVX actuel), mais au lieu de payer depuis block reward, on déduit de T.

**Q: Peut-on "sauver" des PIV dans T?**
A: Oui! Si peu de propositions sont approuvées, T s'accumule indéfiniment. C'est une réserve pour la DAO.

---

## 8. ANNEXES

### 8.1 Comparaison Systèmes DAO

| Aspect | PIVX Budget | DAO Pool T |
|--------|-------------|------------|
| **Type** | Superblocks + votes MN | Pool interne + votes MN (Phase 7) |
| **Financement** | Block reward DAO | Accumulation automatique |
| **Fréquence financement** | Chaque bloc (année 0-6) | Tous les 4 mois |
| **Fréquence dépenses** | Tous les 43200 blocs | Vote MN (Phase 7) |
| **Montant** | reward_year PIV/bloc | 0.5% × (U+Ur) / cycle |
| **Adresse** | Variable (par proposition) | Aucune (pool interne) |
| **Perpétuel** | Non (s'arrête année 6) | Oui (indéfini) |
| **Gouvernance** | Vote MN sur propositions | Vote MN sur propositions (Phase 7) |

### 8.2 Timeline Détaillée

```
┌──────────────────────────────────────────────────────┐
│ AVANT V6.0 (PIVX v5)                                 │
├──────────────────────────────────────────────────────┤
│ DAO: 10 PIV/bloc (constant)                          │
│ Système Budget: Superblocks tous les 43200 blocs    │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ V6.0 ACTIVATION (Phase 1-5)                          │
├──────────────────────────────────────────────────────┤
│ Émission: 6→0 PIV/bloc sur 6 ans                     │
│ DAO block reward: 6→5→4→3→2→1→0 PIV/bloc            │
│ KHU: C/U + Cr/Ur + R% yield actif                   │
│ T: Pas encore implémenté                             │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ V6.3 (Phase 6.3) - MAINTENANT                        │
├──────────────────────────────────────────────────────┤
│ Émission: 6→0 PIV/bloc (inchangé)                    │
│ DAO block reward: 6→5→4→3→2→1→0 PIV/bloc (inchangé) │
│ T: Implémenté, accumulation tous les 172800 blocs   │
│   • T += 0.5% × (U+Ur)                               │
│   • Pas de dépenses encore                           │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ V7.0 (Phase 7) - FUTUR                               │
├──────────────────────────────────────────────────────┤
│ Émission: Peut-être terminée (année 6+)              │
│ DAO block reward: 0 PIV/bloc                         │
│ T: Accumulation + Dépenses                           │
│   • T += 0.5% × (U+Ur) / 4 mois                      │
│   • T -= montant (si proposition votée YES)          │
│   • Seule source financement DAO                     │
└──────────────────────────────────────────────────────┘
```

### 8.3 Références

**Blueprints liés:**
- `01-PIVX-INFLATION-DIMINUTION.md` — Émission 6→0 PIV/bloc
- `02-KHU-YIELD-DAILY.md` — R% daily yield
- `03-KHU-DOMC-GOVERNANCE.md` — R% governance (DOMC cycles)

**Code source:**
- `src/khu/khu_state.h` — KhuGlobalState (avec T)
- `src/khu/khu_dao.h` — Fonctions DAO
- `src/khu/khu_dao.cpp` — Implémentation
- `src/validation.cpp` — Intégration ConnectBlock

**Tests:**
- `src/test/khu_dao_tests.cpp` — Tests unitaires
- `test/functional/khu_dao_treasury.py` — Tests fonctionnels

---

## VALIDATION FINALE

```
╔═══════════════════════════════════════════════════════════╗
║  CE BLUEPRINT EST CANONIQUE                              ║
╚═══════════════════════════════════════════════════════════╝

Phase 6.3 - DAO Treasury Pool (T)

RÈGLES FONDAMENTALES (NON-NÉGOCIABLES):
1. T est une variable dans KhuGlobalState
2. T += 0.5% × (U + Ur) tous les 172800 blocs
3. T >= 0 (invariant)
4. Pas d'adresse, pas de transaction (pool interne)
5. Accumulation perpétuelle (pas de burn)
6. Propositions futures peuvent dépenser depuis T (Phase 7)

STATUT: Prêt pour implémentation Phase 6.3

Toute modification doit:
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date
```

---

**FIN DU BLUEPRINT PHASE 6 DAO v3.0**

**Version:** 3.0 FINAL
**Date:** 2025-01-24
**Status:** CANONIQUE
