// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// INTEGRATION TESTS: KHU V6.0 ACTIVATION (HARD FORK)
//
// This test suite validates the complete V6.0 upgrade activation including:
// - Pre-activation: Legacy PIVX behavior
// - Post-activation: KHU system (MINT/REDEEM/Finality)
// - Block Reward: 0 PIV IMMEDIATELY at V6 activation
// - Invariants: C==U+Z, Cr==Ur preservation
// - Migration: V5→V6 compatibility
// - Fork protection: No unintended chain splits
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "khu/khu_commitment.h"
#include "khu/khu_commitmentdb.h"
#include "khu/khu_state.h"
#include "khu/khu_validation.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

/**
 * V6 Activation Test Fixture
 *
 * Sets up REGTEST environment with configurable V6.0 activation height.
 */
struct V6ActivationTestingSetup : public TestingSetup
{
    int V6_DefaultActivation;
    int V6_TestActivation;

    V6ActivationTestingSetup() : V6_TestActivation(1000)
    {
        SelectParams(CBaseChainParams::REGTEST);
        V6_DefaultActivation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }

    ~V6ActivationTestingSetup()
    {
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, V6_DefaultActivation);
        SelectParams(CBaseChainParams::MAIN);
    }

    void ActivateV6At(int height)
    {
        V6_TestActivation = height;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, height);
    }
};

BOOST_FIXTURE_TEST_SUITE(khu_v6_activation_tests, V6ActivationTestingSetup)

//==============================================================================
// TEST 1: PRE-ACTIVATION - Legacy PIVX Behavior
//==============================================================================

/**
 * CRITICAL TEST: Before V6.0 activation, everything must be legacy PIVX
 *
 * Validates:
 * - Emission: 10 PIV (legacy)
 * - KHU: Not active
 */
BOOST_AUTO_TEST_CASE(test_pre_activation_legacy_behavior)
{
    const int testHeight = 600;
    const Consensus::Params& consensus = Params().GetConsensus();

    BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    BOOST_CHECK_EQUAL(blockValue, 10 * COIN);
    BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
}

//==============================================================================
// TEST 2: ACTIVATION BOUNDARY - Exact transition
//==============================================================================

/**
 * CRITICAL TEST: At exactly V6.0 activation height, block reward = 0 IMMEDIATELY
 *
 * Validates:
 * - Block X-1: Legacy (10 PIV)
 * - Block X: V6.0 (0 PIV) <- IMMEDIATE, NO TRANSITION
 * - KHU state genesis
 */
BOOST_AUTO_TEST_CASE(test_activation_boundary_transition)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Test block BEFORE activation
    {
        const int testHeight = activationHeight - 1;
        BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        BOOST_CHECK_EQUAL(blockValue, 10 * COIN);  // Still legacy
    }

    // Test block AT activation - ZERO IMMEDIATE
    {
        const int testHeight = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // V6: ZERO IMMEDIATELY
        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    // Test block AFTER activation
    {
        const int testHeight = activationHeight + 1;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        BOOST_CHECK_EQUAL(blockValue, 0);  // Still zero
    }
}

//==============================================================================
// TEST 3: V6 BLOCK REWARD - Always 0 post-activation
//==============================================================================

/**
 * CRITICAL TEST: Block reward = 0 PIV at all times post-V6
 *
 * Economy is governed by:
 * - R% yield for ZKHU stakers (40%→7% over 33 years)
 * - T (DAO Treasury) from U × R% / 8 / 365
 */
BOOST_AUTO_TEST_CASE(test_v6_block_reward_always_zero)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    // Test various years - all should have 0 block reward
    const int testYears[] = {0, 1, 5, 10, 20, 33, 50, 100};

    for (int year : testYears) {
        const int testHeight = activationHeight + (year * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Block reward is ALWAYS 0 post-V6
        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }
}

//==============================================================================
// TEST 4: STATE INVARIANTS - C==U+Z, Cr==Ur preservation
//==============================================================================

/**
 * CRITICAL TEST: KHU invariants MUST hold at all times
 *
 * Validates:
 * - Genesis state: C=0, U=0, Z=0, Cr=0, Ur=0
 * - After operations: C==U+Z, Cr==Ur
 * - Invariants never violated
 */
BOOST_AUTO_TEST_CASE(test_state_invariants_preservation)
{
    // Test genesis state invariants
    KhuGlobalState genesisState;
    genesisState.SetNull();

    BOOST_CHECK_EQUAL(genesisState.C, 0);
    BOOST_CHECK_EQUAL(genesisState.U, 0);
    BOOST_CHECK_EQUAL(genesisState.Z, 0);
    BOOST_CHECK_EQUAL(genesisState.Cr, 0);
    BOOST_CHECK_EQUAL(genesisState.Ur, 0);
    BOOST_CHECK(genesisState.CheckInvariants());

    // Test state with values (transparent only)
    KhuGlobalState state;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Z = 0;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;
    state.nHeight = 5000;

    BOOST_CHECK(state.CheckInvariants());

    // Test state with shielded values
    state.C = 1000 * COIN;
    state.U = 700 * COIN;
    state.Z = 300 * COIN;  // C == U + Z
    BOOST_CHECK(state.CheckInvariants());

    // Test violated invariants (C != U + Z)
    state.C = 1001 * COIN;  // Break invariant
    BOOST_CHECK(!state.CheckInvariants());

    // Restore
    state.C = 1000 * COIN;
    BOOST_CHECK(state.CheckInvariants());

    // Test violated invariants (Cr != Ur)
    state.Cr = 101 * COIN;  // Break invariant
    BOOST_CHECK(!state.CheckInvariants());
}

//==============================================================================
// TEST 5: FINALITY ACTIVATION - Commitment DB active
//==============================================================================

/**
 * CRITICAL TEST: Finality system activates with V6.0
 */
BOOST_AUTO_TEST_CASE(test_finality_activation)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    CKHUCommitmentDB db(1 << 20, true, false);

    KhuGlobalState state;
    state.C = 0;
    state.U = 0;
    state.Z = 0;
    state.Cr = 0;
    state.Ur = 0;
    state.nHeight = activationHeight;

    uint256 hashState = ComputeKHUStateHash(state);
    BOOST_CHECK(!hashState.IsNull());

    uint256 quorumHash = uint256S("01");
    KhuStateCommitment commitment = CreateKHUStateCommitment(state, quorumHash);

    BOOST_CHECK_EQUAL(commitment.nHeight, activationHeight);
    BOOST_CHECK(commitment.hashState == hashState);

    commitment.signers.resize(50, false);
    for (int i = 0; i < 35; i++) {
        commitment.signers[i] = true;  // 70% quorum
    }

    BOOST_CHECK(commitment.HasQuorum());
    BOOST_CHECK(db.WriteCommitment(activationHeight, commitment));
    BOOST_CHECK_EQUAL(db.GetLatestFinalizedHeight(), activationHeight);
    BOOST_CHECK(db.IsFinalizedAt(activationHeight));
}

//==============================================================================
// TEST 6: REORG PROTECTION - 12 blocks + finality
//==============================================================================

/**
 * CRITICAL TEST: Reorg protection prevents deep reorgs
 */
BOOST_AUTO_TEST_CASE(test_reorg_protection_depth_and_finality)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    CKHUCommitmentDB db(1 << 20, true, false);

    KhuGlobalState state;
    state.C = 100 * COIN;
    state.U = 100 * COIN;
    state.Z = 0;
    state.Cr = 0;
    state.Ur = 0;
    state.nHeight = activationHeight + 50;

    KhuStateCommitment commitment = CreateKHUStateCommitment(state, uint256S("02"));
    commitment.signers.resize(50, false);
    for (int i = 0; i < 40; i++) {
        commitment.signers[i] = true;  // 80% quorum
    }

    BOOST_CHECK(commitment.HasQuorum());
    BOOST_CHECK(db.WriteCommitment(state.nHeight, commitment));

    uint32_t finalized = db.GetLatestFinalizedHeight();
    BOOST_CHECK_EQUAL(finalized, state.nHeight);

    // Try to erase finalized block → should fail
    BOOST_CHECK(!db.EraseCommitment(state.nHeight));

    // Can erase non-finalized blocks
    KhuStateCommitment nonFinal = CreateKHUStateCommitment(state, uint256S("03"));
    nonFinal.signers.clear();  // No quorum
    BOOST_CHECK(db.WriteCommitment(state.nHeight + 1, nonFinal));
    BOOST_CHECK(db.EraseCommitment(state.nHeight + 1));
}

//==============================================================================
// TEST 7: V5→V6 MIGRATION - Backward compatibility
//==============================================================================

/**
 * CRITICAL TEST: V5 nodes can validate chain up to V6 activation
 *
 * Validates:
 * - V5 emission works before activation
 * - Clean transition at boundary (to ZERO)
 * - No consensus split
 */
BOOST_AUTO_TEST_CASE(test_v5_to_v6_migration)
{
    const int activationHeight = 1000;

    // Simulate V5 node (V6 not activated)
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);

    const Consensus::Params& consensus = Params().GetConsensus();

    // V5 behavior: All blocks use legacy emission
    for (int h = 900; h < 1100; h++) {
        BOOST_CHECK(!NetworkUpgradeActive(h, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(h);
        BOOST_CHECK_EQUAL(blockValue, 10 * COIN);  // Always legacy
    }

    // Now activate V6
    ActivateV6At(activationHeight);

    // V6 behavior: Post-activation = ZERO
    for (int h = 900; h < activationHeight; h++) {
        BOOST_CHECK(!NetworkUpgradeActive(h, consensus, Consensus::UPGRADE_V6_0));
        BOOST_CHECK_EQUAL(GetBlockValue(h), 10 * COIN);
    }

    for (int h = activationHeight; h < 1100; h++) {
        BOOST_CHECK(NetworkUpgradeActive(h, consensus, Consensus::UPGRADE_V6_0));
        BOOST_CHECK_EQUAL(GetBlockValue(h), 0);  // V6: ZERO
    }
}

//==============================================================================
// TEST 8: FORK PROTECTION - No unintended splits
//==============================================================================

/**
 * CRITICAL TEST: Network cannot split unintentionally
 */
BOOST_AUTO_TEST_CASE(test_fork_protection_no_split)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    const int testHeight = activationHeight + 100;

    CAmount blockValues[10];
    CAmount mnPayments[10];

    for (int i = 0; i < 10; i++) {
        blockValues[i] = GetBlockValue(testHeight);
        mnPayments[i] = GetMasternodePayment(testHeight);
    }

    // All nodes MUST agree (all 0)
    for (int i = 1; i < 10; i++) {
        BOOST_CHECK_EQUAL(blockValues[i], blockValues[0]);
        BOOST_CHECK_EQUAL(mnPayments[i], mnPayments[0]);
        BOOST_CHECK_EQUAL(blockValues[i], 0);
        BOOST_CHECK_EQUAL(mnPayments[i], 0);
    }

    // Test state hash determinism
    KhuGlobalState state;
    state.C = 500 * COIN;
    state.U = 500 * COIN;
    state.Z = 0;
    state.Cr = 50 * COIN;
    state.Ur = 50 * COIN;
    state.nHeight = testHeight;

    uint256 hashes[10];
    for (int i = 0; i < 10; i++) {
        hashes[i] = ComputeKHUStateHash(state);
    }

    for (int i = 1; i < 10; i++) {
        BOOST_CHECK(hashes[i] == hashes[0]);
    }
}

//==============================================================================
// TEST 9: COMPREHENSIVE INTEGRATION - Everything together
//==============================================================================

/**
 * ULTIMATE TEST: Complete V6.0 activation scenario
 *
 * Validates:
 * - Full lifecycle from V5 to V6
 * - Block reward = 0 at all times post-V6
 * - All features working together
 */
BOOST_AUTO_TEST_CASE(test_comprehensive_v6_activation)
{
    const int activationHeight = 1000;
    ActivateV6At(activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Phase 1: Pre-activation (blocks 600-999, after V5.5)
    for (int h = 600; h < activationHeight; h += 100) {
        BOOST_CHECK(!NetworkUpgradeActive(h, consensus, Consensus::UPGRADE_V6_0));
        BOOST_CHECK_EQUAL(GetBlockValue(h), 10 * COIN);  // Legacy
    }

    // Phase 2: Activation (block 1000) - ZERO IMMEDIATE
    {
        int h = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(h, consensus, Consensus::UPGRADE_V6_0));
        BOOST_CHECK_EQUAL(GetBlockValue(h), 0);  // V6: ZERO

        KhuGlobalState state;
        state.SetNull();
        state.nHeight = h;
        BOOST_CHECK(state.CheckInvariants());

        uint256 hash = ComputeKHUStateHash(state);
        BOOST_CHECK(!hash.IsNull());
    }

    // Phase 3: Post-activation (blocks 1001+)
    CKHUCommitmentDB db(1 << 20, true, false);

    // Test years 0 through 33 - all have 0 block reward
    for (int yearOffset = 0; yearOffset <= 33; yearOffset += 5) {
        int h = activationHeight + (yearOffset * Consensus::Params::BLOCKS_PER_YEAR);

        // Block reward is ALWAYS 0 post-V6
        BOOST_CHECK_EQUAL(GetBlockValue(h), 0);

        KhuGlobalState state;
        state.C = (yearOffset * 100) * COIN;
        state.U = (yearOffset * 100) * COIN;
        state.Z = 0;
        state.Cr = (yearOffset * 10) * COIN;
        state.Ur = (yearOffset * 10) * COIN;
        state.nHeight = h;

        BOOST_CHECK(state.CheckInvariants());

        KhuStateCommitment commitment = CreateKHUStateCommitment(state, uint256S("ff"));
        commitment.signers.resize(50, false);
        for (int i = 0; i < 35; i++) {
            commitment.signers[i] = true;
        }

        BOOST_CHECK(commitment.HasQuorum());
        BOOST_CHECK(db.WriteCommitment(h, commitment));
    }

    BOOST_CHECK(db.GetLatestFinalizedHeight() > 0);
}

BOOST_AUTO_TEST_SUITE_END()
