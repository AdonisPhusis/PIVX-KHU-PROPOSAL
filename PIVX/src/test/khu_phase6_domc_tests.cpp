// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Phase 6.2 — DOMC Governance Tests
 *
 * Tests for commit-reveal voting mechanism for R% (annual yield rate).
 *
 * Coverage:
 * - Cycle boundary detection
 * - Commit transaction validation (8 rules)
 * - Reveal transaction validation (9 rules)
 * - Median calculation (0 votes, 1 vote, multiple votes, clamping)
 * - Cycle finalization (commit → reveal → median → R_annual update)
 * - Reorg support (undo operations)
 */

#include "test_pivx.h"

#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_domc.h"
#include "khu/khu_domcdb.h"
#include "khu/khu_domc_tx.h"
#include "khu/khu_state.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "streams.h"
#include "uint256.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

using namespace khu_domc;

BOOST_FIXTURE_TEST_SUITE(khu_phase6_domc_tests, TestingSetup)

// ============================================================================
// Helper functions
// ============================================================================

/**
 * Create a masternode outpoint for testing
 */
static COutPoint CreateTestMN(uint32_t index)
{
    uint256 hash = ArithToUint256(index);
    return COutPoint(hash, 0);
}

/**
 * Create a DOMC commit transaction
 */
static CTransaction CreateCommitTx(const DomcCommit& commit)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_DOMC_COMMIT;

    // Serialize commit to OP_RETURN
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << commit;
    std::vector<unsigned char> data(ss.begin(), ss.end());

    CScript script;
    script << OP_RETURN << data;

    CTxOut txout(0, script);
    mtx.vout.push_back(txout);

    return CTransaction(mtx);
}

/**
 * Create a DOMC reveal transaction
 */
static CTransaction CreateRevealTx(const DomcReveal& reveal)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_DOMC_REVEAL;

    // Serialize reveal to OP_RETURN
    CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
    ss << reveal;
    std::vector<unsigned char> data(ss.begin(), ss.end());

    CScript script;
    script << OP_RETURN << data;

    CTxOut txout(0, script);
    mtx.vout.push_back(txout);

    return CTransaction(mtx);
}

// ============================================================================
// Test 1: Cycle Boundary Detection
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_cycle_boundary)
{
    const uint32_t V6_ACTIVATION = 1000000;

    // Test 1: First cycle boundary (activation height)
    BOOST_CHECK(IsDomcCycleBoundary(V6_ACTIVATION, V6_ACTIVATION));

    // Test 2: Not a boundary (activation + 1)
    BOOST_CHECK(!IsDomcCycleBoundary(V6_ACTIVATION + 1, V6_ACTIVATION));

    // Test 3: Second cycle boundary (activation + CYCLE_LENGTH)
    const uint32_t CYCLE_2_START = V6_ACTIVATION + DOMC_CYCLE_LENGTH;
    BOOST_CHECK(IsDomcCycleBoundary(CYCLE_2_START, V6_ACTIVATION));

    // Test 4: One block before boundary
    BOOST_CHECK(!IsDomcCycleBoundary(CYCLE_2_START - 1, V6_ACTIVATION));

    // Test 5: Third cycle boundary
    const uint32_t CYCLE_3_START = V6_ACTIVATION + (2 * DOMC_CYCLE_LENGTH);
    BOOST_CHECK(IsDomcCycleBoundary(CYCLE_3_START, V6_ACTIVATION));

    // Test 6: Random height not on boundary
    BOOST_CHECK(!IsDomcCycleBoundary(V6_ACTIVATION + 50000, V6_ACTIVATION));
}

// ============================================================================
// Test 2: Commit Phase Detection
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_commit_phase)
{
    const uint32_t CYCLE_START = 1000000;

    // Test 1: Commit phase starts at DOMC_COMMIT_OFFSET
    const uint32_t COMMIT_START = CYCLE_START + DOMC_COMMIT_OFFSET;
    BOOST_CHECK(IsDomcCommitPhase(COMMIT_START, CYCLE_START));

    // Test 2: Before commit phase
    BOOST_CHECK(!IsDomcCommitPhase(COMMIT_START - 1, CYCLE_START));

    // Test 3: End of commit phase (one block before reveal)
    const uint32_t COMMIT_END = CYCLE_START + DOMC_REVEAL_OFFSET - 1;
    BOOST_CHECK(IsDomcCommitPhase(COMMIT_END, CYCLE_START));

    // Test 4: Reveal phase start (not commit phase)
    const uint32_t REVEAL_START = CYCLE_START + DOMC_REVEAL_OFFSET;
    BOOST_CHECK(!IsDomcCommitPhase(REVEAL_START, CYCLE_START));
}

// ============================================================================
// Test 3: Reveal Phase Detection
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_reveal_phase)
{
    const uint32_t CYCLE_START = 1000000;

    // Test 1: Reveal phase starts at DOMC_REVEAL_OFFSET
    const uint32_t REVEAL_START = CYCLE_START + DOMC_REVEAL_OFFSET;
    BOOST_CHECK(IsDomcRevealPhase(REVEAL_START, CYCLE_START));

    // Test 2: Before reveal phase (still in commit phase)
    BOOST_CHECK(!IsDomcRevealPhase(REVEAL_START - 1, CYCLE_START));

    // Test 3: End of reveal phase (one block before next cycle)
    const uint32_t REVEAL_END = CYCLE_START + DOMC_CYCLE_LENGTH - 1;
    BOOST_CHECK(IsDomcRevealPhase(REVEAL_END, CYCLE_START));

    // Test 4: Next cycle start (not reveal phase)
    const uint32_t NEXT_CYCLE = CYCLE_START + DOMC_CYCLE_LENGTH;
    BOOST_CHECK(!IsDomcRevealPhase(NEXT_CYCLE, CYCLE_START));
}

// ============================================================================
// Test 4: Commit Validation Rules
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_commit_validation)
{
    // Initialize DOMC DB for testing
    BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true)); // 1 MB cache, reindex
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    BOOST_REQUIRE(domcDB != nullptr);

    const uint32_t V6_ACTIVATION = 1000000;
    const uint32_t CYCLE_START = V6_ACTIVATION;
    const uint32_t COMMIT_HEIGHT = CYCLE_START + DOMC_COMMIT_OFFSET + 100;
    const uint32_t CYCLE_ID = GetCurrentCycleId(COMMIT_HEIGHT, V6_ACTIVATION);

    // Setup KHU state
    KhuGlobalState khuState;
    khuState.SetNull();
    khuState.nHeight = COMMIT_HEIGHT;
    khuState.domc_cycle_start = CYCLE_START;
    khuState.R_annual = 1500; // 15.00%

    // Setup consensus params
    Consensus::Params consensusParams;
    consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = V6_ACTIVATION;

    CValidationState state;

    // Test 1: Valid commit
    {
        COutPoint mnOutpoint = CreateTestMN(1);
        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        CTransaction tx = CreateCommitTx(commit);
        BOOST_CHECK(ValidateDomcCommitTx(tx, state, khuState, COMMIT_HEIGHT, consensusParams));
    }

    // Test 2: Wrong phase (too early - before commit phase)
    {
        COutPoint mnOutpoint = CreateTestMN(2);
        uint32_t wrongHeight = CYCLE_START + 1000; // Before commit phase

        KhuGlobalState wrongState = khuState;
        wrongState.nHeight = wrongHeight;

        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = wrongHeight;

        CTransaction tx = CreateCommitTx(commit);
        BOOST_CHECK(!ValidateDomcCommitTx(tx, state, wrongState, wrongHeight, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-commit-wrong-phase");
    }

    // Test 3: Wrong phase (too late - reveal phase)
    {
        COutPoint mnOutpoint = CreateTestMN(3);
        uint32_t revealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

        KhuGlobalState revealState = khuState;
        revealState.nHeight = revealHeight;

        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = revealHeight;

        CTransaction tx = CreateCommitTx(commit);
        CValidationState state2;
        BOOST_CHECK(!ValidateDomcCommitTx(tx, state2, revealState, revealHeight, consensusParams));
        BOOST_CHECK_EQUAL(state2.GetRejectReason(), "domc-commit-wrong-phase");
    }

    // Test 4: Wrong cycle ID
    {
        COutPoint mnOutpoint = CreateTestMN(4);
        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID + 1; // Wrong cycle
        commit.nCommitHeight = COMMIT_HEIGHT;

        CTransaction tx = CreateCommitTx(commit);
        CValidationState state2;
        BOOST_CHECK(!ValidateDomcCommitTx(tx, state2, khuState, COMMIT_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state2.GetRejectReason(), "domc-commit-wrong-cycle");
    }

    // Test 5: Duplicate commit
    {
        COutPoint mnOutpoint = CreateTestMN(5);
        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        // First commit should succeed
        CTransaction tx = CreateCommitTx(commit);
        CValidationState state2;
        BOOST_CHECK(ValidateDomcCommitTx(tx, state2, khuState, COMMIT_HEIGHT, consensusParams));
        BOOST_CHECK(ApplyDomcCommitTx(tx, COMMIT_HEIGHT));

        // Second commit from same MN should fail
        CValidationState state3;
        BOOST_CHECK(!ValidateDomcCommitTx(tx, state3, khuState, COMMIT_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state3.GetRejectReason(), "domc-commit-duplicate");
    }

    // Test 6: Wrong height
    {
        COutPoint mnOutpoint = CreateTestMN(6);
        DomcCommit commit;
        commit.hashCommit = InsecureRand256();
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT + 1; // Wrong height

        CTransaction tx = CreateCommitTx(commit);
        CValidationState state2;
        BOOST_CHECK(!ValidateDomcCommitTx(tx, state2, khuState, COMMIT_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state2.GetRejectReason(), "domc-commit-wrong-height");
    }
}

// ============================================================================
// Test 5: Reveal Validation Rules
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_reveal_validation)
{
    // Initialize DOMC DB for testing
    BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true)); // 1 MB cache, reindex
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    BOOST_REQUIRE(domcDB != nullptr);

    const uint32_t V6_ACTIVATION = 1000000;
    const uint32_t CYCLE_START = V6_ACTIVATION;
    const uint32_t COMMIT_HEIGHT = CYCLE_START + DOMC_COMMIT_OFFSET + 100;
    const uint32_t REVEAL_HEIGHT = CYCLE_START + DOMC_REVEAL_OFFSET + 100;
    const uint32_t CYCLE_ID = GetCurrentCycleId(REVEAL_HEIGHT, V6_ACTIVATION);

    // Setup KHU state
    KhuGlobalState khuState;
    khuState.SetNull();
    khuState.nHeight = REVEAL_HEIGHT;
    khuState.domc_cycle_start = CYCLE_START;
    khuState.R_annual = 1500; // 15.00%

    // Setup consensus params
    Consensus::Params consensusParams;
    consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = V6_ACTIVATION;

    // Test 1: Valid reveal (after commit)
    {
        COutPoint mnOutpoint = CreateTestMN(10);
        uint16_t R = 2000; // 20.00%
        uint256 salt = InsecureRand256();

        // First, create and apply commit
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        // Calculate commit hash: Hash(R || salt)
        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Now create reveal
        DomcReveal reveal;
        reveal.nRProposal = R;
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(ValidateDomcRevealTx(revealTx, state, khuState, REVEAL_HEIGHT, consensusParams));
    }

    // Test 2: Wrong phase (too early - commit phase)
    {
        COutPoint mnOutpoint = CreateTestMN(11);
        uint32_t wrongHeight = CYCLE_START + DOMC_COMMIT_OFFSET + 100;

        KhuGlobalState wrongState = khuState;
        wrongState.nHeight = wrongHeight;

        DomcReveal reveal;
        reveal.nRProposal = 2000;
        reveal.salt = InsecureRand256();
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = wrongHeight;

        CTransaction tx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(tx, state, wrongState, wrongHeight, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-wrong-phase");
    }

    // Test 3: Wrong cycle ID
    {
        COutPoint mnOutpoint = CreateTestMN(12);

        DomcReveal reveal;
        reveal.nRProposal = 2000;
        reveal.salt = InsecureRand256();
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID + 1; // Wrong cycle
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction tx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(tx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-wrong-cycle");
    }

    // Test 4: No matching commit
    {
        COutPoint mnOutpoint = CreateTestMN(13);

        DomcReveal reveal;
        reveal.nRProposal = 2000;
        reveal.salt = InsecureRand256();
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction tx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(tx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-no-commit");
    }

    // Test 5: Hash mismatch (wrong R or salt)
    {
        COutPoint mnOutpoint = CreateTestMN(14);
        uint16_t R = 2000;
        uint256 salt = InsecureRand256();

        // Create commit with correct hash
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Create reveal with WRONG R
        DomcReveal reveal;
        reveal.nRProposal = R + 100; // Different R
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(revealTx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-hash-mismatch");
    }

    // Test 6: R exceeds R_MAX (absolute maximum 50.00%)
    {
        COutPoint mnOutpoint = CreateTestMN(15);
        uint16_t R = R_MAX + 1; // 50.01% (too high)
        uint256 salt = InsecureRand256();

        // Create commit
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Create reveal with R > R_MAX
        DomcReveal reveal;
        reveal.nRProposal = R;
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(revealTx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-r-too-high");
    }

    // Test 7: Wrong height
    {
        COutPoint mnOutpoint = CreateTestMN(16);
        uint16_t R = 2000;
        uint256 salt = InsecureRand256();

        // Create commit
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Create reveal with WRONG height
        DomcReveal reveal;
        reveal.nRProposal = R;
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT + 1; // Wrong height

        CTransaction revealTx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(!ValidateDomcRevealTx(revealTx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state.GetRejectReason(), "domc-reveal-wrong-height");
    }

    // Test 8: Duplicate reveal
    {
        COutPoint mnOutpoint = CreateTestMN(17);
        uint16_t R = 2000;
        uint256 salt = InsecureRand256();

        // Create commit
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;

        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // First reveal should succeed
        DomcReveal reveal;
        reveal.nRProposal = R;
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        CValidationState state;
        BOOST_CHECK(ValidateDomcRevealTx(revealTx, state, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK(ApplyDomcRevealTx(revealTx, REVEAL_HEIGHT));

        // Second reveal should fail (duplicate)
        CValidationState state2;
        BOOST_CHECK(!ValidateDomcRevealTx(revealTx, state2, khuState, REVEAL_HEIGHT, consensusParams));
        BOOST_CHECK_EQUAL(state2.GetRejectReason(), "domc-reveal-duplicate");
    }
}

// ============================================================================
// Test 6: Median Calculation
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_median_calculation)
{
    // Initialize DOMC DB
    BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true));
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    BOOST_REQUIRE(domcDB != nullptr);

    const uint32_t V6_ACTIVATION = 1000000;
    const uint32_t CYCLE_START = V6_ACTIVATION;
    const uint32_t CYCLE_ID = GetCurrentCycleId(CYCLE_START, V6_ACTIVATION);
    const uint32_t COMMIT_HEIGHT = CYCLE_START + DOMC_COMMIT_OFFSET + 100;

    const uint16_t CURRENT_R = 1500; // 15.00%
    const uint16_t R_MAX_DYNAMIC = 3000; // 30.00%

    // Test 1: Zero votes → R unchanged
    {
        uint16_t result = CalculateDomcMedian(CYCLE_ID, CURRENT_R, R_MAX_DYNAMIC);
        BOOST_CHECK_EQUAL(result, CURRENT_R);
    }

    // Test 2: One vote → median = that vote
    {
        // Create commit + reveal for MN1
        COutPoint mn1 = CreateTestMN(100);
        uint16_t R1 = 2500; // 25.00%
        uint256 salt1 = InsecureRand256();

        DomcCommit commit1;
        commit1.mnOutpoint = mn1;
        commit1.nCycleId = CYCLE_ID;
        commit1.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw1(SER_GETHASH, 0);
        hw1 << R1 << salt1;
        commit1.hashCommit = hw1.GetHash();

        BOOST_CHECK(domcDB->WriteCommit(commit1));
        BOOST_CHECK(domcDB->AddMasternodeToCycleIndex(CYCLE_ID, mn1));

        DomcReveal reveal1;
        reveal1.nRProposal = R1;
        reveal1.salt = salt1;
        reveal1.mnOutpoint = mn1;
        reveal1.nCycleId = CYCLE_ID;
        reveal1.nRevealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

        BOOST_CHECK(domcDB->WriteReveal(reveal1));

        uint16_t result = CalculateDomcMedian(CYCLE_ID, CURRENT_R, R_MAX_DYNAMIC);
        BOOST_CHECK_EQUAL(result, R1);
    }

    // Test 3: Three votes → median (floor index)
    {
        // Add two more masternodes (MN1 already exists from test 2)
        COutPoint mn2 = CreateTestMN(101);
        uint16_t R2 = 1800; // 18.00%
        uint256 salt2 = InsecureRand256();

        DomcCommit commit2;
        commit2.mnOutpoint = mn2;
        commit2.nCycleId = CYCLE_ID;
        commit2.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw2(SER_GETHASH, 0);
        hw2 << R2 << salt2;
        commit2.hashCommit = hw2.GetHash();

        BOOST_CHECK(domcDB->WriteCommit(commit2));
        BOOST_CHECK(domcDB->AddMasternodeToCycleIndex(CYCLE_ID, mn2));

        DomcReveal reveal2;
        reveal2.nRProposal = R2;
        reveal2.salt = salt2;
        reveal2.mnOutpoint = mn2;
        reveal2.nCycleId = CYCLE_ID;
        reveal2.nRevealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

        BOOST_CHECK(domcDB->WriteReveal(reveal2));

        COutPoint mn3 = CreateTestMN(102);
        uint16_t R3 = 2200; // 22.00%
        uint256 salt3 = InsecureRand256();

        DomcCommit commit3;
        commit3.mnOutpoint = mn3;
        commit3.nCycleId = CYCLE_ID;
        commit3.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw3(SER_GETHASH, 0);
        hw3 << R3 << salt3;
        commit3.hashCommit = hw3.GetHash();

        BOOST_CHECK(domcDB->WriteCommit(commit3));
        BOOST_CHECK(domcDB->AddMasternodeToCycleIndex(CYCLE_ID, mn3));

        DomcReveal reveal3;
        reveal3.nRProposal = R3;
        reveal3.salt = salt3;
        reveal3.mnOutpoint = mn3;
        reveal3.nCycleId = CYCLE_ID;
        reveal3.nRevealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

        BOOST_CHECK(domcDB->WriteReveal(reveal3));

        // Sorted: [1800, 2200, 2500]
        // Median (floor index): proposals[3/2] = proposals[1] = 2200
        uint16_t result = CalculateDomcMedian(CYCLE_ID, CURRENT_R, R_MAX_DYNAMIC);
        BOOST_CHECK_EQUAL(result, 2200);
    }

    // Test 4: Clamping to R_MAX_dynamic
    {
        // Clear previous data
        BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true));
        domcDB = GetKHUDomcDB();

        const uint32_t CYCLE_ID_2 = CYCLE_ID + 1;

        // Create 3 votes, all > R_MAX_dynamic
        for (int i = 0; i < 3; i++) {
            COutPoint mn = CreateTestMN(200 + i);
            uint16_t R = R_MAX_DYNAMIC + 100 + (i * 50); // All exceed R_MAX_dynamic
            uint256 salt = InsecureRand256();

            DomcCommit commit;
            commit.mnOutpoint = mn;
            commit.nCycleId = CYCLE_ID_2;
            commit.nCommitHeight = COMMIT_HEIGHT;
            CHashWriter hw(SER_GETHASH, 0);
            hw << R << salt;
            commit.hashCommit = hw.GetHash();

            BOOST_CHECK(domcDB->WriteCommit(commit));
            BOOST_CHECK(domcDB->AddMasternodeToCycleIndex(CYCLE_ID_2, mn));

            DomcReveal reveal;
            reveal.nRProposal = R;
            reveal.salt = salt;
            reveal.mnOutpoint = mn;
            reveal.nCycleId = CYCLE_ID_2;
            reveal.nRevealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

            BOOST_CHECK(domcDB->WriteReveal(reveal));
        }

        // Median calculation should clamp to R_MAX_dynamic
        uint16_t result = CalculateDomcMedian(CYCLE_ID_2, CURRENT_R, R_MAX_DYNAMIC);
        BOOST_CHECK_EQUAL(result, R_MAX_DYNAMIC);
    }

    // Test 5: Even number of votes (4 votes → floor index)
    {
        BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true));
        domcDB = GetKHUDomcDB();

        const uint32_t CYCLE_ID_3 = CYCLE_ID + 2;

        std::vector<uint16_t> votes = {1000, 1500, 2000, 2500};

        for (size_t i = 0; i < votes.size(); i++) {
            COutPoint mn = CreateTestMN(300 + i);
            uint16_t R = votes[i];
            uint256 salt = InsecureRand256();

            DomcCommit commit;
            commit.mnOutpoint = mn;
            commit.nCycleId = CYCLE_ID_3;
            commit.nCommitHeight = COMMIT_HEIGHT;
            CHashWriter hw(SER_GETHASH, 0);
            hw << R << salt;
            commit.hashCommit = hw.GetHash();

            BOOST_CHECK(domcDB->WriteCommit(commit));
            BOOST_CHECK(domcDB->AddMasternodeToCycleIndex(CYCLE_ID_3, mn));

            DomcReveal reveal;
            reveal.nRProposal = R;
            reveal.salt = salt;
            reveal.mnOutpoint = mn;
            reveal.nCycleId = CYCLE_ID_3;
            reveal.nRevealHeight = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

            BOOST_CHECK(domcDB->WriteReveal(reveal));
        }

        // Sorted: [1000, 1500, 2000, 2500]
        // Median (floor index): proposals[4/2] = proposals[2] = 2000
        uint16_t result = CalculateDomcMedian(CYCLE_ID_3, CURRENT_R, R_MAX_DYNAMIC);
        BOOST_CHECK_EQUAL(result, 2000);
    }
}

// ============================================================================
// Test 7: Reorg Support (Undo Operations)
// ============================================================================

BOOST_AUTO_TEST_CASE(domc_reorg_support)
{
    // Initialize DOMC DB
    BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true));
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    BOOST_REQUIRE(domcDB != nullptr);

    const uint32_t V6_ACTIVATION = 1000000;
    const uint32_t CYCLE_START = V6_ACTIVATION;
    const uint32_t CYCLE_ID = GetCurrentCycleId(CYCLE_START, V6_ACTIVATION);
    const uint32_t COMMIT_HEIGHT = CYCLE_START + DOMC_COMMIT_OFFSET + 100;
    const uint32_t REVEAL_HEIGHT = CYCLE_START + DOMC_REVEAL_OFFSET + 100;

    COutPoint mnOutpoint = CreateTestMN(400);
    uint16_t R = 2000;
    uint256 salt = InsecureRand256();

    // Test 1: Undo commit
    {
        // Apply commit
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Verify commit exists
        DomcCommit readCommit;
        BOOST_CHECK(domcDB->ReadCommit(mnOutpoint, CYCLE_ID, readCommit));
        BOOST_CHECK(readCommit.hashCommit == commit.hashCommit);

        // Undo commit
        BOOST_CHECK(UndoDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Verify commit removed
        BOOST_CHECK(!domcDB->ReadCommit(mnOutpoint, CYCLE_ID, readCommit));
    }

    // Test 2: Undo reveal
    {
        // Apply commit first
        DomcCommit commit;
        commit.mnOutpoint = mnOutpoint;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw(SER_GETHASH, 0);
        hw << R << salt;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Apply reveal
        DomcReveal reveal;
        reveal.nRProposal = R;
        reveal.salt = salt;
        reveal.mnOutpoint = mnOutpoint;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        BOOST_CHECK(ApplyDomcRevealTx(revealTx, REVEAL_HEIGHT));

        // Verify reveal exists
        DomcReveal readReveal;
        BOOST_CHECK(domcDB->ReadReveal(mnOutpoint, CYCLE_ID, readReveal));
        BOOST_CHECK(readReveal.nRProposal == R);

        // Undo reveal
        BOOST_CHECK(UndoDomcRevealTx(revealTx, REVEAL_HEIGHT));

        // Verify reveal removed
        BOOST_CHECK(!domcDB->ReadReveal(mnOutpoint, CYCLE_ID, readReveal));

        // Commit should still exist
        DomcCommit readCommit;
        BOOST_CHECK(domcDB->ReadCommit(mnOutpoint, CYCLE_ID, readCommit));
    }

    // Test 3: Full cycle (commit → reveal → undo reveal → undo commit)
    {
        BOOST_REQUIRE(InitKHUDomcDB(1 << 20, true));
        domcDB = GetKHUDomcDB();

        COutPoint mn = CreateTestMN(401);
        uint16_t R_val = 1800;
        uint256 salt_val = InsecureRand256();

        // Step 1: Apply commit
        DomcCommit commit;
        commit.mnOutpoint = mn;
        commit.nCycleId = CYCLE_ID;
        commit.nCommitHeight = COMMIT_HEIGHT;
        CHashWriter hw(SER_GETHASH, 0);
        hw << R_val << salt_val;
        commit.hashCommit = hw.GetHash();

        CTransaction commitTx = CreateCommitTx(commit);
        BOOST_CHECK(ApplyDomcCommitTx(commitTx, COMMIT_HEIGHT));

        // Step 2: Apply reveal
        DomcReveal reveal;
        reveal.nRProposal = R_val;
        reveal.salt = salt_val;
        reveal.mnOutpoint = mn;
        reveal.nCycleId = CYCLE_ID;
        reveal.nRevealHeight = REVEAL_HEIGHT;

        CTransaction revealTx = CreateRevealTx(reveal);
        BOOST_CHECK(ApplyDomcRevealTx(revealTx, REVEAL_HEIGHT));

        // Verify both exist
        DomcCommit readCommit;
        DomcReveal readReveal;
        BOOST_CHECK(domcDB->ReadCommit(mn, CYCLE_ID, readCommit));
        BOOST_CHECK(domcDB->ReadReveal(mn, CYCLE_ID, readReveal));

        // Step 3: Undo reveal
        BOOST_CHECK(UndoDomcRevealTx(revealTx, REVEAL_HEIGHT));
        BOOST_CHECK(!domcDB->ReadReveal(mn, CYCLE_ID, readReveal));
        BOOST_CHECK(domcDB->ReadCommit(mn, CYCLE_ID, readCommit)); // Commit still exists

        // Step 4: Undo commit
        BOOST_CHECK(UndoDomcCommitTx(commitTx, COMMIT_HEIGHT));
        BOOST_CHECK(!domcDB->ReadCommit(mn, CYCLE_ID, readCommit));

        // DB should be empty now
        BOOST_CHECK(!domcDB->HaveCommit(mn, CYCLE_ID));
        BOOST_CHECK(!domcDB->HaveReveal(mn, CYCLE_ID));
    }
}

BOOST_AUTO_TEST_SUITE_END()
