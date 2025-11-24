// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 6.1: Daily Yield Engine
//
// Canonical specification: docs/blueprints/phase6/01-DAILY-YIELD.md
// Implementation: Daily yield distribution via LevelDB streaming
//
// Tests verify:
// - Yield interval detection (1440 blocks)
// - Note maturity checking (4320 blocks)
// - Daily yield formula: (amount × R_annual / 10000) / 365
// - ApplyDailyYield: Ur += totalYield
// - UndoDailyYield: Ur -= totalYield (reorg support)
// - Overflow protection (int128_t arithmetic)
// - Deterministic behavior (no floating point)
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "khu/khu_state.h"
#include "khu/khu_yield.h"

#include <boost/test/unit_test.hpp>
#include <limits>

BOOST_FIXTURE_TEST_SUITE(khu_phase6_yield_tests, BasicTestingSetup)

// ============================================================================
// TEST 1: ShouldApplyDailyYield - Interval detection
// ============================================================================

BOOST_AUTO_TEST_CASE(yield_interval_detection)
{
    uint32_t activationHeight = 1000000;
    uint32_t lastYieldHeight = 0;

    // Before activation
    BOOST_CHECK(!khu_yield::ShouldApplyDailyYield(999999, activationHeight, lastYieldHeight));

    // At activation (first yield)
    BOOST_CHECK(khu_yield::ShouldApplyDailyYield(1000000, activationHeight, 0));

    // After first yield
    lastYieldHeight = 1000000;

    // Not at interval yet
    BOOST_CHECK(!khu_yield::ShouldApplyDailyYield(1000001, activationHeight, lastYieldHeight));
    BOOST_CHECK(!khu_yield::ShouldApplyDailyYield(1001439, activationHeight, lastYieldHeight));

    // At 1440 block interval
    BOOST_CHECK(khu_yield::ShouldApplyDailyYield(1001440, activationHeight, lastYieldHeight));

    // After interval
    BOOST_CHECK(!khu_yield::ShouldApplyDailyYield(1001441, activationHeight, lastYieldHeight));

    // Second interval
    lastYieldHeight = 1001440;
    BOOST_CHECK(khu_yield::ShouldApplyDailyYield(1002880, activationHeight, lastYieldHeight));
}

// ============================================================================
// TEST 2: IsNoteMature - Maturity checking
// ============================================================================

BOOST_AUTO_TEST_CASE(note_maturity_checking)
{
    uint32_t noteHeight = 1000000;

    // Immature: less than 4320 blocks
    BOOST_CHECK(!khu_yield::IsNoteMature(noteHeight, noteHeight));
    BOOST_CHECK(!khu_yield::IsNoteMature(noteHeight, noteHeight + 1));
    BOOST_CHECK(!khu_yield::IsNoteMature(noteHeight, noteHeight + 4319));

    // Mature: exactly 4320 blocks
    BOOST_CHECK(khu_yield::IsNoteMature(noteHeight, noteHeight + 4320));

    // Mature: more than 4320 blocks
    BOOST_CHECK(khu_yield::IsNoteMature(noteHeight, noteHeight + 4321));
    BOOST_CHECK(khu_yield::IsNoteMature(noteHeight, noteHeight + 10000));

    // Edge case: invalid (currentHeight < noteHeight)
    BOOST_CHECK(!khu_yield::IsNoteMature(noteHeight, noteHeight - 1));
}

// ============================================================================
// TEST 3: CalculateDailyYieldForNote - Formula validation
// ============================================================================

BOOST_AUTO_TEST_CASE(daily_yield_calculation_basic)
{
    // Formula: daily = (amount × R_annual / 10000) / 365

    // Zero cases
    BOOST_CHECK_EQUAL(khu_yield::CalculateDailyYieldForNote(0, 1500), 0);
    BOOST_CHECK_EQUAL(khu_yield::CalculateDailyYieldForNote(1000 * COIN, 0), 0);

    // Example 1: 1000 KHU at 15.00% annual (1500 basis points)
    // annual = 1000 × 1500 / 10000 = 150 KHU
    // daily = 150 / 365 = 0.41095890... KHU
    CAmount amount1 = 1000 * COIN;
    uint16_t R1 = 1500;
    CAmount daily1 = khu_yield::CalculateDailyYieldForNote(amount1, R1);
    // Expected: 150 * COIN / 365 = 41095890410 satoshis
    BOOST_CHECK_EQUAL(daily1, (150 * COIN) / 365);

    // Example 2: 10000 KHU at 10.00% annual (1000 basis points)
    // annual = 10000 × 1000 / 10000 = 1000 KHU
    // daily = 1000 / 365 = 2.73972602... KHU
    CAmount amount2 = 10000 * COIN;
    uint16_t R2 = 1000;
    CAmount daily2 = khu_yield::CalculateDailyYieldForNote(amount2, R2);
    BOOST_CHECK_EQUAL(daily2, (1000 * COIN) / 365);

    // Example 3: 100 KHU at 5.00% annual (500 basis points)
    // annual = 100 × 500 / 10000 = 5 KHU
    // daily = 5 / 365 = 0.01369863... KHU
    CAmount amount3 = 100 * COIN;
    uint16_t R3 = 500;
    CAmount daily3 = khu_yield::CalculateDailyYieldForNote(amount3, R3);
    BOOST_CHECK_EQUAL(daily3, (5 * COIN) / 365);
}

BOOST_AUTO_TEST_CASE(daily_yield_calculation_precision)
{
    // Test rounding behavior (integer division truncates)

    // Small amount with low rate
    // 1 KHU at 1.00% = annual 0.01 KHU = daily 0.0000274 KHU
    CAmount amount = 1 * COIN;
    uint16_t R = 100; // 1.00%
    CAmount daily = khu_yield::CalculateDailyYieldForNote(amount, R);
    // annual = 100000000 × 100 / 10000 = 1000000 satoshis
    // daily = 1000000 / 365 = 2739 satoshis (truncated)
    BOOST_CHECK_EQUAL(daily, 2739);

    // Very small result (should not be zero unless inputs are zero)
    amount = 10 * COIN;
    R = 10; // 0.10%
    daily = khu_yield::CalculateDailyYieldForNote(amount, R);
    // annual = 1000000000 × 10 / 10000 = 1000000
    // daily = 1000000 / 365 = 2739
    BOOST_CHECK_EQUAL(daily, 2739);
}

BOOST_AUTO_TEST_CASE(daily_yield_overflow_protection)
{
    // Test that large values don't cause overflow

    // Large amount with reasonable rate
    CAmount largeAmount = 1000000 * COIN; // 1 million KHU
    uint16_t R = 1500; // 15.00%
    CAmount daily = khu_yield::CalculateDailyYieldForNote(largeAmount, R);

    // annual = 1000000 × 1500 / 10000 = 150000 KHU
    // daily = 150000 / 365 = 410.958904 KHU
    CAmount expected = (150000 * COIN) / 365;
    BOOST_CHECK_EQUAL(daily, expected);
    BOOST_CHECK(daily > 0);

    // Very large amount (close to MAX_MONEY but safe)
    // MAX_MONEY in PIVX is typically 21 million coins
    CAmount veryLarge = 10000000 * COIN; // 10 million KHU
    R = 2000; // 20.00%
    daily = khu_yield::CalculateDailyYieldForNote(veryLarge, R);

    // annual = 10000000 × 2000 / 10000 = 2000000 KHU
    // daily = 2000000 / 365 = 5479.452054 KHU
    expected = (2000000 * COIN) / 365;
    BOOST_CHECK_EQUAL(daily, expected);
    BOOST_CHECK(daily > 0);
}

// ============================================================================
// TEST 4: ApplyDailyYield - State updates
// ============================================================================

BOOST_AUTO_TEST_CASE(apply_daily_yield_state_update)
{
    // Note: This test cannot actually iterate real ZKHU notes from LevelDB
    // because the test environment doesn't have a populated ZKHU DB.
    // We test the logic assuming zero notes, which should result in:
    // - Ur unchanged (no yield to add)
    // - last_yield_update_height updated

    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    state.R_annual = 1500; // 15.00%
    state.last_yield_update_height = 0;

    uint32_t activationHeight = 1000000;
    uint32_t yieldHeight = 1000000;

    // Apply yield at activation (no notes in DB, so totalYield = 0)
    bool success = khu_yield::ApplyDailyYield(state, yieldHeight, activationHeight);
    BOOST_CHECK(success);

    // Ur should remain unchanged (no notes)
    BOOST_CHECK_EQUAL(state.Ur, 500000 * COIN);

    // last_yield_update_height should be updated
    BOOST_CHECK_EQUAL(state.last_yield_update_height, yieldHeight);
}

BOOST_AUTO_TEST_CASE(apply_daily_yield_wrong_boundary)
{
    KhuGlobalState state;
    state.SetNull();
    state.last_yield_update_height = 1000000;

    uint32_t activationHeight = 1000000;

    // Try to apply yield at wrong height (not at boundary)
    bool success = khu_yield::ApplyDailyYield(state, 1000100, activationHeight);

    // Should fail (not at yield boundary)
    BOOST_CHECK(!success);
}

// ============================================================================
// TEST 5: UndoDailyYield - Reorg support
// ============================================================================

BOOST_AUTO_TEST_CASE(undo_daily_yield_state_restore)
{
    KhuGlobalState state;
    state.SetNull();
    state.U = 1000000 * COIN;
    state.Ur = 500000 * COIN;
    state.R_annual = 1500;
    state.last_yield_update_height = 1001440; // After first yield

    uint32_t activationHeight = 1000000;
    uint32_t yieldHeight = 1001440;

    CAmount Ur_before = state.Ur;

    // Undo yield (no notes in DB, so totalYield = 0)
    bool success = khu_yield::UndoDailyYield(state, yieldHeight, activationHeight);
    BOOST_CHECK(success);

    // Ur should be restored (unchanged since totalYield = 0)
    BOOST_CHECK_EQUAL(state.Ur, Ur_before);

    // last_yield_update_height should be restored to previous value
    BOOST_CHECK_EQUAL(state.last_yield_update_height, 1000000);
}

BOOST_AUTO_TEST_CASE(undo_daily_yield_at_activation)
{
    KhuGlobalState state;
    state.SetNull();
    state.Ur = 100000 * COIN;
    state.last_yield_update_height = 1000000;

    uint32_t activationHeight = 1000000;

    // Undo yield at activation height
    bool success = khu_yield::UndoDailyYield(state, activationHeight, activationHeight);
    BOOST_CHECK(success);

    // last_yield_update_height should be reset to 0 (before activation)
    BOOST_CHECK_EQUAL(state.last_yield_update_height, 0);
}

// ============================================================================
// TEST 6: Integration - Apply + Undo consistency
// ============================================================================

BOOST_AUTO_TEST_CASE(yield_apply_undo_consistency)
{
    KhuGlobalState state;
    state.SetNull();
    state.U = 2000000 * COIN;
    state.Ur = 1000000 * COIN;
    state.C = 2000000 * COIN;
    state.Cr = 1000000 * COIN;
    state.R_annual = 1000; // 10.00%
    state.last_yield_update_height = 0;

    uint32_t activationHeight = 1000000;

    // Save initial state
    KhuGlobalState initialState = state;

    // Apply yield at activation
    bool applySuccess = khu_yield::ApplyDailyYield(state, activationHeight, activationHeight);
    BOOST_CHECK(applySuccess);

    // Verify invariants still hold (C==U, Cr==Ur)
    // Note: Yield only affects Ur, so C==U remains true
    // Cr==Ur may diverge because only Ur changes, but this is intentional
    BOOST_CHECK_EQUAL(state.C, state.U);

    // Undo yield
    bool undoSuccess = khu_yield::UndoDailyYield(state, activationHeight, activationHeight);
    BOOST_CHECK(undoSuccess);

    // State should be restored to initial values
    BOOST_CHECK_EQUAL(state.U, initialState.U);
    BOOST_CHECK_EQUAL(state.Ur, initialState.Ur);
    BOOST_CHECK_EQUAL(state.C, initialState.C);
    BOOST_CHECK_EQUAL(state.Cr, initialState.Cr);
    BOOST_CHECK_EQUAL(state.last_yield_update_height, initialState.last_yield_update_height);
}

BOOST_AUTO_TEST_CASE(yield_multiple_intervals_consistency)
{
    KhuGlobalState state;
    state.SetNull();
    state.U = 5000000 * COIN;
    state.Ur = 2000000 * COIN;
    state.C = 5000000 * COIN;
    state.Cr = 2000000 * COIN;
    state.R_annual = 1200; // 12.00%
    state.last_yield_update_height = 0;

    uint32_t activationHeight = 1000000;

    // Apply yield at activation
    khu_yield::ApplyDailyYield(state, 1000000, activationHeight);
    CAmount Ur_after_1 = state.Ur;

    // Apply yield at second interval
    khu_yield::ApplyDailyYield(state, 1001440, activationHeight);
    CAmount Ur_after_2 = state.Ur;

    // Apply yield at third interval
    khu_yield::ApplyDailyYield(state, 1002880, activationHeight);
    CAmount Ur_after_3 = state.Ur;

    // Undo in reverse order
    khu_yield::UndoDailyYield(state, 1002880, activationHeight);
    BOOST_CHECK_EQUAL(state.Ur, Ur_after_2);
    BOOST_CHECK_EQUAL(state.last_yield_update_height, 1001440);

    khu_yield::UndoDailyYield(state, 1001440, activationHeight);
    BOOST_CHECK_EQUAL(state.Ur, Ur_after_1);
    BOOST_CHECK_EQUAL(state.last_yield_update_height, 1000000);

    khu_yield::UndoDailyYield(state, 1000000, activationHeight);
    BOOST_CHECK_EQUAL(state.Ur, 2000000 * COIN);
    BOOST_CHECK_EQUAL(state.last_yield_update_height, 0);
}

// ============================================================================
// TEST 7: Edge cases - Zero state, extreme values
// ============================================================================

BOOST_AUTO_TEST_CASE(yield_zero_state)
{
    KhuGlobalState state;
    state.SetNull();
    // All zero
    state.U = 0;
    state.Ur = 0;
    state.C = 0;
    state.Cr = 0;
    state.R_annual = 0;
    state.last_yield_update_height = 0;

    uint32_t activationHeight = 1000000;

    // Apply yield with zero state
    bool success = khu_yield::ApplyDailyYield(state, activationHeight, activationHeight);
    BOOST_CHECK(success);

    // Everything should remain zero
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK_EQUAL(state.last_yield_update_height, activationHeight);
}

BOOST_AUTO_TEST_CASE(yield_max_rate)
{
    // Test maximum rate (100.00% = 10000 basis points)
    CAmount amount = 1000 * COIN;
    uint16_t maxRate = 10000;

    CAmount daily = khu_yield::CalculateDailyYieldForNote(amount, maxRate);

    // annual = 1000 × 10000 / 10000 = 1000 KHU (100% of principal)
    // daily = 1000 / 365 = 2.73972 KHU
    CAmount expected = (1000 * COIN) / 365;
    BOOST_CHECK_EQUAL(daily, expected);
}

BOOST_AUTO_TEST_CASE(yield_constants)
{
    // Verify consensus-critical constants
    BOOST_CHECK_EQUAL(khu_yield::YIELD_INTERVAL, 1440);
    BOOST_CHECK_EQUAL(khu_yield::MATURITY_BLOCKS, 4320);
    BOOST_CHECK_EQUAL(khu_yield::DAYS_PER_YEAR, 365);
}

BOOST_AUTO_TEST_SUITE_END()
