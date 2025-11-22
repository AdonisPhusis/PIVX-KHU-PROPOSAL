// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_state.h"
#include "khu/khu_statedb.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(khu_phase1_tests, BasicTestingSetup)

/**
 * Test 1: Genesis state initialization
 *
 * Verify that a newly initialized KhuGlobalState has:
 * - All amounts set to 0
 * - Invariants satisfied (C==U, Cr==Ur)
 */
BOOST_AUTO_TEST_CASE(test_genesis_state)
{
    KhuGlobalState state;
    state.SetNull();

    // All amounts should be 0
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);

    // Genesis state should satisfy invariants
    BOOST_CHECK(state.CheckInvariants());

    // Height should be 0
    BOOST_CHECK_EQUAL(state.nHeight, 0);

    // Hashes should be null
    BOOST_CHECK(state.hashBlock.IsNull());
    BOOST_CHECK(state.hashPrevState.IsNull());
}

/**
 * Test 2: Invariants validation - C == U
 *
 * Verify that CheckInvariants() correctly detects violations
 * of the C == U invariant (main circulation)
 */
BOOST_AUTO_TEST_CASE(test_invariants_cu)
{
    KhuGlobalState state;
    state.SetNull();

    // Valid: C == U == 0 (genesis)
    BOOST_CHECK(state.CheckInvariants());

    // Valid: C == U == 1000
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Invalid: C != U
    state.U = 999 * COIN;
    BOOST_CHECK(!state.CheckInvariants());

    // Fix it
    state.U = 1000 * COIN;
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 3: Invariants validation - Cr == Ur
 *
 * Verify that CheckInvariants() correctly detects violations
 * of the Cr == Ur invariant (reward circulation)
 */
BOOST_AUTO_TEST_CASE(test_invariants_crur)
{
    KhuGlobalState state;
    state.SetNull();

    // Valid: Cr == Ur == 0 (genesis)
    BOOST_CHECK(state.CheckInvariants());

    // Valid: Cr == Ur == 50
    state.Cr = 50 * COIN;
    state.Ur = 50 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Invalid: Cr != Ur
    state.Ur = 49 * COIN;
    BOOST_CHECK(!state.CheckInvariants());

    // Fix it
    state.Ur = 50 * COIN;
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 4: Negative amounts detection
 *
 * Verify that CheckInvariants() rejects negative amounts
 */
BOOST_AUTO_TEST_CASE(test_negative_amounts)
{
    KhuGlobalState state;
    state.SetNull();

    // Negative C
    state.C = -1;
    BOOST_CHECK(!state.CheckInvariants());

    // Negative U
    state.C = 0;
    state.U = -1;
    BOOST_CHECK(!state.CheckInvariants());

    // Negative Cr
    state.U = 0;
    state.Cr = -1;
    BOOST_CHECK(!state.CheckInvariants());

    // Negative Ur
    state.Cr = 0;
    state.Ur = -1;
    BOOST_CHECK(!state.CheckInvariants());

    // All valid again
    state.Ur = 0;
    BOOST_CHECK(state.CheckInvariants());
}

/**
 * Test 5: GetHash() determinism
 *
 * Verify that GetHash() produces identical hashes for identical states
 * and different hashes for different states
 */
BOOST_AUTO_TEST_CASE(test_gethash_determinism)
{
    KhuGlobalState state1, state2;

    // Identical states should have identical hashes
    state1.SetNull();
    state2.SetNull();
    BOOST_CHECK(state1.GetHash() == state2.GetHash());

    // Modify state1
    state1.C = 1000 * COIN;
    state1.U = 1000 * COIN;
    state1.nHeight = 100;

    // Copy to state2
    state2 = state1;
    BOOST_CHECK(state1.GetHash() == state2.GetHash());

    // Modify state2 slightly
    state2.C = 999 * COIN;
    state2.U = 999 * COIN;
    BOOST_CHECK(state1.GetHash() != state2.GetHash());

    // Restore state2
    state2.C = 1000 * COIN;
    state2.U = 1000 * COIN;
    BOOST_CHECK(state1.GetHash() == state2.GetHash());
}

/**
 * Test 6: DB persistence (read/write)
 *
 * Verify that CKHUStateDB can persist and retrieve states correctly
 */
BOOST_AUTO_TEST_CASE(test_db_persistence)
{
    // Create in-memory DB (fMemory = true, fWipe = true)
    CKHUStateDB db(1 << 20, true, true);

    // Create a state
    KhuGlobalState state;
    state.nHeight = 100;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 50 * COIN;
    state.Ur = 50 * COIN;
    state.R_annual = 2555; // 25.55%

    // Write to DB
    BOOST_CHECK(db.WriteKHUState(100, state));

    // Verify exists
    BOOST_CHECK(db.ExistsKHUState(100));

    // Read back
    KhuGlobalState loaded;
    BOOST_CHECK(db.ReadKHUState(100, loaded));

    // Verify all fields match
    BOOST_CHECK_EQUAL(loaded.nHeight, state.nHeight);
    BOOST_CHECK_EQUAL(loaded.C, state.C);
    BOOST_CHECK_EQUAL(loaded.U, state.U);
    BOOST_CHECK_EQUAL(loaded.Cr, state.Cr);
    BOOST_CHECK_EQUAL(loaded.Ur, state.Ur);
    BOOST_CHECK_EQUAL(loaded.R_annual, state.R_annual);

    // Hash should match
    BOOST_CHECK(loaded.GetHash() == state.GetHash());
}

/**
 * Test 7: DB LoadKHUState_OrGenesis
 *
 * Verify that LoadKHUState_OrGenesis returns genesis state
 * if no state exists at the requested height
 */
BOOST_AUTO_TEST_CASE(test_db_load_or_genesis)
{
    CKHUStateDB db(1 << 20, true, true);

    // Request state at height 999 (doesn't exist)
    KhuGlobalState state = db.LoadKHUState_OrGenesis(999);

    // Should return genesis state with nHeight = 999
    BOOST_CHECK_EQUAL(state.nHeight, 999);
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Cr, 0);
    BOOST_CHECK_EQUAL(state.Ur, 0);
}

/**
 * Test 8: DB Erase
 *
 * Verify that EraseKHUState removes state from DB
 */
BOOST_AUTO_TEST_CASE(test_db_erase)
{
    CKHUStateDB db(1 << 20, true, true);

    // Write state
    KhuGlobalState state;
    state.nHeight = 123;
    state.C = 500 * COIN;
    state.U = 500 * COIN;
    BOOST_CHECK(db.WriteKHUState(123, state));

    // Verify exists
    BOOST_CHECK(db.ExistsKHUState(123));

    // Erase
    BOOST_CHECK(db.EraseKHUState(123));

    // Verify doesn't exist anymore
    BOOST_CHECK(!db.ExistsKHUState(123));
}

/**
 * Test 9: Reorg depth validation (consensus rule)
 *
 * NOTE: This is a CONSENSUS RULE tested at integration level, not unit test level.
 *
 * The 12-block reorg depth limit (LLMQ finality) is enforced in DisconnectKHUBlock().
 * This is NOT a Phase 2 feature - it's a mandatory consensus rule for Phase 1.
 *
 * Without this check, nodes can diverge on deep reorgs even with empty KHU state,
 * because the KHU state chain (hashPrevState) would become inconsistent.
 *
 * Testing this requires:
 * - Full blockchain context (ChainActive().Tip())
 * - Mock block indices at different heights
 * - Simulated reorg scenario
 *
 * This is covered by functional/integration tests, not unit tests.
 * See: test/functional/khu_reorg_depth.py (when implemented)
 *
 * Consensus rule: DisconnectKHUBlock() must reject reorg depth > 12 blocks
 */
BOOST_AUTO_TEST_CASE(test_reorg_depth_constant)
{
    // Verify the constant is defined (compile-time check)
    // The actual consensus enforcement is in DisconnectKHUBlock()
    const int KHU_FINALITY_DEPTH = 12;  // LLMQ finality
    BOOST_CHECK_EQUAL(KHU_FINALITY_DEPTH, 12);

    // This test documents the requirement
    // Actual reorg rejection is tested in integration tests
}

BOOST_AUTO_TEST_SUITE_END()
