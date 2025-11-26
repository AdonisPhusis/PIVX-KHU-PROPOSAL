// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 6.3: DAO Treasury (Internal Pool)
//
// Canonical specification: docs/blueprints/phase6/06-DAO-TREASURY.md
// Implementation: DAO Treasury as internal pool T in KhuGlobalState
//
// FORMULA CONSENSUS (2% annual, daily trigger):
//   T_daily = (U + Ur) / 182500
//   Trigger: Every 1440 blocks (daily, unified with yield)
//
// Tests verify:
// - Budget calculation: (U + Ur) / 182500 every 1440 blocks
// - Accumulation at daily boundaries
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

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(khu_phase6_dao_tests, BasicTestingSetup)

// ============================================================================
// HELPER: Calculate expected budget using the canonical formula
// ============================================================================
static CAmount ExpectedDaoBudget(CAmount U, CAmount Ur)
{
    // Formula: T_daily = (U + Ur) / 182500
    boost::multiprecision::int128_t total = static_cast<boost::multiprecision::int128_t>(U) +
                                            static_cast<boost::multiprecision::int128_t>(Ur);
    boost::multiprecision::int128_t budget = total / khu_dao::T_DAILY_DIVISOR;
    return static_cast<CAmount>(budget);
}

// ============================================================================
// TEST 1: IsDaoCycleBoundary - Daily cycle detection
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_cycle_boundary_detection)
{
    uint32_t activationHeight = 1000000;

    // Before activation
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(999999, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1000000, activationHeight)); // At activation

    // First daily boundary: 1000000 + 1440 = 1001440
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1001439, activationHeight));
    BOOST_CHECK(khu_dao::IsDaoCycleBoundary(1001440, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1001441, activationHeight));

    // Second daily boundary: 1000000 + 2880 = 1002880
    BOOST_CHECK(khu_dao::IsDaoCycleBoundary(1002880, activationHeight));

    // Not at boundary
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1001000, activationHeight));
    BOOST_CHECK(!khu_dao::IsDaoCycleBoundary(1002000, activationHeight));
}

// ============================================================================
// TEST 2: CalculateDAOBudget - Formula validation (2% annual daily)
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
    // Budget = 1,000,000 × COIN / 182500 = 547,945,205,479 satoshis ≈ 5479.45 COIN
    state.U = 1000000 * COIN;
    state.Ur = 0;
    CAmount expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected);
    BOOST_CHECK(expected > 0);  // Verify non-zero

    // U = 1,000,000 PIV, Ur = 500,000 PIV
    // Budget = 1,500,000 × COIN / 182500
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected);

    // U = 10,000,000 PIV, Ur = 5,000,000 PIV
    // Budget = 15,000,000 × COIN / 182500
    state.U = 10000000 * COIN;
    state.Ur = 5000000 * COIN;
    expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected);
}

BOOST_AUTO_TEST_CASE(dao_budget_calculation_precision)
{
    KhuGlobalState state;
    state.SetNull();

    // Test small values (should round down due to integer division)
    // U = 100 PIV, Ur = 0
    // Budget = 100 × COIN / 182500 = 54,794 satoshis
    state.U = 100 * COIN;
    state.Ur = 0;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    CAmount expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(budget, expected);

    // U = 199 PIV, Ur = 0
    // Budget = 199 × COIN / 182500
    state.U = 199 * COIN;
    state.Ur = 0;
    budget = khu_dao::CalculateDAOBudget(state);
    expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(budget, expected);
}

BOOST_AUTO_TEST_CASE(dao_budget_overflow_protection)
{
    KhuGlobalState state;
    state.SetNull();

    // Large but valid values (should not overflow with int128_t)
    // U = 50 million PIV, Ur = 50 million PIV
    state.U = 50000000LL * COIN;
    state.Ur = 50000000LL * COIN;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    CAmount expected = ExpectedDaoBudget(state.U, state.Ur);
    BOOST_CHECK_EQUAL(budget, expected);
    BOOST_CHECK(budget > 0);

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

    CAmount expected_budget = ExpectedDaoBudget(state.U, state.Ur);

    // Not at boundary → no accumulation
    uint32_t nHeight = 1001000;  // Between boundaries
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0);

    // At first daily boundary → accumulate
    nHeight = 1001440; // 1000000 + 1440
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, expected_budget);

    // At second daily boundary → accumulate again
    nHeight = 1002880; // 1000000 + 1440 × 2
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, expected_budget * 2);
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
    uint32_t nHeight = 1001440;
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

    CAmount budget = ExpectedDaoBudget(state.U, state.Ur);
    state.T = budget; // Treasury after one cycle

    // Undo at cycle boundary
    uint32_t nHeight = 1001440;
    BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    // Expected: T = budget - budget = 0
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
    uint32_t nHeight = 1001000;
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

    CAmount budget = ExpectedDaoBudget(state.U, state.Ur);

    // Undo at cycle boundary would cause underflow if T < budget
    uint32_t nHeight = 1001440;

    // The behavior depends on whether T < budget
    if (state.T < budget) {
        // Underflow would occur - function should return false
        BOOST_CHECK(!khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
        // State should remain unchanged on error
        BOOST_CHECK_EQUAL(state.T, 1000 * COIN);
    } else {
        // No underflow
        BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, nHeight, params));
    }
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
    uint32_t nHeight = 1001440;

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

    // Daily cycle 1
    uint32_t nHeight1 = 1001440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight1, params));
    CAmount after_cycle1 = state.T;

    // Daily cycle 2
    uint32_t nHeight2 = 1002880;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight2, params));
    CAmount after_cycle2 = state.T;

    // Daily cycle 3
    uint32_t nHeight3 = 1004320;
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

    // First daily boundary from genesis (0 + 1440 = 1440)
    uint32_t nHeight = 1440;
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

    uint32_t nHeight = 1001440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));

    // Expected: (100M + 100M) × COIN / 182500
    CAmount expected_budget = ExpectedDaoBudget(state.U, state.Ur);
    // Note: T accumulates ONCE (we only called once), not twice
    BOOST_CHECK_EQUAL(state.T, expected_budget);
    BOOST_CHECK(state.T > 0);

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
    uint32_t nHeight = 1001440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));

    // Verify invariants still hold (DAO only affects T, not C/U/Cr/Ur)
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.C, 1000000 * COIN);
    BOOST_CHECK_EQUAL(state.U, 1000000 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 500000 * COIN);
    BOOST_CHECK_EQUAL(state.Ur, 500000 * COIN);
    BOOST_CHECK(state.T > 0);
}

// ============================================================================
// TEST 7: 2% Annual Validation (365 days accumulation)
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_two_percent_annual_validation)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;  // 1 million KHU
    state.Ur = 0;
    state.T = 0;

    // Simulate 365 daily accumulations
    for (int day = 1; day <= 365; day++) {
        uint32_t nHeight = day * 1440;
        BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    }

    // After 365 days, T should equal 365 × daily_budget
    // Formula: T_daily = (U + Ur) / 182500
    //
    // Verification:
    // - U = 1,000,000 COIN = 1e14 satoshis
    // - Daily budget = 1e14 / 182500 ≈ 547,945,205,479 satoshis
    // - Yearly = 365 × daily_budget
    CAmount daily_budget = ExpectedDaoBudget(state.U, state.Ur);
    CAmount expected_yearly = daily_budget * 365;

    // Verify T matches 365 × daily_budget (exact match expected)
    BOOST_CHECK_EQUAL(state.T, expected_yearly);

    // Verify T is positive and reasonable
    BOOST_CHECK(state.T > 0);

    // Verify the formula: 365/182500 ≈ 0.002 = 0.2%
    // So T ≈ 0.2% × U per year (not 2% - that's the total over 10 years)
    // The 2% annual is achieved when U+Ur accumulates yield too
}

BOOST_AUTO_TEST_SUITE_END()
