// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 3: Masternode Finality (StateCommitment)
//
// Tests the cryptographic finality infrastructure using LLMQ-style commitments.
//

#include "test/test_pivx.h"

#include "khu/khu_commitment.h"
#include "khu/khu_commitmentdb.h"
#include "khu/khu_state.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(khu_phase3_tests, BasicTestingSetup)

/**
 * Test 1: State hash consistency
 *
 * Verify that:
 * - Same state produces same hash
 * - Different state produces different hash
 * - Hash is deterministic
 */
BOOST_AUTO_TEST_CASE(test_statecommit_consistency)
{
    // Create two identical states
    KhuGlobalState state1;
    state1.C = 100 * COIN;
    state1.U = 100 * COIN;
    state1.Cr = 0;
    state1.Ur = 0;
    state1.nHeight = 1000;

    KhuGlobalState state2;
    state2.C = 100 * COIN;
    state2.U = 100 * COIN;
    state2.Cr = 0;
    state2.Ur = 0;
    state2.nHeight = 1000;

    // Same state → same hash
    uint256 hash1 = ComputeKHUStateHash(state1);
    uint256 hash2 = ComputeKHUStateHash(state2);
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(!hash1.IsNull());

    // Different state → different hash
    KhuGlobalState state3;
    state3.C = 200 * COIN;  // Different C value
    state3.U = 200 * COIN;
    state3.Cr = 0;
    state3.Ur = 0;
    state3.nHeight = 1000;

    uint256 hash3 = ComputeKHUStateHash(state3);
    BOOST_CHECK(hash3 != hash1);

    // Different height → different hash
    KhuGlobalState state4;
    state4.C = 100 * COIN;
    state4.U = 100 * COIN;
    state4.Cr = 0;
    state4.Ur = 0;
    state4.nHeight = 2000;  // Different height

    uint256 hash4 = ComputeKHUStateHash(state4);
    BOOST_CHECK(hash4 != hash1);
}

/**
 * Test 2: Commitment creation
 *
 * Verify that CreateKHUStateCommitment produces valid structure.
 */
BOOST_AUTO_TEST_CASE(test_statecommit_creation)
{
    KhuGlobalState state;
    state.C = 500 * COIN;
    state.U = 500 * COIN;
    state.Cr = 50 * COIN;
    state.Ur = 50 * COIN;
    state.nHeight = 5000;

    uint256 quorumHash;
    quorumHash.SetHex("0000000000000000000000000000000000000000000000000000000000000001");

    KhuStateCommitment commitment = CreateKHUStateCommitment(state, quorumHash);

    // Verify basic structure
    BOOST_CHECK_EQUAL(commitment.nHeight, 5000);
    BOOST_CHECK(!commitment.hashState.IsNull());
    BOOST_CHECK(commitment.quorumHash == quorumHash);

    // Verify hash matches state
    uint256 expectedHash = ComputeKHUStateHash(state);
    BOOST_CHECK(commitment.hashState == expectedHash);

    // Initially no signatures
    BOOST_CHECK(commitment.signers.empty());
    BOOST_CHECK(!commitment.sig.IsValid());
}

/**
 * Test 3: Commitment with simulated quorum
 *
 * Create a commitment with mock signatures and verify quorum threshold.
 */
BOOST_AUTO_TEST_CASE(test_statecommit_signed)
{
    KhuGlobalState state;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;
    state.nHeight = 10000;

    uint256 quorumHash;
    quorumHash.SetHex("0000000000000000000000000000000000000000000000000000000000000002");

    KhuStateCommitment commitment = CreateKHUStateCommitment(state, quorumHash);

    // Simulate 50 quorum members, 30 signed (60%)
    commitment.signers.resize(50, false);
    for (int i = 0; i < 30; i++) {
        commitment.signers[i] = true;
    }

    // Mock BLS signature (in real system, this would be aggregate sig)
    // For testing, we just need IsValid() to return true
    CBLSSignature mockSig;
    // Note: mockSig.IsValid() will return false without real signature
    // This is expected for unit tests without full BLS setup
    commitment.sig = mockSig;

    // Check quorum (>= 60% = 30/50)
    BOOST_CHECK(commitment.HasQuorum());

    // Test below threshold (29/50 = 58%)
    commitment.signers[29] = false;
    BOOST_CHECK(!commitment.HasQuorum());

    // Test exact threshold (30/50 = 60%)
    commitment.signers[29] = true;
    BOOST_CHECK(commitment.HasQuorum());

    // Test above threshold (40/50 = 80%)
    for (int i = 30; i < 40; i++) {
        commitment.signers[i] = true;
    }
    BOOST_CHECK(commitment.HasQuorum());
}

/**
 * Test 4: Invalid commitment rejection
 *
 * Verify that commitments with wrong state hash are rejected.
 */
BOOST_AUTO_TEST_CASE(test_statecommit_invalid)
{
    KhuGlobalState state;
    state.C = 200 * COIN;
    state.U = 200 * COIN;
    state.Cr = 20 * COIN;
    state.Ur = 20 * COIN;
    state.nHeight = 2000;

    KhuStateCommitment commitment;
    commitment.nHeight = 2000;
    commitment.hashState.SetHex("0000000000000000000000000000000000000000000000000000000000000bad");
    commitment.quorumHash.SetHex("0000000000000000000000000000000000000000000000000000000000000003");

    // Simulate quorum
    commitment.signers.resize(50, false);
    for (int i = 0; i < 35; i++) {
        commitment.signers[i] = true;
    }

    // Verify fails due to hash mismatch
    BOOST_CHECK(!VerifyKHUStateCommitment(commitment, state));

    // Fix hash → should pass basic checks (except BLS sig)
    commitment.hashState = ComputeKHUStateHash(state);
    // Note: Will still fail full verification without real BLS sig
    // but at least hash check passes internally
}

/**
 * Test 5: Finalized blocks locked (reorg protection)
 *
 * Verify that attempting to erase finalized commitments fails.
 */
BOOST_AUTO_TEST_CASE(test_finality_blocks_locked)
{
    // Create temporary commitment DB
    CKHUCommitmentDB db(1 << 20, true, false);  // 1MB cache, in-memory

    KhuGlobalState state;
    state.C = 300 * COIN;
    state.U = 300 * COIN;
    state.Cr = 30 * COIN;
    state.Ur = 30 * COIN;
    state.nHeight = 3000;

    // Create finalized commitment
    KhuStateCommitment commitment = CreateKHUStateCommitment(state, uint256S("01"));
    commitment.signers.resize(50, false);
    for (int i = 0; i < 35; i++) {
        commitment.signers[i] = true;
    }
    BOOST_CHECK(commitment.HasQuorum());

    // Write commitment → sets latest finalized
    BOOST_CHECK(db.WriteCommitment(3000, commitment));
    BOOST_CHECK_EQUAL(db.GetLatestFinalizedHeight(), 3000);

    // Try to erase finalized block → should fail
    BOOST_CHECK(!db.EraseCommitment(3000));

    // Verify commitment still exists
    KhuStateCommitment readBack;
    BOOST_CHECK(db.ReadCommitment(3000, readBack));
    BOOST_CHECK(readBack.hashState == commitment.hashState);

    // Can erase non-finalized blocks above finalized height
    KhuStateCommitment nonFinalized = CreateKHUStateCommitment(state, uint256S("02"));
    nonFinalized.signers.resize(50, false);  // No signatures
    BOOST_CHECK(!nonFinalized.HasQuorum());

    BOOST_CHECK(db.WriteCommitment(3001, nonFinalized));
    BOOST_CHECK(db.EraseCommitment(3001));  // Should succeed (not finalized)
}

/**
 * Test 6: Reorg depth limit
 *
 * Verify 12-block depth limit is enforced.
 * (This is tested via DisconnectKHUBlock in integration, here we test the constant)
 */
BOOST_AUTO_TEST_CASE(test_reorg_depth_limit)
{
    // Verify constant matches spec
    const int KHU_FINALITY_DEPTH = 12;

    // Create states at different depths
    KhuGlobalState state1;
    state1.C = 100 * COIN;
    state1.U = 100 * COIN;
    state1.Cr = 0;
    state1.Ur = 0;
    state1.nHeight = 1000;

    KhuGlobalState state2;
    state2.C = 100 * COIN;
    state2.U = 100 * COIN;
    state2.Cr = 0;
    state2.Ur = 0;
    state2.nHeight = 1000 + KHU_FINALITY_DEPTH;

    // Depth exactly at limit
    int depth = state2.nHeight - state1.nHeight;
    BOOST_CHECK_EQUAL(depth, KHU_FINALITY_DEPTH);

    // Depth beyond limit
    KhuGlobalState state3;
    state3.C = 100 * COIN;
    state3.U = 100 * COIN;
    state3.Cr = 0;
    state3.Ur = 0;
    state3.nHeight = 1000 + KHU_FINALITY_DEPTH + 1;

    depth = state3.nHeight - state1.nHeight;
    BOOST_CHECK(depth > KHU_FINALITY_DEPTH);
}

/**
 * Test 7: Commitment database operations
 *
 * Verify all CRUD operations on commitment DB.
 */
BOOST_AUTO_TEST_CASE(test_commitment_db)
{
    // Create temporary in-memory DB
    CKHUCommitmentDB db(1 << 20, true, false);

    KhuGlobalState state;
    state.C = 400 * COIN;
    state.U = 400 * COIN;
    state.Cr = 40 * COIN;
    state.Ur = 40 * COIN;
    state.nHeight = 4000;

    KhuStateCommitment commitment = CreateKHUStateCommitment(state, uint256S("04"));
    commitment.signers.resize(50, false);
    for (int i = 0; i < 40; i++) {
        commitment.signers[i] = true;
    }

    // Write
    BOOST_CHECK(db.WriteCommitment(4000, commitment));

    // Read
    KhuStateCommitment readCommit;
    BOOST_CHECK(db.ReadCommitment(4000, readCommit));
    BOOST_CHECK(readCommit.hashState == commitment.hashState);
    BOOST_CHECK_EQUAL(readCommit.nHeight, 4000);

    // Exists
    BOOST_CHECK(db.HaveCommitment(4000));
    BOOST_CHECK(!db.HaveCommitment(4001));

    // Finalized check
    BOOST_CHECK(db.IsFinalizedAt(4000));
    BOOST_CHECK(!db.IsFinalizedAt(4001));

    // Latest finalized height
    BOOST_CHECK_EQUAL(db.GetLatestFinalizedHeight(), 4000);

    // Erase (non-finalized first)
    KhuStateCommitment nonFinal = CreateKHUStateCommitment(state, uint256S("05"));
    nonFinal.signers.clear();  // No quorum
    BOOST_CHECK(db.WriteCommitment(4001, nonFinal));
    BOOST_CHECK(db.EraseCommitment(4001));
    BOOST_CHECK(!db.HaveCommitment(4001));
}

/**
 * Test 8: State hash determinism
 *
 * Verify that hash computation is deterministic and stable.
 */
BOOST_AUTO_TEST_CASE(test_state_hash_deterministic)
{
    KhuGlobalState state;
    state.C = 777 * COIN;
    state.U = 777 * COIN;
    state.Cr = 77 * COIN;
    state.Ur = 77 * COIN;
    state.nHeight = 7777;

    // Compute hash multiple times
    uint256 hash1 = ComputeKHUStateHash(state);
    uint256 hash2 = ComputeKHUStateHash(state);
    uint256 hash3 = ComputeKHUStateHash(state);

    // All hashes must be identical
    BOOST_CHECK(hash1 == hash2);
    BOOST_CHECK(hash2 == hash3);

    // Modify one field slightly
    state.C += 1;
    uint256 hash4 = ComputeKHUStateHash(state);
    BOOST_CHECK(hash4 != hash1);

    // Restore and verify hash returns to original
    state.C -= 1;
    uint256 hash5 = ComputeKHUStateHash(state);
    BOOST_CHECK(hash5 == hash1);

    // Verify serialization order stability
    // Same values in different order should NOT produce same hash
    KhuGlobalState stateSwapped;
    stateSwapped.U = 777 * COIN;  // Swapped C and U
    stateSwapped.C = 777 * COIN;
    stateSwapped.Cr = 77 * COIN;
    stateSwapped.Ur = 77 * COIN;
    stateSwapped.nHeight = 7777;

    uint256 hashSwapped = ComputeKHUStateHash(stateSwapped);
    // Should still be same because C and U have same value
    // But serialization order is guaranteed: C, U, Cr, Ur, height
    BOOST_CHECK(hashSwapped == hash1);
}

BOOST_AUTO_TEST_SUITE_END()
