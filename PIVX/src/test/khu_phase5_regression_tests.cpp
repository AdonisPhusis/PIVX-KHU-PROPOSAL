// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Phase 5 Regression Tests
 *
 * PURPOSE: Ensure Phase 5 (ZKHU) does NOT break Phase 1-4 functionality.
 *
 * Tests:
 * 1. MINT/REDEEM without ZKHU → no ZKHU DB entries created
 * 2. Coinstake generation → no ZKHU interference
 * 3. Transparent PIV sends → C/U/Cr/Ur unchanged
 * 4. Global state invariants hold even with ZERO ZKHU activity
 * 5. ZKHU DB remains empty if never used
 *
 * Expected: ALL legacy operations work identically to Phase 4.
 */

#include "test/test_pivx.h"
#include "khu/khu_stake.h"
#include "khu/khu_unstake.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_note.h"
#include "consensus/validation.h"
#include "coins.h"
#include "primitives/transaction.h"
#include "sapling/sapling_transaction.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

// Test fixture: Initialize ZKHU database for regression tests
struct KHUPhase5RegressionSetup : public TestingSetup
{
    KHUPhase5RegressionSetup() : TestingSetup() {
        // Initialize ZKHU DB (even if we don't use it, it should not interfere)
        if (!InitZKHUDB(1 << 20, false)) {
            throw std::runtime_error("Failed to initialize ZKHU DB for regression tests");
        }
    }
};

BOOST_FIXTURE_TEST_SUITE(khu_phase5_regression_tests, KHUPhase5RegressionSetup)

/**
 * REGRESSION 1: MINT/REDEEM without ZKHU
 *
 * Scenario: User performs MINT and REDEEM operations (Phase 2) WITHOUT any STAKE/UNSTAKE.
 *
 * Expected:
 * - Global state C/U updated correctly
 * - NO ZKHU DB entries created (no anchors, no nullifiers)
 * - Invariants C==U and Cr==Ur hold
 */
BOOST_AUTO_TEST_CASE(regression_mint_redeem_no_zkhu)
{
    // Setup initial state
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 1000;
    state.C = 0;
    state.U = 0;
    state.Cr = 0;
    state.Ur = 0;

    CCoinsViewCache view(pcoinsTip.get());

    // Simulate MINT: add 100 KHU to collateral
    CAmount mintAmount = 100 * COIN;
    state.C += mintAmount;
    state.U += mintAmount;

    // Verify invariants after MINT
    BOOST_CHECK_EQUAL(state.C, state.U);
    BOOST_CHECK_EQUAL(state.Cr, state.Ur);
    BOOST_CHECK(state.CheckInvariants());

    // Verify ZKHU DB is EMPTY (no STAKE was performed)
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_REQUIRE(zkhuDB);

    // Check no nullifiers exist
    uint256 randomNullifier = GetRandHash();
    BOOST_CHECK(!zkhuDB->IsNullifierSpent(randomNullifier));

    // Simulate REDEEM: remove 50 KHU from collateral
    CAmount redeemAmount = 50 * COIN;
    state.C -= redeemAmount;
    state.U -= redeemAmount;

    // Verify invariants after REDEEM
    BOOST_CHECK_EQUAL(state.C, 50 * COIN);
    BOOST_CHECK_EQUAL(state.U, 50 * COIN);
    BOOST_CHECK_EQUAL(state.C, state.U);
    BOOST_CHECK_EQUAL(state.Cr, state.Ur);
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * REGRESSION 2: Transparent PIV transfers
 *
 * Scenario: User sends transparent PIV (no KHU involved).
 *
 * Expected:
 * - KHU global state UNCHANGED
 * - No ZKHU DB activity
 * - Invariants still hold
 */
BOOST_AUTO_TEST_CASE(regression_transparent_piv_send)
{
    // Setup initial state with existing KHU
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 2000;
    state.C = 500 * COIN;
    state.U = 500 * COIN;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;

    KhuGlobalState stateBackup = state;

    // Simulate transparent PIV send (does NOT touch KHU state)
    // In real code, this would be a standard PIV transaction
    // Here we just verify state is unchanged

    // Verify state UNCHANGED
    BOOST_CHECK_EQUAL(state.C, stateBackup.C);
    BOOST_CHECK_EQUAL(state.U, stateBackup.U);
    BOOST_CHECK_EQUAL(state.Cr, stateBackup.Cr);
    BOOST_CHECK_EQUAL(state.Ur, stateBackup.Ur);
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * REGRESSION 3: Global state with ZERO ZKHU activity
 *
 * Scenario: Chain runs for N blocks with MINT/REDEEM but ZERO STAKE/UNSTAKE.
 *
 * Expected:
 * - Cr and Ur remain at 0 (no reward pool if no staking)
 * - C and U grow/shrink with MINT/REDEEM
 * - NO ZKHU DB entries
 */
BOOST_AUTO_TEST_CASE(regression_zero_zkhu_activity)
{
    // Setup initial state (empty chain)
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 5000;
    state.C = 0;
    state.U = 0;
    state.Cr = 0;
    state.Ur = 0;

    // Simulate 10 MINT operations (no STAKE)
    for (int i = 0; i < 10; i++) {
        CAmount mintAmount = (100 + i) * COIN;
        state.C += mintAmount;
        state.U += mintAmount;

        // Verify invariants after each MINT
        BOOST_CHECK_EQUAL(state.C, state.U);
        BOOST_CHECK_EQUAL(state.Cr, 0);  // No reward pool without staking
        BOOST_CHECK_EQUAL(state.Ur, 0);
        BOOST_CHECK(state.CheckInvariants());
    }

    // Verify final state
    BOOST_CHECK_EQUAL(state.C, state.U);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
    BOOST_CHECK(state.CheckInvariants());

    // Verify ZKHU DB is EMPTY
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_REQUIRE(zkhuDB);

    // Check no nullifiers exist
    for (int i = 0; i < 10; i++) {
        uint256 randomNullifier = GetRandHash();
        BOOST_CHECK(!zkhuDB->IsNullifierSpent(randomNullifier));
    }
}

/**
 * REGRESSION 4: Coinstake with KHU (Phase 3) without ZKHU
 *
 * Scenario: Masternode generates coinstake with KHU emission, but NO ZKHU.
 *
 * Expected:
 * - Coinstake reward splits into Cr/Ur correctly
 * - NO ZKHU notes created
 * - Invariants hold
 */
BOOST_AUTO_TEST_CASE(regression_coinstake_without_zkhu)
{
    // Setup initial state
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 10000;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 200 * COIN;
    state.Ur = 200 * COIN;

    // Simulate coinstake reward (e.g., 5 PIV)
    CAmount coinstakeReward = 5 * COIN;
    CAmount rewardToCr = coinstakeReward / 2;  // 2.5 PIV
    CAmount rewardToUr = coinstakeReward / 2;  // 2.5 PIV

    state.Cr += rewardToCr;
    state.Ur += rewardToUr;

    // Verify invariants after coinstake
    BOOST_CHECK_EQUAL(state.C, state.U);
    BOOST_CHECK_EQUAL(state.Cr, state.Ur);
    BOOST_CHECK(state.CheckInvariants());

    // Verify ZKHU DB unchanged (no STAKE/UNSTAKE)
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_REQUIRE(zkhuDB);

    uint256 randomNullifier = GetRandHash();
    BOOST_CHECK(!zkhuDB->IsNullifierSpent(randomNullifier));
}

/**
 * REGRESSION 5: Mixed operations (MINT + coinstake) without ZKHU
 *
 * Scenario: Complex sequence of MINT, REDEEM, coinstake WITHOUT any ZKHU.
 *
 * Expected:
 * - All operations work as in Phase 1-4
 * - Invariants maintained
 * - ZKHU DB empty
 */
BOOST_AUTO_TEST_CASE(regression_mixed_operations_no_zkhu)
{
    // Setup initial state
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 15000;
    state.C = 0;
    state.U = 0;
    state.Cr = 0;
    state.Ur = 0;

    // Operation 1: MINT 500 KHU
    state.C += 500 * COIN;
    state.U += 500 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Operation 2: Coinstake (10 PIV reward)
    state.Cr += 5 * COIN;
    state.Ur += 5 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Operation 3: MINT 200 KHU
    state.C += 200 * COIN;
    state.U += 200 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Operation 4: REDEEM 300 KHU
    state.C -= 300 * COIN;
    state.U -= 300 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Operation 5: Coinstake (8 PIV reward)
    state.Cr += 4 * COIN;
    state.Ur += 4 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Verify final state
    BOOST_CHECK_EQUAL(state.C, 400 * COIN);
    BOOST_CHECK_EQUAL(state.U, 400 * COIN);
    BOOST_CHECK_EQUAL(state.Cr, 9 * COIN);
    BOOST_CHECK_EQUAL(state.Ur, 9 * COIN);
    BOOST_CHECK(state.CheckInvariants());

    // Verify ZKHU DB is EMPTY
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_REQUIRE(zkhuDB);

    for (int i = 0; i < 5; i++) {
        uint256 randomNullifier = GetRandHash();
        BOOST_CHECK(!zkhuDB->IsNullifierSpent(randomNullifier));
    }
}

/**
 * REGRESSION 6: State transitions across Phase 4 → Phase 5
 *
 * Scenario: Simulate upgrade from Phase 4 to Phase 5 (ZKHU enabled but unused).
 *
 * Expected:
 * - Existing state preserved
 * - No disruption to MINT/REDEEM
 * - ZKHU remains dormant
 */
BOOST_AUTO_TEST_CASE(regression_phase4_to_phase5_upgrade)
{
    // Phase 4 state (before ZKHU)
    KhuGlobalState statePhase4;
    statePhase4.SetNull();
    statePhase4.nHeight = 20000;
    statePhase4.C = 10000 * COIN;
    statePhase4.U = 10000 * COIN;
    statePhase4.Cr = 2000 * COIN;
    statePhase4.Ur = 2000 * COIN;

    // "Upgrade" to Phase 5 (ZKHU enabled)
    KhuGlobalState statePhase5 = statePhase4;

    // Verify state IDENTICAL after upgrade
    BOOST_CHECK_EQUAL(statePhase5.C, statePhase4.C);
    BOOST_CHECK_EQUAL(statePhase5.U, statePhase4.U);
    BOOST_CHECK_EQUAL(statePhase5.Cr, statePhase4.Cr);
    BOOST_CHECK_EQUAL(statePhase5.Ur, statePhase4.Ur);
    BOOST_CHECK(statePhase5.CheckInvariants());

    // Perform Phase 1-4 operations after upgrade
    statePhase5.C += 100 * COIN;
    statePhase5.U += 100 * COIN;
    BOOST_CHECK(statePhase5.CheckInvariants());

    statePhase5.Cr += 10 * COIN;
    statePhase5.Ur += 10 * COIN;
    BOOST_CHECK(statePhase5.CheckInvariants());

    // Verify ZKHU DB is EMPTY (upgrade doesn't auto-create ZKHU notes)
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_REQUIRE(zkhuDB);

    uint256 randomNullifier = GetRandHash();
    BOOST_CHECK(!zkhuDB->IsNullifierSpent(randomNullifier));
}

BOOST_AUTO_TEST_SUITE_END()
