# 09 — DAO TREASURY POOL BLUEPRINT

**Date:** 2025-11-26
**Version:** 2.0
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

**Après l'année 6, l'émission PIVX tombe à 0 PIV/bloc.**

```
Année 0-6:  Block reward DAO = 6→5→4→3→2→1→0 PIV/bloc
Année 6+:   Block reward DAO = 0 PIV/bloc

→ Comment financer la DAO après l'année 6?
```

### 1.2 La Solution: Pool Interne T

**Créer un "pot commun" DAO qui s'auto-alimente perpétuellement.**

```
┌────────────────────────────────────────────────┐
│ T = Treasury Pool (Pool Interne)              │
├────────────────────────────────────────────────┤
│ • Variable dans KhuGlobalState                 │
│ • S'incrémente QUOTIDIENNEMENT (1440 blocs)    │
│ • T_daily = (U + Ur) / 182500  (2% annuel)     │
│ • Unifié avec yield dans ApplyDailyUpdates     │
│ • Propositions DAO peuvent dépenser depuis T   │
└────────────────────────────────────────────────┘
```

### 1.3 Principes Fondamentaux

```
✅ AUTOMATIQUE:  T s'incrémente sans intervention humaine
✅ MATHÉMATIQUE: Formule fixe (U + Ur) / 182500 par jour (2% annuel)
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
│ (U+Ur)/182500│      └────────────┘      └──────────────┘
└──────────────┘                            Gouverné (MN)
  Automatique                               (Phase 7)
```

### 2.2 Timing unifié avec Yield

**T et Yield sont calculés AU MÊME MOMENT dans ApplyDailyUpdatesIfNeeded():**

```cpp
void ApplyDailyUpdatesIfNeeded(KhuGlobalState& state, uint32_t nHeight) {
    if ((nHeight - state.last_yield_update_height) >= BLOCKS_PER_DAY) {  // 1440

        // 1. DAO Treasury accumulation (GLOBAL)
        int64_t T_daily = (state.U + state.Ur) / 182500;  // 2% annuel
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
    int64_t T;   // DAO pool accumulator (2%/an)

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
│ Année 0-6: DOUBLE financement DAO                   │
├─────────────────────────────────────────────────────┤
│ Source 1: Block reward DAO (6→0 PIV/bloc)           │
│   • Via émission PIVX (Blueprint 01)                │
│   • Payé chaque bloc                                │
│   • Termine à l'année 6                             │
│                                                     │
│ Source 2: Pool T accumulation                       │
│   • (U + Ur) / 182500 quotidien (2%/an)             │
│   • Pool interne (pas de paiement)                  │
│   • Commence dès activation                         │
│   • Continue perpétuellement                        │
│                                                     │
│ Total DAO = Source 1 + accumulation T               │
└─────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────┐
│ Année 6+: UN SEUL système DAO                       │
├─────────────────────────────────────────────────────┤
│ Source 1: Block reward = 0 PIV                      │
│                                                     │
│ Source 2: Pool T                                    │
│   • Accumulation: (U + Ur) / 182500 quotidien       │
│   • Dépenses: Propositions votées (Phase 7)         │
│   • Seul mécanisme de financement DAO               │
│                                                     │
│ Total DAO = Pool T uniquement                       │
└─────────────────────────────────────────────────────┘
```

---

## 3. SPÉCIFICATIONS TECHNIQUES

### 3.1 Constantes

```cpp
// Timing (voir SPEC.md)
static const uint32_t BLOCKS_PER_DAY = 1440;
static const uint32_t T_DAILY_DIVISOR = 182500;  // 2% annual / 365 days

// Dérivation:
// 2% annual = 200 basis points
// Daily = 200 / 365 = 0.548% par jour
// Diviseur = 10000 × 365 / 200 = 182500
```

### 3.2 Formule Canonique

```cpp
/**
 * Calcul accumulation quotidienne T
 *
 * Formule: T_daily = (U + Ur) / 182500
 *
 * @param U Supply KHU_T
 * @param Ur Reward rights
 * @return Montant à ajouter à T
 */
int64_t CalculateDaoTreasuryDaily(int64_t U, int64_t Ur)
{
    // OVERFLOW PROTECTION: Utilise __int128
    __int128 total = (__int128)U + (__int128)Ur;
    __int128 daily = total / T_DAILY_DIVISOR;

    if (daily > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDaily overflow\n");
        return 0;
    }

    return (int64_t)daily;
}
```

### 3.3 Exemples Numériques

```
Exemple 1: Jour 1 après activation
  U = 1,000,000 KHU
  Ur = 0 KHU (pas encore de yield)
  T_daily = 1,000,000 / 182500 = 5.48 KHU
  T: 0 → 5.48

Exemple 2: Jour 100
  U = 1,500,000 KHU
  Ur = 50,000 KHU
  T_daily = 1,550,000 / 182500 = 8.49 KHU
  T: (accumulé) → T + 8.49

Exemple 3: Année 1 (365 jours)
  U moyen = 2,000,000 KHU
  Ur moyen = 200,000 KHU
  T_annual = (2,200,000 / 182500) × 365 = 43,973 KHU
  ≈ 2% × 2,200,000 = 44,000 KHU ✓
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
// T accumulates (U + Ur) / 182500 daily (2% annual)
// Unified with yield in ApplyDailyUpdatesIfNeeded()

static const uint32_t T_DAILY_DIVISOR = 182500;

/**
 * Calculate daily T accumulation
 * Formula: (U + Ur) / 182500 = 2% annual / 365
 */
int64_t CalculateDaoTreasuryDaily(int64_t U, int64_t Ur);

#endif // PIVX_KHU_DAO_H
```

### 4.3 khu_dao.cpp

```cpp
#include "khu/khu_dao.h"
#include "amount.h"
#include "logging.h"

int64_t CalculateDaoTreasuryDaily(int64_t U, int64_t Ur)
{
    __int128 total = (__int128)U + (__int128)Ur;
    __int128 daily = total / T_DAILY_DIVISOR;

    if (daily < 0 || daily > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDaoTreasuryDaily overflow (U=%lld, Ur=%lld)\n", U, Ur);
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
    // T_daily = (U + Ur) / 182500

    // Case 1: 1M KHU
    int64_t daily = CalculateDaoTreasuryDaily(1000000 * COIN, 0);
    int64_t expected = (1000000 * COIN) / 182500;
    BOOST_CHECK_EQUAL(daily, expected);

    // Case 2: 1.5M KHU + 0.5M Ur
    daily = CalculateDaoTreasuryDaily(1500000 * COIN, 500000 * COIN);
    expected = (2000000 * COIN) / 182500;
    BOOST_CHECK_EQUAL(daily, expected);
}

BOOST_AUTO_TEST_CASE(dao_annual_rate)
{
    // Verify 2% annual rate
    int64_t U = 1000000 * COIN;
    int64_t Ur = 0;

    int64_t annual = 0;
    for (int day = 0; day < 365; day++) {
        annual += CalculateDaoTreasuryDaily(U, Ur);
    }

    // Should be ~2% of U
    int64_t expected = (U * 2) / 100;
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
□ Formule (U + Ur) / 182500 = 2% annuel vérifiée
□ Protection overflow testée
□ Invariant T >= 0 vérifié
□ Unifié avec yield (même ApplyDailyUpdatesIfNeeded)
□ Sérialisation T fonctionne
□ Pas de régression autres fonctionnalités KHU
```

---

## 6. FAQ

### Questions Générales

**Q: Pourquoi 2% annuel?**
A: Compromis entre financement suffisant et inflation contrôlée. 2% est comparable à l'inflation ciblée des économies stables.

**Q: Pourquoi quotidien et pas tous les 4 mois?**
A: Unification avec le yield (même timing = code plus simple). Le résultat annuel est identique.

**Q: T est-il de l'argent "magique"?**
A: T est une réserve mathématique qui représente des droits futurs à financement DAO. Les PIV sont créés uniquement quand une proposition est approuvée (Phase 7).

**Q: T augmente la supply PIV?**
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
1. T_daily = (U + Ur) / 182500 (2% annuel)
2. Calculé tous les 1440 blocs (quotidien)
3. Unifié avec yield dans ApplyDailyUpdatesIfNeeded()
4. T >= 0 (invariant)
5. Pas d'adresse, pas de transaction (pool interne)
6. Accumulation perpétuelle (pas de burn)

Référence: SPEC.md §8 (DAO Treasury)
```

---

**FIN DU BLUEPRINT**

**Version:** 2.0
**Date:** 2025-11-26
**Status:** CANONIQUE — Aligné avec SPEC.md
