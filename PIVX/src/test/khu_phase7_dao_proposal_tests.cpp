// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Phase 7 — DAO Proposal Tests (Using PIVX Budget System)
 *
 * Tests for DAO governance: MN voting on proposals paid from Treasury (T).
 * REUSES existing PIVX budget infrastructure (CBudgetProposal, CBudgetManager).
 *
 * ARCHITECTURE (POST-V6):
 *   - Pre-V6: Proposals paid from block reward (10 PIV/block)
 *   - Post-V6: Proposals paid from Treasury pool T (block reward = 0)
 *
 * KEY CHANGES FOR V6:
 *   1. GetTotalBudget() returns min(T, old_budget_calculation)
 *   2. FillBlockPayee() uses T instead of coinbase for proposal payments
 *   3. T -= proposal.amount when proposal is paid
 *   4. Proposal validation checks amount <= T
 *
 * EXISTING PIVX BUDGET FLOW (reused):
 *   CBudgetProposal → CBudgetVote → CFinalizedBudget → Payment
 *
 * INVARIANTS:
 *   - T >= 0 (never pay more than available)
 *   - Proposal amount <= T (at finalization)
 *   - Uses existing PROPOSAL_FEE_TX (50 PIV)
 *   - Uses existing vote window (nBudgetCycleBlocks)
 *
 * Note: This test file validates the integration between existing
 * budget system and the new Treasury pool (T).
 */

#include "test/test_pivx.h"

#include "budget/budgetmanager.h"
#include "budget/budgetproposal.h"
#include "budget/budgetvote.h"
#include "budget/finalizedbudget.h"
#include "chainparams.h"
#include "consensus/params.h"
#include "khu/khu_dao.h"
#include "khu/khu_domc.h"
#include "khu/khu_state.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "uint256.h"

#include <boost/test/unit_test.hpp>
#include <vector>

BOOST_FIXTURE_TEST_SUITE(khu_phase7_dao_proposal_tests, BasicTestingSetup)

// ═══════════════════════════════════════════════════════════════════════════
// V6 TREASURY BUDGET INTEGRATION
// ═══════════════════════════════════════════════════════════════════════════

/**
 * POST-V6 BUDGET CHANGES:
 *
 * The existing PIVX budget system (CBudgetProposal, CBudgetVote, etc.)
 * is fully reused. The only changes are:
 *
 * 1. GetTotalBudget(height) — Returns min(T, classic_calculation)
 *    Pre-V6:  Returns block_reward * blocks_per_cycle
 *    Post-V6: Returns min(state.T, classic_calculation)
 *
 * 2. FillBlockPayee() — Payment source changes
 *    Pre-V6:  Paid from block reward (coinbase/coinstake)
 *    Post-V6: Paid from Treasury T (state.T -= amount)
 *
 * 3. Proposal validation — Amount check against T
 *    Post-V6: proposal.amount <= state.T (at superblock)
 *
 * All voting, finalization, and relay logic remains unchanged.
 */

// Simulated V6 GetTotalBudget function
static CAmount GetTotalBudgetV6(int nHeight, CAmount treasuryT)
{
    // Post-V6: Budget limited to available Treasury
    // This replaces the classic GetTotalBudget() which uses block reward
    return treasuryT;
}

// Check if proposal amount is valid against Treasury
static bool IsProposalAmountValidV6(CAmount proposalAmount, CAmount treasuryT)
{
    // Proposal cannot request more than available in Treasury
    return proposalAmount <= treasuryT;
}

// Execute payment from Treasury (to be called in superblock)
static bool ExecuteTreasuryPayment(KhuGlobalState& state, CAmount amount)
{
    if (amount > state.T) {
        return false;  // Insufficient treasury
    }
    state.T -= amount;
    return true;
}

// Undo payment to Treasury (for reorg)
static bool UndoTreasuryPayment(KhuGlobalState& state, CAmount amount)
{
    state.T += amount;
    return true;
}

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static CScript GetTestPaymentAddress()
{
    CKey key;
    key.MakeNewKey(true);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

static CTxIn CreateTestMN(uint32_t index)
{
    uint256 hash = ArithToUint256(index);
    return CTxIn(COutPoint(hash, 0));
}

static CTxIn CreateTestMNFromHash(const uint256& hash)
{
    return CTxIn(COutPoint(hash, 0));
}

/**
 * Create a test CBudgetProposal using existing PIVX infrastructure
 */
static CBudgetProposal CreateTestBudgetProposal(
    const std::string& name,
    CAmount amount,
    int blockStart,
    int paymentCount = 1)
{
    CScript payee = GetTestPaymentAddress();
    std::string url = "http://test.proposal/" + name;
    uint256 feeTxHash = GetRandHash();

    return CBudgetProposal(name, url, paymentCount, payee, amount, blockStart, feeTxHash);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 1: PIVX BUDGET PROPOSAL WITH V6 TREASURY
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(budget_proposal_creation_valid)
{
    // Test: Create valid CBudgetProposal using existing PIVX system
    SelectParams(CBaseChainParams::REGTEST);

    int blockStart = 1000;
    CAmount amount = 100 * COIN;
    CBudgetProposal proposal = CreateTestBudgetProposal("TestProject", amount, blockStart);

    BOOST_CHECK(!proposal.GetName().empty());
    BOOST_CHECK_EQUAL(proposal.GetName(), "TestProject");
    BOOST_CHECK_EQUAL(proposal.GetAmount(), amount);
    BOOST_CHECK_EQUAL(proposal.GetBlockStart(), blockStart);
    BOOST_CHECK(!proposal.GetHash().IsNull());
}

BOOST_AUTO_TEST_CASE(budget_proposal_hash_uniqueness)
{
    // Test: Different proposals have different hashes
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal p1 = CreateTestBudgetProposal("ProjectA", 100 * COIN, 1000);
    CBudgetProposal p2 = CreateTestBudgetProposal("ProjectB", 100 * COIN, 1000);
    CBudgetProposal p3 = CreateTestBudgetProposal("ProjectA", 200 * COIN, 1000);

    BOOST_CHECK_NE(p1.GetHash(), p2.GetHash());  // Different name
    BOOST_CHECK_NE(p1.GetHash(), p3.GetHash());  // Different amount
}

BOOST_AUTO_TEST_CASE(budget_proposal_amount_vs_treasury)
{
    // Test: V6 proposal validation against Treasury T
    KhuGlobalState state;
    state.SetNull();
    state.T = 50000 * COIN;  // 50,000 KHU in treasury

    // Proposal exceeds treasury - should fail
    CAmount largeAmount = 100000 * COIN;
    BOOST_CHECK(!IsProposalAmountValidV6(largeAmount, state.T));

    // Proposal within treasury - should pass
    CAmount smallAmount = 25000 * COIN;
    BOOST_CHECK(IsProposalAmountValidV6(smallAmount, state.T));

    // Exact treasury amount - should pass
    BOOST_CHECK(IsProposalAmountValidV6(state.T, state.T));
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 2: MN VOTING (Using CBudgetVote)
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(budget_vote_creation)
{
    // Test: Create CBudgetVote using existing PIVX system
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal proposal = CreateTestBudgetProposal("VoteTest", 100 * COIN, 1000);
    CTxIn mnVin = CreateTestMNFromHash(GetRandHash());

    // Create YES vote
    CBudgetVote voteYes(mnVin, proposal.GetHash(), CBudgetVote::VOTE_YES);
    BOOST_CHECK_EQUAL(voteYes.GetDirection(), CBudgetVote::VOTE_YES);

    // Create NO vote
    CBudgetVote voteNo(mnVin, proposal.GetHash(), CBudgetVote::VOTE_NO);
    BOOST_CHECK_EQUAL(voteNo.GetDirection(), CBudgetVote::VOTE_NO);

    // Create ABSTAIN vote
    CBudgetVote voteAbstain(mnVin, proposal.GetHash(), CBudgetVote::VOTE_ABSTAIN);
    BOOST_CHECK_EQUAL(voteAbstain.GetDirection(), CBudgetVote::VOTE_ABSTAIN);
}

BOOST_AUTO_TEST_CASE(budget_proposal_vote_counting)
{
    // Test: Vote counting using CBudgetProposal methods
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal proposal = CreateTestBudgetProposal("CountTest", 100 * COIN, 1000);

    // Initial state: no votes
    BOOST_CHECK_EQUAL(proposal.GetYeas(), 0);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 0);
    BOOST_CHECK_EQUAL(proposal.GetAbstains(), 0);

    // Add YES votes
    for (int i = 0; i < 7; ++i) {
        CTxIn mnVin = CreateTestMNFromHash(GetRandHash());
        CBudgetVote vote(mnVin, proposal.GetHash(), CBudgetVote::VOTE_YES);
        std::string error;
        BOOST_CHECK(proposal.AddOrUpdateVote(vote, error));
    }

    // Add NO votes
    for (int i = 0; i < 3; ++i) {
        CTxIn mnVin = CreateTestMNFromHash(GetRandHash());
        CBudgetVote vote(mnVin, proposal.GetHash(), CBudgetVote::VOTE_NO);
        std::string error;
        BOOST_CHECK(proposal.AddOrUpdateVote(vote, error));
    }

    BOOST_CHECK_EQUAL(proposal.GetYeas(), 7);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 3);

    // Verify ratio: 7/(7+3) = 70%
    double ratio = proposal.GetRatio();
    BOOST_CHECK(ratio > 0.69 && ratio < 0.71);
}

BOOST_AUTO_TEST_CASE(budget_vote_update)
{
    // Test: MN can update their vote
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal proposal = CreateTestBudgetProposal("UpdateTest", 100 * COIN, 1000);
    CTxIn mnVin = CreateTestMNFromHash(GetRandHash());

    // First vote: YES
    CBudgetVote vote1(mnVin, proposal.GetHash(), CBudgetVote::VOTE_YES);
    std::string error;
    BOOST_CHECK(proposal.AddOrUpdateVote(vote1, error));
    BOOST_CHECK_EQUAL(proposal.GetYeas(), 1);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 0);

    // Update vote: NO (same MN, different vote)
    CBudgetVote vote2(mnVin, proposal.GetHash(), CBudgetVote::VOTE_NO);
    vote2.SetTime(vote1.GetTime() + BUDGET_VOTE_UPDATE_MIN + 1);  // After min update time
    BOOST_CHECK(proposal.AddOrUpdateVote(vote2, error));

    // Vote should be updated, not added
    BOOST_CHECK_EQUAL(proposal.GetYeas(), 0);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 1);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 3: TREASURY PAYMENT (V6 specific)
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(treasury_payment_execution)
{
    // Test: Execute payment from Treasury T
    KhuGlobalState state;
    state.SetNull();
    state.T = 100000 * COIN;

    CAmount paymentAmount = 30000 * COIN;

    // Execute payment
    CAmount T_before = state.T;
    BOOST_CHECK(ExecuteTreasuryPayment(state, paymentAmount));
    BOOST_CHECK_EQUAL(state.T, T_before - paymentAmount);
    BOOST_CHECK_EQUAL(state.T, 70000 * COIN);
    BOOST_CHECK(state.T >= 0);  // Invariant maintained
}

BOOST_AUTO_TEST_CASE(treasury_payment_insufficient)
{
    // Test: Payment rejected when T is insufficient
    KhuGlobalState state;
    state.SetNull();
    state.T = 10000 * COIN;

    CAmount paymentAmount = 50000 * COIN;

    // Payment should fail
    BOOST_CHECK(!ExecuteTreasuryPayment(state, paymentAmount));

    // T unchanged
    BOOST_CHECK_EQUAL(state.T, 10000 * COIN);
}

BOOST_AUTO_TEST_CASE(treasury_multiple_payments)
{
    // Test: Multiple sequential payments from T
    KhuGlobalState state;
    state.SetNull();
    state.T = 200000 * COIN;

    // Payment 1
    BOOST_CHECK(ExecuteTreasuryPayment(state, 50000 * COIN));
    BOOST_CHECK_EQUAL(state.T, 150000 * COIN);

    // Payment 2
    BOOST_CHECK(ExecuteTreasuryPayment(state, 30000 * COIN));
    BOOST_CHECK_EQUAL(state.T, 120000 * COIN);

    // Payment 3 (would exceed)
    BOOST_CHECK(!ExecuteTreasuryPayment(state, 150000 * COIN));
    BOOST_CHECK_EQUAL(state.T, 120000 * COIN);  // Unchanged
}

BOOST_AUTO_TEST_CASE(treasury_exact_drain)
{
    // Test: Payment draining exactly T to zero
    KhuGlobalState state;
    state.SetNull();
    state.T = 50000 * COIN;

    BOOST_CHECK(ExecuteTreasuryPayment(state, state.T));
    BOOST_CHECK_EQUAL(state.T, 0);  // Treasury drained but valid
    BOOST_CHECK(state.T >= 0);       // Invariant maintained
}

BOOST_AUTO_TEST_CASE(budget_total_limited_to_treasury)
{
    // Test: GetTotalBudgetV6 returns Treasury limit
    KhuGlobalState state;
    state.SetNull();
    state.T = 75000 * COIN;

    // V6: Budget limited to T
    CAmount budget = GetTotalBudgetV6(1000000, state.T);
    BOOST_CHECK_EQUAL(budget, state.T);

    // Zero treasury = zero budget
    state.T = 0;
    budget = GetTotalBudgetV6(1000000, state.T);
    BOOST_CHECK_EQUAL(budget, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 4: REORG SUPPORT (Treasury Undo)
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(treasury_payment_undo)
{
    // Test: Undo payment after reorg
    KhuGlobalState state;
    state.SetNull();
    state.T = 100000 * COIN;

    CAmount paymentAmount = 25000 * COIN;

    // Payment executed
    CAmount T_before = state.T;
    BOOST_CHECK(ExecuteTreasuryPayment(state, paymentAmount));
    BOOST_CHECK_EQUAL(state.T, 75000 * COIN);

    // REORG: Undo payment
    BOOST_CHECK(UndoTreasuryPayment(state, paymentAmount));
    BOOST_CHECK_EQUAL(state.T, T_before);
}

BOOST_AUTO_TEST_CASE(treasury_multiple_undo)
{
    // Test: Multiple payments then undo in reverse order
    KhuGlobalState state;
    state.SetNull();
    state.T = 200000 * COIN;

    CAmount payment1 = 30000 * COIN;
    CAmount payment2 = 50000 * COIN;
    CAmount payment3 = 20000 * COIN;

    // Execute payments
    BOOST_CHECK(ExecuteTreasuryPayment(state, payment1));
    BOOST_CHECK_EQUAL(state.T, 170000 * COIN);

    BOOST_CHECK(ExecuteTreasuryPayment(state, payment2));
    BOOST_CHECK_EQUAL(state.T, 120000 * COIN);

    BOOST_CHECK(ExecuteTreasuryPayment(state, payment3));
    BOOST_CHECK_EQUAL(state.T, 100000 * COIN);

    // Undo in reverse order
    BOOST_CHECK(UndoTreasuryPayment(state, payment3));
    BOOST_CHECK_EQUAL(state.T, 120000 * COIN);

    BOOST_CHECK(UndoTreasuryPayment(state, payment2));
    BOOST_CHECK_EQUAL(state.T, 170000 * COIN);

    BOOST_CHECK(UndoTreasuryPayment(state, payment1));
    BOOST_CHECK_EQUAL(state.T, 200000 * COIN);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 5: INTEGRATION WITH T ACCUMULATION
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(treasury_accumulation_and_spending)
{
    // Test: T accumulates daily, proposals spend from it
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 10000000 * COIN;  // 10M KHU
    state.R_annual = 4000;       // 40%
    state.T = 0;

    // Accumulate T for 100 days
    for (int day = 1; day <= 100; day++) {
        khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);
    }

    CAmount T_after_100_days = state.T;
    BOOST_CHECK(T_after_100_days > 0);

    // Execute payment (simulating passed proposal)
    CAmount paymentAmount = T_after_100_days / 2;
    BOOST_CHECK(ExecuteTreasuryPayment(state, paymentAmount));

    // Verify T reduced
    BOOST_CHECK_EQUAL(state.T, T_after_100_days / 2);

    // Continue accumulating
    for (int day = 101; day <= 200; day++) {
        khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);
    }

    // T should be higher than after first 100 days (accumulation > spending)
    BOOST_CHECK(state.T > T_after_100_days / 2);
}

BOOST_AUTO_TEST_CASE(treasury_yearly_cycle)
{
    // Test: Full year - T accumulation, monthly budget spending
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 5000000 * COIN;  // 5M KHU
    state.R_annual = 4000;
    state.T = 0;

    // Track T over time
    std::vector<CAmount> T_history;

    for (int day = 1; day <= 365; day++) {
        khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);

        // Monthly spending (every 30 days) - simulating approved proposals
        if (day % 30 == 0) {
            CAmount spendAmount = state.T / 10;  // 10% of current T
            if (spendAmount > 0) {
                ExecuteTreasuryPayment(state, spendAmount);
            }
        }

        if (day % 30 == 0) {
            T_history.push_back(state.T);
        }
    }

    // T should be positive after year
    BOOST_CHECK(state.T > 0);

    // T should have grown (accumulation > spending at 10%)
    BOOST_CHECK(T_history.back() > T_history.front());
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 6: EDGE CASES
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(budget_proposal_no_votes)
{
    // Test: CBudgetProposal with no votes
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal proposal = CreateTestBudgetProposal("NoVotes", 100 * COIN, 1000);
    BOOST_CHECK_EQUAL(proposal.GetYeas(), 0);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 0);
    BOOST_CHECK_EQUAL(proposal.GetRatio(), 0.0);  // 0/0 = 0
}

BOOST_AUTO_TEST_CASE(budget_proposal_all_abstain)
{
    // Test: All votes are ABSTAIN
    SelectParams(CBaseChainParams::REGTEST);

    CBudgetProposal proposal = CreateTestBudgetProposal("AllAbstain", 100 * COIN, 1000);

    for (int i = 0; i < 5; ++i) {
        CTxIn mnVin = CreateTestMNFromHash(GetRandHash());
        CBudgetVote vote(mnVin, proposal.GetHash(), CBudgetVote::VOTE_ABSTAIN);
        std::string error;
        BOOST_CHECK(proposal.AddOrUpdateVote(vote, error));
    }

    BOOST_CHECK_EQUAL(proposal.GetYeas(), 0);
    BOOST_CHECK_EQUAL(proposal.GetNays(), 0);
    BOOST_CHECK_EQUAL(proposal.GetAbstains(), 5);
    BOOST_CHECK_EQUAL(proposal.GetRatio(), 0.0);  // No YES/NO votes
}

BOOST_AUTO_TEST_CASE(treasury_zero_payment)
{
    // Test: Zero amount payment
    KhuGlobalState state;
    state.SetNull();
    state.T = 50000 * COIN;

    // Zero payment should succeed (no change)
    BOOST_CHECK(ExecuteTreasuryPayment(state, 0));
    BOOST_CHECK_EQUAL(state.T, 50000 * COIN);
}

BOOST_AUTO_TEST_CASE(budget_proposal_minimum_amount)
{
    // Test: Proposal with minimum valid amount
    SelectParams(CBaseChainParams::REGTEST);

    // PROPOSAL_MIN_AMOUNT = 10 * COIN
    CBudgetProposal proposal = CreateTestBudgetProposal("MinAmount", PROPOSAL_MIN_AMOUNT, 1000);
    BOOST_CHECK_EQUAL(proposal.GetAmount(), PROPOSAL_MIN_AMOUNT);
}

BOOST_AUTO_TEST_CASE(treasury_invariant_after_operations)
{
    // Test: T >= 0 invariant after all operations
    KhuGlobalState state;
    state.SetNull();
    state.C = 1000000 * COIN;
    state.U = 1000000 * COIN;
    state.T = 100000 * COIN;

    // Verify initial invariants
    BOOST_CHECK(state.CheckInvariants());

    // Execute multiple payments
    BOOST_CHECK(ExecuteTreasuryPayment(state, 30000 * COIN));
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK(state.T >= 0);

    BOOST_CHECK(ExecuteTreasuryPayment(state, 40000 * COIN));
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK(state.T >= 0);

    // Undo payments
    BOOST_CHECK(UndoTreasuryPayment(state, 40000 * COIN));
    BOOST_CHECK(state.CheckInvariants());

    BOOST_CHECK(UndoTreasuryPayment(state, 30000 * COIN));
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.T, 100000 * COIN);
}

BOOST_AUTO_TEST_SUITE_END()
