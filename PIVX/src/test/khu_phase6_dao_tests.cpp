// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 6.3: DAO Treasury (Internal Pool)
//
// Canonical specification: docs/blueprints/phase6/06-DAO-TREASURY.md
// Implementation: DAO Treasury as internal pool T in KhuGlobalState
//
// NEW FORMULA CONSENSUS (2025-11-27):
//   T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
//   Where T_DIVISOR = 8 (from khu_domc.h)
//   At R=40%: T_annual ≈ 5% of U (scales with R% over 33 years)
//   Trigger: Every 1440 blocks (daily, unified with yield)
//
// Tests verify:
// - Budget calculation: (U × R) / 10000 / 8 / 365 every 1440 blocks
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
#include "khu/khu_domc.h"
#include "khu/khu_state.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(khu_phase6_dao_tests, BasicTestingSetup)

// ============================================================================
// HELPER: Calculate expected budget using the NEW canonical formula
// ============================================================================
static CAmount ExpectedDaoBudget(CAmount U, uint16_t R_annual)
{
    // NEW Formula (2025-11-27): T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
    // Where T_DIVISOR = 8 from khu_domc.h
    boost::multiprecision::int128_t base = static_cast<boost::multiprecision::int128_t>(U);
    boost::multiprecision::int128_t budget = (base * R_annual) / 10000 / khu_domc::T_DIVISOR / 365;
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
// TEST 2: CalculateDAOBudget - NEW Formula validation (T_DIVISOR=8)
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_budget_calculation_basic)
{
    KhuGlobalState state;
    state.SetNull();

    // Zero state → zero budget
    state.U = 0;
    state.R_annual = 4000;  // 40%
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), 0);

    // U = 1,000,000 PIV, R = 40% (4000 bp)
    // Budget = 1,000,000 × COIN × 4000 / 10000 / 8 / 365
    // = 1e14 × 4000 / 10000 / 8 / 365 = 1,369,863,013,698 satoshis ≈ 13698.63 KHU/day
    state.U = 1000000 * COIN;
    state.R_annual = 4000;
    CAmount expected = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected);
    BOOST_CHECK(expected > 0);  // Verify non-zero

    // Same U with lower R% (2000 bp = 20%)
    // Budget should be halved
    state.R_annual = 2000;
    CAmount expected_lower = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected_lower);
    BOOST_CHECK(expected_lower < expected);  // Lower R% = lower budget

    // U = 10,000,000 PIV, R = 40%
    state.U = 10000000 * COIN;
    state.R_annual = 4000;
    expected = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(khu_dao::CalculateDAOBudget(state), expected);
}

BOOST_AUTO_TEST_CASE(dao_budget_calculation_precision)
{
    KhuGlobalState state;
    state.SetNull();
    state.R_annual = 4000;  // 40%

    // Test small values (should round down due to integer division)
    // U = 100 PIV
    state.U = 100 * COIN;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    CAmount expected = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(budget, expected);

    // U = 199 PIV
    state.U = 199 * COIN;
    budget = khu_dao::CalculateDAOBudget(state);
    expected = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(budget, expected);
}

BOOST_AUTO_TEST_CASE(dao_budget_overflow_protection)
{
    KhuGlobalState state;
    state.SetNull();
    state.R_annual = 4000;  // 40%

    // Large but valid values (should not overflow with int128_t)
    // U = 100 million PIV
    state.U = 100000000LL * COIN;
    CAmount budget = khu_dao::CalculateDAOBudget(state);
    CAmount expected = ExpectedDaoBudget(state.U, state.R_annual);
    BOOST_CHECK_EQUAL(budget, expected);
    BOOST_CHECK(budget > 0);

    // Near CAmount max (should handle gracefully)
    state.U = std::numeric_limits<CAmount>::max() / 2;
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
    state.R_annual = 4000;  // 40%
    state.T = 0;

    CAmount expected_budget = ExpectedDaoBudget(state.U, state.R_annual);

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
    state.R_annual = 4000;
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
    state.R_annual = 4000;
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
    state.R_annual = 4000;

    CAmount budget = ExpectedDaoBudget(state.U, state.R_annual);
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
    state.R_annual = 4000;
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
    state.R_annual = 4000;
    state.T = 1000 * COIN; // Less than budget

    CAmount budget = ExpectedDaoBudget(state.U, state.R_annual);

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
    state.R_annual = 4000;
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
    state.R_annual = 4000;
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
    state.R_annual = 4000;

    // Genesis block → no accumulation
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, 0, params));
    BOOST_CHECK_EQUAL(state.T, 0);

    // First daily boundary from genesis (0 + 1440 = 1440)
    uint32_t nHeight = 1440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    BOOST_CHECK_EQUAL(state.T, 0); // Budget is 0 since U=0
}

BOOST_AUTO_TEST_CASE(dao_very_large_state)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 1000000;

    KhuGlobalState state;
    state.SetNull();
    // Large but realistic values (200M PIV)
    state.U = 200000000LL * COIN;
    state.R_annual = 4000;
    state.T = 0;

    uint32_t nHeight = 1001440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));

    // Expected: (200M × COIN × 4000) / 10000 / 20 / 365
    CAmount expected_budget = ExpectedDaoBudget(state.U, state.R_annual);
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
    state.R_annual = 4000;
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
// TEST 7: ~5% Annual Validation at R=40% (365 days accumulation)
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_five_percent_annual_validation)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;  // 1 million KHU
    state.R_annual = 4000;     // 40%
    state.T = 0;

    // Simulate 365 daily accumulations
    for (int day = 1; day <= 365; day++) {
        uint32_t nHeight = day * 1440;
        BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    }

    // After 365 days, T should equal 365 × daily_budget
    // NEW Formula: T_daily = (U × R) / 10000 / T_DIVISOR / 365
    //
    // At R=40% (4000 bp), T_DIVISOR=8:
    // - U = 1,000,000 COIN = 1e14 satoshis
    // - Daily budget = 1e14 × 4000 / 10000 / 8 / 365 ≈ 1,369,863,013 satoshis
    // - Yearly = 365 × daily_budget ≈ 5% of U
    CAmount daily_budget = ExpectedDaoBudget(state.U, state.R_annual);
    CAmount expected_yearly = daily_budget * 365;

    // Verify T matches 365 × daily_budget (exact match expected)
    BOOST_CHECK_EQUAL(state.T, expected_yearly);

    // Verify T is positive and reasonable
    BOOST_CHECK(state.T > 0);

    // Verify approximately 5% at R=40%
    // T_annual = U × R / 10000 / T_DIVISOR = U × 4000 / 10000 / 8 = U × 0.05 = 5%
    // Allow small rounding error
    CAmount expected_five_percent = state.U / 20;  // 5% = U/20
    BOOST_CHECK(state.T > expected_five_percent * 99 / 100);  // At least 99% of 5%
    BOOST_CHECK(state.T < expected_five_percent * 101 / 100); // At most 101% of 5%
}

// ============================================================================
// TEST 8: R% scaling - T% decreases as R% decreases
// ============================================================================

BOOST_AUTO_TEST_CASE(dao_r_percent_scaling)
{
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;  // 1 million KHU
    state.T = 0;

    // At R=40%, T ≈ 2%
    state.R_annual = 4000;
    uint32_t nHeight = 1440;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    CAmount budget_at_40 = state.T;

    // At R=20%, T ≈ 1% (half of 2%)
    state.T = 0;
    state.R_annual = 2000;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    CAmount budget_at_20 = state.T;

    // At R=7% (floor), T ≈ 0.35%
    state.T = 0;
    state.R_annual = 700;
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    CAmount budget_at_7 = state.T;

    // Verify scaling: budget_at_40 > budget_at_20 > budget_at_7
    BOOST_CHECK(budget_at_40 > budget_at_20);
    BOOST_CHECK(budget_at_20 > budget_at_7);

    // Verify proportions (with some tolerance for integer division)
    BOOST_CHECK(budget_at_20 * 2 >= budget_at_40 * 95 / 100);  // 20% should be ~half of 40%
    BOOST_CHECK(budget_at_20 * 2 <= budget_at_40 * 105 / 100);
}

BOOST_AUTO_TEST_SUITE_END()
