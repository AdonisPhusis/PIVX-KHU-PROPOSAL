// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// KHU Testnet Readiness Tests
//
// Additional test scenarios for testnet deployment:
// - Extreme values (very small, very large amounts)
// - Multiple sequential operations
// - Reorg scenarios around KHU operations
// - Fuzz-like input validation
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"
#include "khu/khu_stake.h"
#include "khu/khu_unstake.h"
#include "khu/khu_state.h"
#include "khu/khu_utxo.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "sync.h"
#include "amount.h"

#include <boost/test/unit_test.hpp>

// Test-only lock for KHU operations
static RecursiveMutex cs_khu_test;
#define cs_khu cs_khu_test

BOOST_FIXTURE_TEST_SUITE(khu_testnet_readiness_tests, BasicTestingSetup)

// Helper: Create destination script
static CScript GetTestScript()
{
    CKey key;
    key.MakeNewKey(true);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

// Helper: Create minimal MINT transaction
static CTransactionRef CreateMintTx(CAmount amount, const CScript& dest)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_MINT;

    CMintKHUPayload payload(amount, dest);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    CScript burnScript = CScript() << OP_RETURN << std::vector<unsigned char>(32, 0x01);
    mtx.vout.emplace_back(amount, burnScript);
    mtx.vout.emplace_back(amount, dest);
    mtx.vin.emplace_back(GetRandHash(), 0);

    return MakeTransactionRef(mtx);
}

// Helper: Create REDEEM transaction
static CTransactionRef CreateRedeemTx(CAmount amount, const CScript& dest, const COutPoint& khuInput)
{
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_REDEEM;

    CRedeemKHUPayload payload(amount, dest);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    mtx.vin.emplace_back(khuInput);
    mtx.vout.emplace_back(amount, dest);

    return MakeTransactionRef(mtx);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 1: EXTREME VALUES
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(extreme_small_amount_mint)
{
    // Test MINT with smallest valid amount (1 satoshi)
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    CAmount smallAmount = 1;  // 1 satoshi
    auto tx = CreateMintTx(smallAmount, dest);

    bool result = ApplyKHUMint(*tx, state, view, 200);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(state.C, smallAmount);
    BOOST_CHECK_EQUAL(state.U, smallAmount);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(extreme_large_amount_mint)
{
    // Test MINT with very large amount (near MAX_MONEY / 2)
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    // Use a large but safe amount (10 million PIV)
    CAmount largeAmount = 10000000 * COIN;
    auto tx = CreateMintTx(largeAmount, dest);

    bool result = ApplyKHUMint(*tx, state, view, 200);
    BOOST_CHECK(result);
    BOOST_CHECK_EQUAL(state.C, largeAmount);
    BOOST_CHECK_EQUAL(state.U, largeAmount);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(extreme_zero_amount_rejected)
{
    // Test that MINT with 0 amount is rejected
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();
    CValidationState valState;

    auto tx = CreateMintTx(0, dest);
    // Zero amount MINT should fail validation
    bool result = CheckKHUMint(*tx, valState, view);
    BOOST_CHECK(!result);  // Should be rejected
}

BOOST_AUTO_TEST_CASE(extreme_negative_state_rejected)
{
    // Verify that negative state values fail CheckInvariants
    KhuGlobalState state;
    state.SetNull();

    state.C = -1;
    BOOST_CHECK(!state.CheckInvariants());

    state.SetNull();
    state.U = -1;
    BOOST_CHECK(!state.CheckInvariants());

    state.SetNull();
    state.Z = -1;
    BOOST_CHECK(!state.CheckInvariants());

    state.SetNull();
    state.Cr = -1;
    BOOST_CHECK(!state.CheckInvariants());

    state.SetNull();
    state.Ur = -1;
    BOOST_CHECK(!state.CheckInvariants());

    state.SetNull();
    state.T = -1;
    BOOST_CHECK(!state.CheckInvariants());
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 2: MULTIPLE SEQUENTIAL OPERATIONS
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(sequential_multiple_mints)
{
    // Test 10 sequential MINT operations
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    CAmount totalMinted = 0;
    const int NUM_MINTS = 10;
    const CAmount perMint = 100 * COIN;

    for (int i = 0; i < NUM_MINTS; ++i) {
        auto tx = CreateMintTx(perMint, dest);
        bool result = ApplyKHUMint(*tx, state, view, 200 + i);
        BOOST_CHECK_MESSAGE(result, "MINT " << i << " failed");
        totalMinted += perMint;
        BOOST_CHECK_EQUAL(state.C, totalMinted);
        BOOST_CHECK_EQUAL(state.U, totalMinted);
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.C, NUM_MINTS * perMint);
}

BOOST_AUTO_TEST_CASE(sequential_mint_redeem_alternating)
{
    // Test alternating MINT/REDEEM operations
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    const CAmount amount = 50 * COIN;

    for (int i = 0; i < 5; ++i) {
        // MINT
        auto mintTx = CreateMintTx(amount, dest);
        bool mintResult = ApplyKHUMint(*mintTx, state, view, 200 + i * 2);
        BOOST_CHECK(mintResult);
        BOOST_CHECK_EQUAL(state.C, amount);
        BOOST_CHECK_EQUAL(state.U, amount);

        // REDEEM (using the minted UTXO)
        COutPoint khuOut(mintTx->GetHash(), 1);
        auto redeemTx = CreateRedeemTx(amount, dest, khuOut);
        bool redeemResult = ApplyKHURedeem(*redeemTx, state, view, 201 + i * 2);
        BOOST_CHECK(redeemResult);
        BOOST_CHECK_EQUAL(state.C, 0);
        BOOST_CHECK_EQUAL(state.U, 0);
        BOOST_CHECK(state.CheckInvariants());
    }
}

BOOST_AUTO_TEST_CASE(sequential_varying_amounts)
{
    // Test MINT with varying amounts
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    std::vector<CAmount> amounts = {
        1,                  // 1 satoshi
        100,                // 100 satoshis
        COIN,               // 1 PIV
        10 * COIN,          // 10 PIV
        100 * COIN,         // 100 PIV
        1000 * COIN,        // 1000 PIV
        10000 * COIN        // 10000 PIV
    };

    CAmount expectedTotal = 0;
    for (size_t i = 0; i < amounts.size(); ++i) {
        auto tx = CreateMintTx(amounts[i], dest);
        bool result = ApplyKHUMint(*tx, state, view, 200 + i);
        BOOST_CHECK(result);
        expectedTotal += amounts[i];
        BOOST_CHECK_EQUAL(state.C, expectedTotal);
        BOOST_CHECK_EQUAL(state.U, expectedTotal);
        BOOST_CHECK(state.CheckInvariants());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 3: REORG SCENARIOS
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(reorg_single_mint_undo)
{
    // Test MINT followed by UNDO (simulating reorg)
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();
    CAmount amount = 100 * COIN;

    auto tx = CreateMintTx(amount, dest);

    // Apply MINT
    bool applyResult = ApplyKHUMint(*tx, state, view, 200);
    BOOST_CHECK(applyResult);
    BOOST_CHECK_EQUAL(state.C, amount);
    BOOST_CHECK_EQUAL(state.U, amount);

    // Undo MINT (simulating reorg)
    bool undoResult = UndoKHUMint(*tx, state, view);
    BOOST_CHECK(undoResult);
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(reorg_multiple_mint_undo_order)
{
    // Test multiple MINTs then UNDO in reverse order
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    std::vector<CTransactionRef> txs;
    std::vector<CAmount> amounts = {10 * COIN, 20 * COIN, 30 * COIN};
    CAmount totalAmount = 0;

    // Apply all MINTs
    for (size_t i = 0; i < amounts.size(); ++i) {
        auto tx = CreateMintTx(amounts[i], dest);
        txs.push_back(tx);
        bool result = ApplyKHUMint(*tx, state, view, 200 + i);
        BOOST_CHECK(result);
        totalAmount += amounts[i];
    }

    BOOST_CHECK_EQUAL(state.C, totalAmount);

    // Undo in reverse order (as in real reorg)
    for (int i = amounts.size() - 1; i >= 0; --i) {
        totalAmount -= amounts[i];
        bool result = UndoKHUMint(*txs[i], state, view);
        BOOST_CHECK(result);
        BOOST_CHECK_EQUAL(state.C, totalAmount);
        BOOST_CHECK_EQUAL(state.U, totalAmount);
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
}

BOOST_AUTO_TEST_CASE(reorg_mint_redeem_undo_sequence)
{
    // Test MINT -> REDEEM -> UNDO REDEEM (state consistency)
    // Note: Full UNDO MINT after UNDO REDEEM requires undo data storage
    //       which is a Phase 2 limitation (UndoKHURedeem doesn't restore UTXO)
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();
    CAmount amount = 100 * COIN;

    // Step 1: MINT
    auto mintTx = CreateMintTx(amount, dest);
    BOOST_CHECK(ApplyKHUMint(*mintTx, state, view, 200));
    BOOST_CHECK_EQUAL(state.C, amount);

    // Step 2: REDEEM
    COutPoint khuOut(mintTx->GetHash(), 1);
    auto redeemTx = CreateRedeemTx(amount, dest, khuOut);
    BOOST_CHECK(ApplyKHURedeem(*redeemTx, state, view, 201));
    BOOST_CHECK_EQUAL(state.C, 0);

    // Step 3: UNDO REDEEM - state should be restored
    BOOST_CHECK(UndoKHURedeem(*redeemTx, state, view));
    BOOST_CHECK_EQUAL(state.C, amount);
    BOOST_CHECK_EQUAL(state.U, amount);
    BOOST_CHECK(state.CheckInvariants());

    // Note: UndoKHUMint would fail here because UndoKHURedeem doesn't
    // restore the UTXO to mapKHUUTXOs (Phase 2 limitation).
    // In production, undo data properly tracks spent UTXOs.
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 4: INPUT VALIDATION (FUZZ-LIKE)
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(fuzz_redeem_more_than_supply)
{
    // Test REDEEM attempting to withdraw more than available supply
    // The state mutation should fail because U < amount
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    state.C = 100 * COIN;
    state.U = 100 * COIN;

    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    // Try to redeem 200 PIV when only 100 exists in state
    COutPoint fakeInput(GetRandHash(), 0);
    auto tx = CreateRedeemTx(200 * COIN, dest, fakeInput);

    // ApplyKHURedeem should fail because U (100) < amount (200)
    bool result = ApplyKHURedeem(*tx, state, view, 200);
    BOOST_CHECK(!result);  // Should fail due to insufficient U
}

BOOST_AUTO_TEST_CASE(fuzz_state_invariant_c_ne_u_plus_z)
{
    // Verify C != U + Z is detected
    KhuGlobalState state;
    state.SetNull();
    state.C = 100;
    state.U = 50;
    state.Z = 40;  // C (100) != U + Z (90)

    BOOST_CHECK(!state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(fuzz_state_invariant_cr_ne_ur)
{
    // Verify Cr != Ur is detected (when both non-zero)
    KhuGlobalState state;
    state.SetNull();
    state.Cr = 100;
    state.Ur = 50;

    BOOST_CHECK(!state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(fuzz_overflow_protection)
{
    // Verify overflow doesn't cause invariant to pass
    KhuGlobalState state;
    state.SetNull();

    // Set C to near max value
    state.C = INT64_MAX - 100;
    state.U = INT64_MAX - 100;

    // Attempting to add more should be caught
    // (This tests the boundary, actual overflow protection is in Apply functions)
    BOOST_CHECK(state.CheckInvariants());  // Valid state

    // But if we corrupt it:
    state.C = INT64_MAX;
    state.U = 100;  // Now C != U
    BOOST_CHECK(!state.CheckInvariants());
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 5: STATE CONSISTENCY
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(state_hash_changes_with_mutations)
{
    // Verify that state hash changes after mutations
    KhuGlobalState state1, state2;
    state1.SetNull();
    state2.SetNull();

    uint256 hash1 = state1.GetHash();
    uint256 hash2 = state2.GetHash();
    BOOST_CHECK_EQUAL(hash1, hash2);  // Same initial state

    // Mutate state2
    state2.C = 100;
    state2.U = 100;
    uint256 hash3 = state2.GetHash();
    BOOST_CHECK_NE(hash1, hash3);  // Different after mutation
}

BOOST_AUTO_TEST_CASE(state_serialization_roundtrip)
{
    // Test state serialization/deserialization
    KhuGlobalState original;
    original.SetNull();
    original.C = 1234567890;
    original.U = 1234567890;
    original.Z = 0;
    original.Cr = 123456;
    original.Ur = 123456;
    original.T = 98765;
    original.R_annual = 3700;
    original.R_MAX_dynamic = 3700;
    original.nHeight = 12345;

    // Serialize
    CDataStream ss(SER_DISK, PROTOCOL_VERSION);
    ss << original;

    // Deserialize
    KhuGlobalState loaded;
    ss >> loaded;

    // Verify
    BOOST_CHECK_EQUAL(original.C, loaded.C);
    BOOST_CHECK_EQUAL(original.U, loaded.U);
    BOOST_CHECK_EQUAL(original.Z, loaded.Z);
    BOOST_CHECK_EQUAL(original.Cr, loaded.Cr);
    BOOST_CHECK_EQUAL(original.Ur, loaded.Ur);
    BOOST_CHECK_EQUAL(original.T, loaded.T);
    BOOST_CHECK_EQUAL(original.R_annual, loaded.R_annual);
    BOOST_CHECK_EQUAL(original.R_MAX_dynamic, loaded.R_MAX_dynamic);
    BOOST_CHECK_EQUAL(original.nHeight, loaded.nHeight);
    BOOST_CHECK_EQUAL(original.GetHash(), loaded.GetHash());
}

BOOST_AUTO_TEST_SUITE_END()
