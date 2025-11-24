# PHASE 6 — ARCHITECTURE TECHNIQUE

**Date**: 2025-11-24
**Version**: 2.0 (DRAFT)
**Status**: Architecture Proposal (Updated with PIVX Budget Integration)

---

## 📋 OBJECTIFS PHASE 6

1. **DAILY YIELD ENGINE** — Moteur de yield quotidien (R%)
2. **DOMC R% GOVERNANCE** — Vote commit/reveal masternodes (cycle 4 mois)
3. **DAO BUDGET** — Budget automatique 0.5% (U + Ur) tous les 4 mois

---

## 🏗️ ARCHITECTURE FICHIERS

### Nouveaux Fichiers

```
src/khu/
├── khu_yield.h                # ApplyDailyYield, UndoDailyYield
├── khu_yield.cpp
├── khu_domc.h                 # Vote R%, commit-reveal, cycles
├── khu_domc.cpp
├── khu_dao.h                  # ✅ DAO budget automatique (3 fonctions)
└── khu_dao.cpp

src/test/
├── khu_yield_tests.cpp        # ~15 tests yield
├── khu_domc_tests.cpp         # ~15 tests DOMC
└── khu_dao_tests.cpp          # ~5 tests DAO (automatique)

test/functional/
├── khu_phase6_yield.py        # Yield multi-notes, reorgs
├── khu_phase6_domc.py         # Cycles DOMC, vote median
└── khu_phase6_dao.py          # DAO budget automatique
```

**Note**: Architecture DAO ultra-simple (budget automatique 0.5% sans vote MN)

### Fichiers Modifiés (Intégration)

```
src/validation.cpp        # ConnectBlock, DisconnectBlock (ordre yield→tx)
src/consensus/tx_verify.cpp  # Validation TX_DOMC_VOTE (si nécessaire)
src/masternode/masternode.h  # Extension CMasternodePing (nRCommitment, nRProposal, nRSecret)
```

---

## 🔧 1. KHU_YIELD — DAILY YIELD ENGINE

### 1.1 Header (`khu_yield.h`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#ifndef PIVX_KHU_YIELD_H
#define PIVX_KHU_YIELD_H

#include "khu/khu_state.h"
#include "coins.h"
#include "amount.h"

#include <stdint.h>

/**
 * DAILY YIELD ENGINE — Phase 6
 *
 * Applique le yield quotidien (R%) à toutes les notes ZKHU matures.
 *
 * RÈGLES:
 * - Trigger toutes les BLOCKS_PER_DAY (1440 blocs)
 * - Notes matures uniquement (≥4320 blocs = 3 jours)
 * - Yield linéaire (pas de compounding)
 * - Formule: Ur_daily = floor(amount × R_annual / 10000 / 365)
 * - Invariant: Cr += Δ, Ur += Δ (même delta, atomique)
 */

// Constants
static const uint32_t BLOCKS_PER_DAY = 1440;  // 1 bloc/minute
static const uint32_t MATURITY_BLOCKS = 4320; // 3 jours
static const uint32_t BLOCKS_PER_YEAR = 525600; // 365 × 1440

/**
 * Vérifier si yield doit être appliqué à ce height
 *
 * @param state État KHU global actuel
 * @param nHeight Height du bloc courant
 * @return true si yield doit être appliqué
 */
bool ShouldApplyDailyYield(const KhuGlobalState& state, uint32_t nHeight);

/**
 * Appliquer le yield quotidien à toutes les notes ZKHU matures
 *
 * ORDRE CONNECTBLOCK:
 *   1. Load prevState
 *   2. ApplyDailyYield(newState, height)  ← ICI
 *   3. Apply TX KHU (MINT/REDEEM/STAKE/UNSTAKE)
 *   4. Apply block reward
 *   5. CheckInvariants()
 *   6. Persist newState
 *
 * @param state État KHU global (modifié in-place)
 * @param nHeight Height du bloc courant
 * @param view Coins view (pour itération notes ZKHU)
 * @return true si succès, false si erreur
 */
bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view);

/**
 * Annuler le yield quotidien lors d'un reorg
 *
 * Inverse l'effet de ApplyDailyYield pour le bloc nHeight.
 *
 * @param state État KHU global (modifié in-place)
 * @param nHeight Height du bloc à déconnecter
 * @param view Coins view
 * @return true si succès, false si erreur
 */
bool UndoDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view);

/**
 * Calculer le yield quotidien pour un montant donné
 *
 * Formule canonique:
 *   annual = (amount × R_annual) / 10000
 *   daily  = annual / 365
 *
 * OVERFLOW PROTECTION: Utilise __int128 pour calcul intermédiaire
 *
 * @param amount Montant staké (principal)
 * @param R_annual Taux annuel en basis points (ex: 500 = 5.00%)
 * @return Yield quotidien en satoshis
 */
CAmount CalculateDailyYield(CAmount amount, uint16_t R_annual);

/**
 * Vérifier si une note ZKHU est mature
 *
 * @param stake_start_height Height de création de la note (STAKE)
 * @param current_height Height actuel
 * @return true si note mature (≥4320 blocs)
 */
bool IsNoteMature(uint32_t stake_start_height, uint32_t current_height);

/**
 * Itérer sur toutes les notes ZKHU et appliquer le yield
 *
 * IMPLÉMENTATION:
 * - Streaming via LevelDB cursor (pas de "load all into RAM")
 * - Batch update via CDBBatch pour performance
 * - Accumulation total_yield pour mise à jour Cr/Ur globale
 *
 * @param state État KHU global (Cr/Ur modifiés)
 * @param R_annual Taux annuel actuel
 * @param nHeight Height courant
 * @return Total yield appliqué (pour validation)
 */
CAmount ApplyYieldToAllNotes(KhuGlobalState& state, uint16_t R_annual, uint32_t nHeight);

#endif // PIVX_KHU_YIELD_H
```

### 1.2 Implémentation (`khu_yield.cpp`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#include "khu/khu_yield.h"
#include "khu/khu_db.h"
#include "logging.h"
#include "sync.h"

#include <stdexcept>

bool ShouldApplyDailyYield(const KhuGlobalState& state, uint32_t nHeight)
{
    // Yield quotidien toutes les 1440 blocs
    //
    // Si last_yield_update_height = 0 (genesis), attendre 1440 blocs
    // Sinon, trigger si (nHeight - last_yield_update_height) >= BLOCKS_PER_DAY

    if (state.last_yield_update_height == 0) {
        // Premier yield après 1 jour complet
        return (nHeight >= BLOCKS_PER_DAY);
    }

    return (nHeight - state.last_yield_update_height) >= BLOCKS_PER_DAY;
}

CAmount CalculateDailyYield(CAmount amount, uint16_t R_annual)
{
    if (R_annual == 0 || amount <= 0) {
        return 0;
    }

    // OVERFLOW PROTECTION: Utilise __int128 pour calcul intermédiaire
    //
    // annual = (amount × R_annual) / 10000
    // daily  = annual / 365

    __int128 numerator = ((__int128)amount * (CAmount)R_annual);
    CAmount annual = (CAmount)(numerator / 10000);

    // Division entière = floor automatique
    CAmount daily = annual / 365;

    if (daily < 0) {
        LogPrintf("WARNING: CalculateDailyYield overflow detected (amount=%lld, R=%u)\n",
                  amount, R_annual);
        return 0;
    }

    return daily;
}

bool IsNoteMature(uint32_t stake_start_height, uint32_t current_height)
{
    if (current_height < stake_start_height) {
        return false; // Impossible
    }

    return (current_height - stake_start_height) >= MATURITY_BLOCKS;
}

bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view)
{
    AssertLockHeld(cs_khu); // Requis: LOCK(cs_khu)

    if (!ShouldApplyDailyYield(state, nHeight)) {
        return true; // Pas de yield ce bloc, c'est OK
    }

    LogPrint(BCLog::KHU, "ApplyDailyYield: height=%u, R_annual=%u\n", nHeight, state.R_annual);

    // Appliquer yield à toutes les notes ZKHU matures
    CAmount total_yield = ApplyYieldToAllNotes(state, state.R_annual, nHeight);

    if (total_yield < 0) {
        return error("ApplyDailyYield: negative total_yield (%lld)", total_yield);
    }

    // Mettre à jour Cr/Ur (atomique, même delta)
    state.Cr += total_yield;
    state.Ur += total_yield;

    // Mettre à jour last_yield_update_height
    state.last_yield_update_height = nHeight;

    LogPrint(BCLog::KHU, "ApplyDailyYield: total_yield=%lld, Cr=%lld, Ur=%lld\n",
             total_yield, state.Cr, state.Ur);

    return true;
}

bool UndoDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view)
{
    AssertLockHeld(cs_khu);

    // TODO: Implémenter undo logic
    //
    // Stratégies possibles:
    // 1. Stocker delta total par height (simple, reorg ≤12 blocs)
    // 2. Recalculer yield inverse (streaming notes)
    // 3. Snapshot notes (lourd, éviter)
    //
    // Pour reorg ≤12 blocs, option 1 est optimale:
    //   - Stocker YieldDelta(height) dans LevelDB ('Y' + height → total_yield)
    //   - UndoDailyYield: Cr -= YieldDelta(height), Ur -= YieldDelta(height)

    LogPrint(BCLog::KHU, "UndoDailyYield: height=%u (TODO: implement)\n", nHeight);

    // PLACEHOLDER: Implement me!
    return true;
}

CAmount ApplyYieldToAllNotes(KhuGlobalState& state, uint16_t R_annual, uint32_t nHeight)
{
    AssertLockHeld(cs_khu);

    // TODO: Implémenter itération streaming sur notes ZKHU
    //
    // Pseudo-code:
    //   CAmount total_yield = 0;
    //
    //   for each note in ZKHU_DB (streaming cursor):
    //     if (!note.is_spent && IsNoteMature(note.stake_height, nHeight)):
    //       daily = CalculateDailyYield(note.amount, R_annual);
    //       note.Ur_accumulated += daily;
    //       total_yield += daily;
    //
    //       // Batch update DB
    //       batch.Write(note_key, note);
    //
    //   db.WriteBatch(batch);
    //   return total_yield;

    LogPrint(BCLog::KHU, "ApplyYieldToAllNotes: R_annual=%u, height=%u (TODO: iterate notes)\n",
             R_annual, nHeight);

    // PLACEHOLDER
    return 0;
}
```

---

## 🔧 2. KHU_DOMC — R% GOVERNANCE

### 2.1 Header (`khu_domc.h`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#ifndef PIVX_KHU_DOMC_H
#define PIVX_KHU_DOMC_H

#include "khu/khu_state.h"
#include "uint256.h"

#include <stdint.h>
#include <vector>

/**
 * DOMC R% GOVERNANCE — Phase 6
 *
 * Vote commit-reveal par masternodes pour déterminer R% (yield annuel).
 *
 * CYCLE: 172800 blocs (4 mois)
 * PHASES:
 *   - 0-132480:  R% ACTIF uniquement (3m+2j)
 *   - 132480-152640: COMMIT (2 semaines)
 *   - 152640-172800: PRÉAVIS (2 semaines, R_next visible)
 *
 * RÈGLES:
 * - R% ∈ [0, R_MAX_dynamic]
 * - R_MAX_dynamic = max(400, 3000 - year×100) // 30%→4% sur 25 ans
 * - Agrégation: Médiane arithmétique (ou moyenne, selon spec)
 * - Privacy: Commitment SHA256 pendant commit phase
 */

// Constants
static const uint32_t DOMC_CYCLE_LENGTH = 172800;   // 4 mois
static const uint32_t DOMC_ACTIVE_LENGTH = 132480;  // 3m+2j
static const uint32_t DOMC_COMMIT_LENGTH = 20160;   // 2 semaines
static const uint32_t DOMC_NOTICE_LENGTH = 20160;   // 2 semaines

// Phases DOMC
enum DOMCPhase {
    DOMC_PHASE_ACTIVE = 0,  // R% actif, pas de vote
    DOMC_PHASE_COMMIT = 1,  // Commit votes (privacy)
    DOMC_PHASE_NOTICE = 2   // Préavis (R_next visible)
};

/**
 * Vote R% masternode (commit-reveal)
 */
struct DOMCVote {
    uint256 commitment;   // SHA256(R_proposal || secret) - phase commit
    uint16_t R_proposal;  // R% proposé (basis points) - phase reveal
    uint256 secret;       // Secret révélé - phase reveal

    bool IsValid() const {
        // Vérifier reveal: SHA256(R || secret) == commitment
        CHashWriter ss(SER_GETHASH, 0);
        ss << R_proposal << secret;
        return (ss.GetHash() == commitment);
    }
};

/**
 * Calculer R_MAX_dynamic selon l'année
 *
 * Formule: max(400, 3000 - year×100)
 *
 * @param nHeight Height actuel
 * @param nActivationHeight Height activation KHU
 * @return R_MAX_dynamic en basis points
 */
uint16_t CalculateRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * Déterminer phase DOMC actuelle
 *
 * @param state État KHU global
 * @param nHeight Height actuel
 * @return Phase DOMC (ACTIVE, COMMIT, NOTICE)
 */
DOMCPhase GetDOMCPhase(const KhuGlobalState& state, uint32_t nHeight);

/**
 * Vérifier si cycle DOMC doit se terminer
 *
 * @param state État KHU global
 * @param nHeight Height actuel
 * @return true si fin de cycle (activation nouveau R%)
 */
bool ShouldFinalizeDOMCCycle(const KhuGlobalState& state, uint32_t nHeight);

/**
 * Calculer médiane (ou moyenne) des votes valides
 *
 * @param votes Vecteur de votes R% valides
 * @return R_annual médian (clamped à R_MAX_dynamic)
 */
uint16_t CalculateRMedian(const std::vector<uint16_t>& votes, uint16_t R_MAX_dynamic);

/**
 * Finaliser cycle DOMC et activer nouveau R%
 *
 * PROCESSUS:
 *   1. Collecter tous les votes reveals valides
 *   2. Calculer médiane
 *   3. Clamp à [0, R_MAX_dynamic]
 *   4. Activer state.R_annual = nouveau R%
 *   5. Réinitialiser cycle (last_domc_height = nHeight)
 *
 * @param state État KHU global (modifié)
 * @param nHeight Height actuel
 * @return true si succès
 */
bool FinalizeDOMCCycle(KhuGlobalState& state, uint32_t nHeight);

/**
 * Valider vote DOMC dans un bloc
 *
 * (Si implémenté via TX spéciale TX_DOMC_VOTE)
 *
 * @param vote Vote à valider
 * @param phase Phase DOMC actuelle
 * @param R_MAX_dynamic R_MAX actuel
 * @return true si vote valide
 */
bool ValidateDOMCVote(const DOMCVote& vote, DOMCPhase phase, uint16_t R_MAX_dynamic);

#endif // PIVX_KHU_DOMC_H
```

### 2.2 Implémentation (`khu_domc.cpp`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#include "khu/khu_domc.h"
#include "logging.h"

#include <algorithm>

uint16_t CalculateRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight < nActivationHeight) {
        return 3000; // 30.00% (avant activation)
    }

    // year = (height - activation) / BLOCKS_PER_YEAR
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    uint32_t year = blocks_since_activation / 525600; // 365 × 1440

    // R_MAX = max(400, 3000 - year×100)
    uint16_t R_MAX = 3000;
    if (year > 0) {
        uint32_t reduction = year * 100;
        if (reduction >= 2600) {
            R_MAX = 400; // Plancher 4.00%
        } else {
            R_MAX = 3000 - reduction;
        }
    }

    return R_MAX;
}

DOMCPhase GetDOMCPhase(const KhuGlobalState& state, uint32_t nHeight)
{
    // Position dans le cycle actuel
    uint32_t cycle_start = state.domc_cycle_start;
    uint32_t position = nHeight - cycle_start;

    if (position < DOMC_ACTIVE_LENGTH) {
        return DOMC_PHASE_ACTIVE;
    } else if (position < DOMC_ACTIVE_LENGTH + DOMC_COMMIT_LENGTH) {
        return DOMC_PHASE_COMMIT;
    } else {
        return DOMC_PHASE_NOTICE;
    }
}

bool ShouldFinalizeDOMCCycle(const KhuGlobalState& state, uint32_t nHeight)
{
    uint32_t cycle_start = state.domc_cycle_start;
    uint32_t position = nHeight - cycle_start;

    // Finaliser à la fin du cycle (172800 blocs)
    return (position >= DOMC_CYCLE_LENGTH);
}

uint16_t CalculateRMedian(const std::vector<uint16_t>& votes, uint16_t R_MAX_dynamic)
{
    if (votes.empty()) {
        return 0; // Pas de votes → R% = 0
    }

    std::vector<uint16_t> sorted = votes;
    std::sort(sorted.begin(), sorted.end());

    size_t n = sorted.size();
    uint16_t median;

    if (n % 2 == 0) {
        // Pair: moyenne des 2 éléments centraux
        median = (sorted[n/2 - 1] + sorted[n/2]) / 2;
    } else {
        // Impair: élément central
        median = sorted[n/2];
    }

    // Clamp à [0, R_MAX_dynamic]
    if (median > R_MAX_dynamic) {
        median = R_MAX_dynamic;
    }

    return median;
}

bool FinalizeDOMCCycle(KhuGlobalState& state, uint32_t nHeight)
{
    AssertLockHeld(cs_khu);

    // TODO: Implémenter collecte votes reveals + calcul médiane
    //
    // Pseudo-code:
    //   std::vector<uint16_t> valid_votes;
    //
    //   for each masternode:
    //     if (mn.vote.IsValid() && mn.vote.R_proposal <= R_MAX):
    //       valid_votes.push_back(mn.vote.R_proposal);
    //
    //   uint16_t R_new = CalculateRMedian(valid_votes, state.R_MAX_dynamic);
    //
    //   state.R_annual = R_new;
    //   state.last_domc_height = nHeight;
    //   state.domc_cycle_start = nHeight;

    LogPrint(BCLog::KHU, "FinalizeDOMCCycle: height=%u (TODO: collect votes)\n", nHeight);

    // PLACEHOLDER
    return true;
}

bool ValidateDOMCVote(const DOMCVote& vote, DOMCPhase phase, uint16_t R_MAX_dynamic)
{
    // Phase COMMIT: commitment doit être non-null
    if (phase == DOMC_PHASE_COMMIT) {
        return !vote.commitment.IsNull();
    }

    // Phase NOTICE/REVEAL: reveal doit être valide
    if (phase == DOMC_PHASE_NOTICE) {
        if (!vote.IsValid()) {
            return false; // SHA256 mismatch
        }

        // R_proposal doit être dans [0, R_MAX_dynamic]
        if (vote.R_proposal > R_MAX_dynamic) {
            return false;
        }
    }

    return true;
}
```

---

## 🔧 3. KHU_DAO — DAO BUDGET (Automatique)

**🎯 PRINCIPE**: Budget DAO **automatique**, sans vote, sans gouvernance

### Architecture DAO — 1 Fichier Simple

```
src/khu/
└── khu_dao.{h,cpp}    # DAO budget automatique (3 fonctions)
```

**Règle Unique:**
```
Tous les 172800 blocs (4 mois):
  DAO_budget = (U + Ur) × 0.5%
  Mint DAO_budget PIV → DAO treasury
  SANS VOTE, SANS GOUVERNANCE
```

**Pas de:**
- ❌ Vote MN sur budget
- ❌ Propositions user
- ❌ Collateral
- ❌ Système de gouvernance
- ❌ Validation approve/reject
- ❌ Burn conditionnel

**Juste:**
- ✅ Timer (172800 blocs)
- ✅ Calcul (0.5% × (U+Ur))
- ✅ Mint PIV automatique
- ✅ Paiement DAO treasury

---

### 3.1 Header (`khu_dao.h`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#ifndef PIVX_KHU_DAO_H
#define PIVX_KHU_DAO_H

#include "khu/khu_state.h"
#include "amount.h"
#include "primitives/transaction.h"

/**
 * KHU DAO BUDGET — Phase 6 (Automatique)
 *
 * RÈGLE:
 *   Tous les 172800 blocs (4 mois):
 *     - Calculer: budget = 0.5% × (U + Ur)
 *     - Mint: budget PIV → DAO treasury
 *     - SANS VOTE, SANS GOUVERNANCE
 *
 * Budget en PIV, AUCUN impact sur C/U/Cr/Ur.
 */

// Constants
static const uint32_t DAO_CYCLE_LENGTH = 172800;  // 4 mois

/**
 * Vérifier si c'est un bloc "cycle boundary" DAO
 *
 * @param nHeight Height du bloc actuel
 * @param nActivationHeight Height activation KHU
 * @return true si c'est la fin d'un cycle DAO
 */
bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * Calculer budget DAO automatique
 *
 * Formule: (U + Ur) × 0.5%
 *
 * OVERFLOW PROTECTION: Utilise __int128
 *
 * @param state État KHU global
 * @return Budget DAO en PIV (satoshis)
 */
CAmount CalculateDAOBudget(const KhuGlobalState& state);

/**
 * Ajouter paiement DAO au coinstake
 *
 * IMPORTANT: Budget en PIV, externe à KHU (pas de modification C/U/Cr/Ur)
 *
 * @param txCoinstake Transaction coinstake (modifiée)
 * @param daoAmount Montant DAO à payer
 * @return true si succès
 */
bool AddDaoPaymentToCoinstake(CMutableTransaction& txCoinstake, CAmount daoAmount);

#endif // PIVX_KHU_DAO_H
```

---

### 3.2 Implémentation (`khu_dao.cpp`)

```cpp
// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license.

#include "khu/khu_dao.h"
#include "chainparams.h"
#include "logging.h"

bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}

CAmount CalculateDAOBudget(const KhuGlobalState& state)
{
    // DAO_budget = (U + Ur) × 0.5% = (U + Ur) × 5 / 1000

    __int128 total = (__int128)state.U + (__int128)state.Ur;
    __int128 budget = (total * 5) / 1000;

    // Overflow protection
    if (budget < 0 || budget > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDAOBudget overflow (U=%lld, Ur=%lld)\n",
                  state.U, state.Ur);
        return 0;
    }

    return (CAmount)budget;
}

bool AddDaoPaymentToCoinstake(CMutableTransaction& txCoinstake, CAmount daoAmount)
{
    if (daoAmount <= 0) {
        return true;
    }

    // Récupérer adresse DAO treasury (définie dans chainparams)
    const Consensus::Params& consensusParams = Params().GetConsensus();
    CScript daoTreasury = consensusParams.daoScript;

    // Ajouter output DAO
    txCoinstake.vout.emplace_back(daoAmount, daoTreasury);

    LogPrint(BCLog::KHU, "DAO Budget: Minting %lld PIV to treasury\n", daoAmount);

    return true;
}
```

---

### 3.3 Intégration ConnectBlock

Le budget DAO est appliqué dans `ConnectBlock` après le block reward:

```cpp
// 5. DAO BUDGET (automatique, tous les 172800 blocs)
if (IsDaoCycleBoundary(nHeight, Params().GetConsensus().nKhuActivationHeight)) {
    CAmount dao_budget = CalculateDAOBudget(newState);

    // Sera ajouté dans CreateCoinStake
    // (ou ici si coinstake déjà créé)
    LogPrint(BCLog::KHU, "DAO Cycle @ height %u: budget = %lld PIV\n",
             nHeight, dao_budget);
}
```

**Note:** Le paiement DAO est ajouté au coinstake via `AddDaoPaymentToCoinstake()` dans la fonction `CreateCoinStake` (ou équivalent), avant `ConnectBlock`.

---

### 3.4 Tests DAO (5 Tests)

```cpp
// src/test/khu_dao_tests.cpp

BOOST_AUTO_TEST_CASE(dao_cycle_boundary)
{
    // Vérifier détection cycle boundary
    BOOST_CHECK(IsDaoCycleBoundary(172800, 0));
    BOOST_CHECK(IsDaoCycleBoundary(345600, 0));
    BOOST_CHECK(!IsDaoCycleBoundary(172799, 0));
    BOOST_CHECK(!IsDaoCycleBoundary(100, 0));
}

BOOST_AUTO_TEST_CASE(dao_budget_calculation)
{
    KhuGlobalState state;
    state.U = 1000000 * COIN;   // 1M KHU
    state.Ur = 500000 * COIN;   // 500K KHU

    // Budget = 0.5% × (1M + 0.5M) = 0.5% × 1.5M = 7500 PIV
    CAmount expected = (1500000 * COIN * 5) / 1000;
    CAmount actual = CalculateDAOBudget(state);

    BOOST_CHECK_EQUAL(actual, expected);
}

BOOST_AUTO_TEST_CASE(dao_budget_zero_supply)
{
    KhuGlobalState state;
    state.U = 0;
    state.Ur = 0;

    // Budget = 0 si supply = 0
    BOOST_CHECK_EQUAL(CalculateDAOBudget(state), 0);
}

BOOST_AUTO_TEST_CASE(dao_payment_coinstake)
{
    CMutableTransaction tx;
    CAmount dao_budget = 1000 * COIN;

    BOOST_CHECK(AddDaoPaymentToCoinstake(tx, dao_budget));
    BOOST_CHECK_EQUAL(tx.vout.size(), 1);
    BOOST_CHECK_EQUAL(tx.vout[0].nValue, dao_budget);
}

BOOST_AUTO_TEST_CASE(dao_overflow_protection)
{
    KhuGlobalState state;
    state.U = MAX_MONEY;
    state.Ur = MAX_MONEY;  // Overflow!

    // Should return 0 (overflow detected)
    BOOST_CHECK_EQUAL(CalculateDAOBudget(state), 0);
}
```

---

## 🔗 4. INTÉGRATION VALIDATION.CPP

### 4.1 Ordre ConnectBlock (SACRÉ)

```cpp
bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, ...)
{
    // ... (code existant)

    // === KHU PHASE 6 INTEGRATION ===
    if (IsKHUActive(nHeight)) {

        // 1. Charger état précédent
        KhuGlobalState prevState;
        if (!LoadKhuState(pindex->pprev->GetBlockHash(), prevState)) {
            return state.Invalid(REJECT_INVALID, "khu-state-missing");
        }

        KhuGlobalState newState = prevState;
        newState.nHeight = nHeight;
        newState.hashBlock = block.GetHash();

        // 2. APPLY DAILY YIELD (si trigger)
        if (!ApplyDailyYield(newState, nHeight, view)) {
            return state.Invalid(REJECT_INVALID, "khu-yield-failed");
        }

        // 3. APPLY TX KHU (MINT, REDEEM, STAKE, UNSTAKE)
        for (const auto& tx : block.vtx) {
            if (!ProcessKHUTransaction(tx, newState, view)) {
                return state.Invalid(REJECT_INVALID, "khu-tx-failed");
            }
        }

        // 4. APPLY BLOCK REWARD (émission PIVX, pas de modification Cr/Ur)
        // (code existant block reward)

        // 5. DOMC CYCLE FINALIZATION (si fin cycle)
        if (ShouldFinalizeDOMCCycle(newState, nHeight)) {
            if (!FinalizeDOMCCycle(newState, nHeight)) {
                return state.Invalid(REJECT_INVALID, "khu-domc-failed");
            }
        }

        // 6. DAO BUDGET (si fin cycle) — Intégration CKhuDAOManager
        // Note: DAO budget modifie coinstake (paiement ou burn)
        // Appelé AVANT création coinstake dans CreateCoinStake
        if (khuDAOManager.ShouldCreateDAOBudget(newState, nHeight)) {
            // FillDAOBudgetPayment sera appelé dans CreateCoinStake
            // (voir src/stakeinput.cpp ou équivalent)
            LogPrint(BCLog::KHU, "DAO Budget cycle @ height %u\n", nHeight);
        }

        // 7. CHECK INVARIANTS (SACRÉ)
        if (!newState.CheckInvariants()) {
            return state.Invalid(REJECT_INVALID, "khu-invariants-broken",
                                 strprintf("C=%lld U=%lld Cr=%lld Ur=%lld",
                                          newState.C, newState.U, newState.Cr, newState.Ur));
        }

        // 8. PERSIST STATE
        if (!PersistKhuState(newState)) {
            return state.Invalid(REJECT_INVALID, "khu-persist-failed");
        }
    }

    // ... (reste du code)
}
```

### 4.2 Ordre DisconnectBlock (Reorg)

```cpp
bool DisconnectBlock(const CBlock& block, CBlockIndex* pindex, ...)
{
    // ... (code existant)

    if (IsKHUActive(nHeight)) {

        // ORDRE INVERSE:
        // 1. Load current state
        // 2. Undo DAO budget (si applicable)
        // 3. Undo DOMC cycle (si applicable)
        // 4. Undo TX KHU (UNSTAKE → STAKE → REDEEM → MINT)
        // 5. Undo Daily Yield
        // 6. Restore prev state

        KhuGlobalState currentState;
        if (!LoadKhuState(block.GetHash(), currentState)) {
            return error("DisconnectBlock: khu-state-missing");
        }

        // Undo yield
        if (!UndoDailyYield(currentState, nHeight, view)) {
            return error("DisconnectBlock: khu-undo-yield-failed");
        }

        // Undo TX...
        // (code existant)
    }
}
```

---

## ✅ 5. CHECKLIST DÉVELOPPEMENT

### Phase 6.1 — Yield Engine
- [ ] `khu_yield.h` signatures
- [ ] `khu_yield.cpp` implémentation
  - [ ] `CalculateDailyYield` (avec __int128)
  - [ ] `ApplyDailyYield` (streaming notes)
  - [ ] `UndoDailyYield` (delta storage)
  - [ ] `ApplyYieldToAllNotes` (LevelDB cursor + batch)
- [ ] Tests yield (`khu_yield_tests.cpp`, 15 tests)
- [ ] Python stress tests (`khu_phase6_yield.py`)

### Phase 6.2 — DOMC R%
- [ ] `khu_domc.h` structures + signatures
- [ ] `khu_domc.cpp` implémentation
  - [ ] `CalculateRMaxDynamic` (schedule 30%→4%)
  - [ ] `GetDOMCPhase` (ACTIVE/COMMIT/NOTICE)
  - [ ] `CalculateRMedian` (agrégation votes)
  - [ ] `FinalizeDOMCCycle` (activation nouveau R%)
- [ ] Extension `CMasternodePing` (nRCommitment, nRProposal, nRSecret)
- [ ] Tests DOMC (`khu_domc_tests.cpp`, 15 tests)
- [ ] Python functional tests (`khu_phase6_domc.py`)

### Phase 6.3 — DAO Budget (Automatique)
- [ ] `khu_dao.{h,cpp}` — Budget automatique (3 fonctions)
  - [ ] `IsDaoCycleBoundary(height, activation)` — Détecter cycle boundary
  - [ ] `CalculateDAOBudget(state)` — Calculer 0.5% × (U+Ur)
  - [ ] `AddDaoPaymentToCoinstake(tx, amount)` — Ajouter paiement DAO
- [ ] Définir `daoScript` dans chainparams (adresse treasury DAO)
- [ ] Tests DAO (`khu_dao_tests.cpp`, 5 tests)
  - [ ] Cycle boundary detection
  - [ ] Budget calculation (0.5%)
  - [ ] Zero supply handling
  - [ ] Coinstake payment
  - [ ] Overflow protection
- [ ] Python tests (`khu_phase6_dao.py`)

### Phase 6.4 — Intégration
- [ ] `validation.cpp` ConnectBlock (ordre yield→tx→reward→domc→dao→invariants)
- [ ] `validation.cpp` DisconnectBlock (undo inverse)
- [ ] Locks (`cs_khu` + `AssertLockHeld`)
- [ ] Logging (`BCLog::KHU`)

### Phase 6.5 — Tests & Rapport
- [ ] `make check` (42+ tests Phase 6)
- [ ] Functional tests Python (yield, domc, dao, stress)
- [ ] Rapport Phase 6 (`RAPPORT_PHASE6_FINAL.md`)
  - [ ] Implémentation summary
  - [ ] Attack vectors testés
  - [ ] Checklist invariants

---

## 🚫 INTERDICTIONS ABSOLUES

### NE PAS TOUCHER (Phases 1-5 gelées)
- ❌ `khu_state.h` (struct KhuGlobalState)
- ❌ `khu_tx.cpp` (MINT/REDEEM/STAKE/UNSTAKE)
- ❌ `sapling_operation.cpp` (Sapling ZKHU)
- ❌ Constants: BLOCKS_PER_DAY, MATURITY, BLOCKS_PER_YEAR
- ❌ Émission PIVX (max(6-year, 0))

### NE PAS FAIRE
- ❌ Modifier C, U en dehors de MINT/REDEEM/UNSTAKE
- ❌ Modifier Cr, Ur en dehors de YIELD/UNSTAKE
- ❌ Utiliser float/double (int64_t uniquement)
- ❌ Violer ordre ConnectBlock: Yield → TX → Reward → CheckInvariants → Persist
- ❌ Injecter Cr/Ur depuis émission PIVX (KHUPoolInjection = 0)

---

## 📝 PROCHAINES ÉTAPES

1. **Validation architecture** — User approve ce document
2. **Branch création** — `git checkout -b khu-phase6-domc-yield-dao`
3. **Implémentation squelettes** — Créer fichiers .h/.cpp avec TODOs
4. **Tests TDD** — Écrire tests AVANT implémentation complète
5. **Implémentation itérative** — Yield → DOMC → DAO
6. **Intégration** — `validation.cpp` hooks
7. **Rapport final** — `RAPPORT_PHASE6_FINAL.md`
8. **PR** — Merge après validation complète

---

**FIN ARCHITECTURE PHASE 6**
