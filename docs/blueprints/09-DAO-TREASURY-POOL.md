# 09 — DAO TREASURY POOL BLUEPRINT

**Date:** 2025-11-27
**Version:** 2.1
**Status:** CANONIQUE

---

## TABLE DES MATIÈRES

1. [Vision & Objectifs](#1-vision--objectifs)
2. [Architecture Système](#2-architecture-système)
3. [Spécifications Techniques](#3-spécifications-techniques)
4. [Implémentation Code](#4-implémentation-code)
5. [Tests & Validation](#5-tests--validation)
6. [FAQ](#7-faq)

---

## 1. VISION & OBJECTIFS

### 1.1 Le Problème

**À l'activation V6, l'émission PIVX tombe à 0 PIV/bloc IMMÉDIATEMENT.**

```
PRE-V6:   Block reward = 20 PIV/bloc (6 Staker + 4 MN + 10 DAO)
POST-V6:  Block reward = 0 PIV/bloc (IMMÉDIAT)

→ Comment financer la DAO après V6?
```

### 1.2 La Solution: Pool Interne T

**Créer un "pot commun" DAO qui s'auto-alimente perpétuellement.**

```
┌────────────────────────────────────────────────┐
│ T = Treasury Pool (Pool Interne)              │
├────────────────────────────────────────────────┤
│ • Variable dans KhuGlobalState                 │
│ • S'incrémente QUOTIDIENNEMENT (1440 blocs)    │
│ • T_daily = (U × R_annual) / 10000 / 8 / 365   │
│ • Unifié avec yield dans ApplyDailyUpdates     │
│ • Propositions DAO peuvent dépenser depuis T   │
└────────────────────────────────────────────────┘
```

### 1.3 Principes Fondamentaux

```
✅ AUTOMATIQUE:  T s'incrémente sans intervention humaine
✅ MATHÉMATIQUE: T_daily = (U × R_annual) / 10000 / 8 / 365 (~5% at R=40%)
✅ TRANSPARENT:  Visible dans KhuGlobalState
✅ GOUVERNÉ:     Dépenses approuvées par vote MN (Phase 7)
✅ PERPÉTUEL:    Continue indéfiniment (après année 6)
✅ UNIFIÉ:       Même timing que yield (1440 blocs)
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
│ 1440 blocs   │ ───> │     T      │ ───> │ votée YES    │
│ (quotidien)  │      │            │      │ par MN       │
│              │      │ Accumule   │      │              │
│ T_daily =    │      │ perpétuel  │      │ T -= montant │
│ (U×R%)/20/365│      └────────────┘      └──────────────┘
└──────────────┘                            Gouverné (MN)
  Automatique                               (Phase 7)
```

### 2.2 Timing unifié avec Yield

**T et Yield sont calculés AU MÊME MOMENT dans ApplyDailyUpdatesIfNeeded():**

```cpp
void ApplyDailyUpdatesIfNeeded(KhuGlobalState& state, uint32_t nHeight) {
    if ((nHeight - state.last_yield_update_height) >= BLOCKS_PER_DAY) {  // 1440

        // 1. DAO Treasury accumulation (GLOBAL)
        // T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
        int64_t T_daily = CalculateDaoTreasuryDaily(state.U, state.R_annual);
        state.T += T_daily;

        // 2. Yield per-note (INDIVIDUEL)
        for (auto& note : zkhuNotes) {
            if (IsMature(note, nHeight)) {
                int64_t daily = (note.amount * state.R_annual) / 10000 / 365;
                note.Ur_accumulated += daily;
                state.Cr += daily;
                state.Ur += daily;
            }
        }

        state.last_yield_update_height = nHeight;
    }
}
```

**Avantages:**
- Code plus simple (un seul point d'update quotidien)
- Cohérence temporelle (T et yield évoluent ensemble)
- Pas de "cycle" séparé pour T

### 2.3 KhuGlobalState

```cpp
struct KhuGlobalState {
    // Stable pipeline
    int64_t C;   // collateral backing
    int64_t U;   // KHU_T supply (transparent)
    int64_t Z;   // ZKHU supply (shielded)

    // Reward pipeline
    int64_t Cr;  // reward collateral
    int64_t Ur;  // reward rights

    // DAO treasury
    int64_t T;   // DAO pool accumulator (~5%/an at R=40%)

    // ... autres champs ...
};
```

### 2.4 Invariants

```cpp
Invariants existants (inchangés):
  C == U + Z  ✅ (stable pipeline)
  Cr == Ur    ✅ (reward pipeline)

Nouvel invariant:
  T >= 0     ✅ (pool non-négatif)
```

### 2.5 Timeline Complète

```
┌─────────────────────────────────────────────────────┐
│ POST-V6: Pool T = SEUL financement DAO              │
├─────────────────────────────────────────────────────┤
│ Block reward = 0 PIV (immédiat à V6)                │
│                                                     │
│ Pool T accumulation:                                │
│   • Formule: (U × R_annual) / 10000 / 8 / 365       │
│   • Trigger: tous les 1440 blocs (quotidien)        │
│   • Pool interne (pas de paiement externe)          │
│   • Commence dès activation V6                      │
│   • Continue perpétuellement                        │
│   • ~5% annuel de U quand R%=40%                    │
│   • Décroît avec R% (40%→7% sur 33 ans)             │
│                                                     │
│ Dépenses:                                           │
│   • Propositions votées par MN (Phase 7)            │
│   • Seul mécanisme de financement DAO               │
└─────────────────────────────────────────────────────┘
```

---

## 3. SPÉCIFICATIONS TECHNIQUES

### 3.1 Constantes

```cpp
// Timing (voir SPEC.md)
static const uint32_t BLOCKS_PER_DAY = 1440;
static const uint32_t T_DIVISOR = 8;   // Treasury = 1/8 of yield rate (~5% at R=40%)

// Dérivation:
// T_daily = (U × R_annual) / 10000 / 8 / 365
// With R_annual = 4000 (40%): T ≈ U × 5% / 365
// T_DIVISOR = 8 (Treasury gets 1/8th of yield rate)
```

### 3.2 Formule Canonique

```cpp
/**
 * Calcul accumulation quotidienne T
 *
 * Formule: T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
 *
 * @param U Supply KHU_T
 * @param R_annual Annual rate in basis points (4000 = 40%)
 * @return Montant à ajouter à T
 */
int64_t CalculateDaoTreasuryDaily(int64_t U, uint16_t R_annual)
{
    // OVERFLOW PROTECTION: Utilise __int128
    // T = (U × R_annual) / 10000 / T_DIVISOR / 365
    __int128 daily = (__int128)U * R_annual / 10000 / T_DIVISOR / 365;

    if (daily > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDaily overflow\n");
        return 0;
    }

    return (int64_t)daily;
}
```

### 3.3 Exemples Numériques

```
Exemple 1: Jour 1 après activation (R% = 40%)
  U = 1,000,000 KHU
  R_annual = 4000 (40%)
  T_daily = (1,000,000 × 4000) / 10000 / 8 / 365 = 13.70 KHU
  T: 0 → 13.70

Exemple 2: Jour 100 (R% = 40%)
  U = 1,500,000 KHU
  R_annual = 4000
  T_daily = (1,500,000 × 4000) / 10000 / 8 / 365 = 20.55 KHU
  T: (accumulé) → T + 20.55

Exemple 3: Année 1 (365 jours)
  U moyen = 2,000,000 KHU
  R_annual = 4000
  T_annual = (2,000,000 × 4000) / 10000 / 8 = 100,000 KHU
  ≈ U × R% / 8 = 2,000,000 × 40% / 8 = 100,000 KHU ✓
  ≈ 5% de U annuel (à R%=40%)
```

### 3.4 Sécurité & Protection

```cpp
Protections implémentées:
✅ Overflow (__int128 arithmetic)
✅ Underflow (total >= 0 par construction)
✅ MAX_MONEY validation
✅ Invariant T >= 0
✅ Consensus-driven (pas de clé privée)
```

---

## 4. IMPLÉMENTATION CODE

### 4.1 Fichiers

```
src/khu/khu_dao.h        (~15 lignes)
src/khu/khu_dao.cpp      (~25 lignes)
src/khu/khu_yield.cpp    (modifier ApplyDailyUpdatesIfNeeded)
```

**Total: ~50 lignes de code!**

### 4.2 khu_dao.h

```cpp
#ifndef PIVX_KHU_DAO_H
#define PIVX_KHU_DAO_H

#include <cstdint>

// DAO Treasury Pool (T) — Phase 6
// T = (U × R_annual) / 10000 / T_DIVISOR / 365
// Unified with yield in ApplyDailyUpdatesIfNeeded()

static const uint32_t T_DIVISOR = 8;  // Treasury = 1/8 of yield rate (~5% at R=40%)

/**
 * Calculate daily T accumulation
 * Formula: (U × R_annual) / 10000 / T_DIVISOR / 365
 */
int64_t CalculateDaoTreasuryDaily(int64_t U, uint16_t R_annual);

#endif // PIVX_KHU_DAO_H
```

### 4.3 khu_dao.cpp

```cpp
#include "khu/khu_dao.h"
#include "amount.h"
#include "logging.h"
#include "khu/khu_domc.h"  // For T_DIVISOR

int64_t CalculateDaoTreasuryDaily(int64_t U, uint16_t R_annual)
{
    // T = (U × R_annual) / 10000 / T_DIVISOR / 365
    // With T_DIVISOR = 8 → T ≈ 5% annual at R=40%
    __int128 daily = (__int128)U * R_annual / 10000 / khu_domc::T_DIVISOR / 365;

    if (daily < 0 || daily > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDaily overflow (U=%lld, R=%u)\n", U, R_annual);
        return 0;
    }

    return (int64_t)daily;
}
```

### 4.4 Intégration dans ApplyDailyUpdatesIfNeeded

```cpp
// src/khu/khu_yield.cpp

bool ApplyDailyUpdatesIfNeeded(KhuGlobalState& state, uint32_t nHeight)
{
    if ((nHeight - state.last_yield_update_height) < BLOCKS_PER_DAY)
        return true;  // Pas encore l'heure

    // 1. DAO Treasury accumulation (GLOBAL, AVANT yield)
    int64_t T_daily = CalculateDaoTreasuryDaily(state.U, state.Ur);
    state.T += T_daily;

    LogPrint(BCLog::KHU, "DAO Treasury: T += %lld (total = %lld)\n",
             T_daily, state.T);

    // 2. Yield per-note (INDIVIDUEL)
    // ... (code existant yield) ...

    state.last_yield_update_height = nHeight;
    return true;
}
```

---

## 5. TESTS & VALIDATION

### 5.1 Tests Unitaires

```cpp
BOOST_AUTO_TEST_SUITE(khu_dao_tests)

BOOST_AUTO_TEST_CASE(dao_daily_calculation)
{
    // T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365

    // Case 1: 1M KHU @ 40%
    int64_t daily = CalculateDaoTreasuryDaily(1000000 * COIN, 4000);
    // = (1,000,000 × 4000) / 10000 / 20 / 365 = 5.48 KHU
    int64_t expected = ((__int128)1000000 * COIN * 4000) / 10000 / 20 / 365;
    BOOST_CHECK_EQUAL(daily, expected);

    // Case 2: 1.5M KHU @ 40%
    daily = CalculateDaoTreasuryDaily(1500000 * COIN, 4000);
    expected = ((__int128)1500000 * COIN * 4000) / 10000 / 20 / 365;
    BOOST_CHECK_EQUAL(daily, expected);
}

BOOST_AUTO_TEST_CASE(dao_annual_rate)
{
    // Verify T_annual = U × R% / 20 (at R% = 40%, T ≈ 2%)
    int64_t U = 1000000 * COIN;
    uint16_t R_annual = 4000;  // 40%

    int64_t annual = 0;
    for (int day = 0; day < 365; day++) {
        annual += CalculateDaoTreasuryDaily(U, R_annual);
    }

    // Should be U × 40% / 20 = 2% of U
    int64_t expected = ((__int128)U * R_annual) / 10000 / 20;
    int64_t tolerance = expected / 100;  // 1% tolerance

    BOOST_CHECK(abs(annual - expected) < tolerance);
}

BOOST_AUTO_TEST_CASE(dao_zero_supply)
{
    int64_t daily = CalculateDaoTreasuryDaily(0, 0);
    BOOST_CHECK_EQUAL(daily, 0);
}

BOOST_AUTO_TEST_CASE(dao_accumulation)
{
    KhuGlobalState state;
    state.U = 1000000 * COIN;
    state.Ur = 0;
    state.T = 0;

    // Day 1
    state.T += CalculateDaoTreasuryDaily(state.U, state.Ur);
    BOOST_CHECK(state.T > 0);
    BOOST_CHECK(state.CheckInvariants());  // T >= 0

    // Day 2
    int64_t T_before = state.T;
    state.T += CalculateDaoTreasuryDaily(state.U, state.Ur);
    BOOST_CHECK(state.T > T_before);
}

BOOST_AUTO_TEST_SUITE_END()
```

### 5.2 Checklist Validation

```
Avant merge:
□ Tests unitaires passent
□ T s'incrémente correctement tous les 1440 blocs
□ Formule (U × R_annual) / 10000 / T_DIVISOR / 365 vérifiée
□ Protection overflow testée
□ Invariant T >= 0 vérifié
□ Unifié avec yield (même ApplyDailyUpdatesIfNeeded)
□ Sérialisation T fonctionne
□ Pas de régression autres fonctionnalités KHU
```

---

## 6. FAQ

### Questions Générales

**Q: Pourquoi ~5% annuel (à R%=40%)?**
A: T_DIVISOR=8 signifie que T représente 1/8 du taux de yield. À R%=40%, cela donne ~5% annuel de U. À mesure que R% décroît (40%→7% sur 33 ans), T décroît proportionnellement (~0.875% à R%=7%).

**Q: Pourquoi quotidien et pas tous les 4 mois?**
A: Unification avec le yield (même timing = code plus simple). Le résultat annuel est identique.

**Q: T est-il de l'argent "magique"?**
A: T est une réserve mathématique qui représente des droits futurs à financement DAO. Les KHU sont créés uniquement quand une proposition est approuvée (Phase 7).

**Q: T augmente la supply?**
A: Non dans Phase 6 (accumulation uniquement). En Phase 7, les dépenses depuis T créeront de l'inflation contrôlée et gouvernée.

### Questions Techniques

**Q: Comment vérifier T?**
```bash
pivx-cli getkhustate
# {
#   "T": 123456789,
#   ...
# }
```

**Q: Que se passe-t-il en cas de reorg?**
A: T est recalculé lors de la reconnexion des blocs (state-based reorg).

**Q: Peut-on changer la formule 2%?**
A: Nécessite un hard fork (consensus change).

---

## VALIDATION FINALE

```
╔═══════════════════════════════════════════════════════════╗
║  CE BLUEPRINT EST CANONIQUE                               ║
╚═══════════════════════════════════════════════════════════╝

RÈGLES FONDAMENTALES (NON-NÉGOCIABLES):
1. T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
2. T_DIVISOR = 8 (défini dans khu_domc.h)
3. ~5% annuel de U à R%=40%, décroît avec R%
4. Calculé tous les 1440 blocs (quotidien)
5. Unifié avec yield dans ApplyDailyUpdatesIfNeeded()
6. T >= 0 (invariant)
7. Pas d'adresse, pas de transaction (pool interne)
8. Accumulation perpétuelle (pas de burn)
9. POST-V6: Seule source de financement DAO (block reward = 0)

Référence: SPEC.md §8 (DAO Treasury)
```

---

**FIN DU BLUEPRINT**

**Version:** 2.0
**Date:** 2025-11-26
**Status:** CANONIQUE — Aligné avec SPEC.md
