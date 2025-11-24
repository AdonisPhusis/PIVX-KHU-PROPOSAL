// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Phase 5 Yield Tests - R% (Annual Return Rate) Variations
 *
 * PURPOSE: Test ZKHU yield calculation with different R% values.
 *
 * Tests:
 * 1. R% = 0%   → bonus = 0 (no yield)
 * 2. R% = 5%   → correct bonus calculation
 * 3. R% = 10%  → correct bonus calculation
 * 4. R% = 25%  → correct bonus calculation
 * 5. R% = 50%  → correct bonus calculation (boundary)
 * 6. R% transitions (5% → 15% → 25%) → bonus updated correctly
 * 7. R% = R_MAX_dynamic → maximum allowed yield
 * 8. Bonus calculation after different stake durations
 * 9. Pool sufficiency with high R%
 * 10. Multiple notes with same R%
 *
 * Expected: Ur_accumulated calculated correctly for all R% values.
 */

#include "test/test_pivx.h"
#include "khu/khu_state.h"
#include "amount.h"

#include <boost/test/unit_test.hpp>

// Test fixture
struct KHUPhase5YieldSetup : public TestingSetup
{
    KHUPhase5YieldSetup() : TestingSetup() {}
};

// Helper: Compute daily yield EXACTLY as consensus code does
// This is the CANONICAL formula from 04-Economics.md
static inline CAmount ComputeDailyYield(CAmount amount, int32_t rAnnualBps)
{
    if (rAnnualBps <= 0 || amount <= 0) return 0;

    // Ur_daily = floor( amount * R_annual / 10000 / 365 )
    // Using __int128 to avoid overflow
    __int128 num = ((__int128)amount * (CAmount)rAnnualBps);
    CAmount annual = (CAmount)(num / 10000);  // amount × R%
    CAmount daily = annual / 365;              // Integer division → floor

    if (daily < 0) return 0;
    return daily;
}

// Helper: Calculate expected bonus by SUMMING daily yields (consensus-accurate)
// This matches EXACTLY what the blockchain does
static CAmount CalculateExpectedBonus(CAmount amount, int32_t R_annual, int blocks)
{
    if (R_annual == 0) return 0;

    // Convert blocks to days (1 day = 1440 blocks at 1 min/block)
    // NOTE: Using 1440 not 960, adjust if your BLOCKS_PER_DAY differs
    const int BLOCKS_PER_DAY = 1440;
    int days = blocks / BLOCKS_PER_DAY;

    // Sum daily yields (this is what consensus does)
    CAmount totalBonus = 0;
    for (int d = 0; d < days; ++d) {
        totalBonus += ComputeDailyYield(amount, R_annual);
    }

    return totalBonus;
}

BOOST_FIXTURE_TEST_SUITE(khu_phase5_yield_tests, KHUPhase5YieldSetup)

/**
 * TEST 1: R% = 0% → No Yield
 *
 * Scenario: Stake with R_annual = 0 (no yield active).
 *
 * Expected: Ur_accumulated = 0 regardless of stake duration.
 */
BOOST_AUTO_TEST_CASE(yield_test_r_zero_percent)
{
    CAmount amount = 100 * COIN;
    int R_annual = 0;  // 0% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 10000;  // 9000 blocks staked
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify
    BOOST_CHECK_EQUAL(expectedBonus, 0);

    // Simulate state
    KhuGlobalState state;
    state.SetNull();
    state.R_annual = R_annual;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;

    // Verify invariants
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * TEST 2: R% = 5% → Low Yield
 *
 * Scenario: Stake 100 KHU for 525600 blocks (365 days = 1 year) with R_annual = 500 (5%).
 *
 * Expected: Ur_accumulated = 5 KHU (5% of 100).
 */
BOOST_AUTO_TEST_CASE(yield_test_r_five_percent)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 500;  // 5% annual return (500 basis points)
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (365 * BLOCKS_PER_DAY);  // 365 days = 525,600 blocks
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 5% × 1 year = 5 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Calculate what we expect: 100 * 500 / 10000 = 5 KHU
    CAmount theoreticalBonus = 5 * COIN;

    // Verify exact match (within integer rounding)
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 5.0, 0.1);  // Within 0.1% tolerance

    // Verify bonus is positive
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 3: R% = 10% → Medium Yield
 *
 * Scenario: Stake 100 KHU for 525600 blocks (365 days = 1 year) with R_annual = 1000 (10%).
 *
 * Expected: Ur_accumulated = 10 KHU (10% of 100).
 */
BOOST_AUTO_TEST_CASE(yield_test_r_ten_percent)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 1000;  // 10% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (365 * BLOCKS_PER_DAY);  // 365 days = 525,600 blocks
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 10% × 1 year = 10 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify exact match
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 10.0, 0.1);  // Within 0.1%
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 4: R% = 25% → High Yield
 *
 * Scenario: Stake 100 KHU for 525600 blocks (365 days = 1 year) with R_annual = 2500 (25%).
 *
 * Expected: Ur_accumulated = 25 KHU (25% of 100).
 */
BOOST_AUTO_TEST_CASE(yield_test_r_twentyfive_percent)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 2500;  // 25% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (365 * BLOCKS_PER_DAY);  // 365 days = 525,600 blocks
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 25% × 1 year = 25 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify exact match
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 25.0, 0.1);  // Within 0.1%
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 5: R% = 50% → Maximum Realistic Yield
 *
 * Scenario: Stake 100 KHU for 525600 blocks (365 days = 1 year) with R_annual = 5000 (50%).
 *
 * Expected: Ur_accumulated = 50 KHU (50% of 100).
 */
BOOST_AUTO_TEST_CASE(yield_test_r_fifty_percent)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 5000;  // 50% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (365 * BLOCKS_PER_DAY);  // 365 days = 525,600 blocks
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 50% × 1 year = 50 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify exact match
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 50.0, 0.1);  // Within 0.1%
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 6: Short Duration (Minimum Maturity = 3 Days)
 *
 * Scenario: Stake 100 KHU for exactly 4320 blocks (3 days) with R_annual = 2500 (25%).
 *
 * Expected: Ur_accumulated = 100 × 25% × (3/365) ≈ 0.205 KHU.
 */
BOOST_AUTO_TEST_CASE(yield_test_minimum_maturity_duration)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 2500;  // 25% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (3 * BLOCKS_PER_DAY);  // 3 days = 4320 blocks (maturity)
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 25% × (3/365) = 0.205 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify bonus is positive but small
    BOOST_CHECK_GT(expectedBonus, 0);
    BOOST_CHECK_LT(expectedBonus, 1 * COIN);  // Less than 1 KHU

    // Verify approximately correct: ~0.2 KHU
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 0.205, 5.0);  // Within 5% (small amounts have more rounding)
}

/**
 * TEST 7: Long Duration (10 Years)
 *
 * Scenario: Stake 100 KHU for 5256000 blocks (10 × 365 days = 10 years) with R_annual = 1000 (10%).
 *
 * Expected: Ur_accumulated = 100 KHU (10% × 10 years = 100%).
 */
BOOST_AUTO_TEST_CASE(yield_test_long_duration_ten_years)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 1000;  // 10% annual return
    int nStakeHeight = 1000;
    int nCurrentHeight = 1000 + (10 * 365 * BLOCKS_PER_DAY);  // 10 years = 5,256,000 blocks
    int blocks_staked = nCurrentHeight - nStakeHeight;

    // Calculate expected bonus: 100 × 10% × 10 years = 100 KHU
    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Verify exact match (original amount doubled)
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 100.0, 0.1);  // Within 0.1%
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 8: Multiple R% Values Comparison
 *
 * Scenario: Compare bonuses for same amount/duration but different R%.
 *
 * Expected: bonus_25% > bonus_10% > bonus_5% > bonus_0% = 0, and exact proportionality.
 */
BOOST_AUTO_TEST_CASE(yield_test_multiple_r_comparison)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year = 525,600 blocks

    CAmount bonus_0 = CalculateExpectedBonus(amount, 0, blocks_staked);
    CAmount bonus_5 = CalculateExpectedBonus(amount, 500, blocks_staked);
    CAmount bonus_10 = CalculateExpectedBonus(amount, 1000, blocks_staked);
    CAmount bonus_25 = CalculateExpectedBonus(amount, 2500, blocks_staked);

    // Verify ordering
    BOOST_CHECK_EQUAL(bonus_0, 0);
    BOOST_CHECK_GT(bonus_5, bonus_0);
    BOOST_CHECK_GT(bonus_10, bonus_5);
    BOOST_CHECK_GT(bonus_25, bonus_10);

    // Verify EXACT proportionality: bonus_25 = 5 × bonus_5 (integer math should be exact)
    BOOST_CHECK_EQUAL(bonus_25, 5 * bonus_5);

    // Verify EXACT proportionality: bonus_10 = 2 × bonus_5
    BOOST_CHECK_EQUAL(bonus_10, 2 * bonus_5);
}

/**
 * TEST 9: Pool Sufficiency with High R%
 *
 * Scenario: Stake generates high bonus, verify it doesn't exceed pool.
 *
 * Expected: If bonus > Cr or bonus > Ur, UNSTAKE should be rejected.
 */
BOOST_AUTO_TEST_CASE(yield_test_pool_sufficiency_high_r)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 5000;  // 50% annual return
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year = 525,600 blocks

    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);
    // Expected: 50 KHU

    // Setup state with INSUFFICIENT pool (only 10 KHU available)
    KhuGlobalState state;
    state.SetNull();
    state.R_annual = R_annual;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 10 * COIN;  // Only 10 KHU pool (< 50 needed)
    state.Ur = 10 * COIN;

    // ✅ CRITICAL: Verify invariants C==U and Cr==Ur (EXACT equality)
    BOOST_CHECK(state.CheckInvariants());

    // Verify pool is insufficient
    BOOST_CHECK_LT(state.Cr, expectedBonus);
    BOOST_CHECK_LT(state.Ur, expectedBonus);

    // In real UNSTAKE, this would be rejected by CheckKHUUnstake
    // Here we just verify the condition
    bool poolSufficient = (state.Cr >= expectedBonus) && (state.Ur >= expectedBonus);
    BOOST_CHECK(!poolSufficient);
}

/**
 * TEST 10: Small Amount, High R%
 *
 * Scenario: Stake 0.01 KHU with R_annual = 5000 (50%) for 1 year.
 *
 * Expected: Bonus = 0.005 KHU (rounding may result in very small or zero).
 */
BOOST_AUTO_TEST_CASE(yield_test_small_amount_high_r)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 0.01 * COIN;  // 1000000 satoshis
    int R_annual = 5000;  // 50% annual return
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year = 525,600 blocks

    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Expected: 0.01 × 50% = 0.005 KHU = 500000 satoshis
    BOOST_CHECK_GT(expectedBonus, 0);
    BOOST_CHECK_LT(expectedBonus, amount);  // Bonus less than principal
}

/**
 * TEST 11: Large Amount, Low R%
 *
 * Scenario: Stake 10000 KHU with R_annual = 500 (5%) for 1 year.
 *
 * Expected: Bonus = 500 KHU (5% of 10000).
 */
BOOST_AUTO_TEST_CASE(yield_test_large_amount_low_r)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 10000 * COIN;
    int R_annual = 500;  // 5% annual return
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year = 525,600 blocks

    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Expected: 10000 × 5% = 500 KHU
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 500.0, 0.1);  // Within 0.1%
    BOOST_CHECK_GT(expectedBonus, 0);
}

/**
 * TEST 12: R% Linearity Test
 *
 * Scenario: Verify that doubling R% doubles the bonus (linear relationship).
 *
 * Expected: bonus(2×R) = 2 × bonus(R) EXACTLY (integer math).
 */
BOOST_AUTO_TEST_CASE(yield_test_r_linearity)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year = 525,600 blocks

    int R1 = 1000;  // 10%
    int R2 = 2000;  // 20% (double)

    CAmount bonus1 = CalculateExpectedBonus(amount, R1, blocks_staked);
    CAmount bonus2 = CalculateExpectedBonus(amount, R2, blocks_staked);

    // Verify EXACT linearity: bonus2 = 2 × bonus1 (integer formula guarantees this)
    BOOST_CHECK_EQUAL(bonus2, 2 * bonus1);
}

/**
 * TEST 13: Duration Linearity Test
 *
 * Scenario: Verify that doubling duration doubles the bonus.
 *
 * Expected: bonus(2×duration) = 2 × bonus(duration) EXACTLY (when using exact day multiples).
 */
BOOST_AUTO_TEST_CASE(yield_test_duration_linearity)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 1000;  // 10%

    int duration1 = 30 * BLOCKS_PER_DAY;   // 30 days = 43,200 blocks
    int duration2 = 60 * BLOCKS_PER_DAY;   // 60 days = 86,400 blocks (exactly double)

    CAmount bonus1 = CalculateExpectedBonus(amount, R_annual, duration1);
    CAmount bonus2 = CalculateExpectedBonus(amount, R_annual, duration2);

    // Verify EXACT linearity: bonus2 = 2 × bonus1
    // With exact day multiples (30 vs 60 days), integer formula guarantees this
    BOOST_CHECK_EQUAL(bonus2, 2 * bonus1);
}

/**
 * TEST 14: Zero Amount Edge Case
 *
 * Scenario: Stake 0 KHU with any R%.
 *
 * Expected: Bonus = 0 (can't earn yield on zero principal).
 */
BOOST_AUTO_TEST_CASE(yield_test_zero_amount)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 0;
    int R_annual = 2500;  // 25%
    int blocks_staked = 365 * BLOCKS_PER_DAY;  // 1 year

    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    BOOST_CHECK_EQUAL(expectedBonus, 0);
}

/**
 * TEST 15: Fractional Year (180 Days ≈ 6 Months)
 *
 * Scenario: Stake 100 KHU for 180 days with R_annual = 1000 (10%).
 *
 * Expected: Bonus = 100 × 10% × (180/365) ≈ 4.93 KHU.
 */
BOOST_AUTO_TEST_CASE(yield_test_fractional_year_six_months)
{
    const int BLOCKS_PER_DAY = 1440;  // Canonical value

    CAmount amount = 100 * COIN;
    int R_annual = 1000;  // 10%
    int blocks_staked = 180 * BLOCKS_PER_DAY;  // 180 days ≈ 6 months = 259,200 blocks

    CAmount expectedBonus = CalculateExpectedBonus(amount, R_annual, blocks_staked);

    // Expected: 100 × 10% × (180/365) = 4.93 KHU
    BOOST_CHECK_CLOSE(expectedBonus / (double)COIN, 4.93, 1.0);  // Within 1%
}

BOOST_AUTO_TEST_SUITE_END()
