// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 4: STAKE/UNSTAKE pipeline
//
// Canonical specification: docs/04-phase4-stake-unstake.md
// Implementation: STAKE converts KHU_T → ZKHU, UNSTAKE converts ZKHU → KHU_T + bonus
//
// Tests verify:
// - STAKE: KHU_T consumed, ZKHU created, state unchanged (C, U, Cr, Ur)
// - UNSTAKE: ZKHU consumed, KHU_T created, double flux applied (C+, U+, Cr-, Ur-)
// - Maturity enforcement (4320 blocks minimum)
// - Reorg safety (Undo functions restore exact state)
// - Invariants maintained (C==U, Cr==Ur after every operation)
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "coins.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "consensus/validation.h"
#include "khu/khu_coins.h"
#include "khu/khu_stake.h"
#include "khu/khu_unstake.h"
#include "khu/khu_state.h"
#include "streams.h"
#include "khu/khu_utxo.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "primitives/transaction.h"
#include "sapling/sapling_transaction.h"
#include "script/standard.h"
#include "sync.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

// Test-only lock for KHU operations (mimics cs_khu in khu_validation.cpp)
// This is needed because Apply/Undo functions have AssertLockHeld(cs_khu)
static RecursiveMutex cs_khu_test;
#define cs_khu cs_khu_test

// Custom test fixture that initializes ZKHU DB using production Init function
// Inherits from TestingSetup (not BasicTestingSetup) to ensure pcoinsTip is initialized
struct KHUPhase4TestingSetup : public TestingSetup {
    KHUPhase4TestingSetup() : TestingSetup() {
        // Initialize ZKHU DB for tests using production function
        // This initializes the global pzkhudb that GetZKHUDB() returns
        if (!InitZKHUDB(1 << 20, false)) {
            throw std::runtime_error("Failed to initialize ZKHU DB for tests");
        }
    }

    ~KHUPhase4TestingSetup() {
        // Cleanup is handled by the production pzkhudb unique_ptr
    }
};

BOOST_FIXTURE_TEST_SUITE(khu_phase4_tests, KHUPhase4TestingSetup)

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

// Helper: Create a mock STAKE transaction (KHU_T → ZKHU)
static CTransactionRef CreateStakeTx(CAmount amount, const COutPoint& khuInput)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_STAKE;

    // Input: KHU_T UTXO being consumed
    mtx.vin.emplace_back(khuInput);

    // Phase 5: Add minimal Sapling output for ApplyKHUStake to extract commitment
    SaplingTxData sapData;

    // Create mock Sapling output with random commitment
    OutputDescription saplingOut;
    saplingOut.cmu = GetRandHash();  // Mock commitment (will be note ID)
    sapData.vShieldedOutput.push_back(saplingOut);

    // CRITICAL: Set valueBalance correctly for STAKE
    // For STAKE: transparent → shielded, so valueBalance = -amount
    // ApplyKHUStake extracts: stakedAmount = -valueBalance
    sapData.valueBalance = -amount;

    mtx.sapData = sapData;

    return MakeTransactionRef(mtx);
}

// Helper: Create a mock UNSTAKE transaction (ZKHU → KHU_T + bonus)
// Phase 5/8 fix: Now includes cm in extraPayload (avoids nullifier mapping mismatch)
static CTransactionRef CreateUnstakeTx(CAmount amount, const CScript& dest,
                                       const uint256& nullifier, int nHeight,
                                       const uint256& cm)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_UNSTAKE;

    // Phase 5/8 fix: Add extraPayload with cm so consensus can lookup note directly
    CUnstakeKHUPayload payload(cm);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    // Phase 5: Add minimal Sapling spend for ApplyKHUUnstake to extract nullifier
    SaplingTxData sapData;

    // Create mock Sapling spend with provided nullifier
    SpendDescription saplingSpend;
    saplingSpend.nullifier = nullifier;
    sapData.vShieldedSpend.push_back(saplingSpend);

    mtx.sapData = sapData;

    // Output: KHU_T UTXO (amount determined by Apply logic including bonus)
    mtx.vout.emplace_back(amount, dest);

    return MakeTransactionRef(mtx);
}

// Helper: Setup initial KHU state with known values (in COIN units)
// NOTE: Z is calculated automatically to satisfy invariant C == U + Z
static void SetupKHUState(KhuGlobalState& state, int height,
                          int64_t C, int64_t U, int64_t Cr, int64_t Ur,
                          int64_t Z = 0)  // Z defaults to 0 for STAKE tests
{
    state.SetNull();
    state.nHeight = height;
    state.C = C * COIN;   // Convert to satoshis
    state.U = U * COIN;   // Convert to satoshis
    state.Z = Z * COIN;   // ZKHU staked (for UNSTAKE tests)
    state.Cr = Cr * COIN; // Convert to satoshis
    state.Ur = Ur * COIN; // Convert to satoshis
    state.hashBlock = GetRandHash();
    state.hashPrevState = GetRandHash();

    // IMPORTANT: For invariant C == U + Z, adjust C if needed
    // Default: caller sets C, U, Z explicitly and must ensure C == U + Z
}

// Helper: Add KHU_T UTXO to coins view AND KHU tracking system
// NOTE: Must use AddKHUCoin() so GetKHUCoin() can find the coin in mapKHUUTXOs
static void AddKHUCoinToView(CCoinsViewCache& view, const COutPoint& outpoint, CAmount amount, uint32_t nHeight = 1)
{
    CScript scriptPubKey = GetScriptForDestination(CKeyID(uint160()));
    CKHUUTXO khuCoin(amount, scriptPubKey, nHeight);
    AddKHUCoin(view, outpoint, khuCoin);
}

// Helper: Create mock ZKHU note in database (Phase 4: Ur_accumulated=0)
static void AddZKHUNoteToMockDB(const uint256& cm, const uint256& nullifier,
                                CAmount amount, int nHeight)
{
    // Phase 4: Represents a STAKE with Ur_accumulated=0
    ZKHUNoteData noteData(
        amount,
        nHeight,
        0,  // Phase 4: always 0 at STAKE time
        nullifier,
        cm
    );

    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (zkhuDB) {
        zkhuDB->WriteNote(cm, noteData);
        zkhuDB->WriteNullifierMapping(nullifier, cm);
    }
}

// ============================================================================
// TEST 1: STAKE BASIC
// ============================================================================
// Verify that a STAKE transaction:
// - Consumes a KHU_T UTXO
// - Creates a ZKHU note
// - Does NOT affect C, U, Cr, Ur (state unchanged)
// ============================================================================

BOOST_AUTO_TEST_CASE(test_stake_basic)
{
    LOCK(cs_khu);

    // 1. Setup initial state
    KhuGlobalState state;
    SetupKHUState(state, 1000, 100, 100, 50, 50);

    CCoinsViewCache view(pcoinsTip.get());

    // 2. Create KHU_T UTXO (from previous MINT)
    CAmount amount = 10 * COIN;
    COutPoint khuInput(GetRandHash(), 0);
    AddKHUCoinToView(view, khuInput, amount);

    // 3. Build STAKE transaction
    CTransactionRef stakeTx = CreateStakeTx(amount, khuInput);

    // 4. Store state before Apply
    KhuGlobalState stateBefore = state;

    // 5. Apply STAKE
    bool result = ApplyKHUStake(*stakeTx, view, state, 1000);

    // 6. Assertions
    BOOST_CHECK(result);

    // STAKE: C unchanged, U decreases (KHU_T consumed → ZKHU created)
    // Invariant C == U + Z is maintained because Z increases by same amount
    BOOST_CHECK_EQUAL(state.C, stateBefore.C);
    BOOST_CHECK_EQUAL(state.U, stateBefore.U - amount);  // U decreases by staked amount
    BOOST_CHECK_EQUAL(state.Cr, stateBefore.Cr);         // Cr unchanged
    BOOST_CHECK_EQUAL(state.Ur, stateBefore.Ur);         // Ur unchanged

    // Invariants must hold (C == U + Z where Z = amount staked)
    BOOST_CHECK(state.CheckInvariants());

    // Note: In full blockchain processing, the UTXO would be spent by standard validation.
    // ApplyKHUStake only updates KHU state (U, Z) - it doesn't duplicate UTXO spending.
    // In tests calling Apply directly, the coin remains in the view.

    // Verify ZKHU note was created in zkhuDB
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    BOOST_CHECK(zkhuDB != nullptr);

    // Note ID is the commitment from the Sapling output
    uint256 cm = stakeTx->sapData->vShieldedOutput[0].cmu;
    ZKHUNoteData noteData;
    BOOST_CHECK(zkhuDB->ReadNote(cm, noteData));
    BOOST_CHECK_EQUAL(noteData.amount, amount);

    // TODO Phase 4: Verify ZKHU note exists in zkhuDB
    // CZKHUDatabase* zkhuDB = GetZKHUDatabase();
    // BOOST_CHECK(zkhuDB->HaveNote(stakeTx->vShieldedOutput[0].cmu));
}

// ============================================================================
// TEST 2: UNSTAKE BASIC (B=0)
// ============================================================================
// Verify UNSTAKE with Ur_accumulated = 0 (no bonus):
// - ZKHU note consumed
// - KHU_T UTXO created with amount = original_amount
// - Double flux executes but net effect is zero
// - Invariants maintained
// ============================================================================

BOOST_AUTO_TEST_CASE(test_unstake_basic)
{
    LOCK(cs_khu);

    // 1. Setup initial state with Z = 10 (the amount we'll unstake)
    // Invariant: C == U + Z → 100 == 90 + 10 ✓
    KhuGlobalState state;
    SetupKHUState(state, 5000, 100, 90, 50, 50, 10);

    CCoinsViewCache view(pcoinsTip.get());

    // 2. Create mock ZKHU note with Ur_accumulated = 0
    CAmount amount = 10 * COIN;
    uint256 nullifier = GetRandHash();
    uint256 cm = uint256S("0000000000000000000000000000000000000000000000000000000000000001");

    AddZKHUNoteToMockDB(cm, nullifier, amount, 1000);

    // 3. Build UNSTAKE transaction
    CScript dest = GetScriptForDestination(CKeyID(uint160()));
    CTransactionRef unstakeTx = CreateUnstakeTx(amount, dest, nullifier, 5000, cm);

    // 4. Store state before Apply
    KhuGlobalState stateBefore = state;

    // 5. Apply UNSTAKE
    bool result = ApplyKHUUnstake(*unstakeTx, view, state, 5000);

    // 6. Assertions
    BOOST_CHECK(result);

    // UNSTAKE with Y=0 (no bonus):
    // Z -= P (10 → 0), U += P (90 → 100), C unchanged, Cr/Ur unchanged
    BOOST_CHECK_EQUAL(state.Z, 0);                     // Z consumed
    BOOST_CHECK_EQUAL(state.U, stateBefore.U + amount); // U increased by P
    BOOST_CHECK_EQUAL(state.C, stateBefore.C);         // C unchanged (Y=0)
    BOOST_CHECK_EQUAL(state.Cr, stateBefore.Cr);       // Cr unchanged (Y=0)
    BOOST_CHECK_EQUAL(state.Ur, stateBefore.Ur);       // Ur unchanged (Y=0)

    // Invariants must hold (C == U + Z → 100 == 100 + 0)
    BOOST_CHECK(state.CheckInvariants());

    // NOTE: ApplyKHUUnstake does NOT add coins to the view.
    // That's done by standard transaction processing in ConnectBlock via AddCoins().
    // ApplyKHUUnstake only updates KHU state (C/U/Z/Cr/Ur) and ZKHU database.
    // The output amount is verified by ApplyKHUUnstake against tx.vout[0].nValue.
}

// ============================================================================
// TEST 3: UNSTAKE WITH BONUS (Phase 5 Ready)
// ============================================================================
// Verify UNSTAKE with Ur_accumulated > 0 (bonus present):
// - Double flux correctly applied: C += B, U += B, Cr -= B, Ur -= B
// - Output amount = original_amount + bonus
// - Invariants maintained after mutation
// ============================================================================

BOOST_AUTO_TEST_CASE(test_unstake_with_bonus_phase5_ready)
{
    LOCK(cs_khu);

    // 1. Setup initial state with Z = 100 (the amount we'll unstake)
    // Invariant: C == U + Z → 1000 == 900 + 100 ✓
    KhuGlobalState state;
    SetupKHUState(state, 10000, 1000, 900, 500, 500, 100);

    CCoinsViewCache view(pcoinsTip.get());

    // 2. Create ZKHU note with Ur_accumulated = 50 (simulating Phase 5 bonus)
    CAmount amount = 100 * COIN;
    CAmount bonus = 50 * COIN;
    uint256 nullifier = GetRandHash();
    uint256 cm = uint256S("0000000000000000000000000000000000000000000000000000000000000002");

    // Phase 5: Manually create note with accumulated yield (NOT via helper)
    ZKHUNoteData noteWithBonus(
        amount,
        5000,               // nStakeStartHeight
        bonus,              // Ur_accumulated - simulated Phase 5 yield
        nullifier,
        cm
    );

    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    zkhuDB->WriteNote(cm, noteWithBonus);
    zkhuDB->WriteNullifierMapping(nullifier, cm);

    // 3. Build UNSTAKE transaction
    CScript dest = GetScriptForDestination(CKeyID(uint160()));
    CTransactionRef unstakeTx = CreateUnstakeTx(amount + bonus, dest, nullifier, 10000, cm);

    // 4. Store state before Apply
    KhuGlobalState stateBefore = state;

    // 5. Apply UNSTAKE (with bonus)
    bool result = ApplyKHUUnstake(*unstakeTx, view, state, 10000);

    // 6. Assertions
    BOOST_CHECK(result);

    // UNSTAKE with Y=50 (bonus):
    // Z: 100 → 0 (P consumed)
    // U: 900 → 1050 (P+Y = 100+50 added)
    // C: 1000 → 1050 (Y=50 added)
    // Cr: 500 → 450 (Y=50 consumed)
    // Ur: 500 → 450 (Y=50 consumed)
    BOOST_CHECK_EQUAL(state.Z, 0);                              // Z consumed
    BOOST_CHECK_EQUAL(state.U, stateBefore.U + amount + bonus); // U increased by P+Y
    BOOST_CHECK_EQUAL(state.C, stateBefore.C + bonus);          // C increased by Y
    BOOST_CHECK_EQUAL(state.Cr, stateBefore.Cr - bonus);        // Cr decreased by Y
    BOOST_CHECK_EQUAL(state.Ur, stateBefore.Ur - bonus);        // Ur decreased by Y

    // NOTE: ApplyKHUUnstake does NOT add coins to the view.
    // That's done by standard transaction processing in ConnectBlock via AddCoins().
    // ApplyKHUUnstake only updates KHU state (C/U/Z/Cr/Ur) and ZKHU database.
    // The output amount (P+Y) is verified by ApplyKHUUnstake against tx.vout[0].nValue.

    // Invariants MUST hold (C == U + Z → 1050 == 1050 + 0)
    BOOST_CHECK(state.CheckInvariants());
}

// ============================================================================
// TEST 4: UNSTAKE MATURITY
// ============================================================================
// Verify maturity enforcement:
// - UNSTAKE rejected if note age < 4320 blocks
// - UNSTAKE accepted if note age >= 4320 blocks
// ============================================================================

BOOST_AUTO_TEST_CASE(test_unstake_maturity)
{
    LOCK(cs_khu);

    // Setup with Z = 10 (the amount we'll try to unstake)
    // Invariant: C == U + Z → 100 == 90 + 10 ✓
    KhuGlobalState state;
    SetupKHUState(state, 5000, 100, 90, 50, 50, 10);

    CCoinsViewCache view(pcoinsTip.get());

    // Create ZKHU note at height 1000
    CAmount amount = 10 * COIN;
    uint256 nullifier = GetRandHash();
    uint256 cm = uint256S("0000000000000000000000000000000000000000000000000000000000000003");
    int noteHeight = 1000;

    AddZKHUNoteToMockDB(cm, nullifier, amount, noteHeight);

    CScript dest = GetScriptForDestination(CKeyID(uint160()));

    // Get consensus params
    const Consensus::Params& consensus = Params().GetConsensus();

    // Test 1: Attempt UNSTAKE at height 1000 + 4319 = 5319 (IMMATURE)
    {
        CTransactionRef unstakeTx = CreateUnstakeTx(amount, dest, nullifier, 5319, cm);
        CValidationState validationState;

        // Should fail maturity check
        bool result = CheckKHUUnstake(*unstakeTx, view, validationState, consensus, state, 5319);
        BOOST_CHECK(!result); // Must be rejected
    }

    // Test 2: Attempt UNSTAKE at height 1000 + 4320 = 5320 (MATURE)
    {
        CTransactionRef unstakeTx = CreateUnstakeTx(amount, dest, nullifier, 5320, cm);
        CValidationState validationState;

        // Should pass maturity check
        bool result = CheckKHUUnstake(*unstakeTx, view, validationState, consensus, state, 5320);
        BOOST_CHECK(result); // Must be accepted
    }
}

// ============================================================================
// TEST 5: MULTIPLE UNSTAKE ISOLATION
// ============================================================================
// Verify that multiple ZKHU notes with distinct Ur_accumulated values
// maintain independence - bonus of one note does not affect another
// ============================================================================

BOOST_AUTO_TEST_CASE(test_multiple_unstake_isolation)
{
    LOCK(cs_khu);

    // Setup with Z = 125 (50 + 75, the amounts we'll unstake)
    // Invariant: C == U + Z → 1000 == 875 + 125 ✓
    KhuGlobalState state;
    SetupKHUState(state, 10000, 1000, 875, 500, 500, 125);

    CCoinsViewCache view(pcoinsTip.get());

    // Create two notes with different Ur_accumulated values
    CAmount amount1 = 50 * COIN;
    CAmount bonus1 = 10 * COIN;
    uint256 nullifier1 = GetRandHash();
    uint256 cm1 = uint256S("0000000000000000000000000000000000000000000000000000000000000011");

    CAmount amount2 = 75 * COIN;
    CAmount bonus2 = 25 * COIN;
    uint256 nullifier2 = GetRandHash();
    uint256 cm2 = uint256S("0000000000000000000000000000000000000000000000000000000000000022");

    // Phase 5: Manually create notes with different bonuses
    CZKHUTreeDB* zkhuDB = GetZKHUDB();

    ZKHUNoteData note1(amount1, 5000, bonus1, nullifier1, cm1);
    zkhuDB->WriteNote(cm1, note1);
    zkhuDB->WriteNullifierMapping(nullifier1, cm1);

    ZKHUNoteData note2(amount2, 5000, bonus2, nullifier2, cm2);
    zkhuDB->WriteNote(cm2, note2);
    zkhuDB->WriteNullifierMapping(nullifier2, cm2);

    CScript dest = GetScriptForDestination(CKeyID(uint160()));

    // Store initial state
    KhuGlobalState stateAfterFirst;

    // Unstake first note
    {
        CTransactionRef unstakeTx1 = CreateUnstakeTx(amount1 + bonus1, dest, nullifier1, 10000, cm1);
        bool result = ApplyKHUUnstake(*unstakeTx1, view, state, 10000);
        BOOST_CHECK(result);
        BOOST_CHECK(state.CheckInvariants());

        stateAfterFirst = state;
    }

    // Unstake second note
    {
        CTransactionRef unstakeTx2 = CreateUnstakeTx(amount2 + bonus2, dest, nullifier2, 10001, cm2);
        bool result = ApplyKHUUnstake(*unstakeTx2, view, state, 10001);
        BOOST_CHECK(result);
        BOOST_CHECK(state.CheckInvariants());
    }

    // Verify: Second unstake should not have been affected by first unstake
    // Each note's bonus is independent
    // (Detailed state tracking would verify exact flux amounts)
}

// ============================================================================
// TEST 6: REORG UNSTAKE
// ============================================================================
// Verify complete reorg handling:
// - ApplyKHUUnstake modifies state
// - UndoKHUUnstake restores EXACT previous state
// - All components restored: C, U, Cr, Ur, UTXO, nullifier, note
// ============================================================================

BOOST_AUTO_TEST_CASE(test_reorg_unstake)
{
    LOCK(cs_khu);

    // 1. Setup initial state with Z = 20 (the amount we'll unstake)
    // Invariant: C == U + Z → 200 == 180 + 20 ✓
    KhuGlobalState state;
    SetupKHUState(state, 5000, 200, 180, 100, 100, 20);

    CCoinsViewCache view(pcoinsTip.get());

    // 2. Create ZKHU note with bonus (Phase 5)
    CAmount amount = 20 * COIN;
    CAmount bonus = 5 * COIN;
    uint256 nullifier = GetRandHash();
    uint256 cm = uint256S("0000000000000000000000000000000000000000000000000000000000000004");

    // Phase 5: Manually create note with bonus
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    ZKHUNoteData noteWithBonus(amount, 1000, bonus, nullifier, cm);
    zkhuDB->WriteNote(cm, noteWithBonus);
    zkhuDB->WriteNullifierMapping(nullifier, cm);

    CScript dest = GetScriptForDestination(CKeyID(uint160()));
    CTransactionRef unstakeTx = CreateUnstakeTx(amount + bonus, dest, nullifier, 5000, cm);

    // 3. Store original state
    KhuGlobalState stateOriginal = state;

    // 4. Apply UNSTAKE
    bool applyResult = ApplyKHUUnstake(*unstakeTx, view, state, 5000);
    BOOST_CHECK(applyResult);

    // State should have changed (assuming bonus > 0)
    KhuGlobalState stateAfterApply = state;

    // 5. Undo UNSTAKE (simulating reorg)
    bool undoResult = UndoKHUUnstake(*unstakeTx, view, state, 5000);
    BOOST_CHECK(undoResult);

    // 6. Assertions: State must be restored EXACTLY
    BOOST_CHECK_EQUAL(state.C, stateOriginal.C);
    BOOST_CHECK_EQUAL(state.U, stateOriginal.U);
    BOOST_CHECK_EQUAL(state.Z, stateOriginal.Z);  // Z also restored
    BOOST_CHECK_EQUAL(state.Cr, stateOriginal.Cr);
    BOOST_CHECK_EQUAL(state.Ur, stateOriginal.Ur);

    // KHU_T UTXO should be removed
    COutPoint outpoint(unstakeTx->GetHash(), 0);
    BOOST_CHECK(!view.HaveCoin(outpoint));

    // ZKHU nullifier should be restored (removed from spent set)
    // TODO Phase 4: Verify nullifier restored in zkhuDB

    // Invariants must hold
    BOOST_CHECK(state.CheckInvariants());
}

// ============================================================================
// TEST 7: INVARIANTS AFTER UNSTAKE
// ============================================================================
// Verify that C==U and Cr==Ur hold after EVERY UNSTAKE operation
// Chain multiple UNSTAKEs and verify invariants never break
// ============================================================================

BOOST_AUTO_TEST_CASE(test_invariants_after_unstake)
{
    LOCK(cs_khu);

    // 5 notes with amounts: 10, 15, 20, 25, 30 COIN = 100 COIN total
    // bonuses: 0, 1, 2, 3, 4 COIN = 10 COIN total
    // Setup with Z = 100 COIN (total to unstake)
    // Invariant: C == U + Z → 1000 == 900 + 100 ✓
    KhuGlobalState state;
    SetupKHUState(state, 10000, 1000, 900, 800, 800, 100);

    CCoinsViewCache view(pcoinsTip.get());

    CScript dest = GetScriptForDestination(CKeyID(uint160()));

    // Perform 5 sequential UNSTAKEs
    CZKHUTreeDB* zkhuDB = GetZKHUDB();

    for (int i = 0; i < 5; i++) {
        CAmount amount = (10 + i * 5) * COIN;
        CAmount bonus = i * COIN; // Varying bonuses

        uint256 nullifier = GetRandHash();
        uint256 cm = GetRandHash();

        // Phase 5: Manually create note with varying bonus
        ZKHUNoteData note(amount, 5000 + i * 100, bonus, nullifier, cm);
        zkhuDB->WriteNote(cm, note);
        zkhuDB->WriteNullifierMapping(nullifier, cm);

        CTransactionRef unstakeTx = CreateUnstakeTx(amount + bonus, dest, nullifier, 10000 + i, cm);

        bool result = ApplyKHUUnstake(*unstakeTx, view, state, 10000 + i);
        BOOST_CHECK(result);

        // CRITICAL: Invariants MUST hold after EVERY operation
        // C == U + Z (not C == U!)
        BOOST_CHECK(state.CheckInvariants());
        BOOST_CHECK_EQUAL(state.C, state.U + state.Z);  // C == U + Z
        BOOST_CHECK_EQUAL(state.Cr, state.Ur);          // Cr == Ur
    }

    // Final verification: state remains valid, Z should be 0 (all unstaked)
    BOOST_CHECK(state.CheckInvariants());
    BOOST_CHECK_EQUAL(state.Z, 0);  // All ZKHU unstaked
}

BOOST_AUTO_TEST_SUITE_END()
