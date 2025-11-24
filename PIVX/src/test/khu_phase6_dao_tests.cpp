// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 6.3: DAO Treasury (Internal Pool)
//
// Canonical specification: docs/blueprints/phase6/06-DAO-TREASURY.md
// Implementation: DAO Treasury as internal pool T in KhuGlobalState
//
// Tests verify:
// - Budget calculation: 0.5% × (U + Ur) every 172800 blocks (4 months)
// - Accumulation at cycle boundaries only
// - Undo support for reorgs
// - Overflow protection (int128_t arithmetic)
// - Edge cases: zero state, large values, genesis block
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "khu/khu_dao.h"
#include "khu/khu_state.h"

#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(khu_phase6_dao_tests, BasicTestingSetup)

// ============================================================================
// TEST 1: IsDaoCycleBoundary - Cycle detection
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_cycle_boundary_detection)
{
    uint32_t activationHeight = 1000000;

    // Before activation
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(999999, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1000000, activationHeight)); // At activation

    // First cycle boundary: 1000000 + 172800 = 1172800
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1172799, activationHeight));
    BOOST_CHECK(khu_dao::IsDaoCycleBoundary(1172800, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1172801, activationHeight));

    // Second cycle boundary: 1000000 + 344600 = 1345600
    BOOST_CHECK(khu_dao::IsDaoCycleBoundary(1345600, activationHeight));

    // Not at boundary
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1100000, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1200000, activationHeight));
}

// ============================================================================
// TEST 2: CalculateDAOBudget - Formula validation
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_budget_calculation_basic)
{
    KhuGlobalState state;
    state.SetNull();

    // Zero state → zero budget
    state.U = 0;
    state.Ur = 0;
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), 0);

    // U = 1,000,000 PIV, Ur = 0
    // Budget = 1,000,000 × 0.005 = 5,000 PIV
    state.U = 1000000 * COIN;
    state.Ur = 0;
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), 5000 * COIN);

    // U = 1,000,000 PIV, Ur = 500,000 PIV
    // Budget = 1,500,000 × 0.005 = 7,500 PIV
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), 7500 * COIN);

    // U = 10,000,000 PIV, Ur = 5,000,000 PIV
    // Budget = 15,000,000 × 0.005 = 75,000 PIV
    state.U = 10000000 * COIN;
    state.Ur = 5000000 * COIN;
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), 75000 * COIN);
}

BOOST_AUTO_TEST_CASE(dao_budget_calculation_precision)
{
    KhuGlobalState state;
    state.SetNull();

    // Test fractional budget (should round down)
    // U = 100 PIV, Ur = 0
    // Budget = 100 × 5 / 1000 = 0.5 PIV
    state.U = 100 * COIN;
    state.Ur = 0;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    BOOST_CHECK_EQUAL(budget, 50000000); // 0.5 * COIN

    // U = 199 PIV, Ur = 0
    // Budget = 199 × 5 / 1000 = 0.995 PIV
    state.U = 199 * COIN;
    state.Ur = 0;
    budget = khu_dao::CalculateDAOBudget(state);
    BOOST_CHECK_EQUAL(budget, 99500000); // 0.995 * COIN
}

BOOST_AUTO_TEST_CASE(dao_budget_overflow_protection)
{
    KhuGlobalState state;
    state.SetNull();

    // Large but valid values (should not overflow with int128_t)
    // U = 50 million PIV, Ur = 50 million PIV
    // Budget = 100 million × 0.005 = 500,000 PIV
    state.U = 50000000LL * COIN;
    state.Ur = 50000000LL * COIN;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    BOOST_CHECK_EQUAL(budget, 500000LL * COIN);

    // Near CAmount max (should handle gracefully)
    state.U = std::numeric_limits<CAmount>::max() / 2;
    state.Ur = std::numeric_limits<CAmount>::max() / 2;
    budget = khu_dao::CalculateDAOBudget(state);
    // Budget should be valid (int128_t prevents overflow)
    BOOST_CHECK(budget > 0);
}

// ============================================================================
// TEST 3: AccumulateDaoTreasuryIfNeeded - Integration
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_accumulation_at_boundary)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    state.T = 0;

    // Not at boundary → no accumulation
    uint32_t nHeight = 1100000;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0);

    // At first cycle boundary → accumulate
    nHeight = 1172800; // 1000000 + 172800
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    // Expected: (1,000,000 + 500,000) × 0.005 = 7,500 PIV
    BOOST_CHECK_EQUAL(state.T, 7500 * COIN);

    // At second cycle boundary → accumulate again
    nHeight = 1345600; // 1000000 + 172800 × 2
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    // Expected: T = 7500 + 7500 = 15,000 PIV
    BOOST_CHECK_EQUAL(state.T, 15000 * COIN);
}

BOOST_AUTO_TEST_CASE(dao_accumulation_before_activation)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 0;
    state.T = 0;

    // Before activation height → no accumulation
    uint32_t nHeight = 999999;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0);

    // At activation height → no accumulation (not cycle boundary)
    nHeight = 1000000;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0);
}

BOOST_AUTO_TEST_CASE(dao_accumulation_zero_budget)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 0;
    state.Ur = 0;
    state.T = 100 * COIN; // Existing treasury

    // At cycle boundary but zero budget → T unchanged
    uint32_t nHeight = 1172800;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 100 * COIN);
}

// ============================================================================
// TEST 4: UndoDaoTreasuryIfNeeded - Reorg support
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_undo_at_boundary)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    state.T = 7500 * COIN; // Treasury after one cycle

    // Undo at cycle boundary
    uint32_t nHeight = 1172800;
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    // Expected: T = 7500 - 7500 = 0
    BOOST_CHECK_EQUAL(state.T, 0);
}

BOOST_AUTO_TEST_CASE(dao_undo_not_at_boundary)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 0;
    state.T = 5000 * COIN;

    // Not at boundary → no undo
    uint32_t nHeight = 1100000;
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 5000 * COIN);
}

BOOST_AUTO_TEST_CASE(dao_undo_underflow_protection)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 0;
    state.T = 1000 * COIN; // Less than budget

    // Undo at cycle boundary would cause underflow
    // Budget = 1,000,000 × 0.005 = 5,000 PIV
    // T - budget = 1,000 - 5,000 = -4,000 (INVALID)
    uint32_t nHeight = 1172800;
    BOOST_CHECK(!khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    // State should remain unchanged on error
    BOOST_CHECK_EQUAL(state.T, 1000 * COIN);
}

// ============================================================================
// TEST 5: Round-trip testing (Accumulate → Undo)
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_roundtrip_single_cycle)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 2000000 * COIN;
    state.Ur = 1000000 * COIN;
    state.T = 10000 * COIN;

    CAmount initial_T = state.T;
    uint32_t nHeight = 1172800;

    // Accumulate
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    CAmount after_accumulate = state.T;
    BOOST_CHECK(after_accumulate > initial_T);

    // Undo
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, initial_T);
}

BOOST_AUTO_TEST_CASE(dao_roundtrip_multiple_cycles)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.U = 5000000 * COIN;
    state.Ur = 2500000 * COIN;
    state.T = 0;

    // Cycle 1
    uint32_t nHeight1 = 1172800;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight1, params));
    CAmount after_cycle1 = state.T;

    // Cycle 2
    uint32_t nHeight2 = 1345600;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight2, params));
    CAmount after_cycle2 = state.T;

    // Cycle 3
    uint32_t nHeight3 = 1518400;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight3, params));
    CAmount after_cycle3 = state.T;

    BOOST_CHECK(after_cycle2 > after_cycle1);
    BOOST_CHECK(after_cycle3 > after_cycle2);

    // Undo in reverse order
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight3, params));
    BOOST_CHECK_EQUAL(state.T, after_cycle2);

    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight2, params));
    BOOST_CHECK_EQUAL(state.T, after_cycle1);

    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight1, params));
    BOOST_CHECK_EQUAL(state.T, 0);
}

// ============================================================================
// TEST 6: Edge cases
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_genesis_state)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();

    // Genesis block → no accumulation
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, 0, params));
    BOOST_CHECK_EQUAL(state.T, 0);

    // First cycle boundary from genesis (0 + 172800 = 172800)
    uint32_t nHeight = 172800;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0); // Budget is 0 since U=Ur=0
}

BOOST_AUTO_TEST_CASE(dao_very_large_state)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    // Large but realistic values (100M PIV each)
    state.U = 100000000LL * COIN;
    state.Ur = 100000000LL * COIN;
    state.T = 0;

    uint32_t nHeight = 1172800;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));

    // Expected: (100M + 100M) × 0.005 = 1M PIV
    CAmount expected_budget = 1000000LL * COIN;
    BOOST_CHECK_EQUAL(state.T, expected_budget);

    // Verify undo works with large values
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0);
}

BOOST_AUTO_TEST_CASE(dao_invariants_preservation)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    state.C = 1000000 * COIN;
    state.U = 1000000 * COIN;
    state.Cr = 500000 * COIN;
    state.Ur = 500000 * COIN;
    state.T = 0;

    // Verify invariants before
    BOOST_CHECK(state.CheckInvariants());

    // Accumulate DAO budget
    uint32_t nHeight = 1172800;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));

    // Verify invariants still hold (DAO only affects T, not C/U/Cr/Ur)
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.C, 1000000 * COIN);
    BOOST_CHECK_EQUAL(state.U, 1000000 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 500000 * COIN);
    BOOST_CHECK_EQUAL(state.Ur, 500000 * COIN);
    BOOST_CHECK(state.T > 0);
}

BOOST_AUTO_TEST_SUITE_END()
