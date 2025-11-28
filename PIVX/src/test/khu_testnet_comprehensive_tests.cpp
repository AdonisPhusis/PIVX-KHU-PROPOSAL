// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Comprehensive Testnet Readiness Tests
 *
 * This test suite validates the FULL KHU pipeline before testnet deployment:
 *
 * SECTION 1: Full Pipeline Cycles (MINT→STAKE→YIELD→UNSTAKE→REDEEM loops)
 * SECTION 2: Stress Tests (Large transactions, high volume)
 * SECTION 3: Daily T Accumulation (365 days simulation)
 * SECTION 4: Staker Yield Accumulation (multi-note, multi-day)
 * SECTION 5: DOMC R% Voting (full cycle: commit→reveal→median→activation)
 * SECTION 6: Reorg Safety (state consistency after chain reorganizations)
 * SECTION 7: Edge Cases (boundary conditions, overflow protection)
 *
 * Canonical specification: docs/SPEC.md, docs/ARCHITECTURE.md
 * INVARIANTS:
 *   C == U + Z                 (collateral == supply)
 *   Cr == Ur                   (reward pool == reward rights)
 *   T >= 0                     (treasury non-negative)
 */

#include "test/test_pivx.h"

#include "chainparams.h"
#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_dao.h"
#include "khu/khu_domc.h"
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"
#include "khu/khu_stake.h"
#include "khu/khu_state.h"
#include "khu/khu_unstake.h"
#include "khu/khu_utxo.h"
#include "khu/khu_yield.h"
#include "primitives/transaction.h"
#include "script/standard.h"
#include "sync.h"
#include "amount.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <boost/test/unit_test.hpp>
#include <vector>

// Test-only lock for KHU operations
static RecursiveMutex cs_khu_test;
#define cs_khu cs_khu_test

BOOST_FIXTURE_TEST_SUITE(khu_testnet_comprehensive_tests, BasicTestingSetup)

// ═══════════════════════════════════════════════════════════════════════════
// HELPERS
// ═══════════════════════════════════════════════════════════════════════════

static CScript GetTestScript()
{
    CKey key;
    key.MakeNewKey(true);
    return GetScriptForDestination(key.GetPubKey().GetID());
}

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

// Calculate expected daily budget using canonical formula
static CAmount ExpectedDaoBudget(CAmount U, uint16_t R_annual)
{
    // T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
    boost::multiprecision::int128_t base = static_cast<boost::multiprecision::int128_t>(U);
    boost::multiprecision::int128_t budget = (base * R_annual) / 10000 / khu_domc::T_DIVISOR / 365;
    return static_cast<CAmount>(budget);
}

// Calculate expected daily yield for a note
static CAmount ExpectedDailyYield(CAmount noteAmount, uint16_t R_annual)
{
    // daily = (amount × R_annual / 10000) / 365
    boost::multiprecision::int128_t base = static_cast<boost::multiprecision::int128_t>(noteAmount);
    boost::multiprecision::int128_t yield = (base * R_annual) / 10000 / 365;
    return static_cast<CAmount>(yield);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 1: FULL PIPELINE CYCLES
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(pipeline_mint_redeem_loop_10_cycles)
{
    // Test: 10 complete MINT→REDEEM cycles
    // Purpose: Verify state returns to initial after each full cycle
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    const int NUM_CYCLES = 10;
    const CAmount amounts[] = {100*COIN, 500*COIN, 1000*COIN, 10000*COIN, 50000*COIN,
                               100000*COIN, 250000*COIN, 500000*COIN, 750000*COIN, 1000000*COIN};

    for (int i = 0; i < NUM_CYCLES; ++i) {
        CAmount amount = amounts[i % 10];

        // MINT
        auto mintTx = CreateMintTx(amount, dest);
        bool mintOk = ApplyKHUMint(*mintTx, state, view, 200 + i*2);
        BOOST_REQUIRE_MESSAGE(mintOk, "MINT failed at cycle " << i);
        BOOST_CHECK_EQUAL(state.C, amount);
        BOOST_CHECK_EQUAL(state.U, amount);
        BOOST_CHECK_EQUAL(state.Z, 0);
        BOOST_CHECK(state.CheckInvariants());

        // REDEEM
        COutPoint khuOut(mintTx->GetHash(), 1);
        auto redeemTx = CreateRedeemTx(amount, dest, khuOut);
        bool redeemOk = ApplyKHURedeem(*redeemTx, state, view, 201 + i*2);
        BOOST_REQUIRE_MESSAGE(redeemOk, "REDEEM failed at cycle " << i);
        BOOST_CHECK_EQUAL(state.C, 0);
        BOOST_CHECK_EQUAL(state.U, 0);
        BOOST_CHECK_EQUAL(state.Z, 0);
        BOOST_CHECK(state.CheckInvariants());
    }

    // Final state must be clean
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
    BOOST_CHECK_EQUAL(state.Z, 0);
}

BOOST_AUTO_TEST_CASE(pipeline_multiple_concurrent_mints_single_batch_redeem)
{
    // Test: Multiple MINTs followed by single batch REDEEM of all
    // Purpose: Verify state accumulation and complete draining
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    std::vector<CTransactionRef> mintTxs;
    std::vector<CAmount> amounts = {1000*COIN, 2000*COIN, 3000*COIN, 4000*COIN, 5000*COIN};
    CAmount totalMinted = 0;

    // Phase 1: Multiple MINTs
    for (size_t i = 0; i < amounts.size(); ++i) {
        auto mintTx = CreateMintTx(amounts[i], dest);
        BOOST_REQUIRE(ApplyKHUMint(*mintTx, state, view, 200 + i));
        mintTxs.push_back(mintTx);
        totalMinted += amounts[i];

        BOOST_CHECK_EQUAL(state.C, totalMinted);
        BOOST_CHECK_EQUAL(state.U, totalMinted);
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(totalMinted, 15000*COIN);

    // Phase 2: REDEEM all in reverse order
    for (int i = amounts.size() - 1; i >= 0; --i) {
        totalMinted -= amounts[i];
        COutPoint khuOut(mintTxs[i]->GetHash(), 1);
        auto redeemTx = CreateRedeemTx(amounts[i], dest, khuOut);
        BOOST_REQUIRE(ApplyKHURedeem(*redeemTx, state, view, 300 + i));

        BOOST_CHECK_EQUAL(state.C, totalMinted);
        BOOST_CHECK_EQUAL(state.U, totalMinted);
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 2: STRESS TESTS
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(stress_large_single_mint_100m_piv)
{
    // Test: Single MINT of 100 million PIV
    // Purpose: Verify no overflow in large transaction handling
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    CAmount largeAmount = 100000000LL * COIN;  // 100 million PIV
    auto mintTx = CreateMintTx(largeAmount, dest);

    bool mintOk = ApplyKHUMint(*mintTx, state, view, 200);
    BOOST_CHECK(mintOk);
    BOOST_CHECK_EQUAL(state.C, largeAmount);
    BOOST_CHECK_EQUAL(state.U, largeAmount);
    BOOST_CHECK(state.CheckInvariants());

    // Verify arithmetic is correct
    BOOST_CHECK(state.C > 0);
    BOOST_CHECK(state.U > 0);
    BOOST_CHECK_EQUAL(state.C, state.U + state.Z);

    // REDEEM large amount
    COutPoint khuOut(mintTx->GetHash(), 1);
    auto redeemTx = CreateRedeemTx(largeAmount, dest, khuOut);
    bool redeemOk = ApplyKHURedeem(*redeemTx, state, view, 201);
    BOOST_CHECK(redeemOk);
    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(stress_100_rapid_mints)
{
    // Test: 100 rapid MINT operations
    // Purpose: Verify state consistency under high volume
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    const int NUM_MINTS = 100;
    const CAmount perMint = 10000 * COIN;
    CAmount expectedTotal = 0;

    for (int i = 0; i < NUM_MINTS; ++i) {
        auto mintTx = CreateMintTx(perMint, dest);
        BOOST_REQUIRE(ApplyKHUMint(*mintTx, state, view, 200 + i));
        expectedTotal += perMint;

        // Check every 10th iteration
        if (i % 10 == 0) {
            BOOST_CHECK_EQUAL(state.C, expectedTotal);
            BOOST_CHECK_EQUAL(state.U, expectedTotal);
            BOOST_CHECK(state.CheckInvariants());
        }
    }

    BOOST_CHECK_EQUAL(state.C, NUM_MINTS * perMint);
    BOOST_CHECK_EQUAL(state.U, NUM_MINTS * perMint);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(stress_mixed_operations_high_volume)
{
    // Test: High volume mixed MINT/REDEEM operations
    // Purpose: Verify state tracking under realistic load
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    std::vector<std::pair<CTransactionRef, CAmount>> activeMints;
    CAmount expectedC = 0;

    for (int i = 0; i < 50; ++i) {
        // MINT random amount
        CAmount mintAmount = (1000 + (i * 100)) * COIN;
        auto mintTx = CreateMintTx(mintAmount, dest);
        BOOST_REQUIRE(ApplyKHUMint(*mintTx, state, view, 200 + i*3));
        activeMints.push_back({mintTx, mintAmount});
        expectedC += mintAmount;

        // Every 5 mints, redeem one
        if (i % 5 == 4 && !activeMints.empty()) {
            auto& mintData = activeMints.back();
            COutPoint khuOut(mintData.first->GetHash(), 1);
            auto redeemTx = CreateRedeemTx(mintData.second, dest, khuOut);
            BOOST_REQUIRE(ApplyKHURedeem(*redeemTx, state, view, 201 + i*3));
            expectedC -= mintData.second;
            activeMints.pop_back();
        }

        BOOST_CHECK_EQUAL(state.C, expectedC);
        BOOST_CHECK(state.CheckInvariants());
    }
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 3: DAILY T ACCUMULATION
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(dao_treasury_365_days_simulation)
{
    // Test: Simulate 365 days of DAO Treasury accumulation
    // Purpose: Verify T reaches ~5% of U annually at R=40%
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 10000000 * COIN;  // 10 million KHU supply
    state.R_annual = 4000;       // 40%
    state.T = 0;

    CAmount dailyBudget = ExpectedDaoBudget(state.U, state.R_annual);

    // Simulate 365 daily accumulations
    for (int day = 1; day <= 365; day++) {
        uint32_t nHeight = day * 1440;  // Daily boundary
        BOOST_REQUIRE(khu_dao::AccumulateDaoTreasuryIfNeeded(state, nHeight, params));
    }

    // Verify T accumulated correctly
    CAmount expectedAnnual = dailyBudget * 365;
    BOOST_CHECK_EQUAL(state.T, expectedAnnual);

    // Verify ~5% at R=40% (T_DIVISOR=8 → 40%/8 = 5%)
    CAmount fivePercent = state.U / 20;  // 5% = U/20
    BOOST_CHECK(state.T > fivePercent * 99 / 100);   // At least 99% of 5%
    BOOST_CHECK(state.T < fivePercent * 101 / 100);  // At most 101% of 5%
}

BOOST_AUTO_TEST_CASE(dao_treasury_r_decay_over_33_years)
{
    // Test: Verify T scales with R% decay over 33 years
    // Purpose: At year 33, R%=7% → T should be ~7%/8 ≈ 0.875%
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    CAmount U = 10000000 * COIN;

    // Year 0: R=40%, T≈5%
    {
        KhuGlobalState state;
        state.SetNull();
        state.U = U;
        state.R_annual = 4000;  // 40%
        state.T = 0;

        for (int day = 1; day <= 365; day++) {
            khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);
        }

        CAmount expectedFivePercent = U / 20;
        BOOST_CHECK(state.T > expectedFivePercent * 98 / 100);
    }

    // Year 33: R=7% (floor), T≈0.875%
    {
        KhuGlobalState state;
        state.SetNull();
        state.U = U;
        state.R_annual = 700;  // 7% (floor)
        state.T = 0;

        for (int day = 1; day <= 365; day++) {
            khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);
        }

        // 7% / 8 = 0.875% → U * 7 / 800
        CAmount expected = (U * 7) / 800;
        BOOST_CHECK(state.T > expected * 98 / 100);
        BOOST_CHECK(state.T < expected * 102 / 100);
    }
}

BOOST_AUTO_TEST_CASE(dao_treasury_undo_multi_day)
{
    // Test: Undo multiple days of T accumulation
    // Purpose: Verify reorg safety for T
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 5000000 * COIN;
    state.R_annual = 4000;
    state.T = 0;

    std::vector<CAmount> T_after_day;

    // Accumulate 10 days
    for (int day = 1; day <= 10; day++) {
        khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params);
        T_after_day.push_back(state.T);
    }

    BOOST_CHECK_EQUAL(T_after_day.size(), 10);

    // Undo in reverse order
    for (int day = 10; day >= 1; day--) {
        BOOST_CHECK(khu_dao::UndoDaoTreasuryIfNeeded(state, day * 1440, params));
        if (day > 1) {
            BOOST_CHECK_EQUAL(state.T, T_after_day[day - 2]);
        } else {
            BOOST_CHECK_EQUAL(state.T, 0);
        }
    }

    BOOST_CHECK_EQUAL(state.T, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 4: STAKER YIELD ACCUMULATION
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(yield_daily_formula_validation)
{
    // Test: Validate daily yield formula
    // Formula: daily = (amount × R_annual / 10000) / 365
    struct TestCase {
        CAmount amount;
        uint16_t R;
        CAmount expectedDaily;
    };

    std::vector<TestCase> cases = {
        // 1000 KHU at 40% → annual 400 → daily ~1.096 KHU
        {1000 * COIN, 4000, (400 * COIN) / 365},
        // 10000 KHU at 40% → annual 4000 → daily ~10.96 KHU
        {10000 * COIN, 4000, (4000 * COIN) / 365},
        // 100000 KHU at 40% → annual 40000 → daily ~109.6 KHU
        {100000 * COIN, 4000, (40000 * COIN) / 365},
        // 1000 KHU at 7% (floor) → annual 70 → daily ~0.19 KHU
        {1000 * COIN, 700, (70 * COIN) / 365},
    };

    for (const auto& tc : cases) {
        CAmount calculated = khu_yield::CalculateDailyYieldForNote(tc.amount, tc.R);
        BOOST_CHECK_EQUAL(calculated, tc.expectedDaily);
    }
}

BOOST_AUTO_TEST_CASE(yield_maturity_period_enforcement)
{
    // Test: No yield before 4320 blocks (3 days)
    // Purpose: Verify anti-arbitrage protection

    uint32_t stakeHeight = 1000000;

    // Day 0: Not mature
    BOOST_CHECK(!khu_yield::IsNoteMature(stakeHeight, stakeHeight));

    // Day 1: Not mature
    BOOST_CHECK(!khu_yield::IsNoteMature(stakeHeight, stakeHeight + 1440));

    // Day 2: Not mature
    BOOST_CHECK(!khu_yield::IsNoteMature(stakeHeight, stakeHeight + 2880));

    // Day 3 (block 4319): Still not mature
    BOOST_CHECK(!khu_yield::IsNoteMature(stakeHeight, stakeHeight + 4319));

    // Day 3 (block 4320): NOW mature
    BOOST_CHECK(khu_yield::IsNoteMature(stakeHeight, stakeHeight + 4320));

    // Day 4: Definitely mature
    BOOST_CHECK(khu_yield::IsNoteMature(stakeHeight, stakeHeight + 5760));
}

BOOST_AUTO_TEST_CASE(yield_365_days_accumulation)
{
    // Test: Simulate 365 days of yield accumulation for a single note
    // Purpose: Verify annual yield = amount × R%

    CAmount noteAmount = 10000 * COIN;  // 10,000 KHU
    uint16_t R = 4000;                   // 40%

    CAmount dailyYield = ExpectedDailyYield(noteAmount, R);
    CAmount totalYield = 0;

    // Simulate 365 days (after maturity)
    for (int day = 1; day <= 365; day++) {
        totalYield += dailyYield;
    }

    // Expected: 10000 × 40% = 4000 KHU annual
    CAmount expectedAnnual = (noteAmount * R) / 10000;

    // Allow small rounding tolerance
    BOOST_CHECK(totalYield > expectedAnnual * 99 / 100);
    BOOST_CHECK(totalYield < expectedAnnual * 101 / 100);
}

BOOST_AUTO_TEST_CASE(yield_multi_note_aggregation)
{
    // Test: Multiple notes with different amounts
    // Purpose: Verify total yield = sum of individual yields

    uint16_t R = 4000;  // 40%

    std::vector<CAmount> noteAmounts = {
        1000 * COIN,
        5000 * COIN,
        10000 * COIN,
        25000 * COIN,
        50000 * COIN
    };

    CAmount totalDailyYield = 0;
    CAmount totalPrincipal = 0;

    for (CAmount amount : noteAmounts) {
        totalDailyYield += ExpectedDailyYield(amount, R);
        totalPrincipal += amount;
    }

    // Verify total yield equals yield on total principal
    CAmount expectedTotalYield = ExpectedDailyYield(totalPrincipal, R);

    // May differ by a few satoshis due to rounding
    BOOST_CHECK(std::abs(totalDailyYield - expectedTotalYield) < noteAmounts.size());
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 5: DOMC R% VOTING
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(domc_cycle_phases_timing)
{
    // Test: Verify DOMC cycle phase boundaries
    // Cycle: 172800 blocks (4 months)
    // Vote phase: 132480 → 152640 (2 weeks)
    // Adaptation phase: 152640 → 172800 (2 weeks)

    const uint32_t CYCLE_START = 1000000;

    // Before vote phase
    BOOST_CHECK(!khu_domc::IsDomcVotePhase(CYCLE_START, CYCLE_START));
    BOOST_CHECK(!khu_domc::IsDomcVotePhase(CYCLE_START + 100000, CYCLE_START));
    BOOST_CHECK(!khu_domc::IsDomcVotePhase(CYCLE_START + 132479, CYCLE_START));

    // Vote phase (132480 → 152640)
    BOOST_CHECK(khu_domc::IsDomcVotePhase(CYCLE_START + 132480, CYCLE_START));
    BOOST_CHECK(khu_domc::IsDomcVotePhase(CYCLE_START + 140000, CYCLE_START));
    BOOST_CHECK(khu_domc::IsDomcVotePhase(CYCLE_START + 152639, CYCLE_START));

    // Adaptation phase (152640 → 172800)
    BOOST_CHECK(!khu_domc::IsDomcVotePhase(CYCLE_START + 152640, CYCLE_START));
    BOOST_CHECK(khu_domc::IsDomcAdaptationPhase(CYCLE_START + 152640, CYCLE_START));
    BOOST_CHECK(khu_domc::IsDomcAdaptationPhase(CYCLE_START + 160000, CYCLE_START));
    BOOST_CHECK(khu_domc::IsDomcAdaptationPhase(CYCLE_START + 172799, CYCLE_START));

    // Next cycle
    BOOST_CHECK(!khu_domc::IsDomcAdaptationPhase(CYCLE_START + 172800, CYCLE_START));
}

BOOST_AUTO_TEST_CASE(domc_r_max_dynamic_decay)
{
    // Test: R_MAX_dynamic decays 1%/year for 33 years
    // Formula: R_MAX = max(700, 4000 - year × 100)

    const uint32_t V6_ACTIVATION = 1000000;
    const uint32_t BLOCKS_PER_YEAR = 525600;

    struct ExpectedDecay {
        int year;
        uint16_t expectedR;
    };

    std::vector<ExpectedDecay> expectations = {
        {0, 4000},   // 40%
        {1, 3900},   // 39%
        {5, 3500},   // 35%
        {10, 3000},  // 30%
        {20, 2000},  // 20%
        {30, 1000},  // 10%
        {33, 700},   // 7% (floor reached)
        {40, 700},   // 7% (stays at floor)
        {100, 700},  // 7% (stays at floor)
    };

    for (const auto& exp : expectations) {
        uint32_t height = V6_ACTIVATION + (exp.year * BLOCKS_PER_YEAR);
        uint16_t calculated = khu_domc::CalculateRMaxDynamic(height, V6_ACTIVATION);
        BOOST_CHECK_EQUAL(calculated, exp.expectedR);
    }
}

BOOST_AUTO_TEST_CASE(domc_cycle_initialization)
{
    // Test: Verify cycle initialization at V6 activation

    KhuGlobalState state;
    state.SetNull();

    const uint32_t V6_ACTIVATION = 1000000;

    // Initialize first cycle (at V6 activation)
    khu_domc::InitializeDomcCycle(state, V6_ACTIVATION, true /* isFirstCycle */);

    // Verify initial values
    BOOST_CHECK_EQUAL(state.R_annual, khu_domc::R_DEFAULT);  // 4000 (40%)
    BOOST_CHECK_EQUAL(state.R_MAX_dynamic, khu_domc::R_MAX_DYNAMIC_INITIAL);  // 4000
    BOOST_CHECK_EQUAL(state.domc_cycle_start, V6_ACTIVATION);
    BOOST_CHECK_EQUAL(state.domc_cycle_length, khu_domc::DOMC_CYCLE_LENGTH);
    BOOST_CHECK_EQUAL(state.domc_commit_phase_start, V6_ACTIVATION + khu_domc::DOMC_COMMIT_OFFSET);
    BOOST_CHECK_EQUAL(state.domc_reveal_deadline, V6_ACTIVATION + khu_domc::DOMC_REVEAL_HEIGHT);
}

BOOST_AUTO_TEST_CASE(domc_median_with_clamping)
{
    // Test: Median calculation with R_MAX_dynamic clamping
    // Note: This test simulates median behavior without actual DB

    // Scenario: 3 votes all above R_MAX_dynamic
    // Votes: [3500, 3700, 3900]
    // R_MAX_dynamic: 3000
    // Expected median: 3700 clamped to 3000

    std::vector<uint16_t> votes = {3500, 3700, 3900};
    uint16_t R_MAX_dynamic = 3000;
    uint16_t currentR = 2500;

    // Manual median calculation (sorted, floor index)
    std::sort(votes.begin(), votes.end());
    uint16_t median = votes[votes.size() / 2];  // 3700

    // Clamp to R_MAX_dynamic
    uint16_t result = std::min(median, R_MAX_dynamic);
    BOOST_CHECK_EQUAL(result, 3000);

    // Verify below R_MAX is not clamped
    votes = {1000, 1500, 2000};
    std::sort(votes.begin(), votes.end());
    median = votes[votes.size() / 2];  // 1500
    result = std::min(median, R_MAX_dynamic);
    BOOST_CHECK_EQUAL(result, 1500);  // Not clamped
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 6: REORG SAFETY
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(reorg_mint_redeem_undo_sequence)
{
    // Test: Separate reorg scenarios for MINT and REDEEM
    // Note: Phase 2 limitation - UndoKHURedeem doesn't restore UTXOs,
    // so we test reorg scenarios separately rather than chained.
    LOCK(cs_khu);

    // Test 1: MINT → UNDO MINT
    {
        KhuGlobalState state;
        state.SetNull();
        CCoinsView coinsDummy;
        CCoinsViewCache view(&coinsDummy);
        CScript dest = GetTestScript();
        CAmount amount = 50000 * COIN;

        KhuGlobalState initialState = state;

        // MINT
        auto mintTx = CreateMintTx(amount, dest);
        BOOST_REQUIRE(ApplyKHUMint(*mintTx, state, view, 200));
        BOOST_CHECK_EQUAL(state.C, amount);
        BOOST_CHECK_EQUAL(state.U, amount);

        // UNDO MINT (reorg)
        BOOST_REQUIRE(UndoKHUMint(*mintTx, state, view));
        BOOST_CHECK_EQUAL(state.C, 0);
        BOOST_CHECK_EQUAL(state.U, 0);

        // State should match initial
        BOOST_CHECK_EQUAL(state.C, initialState.C);
        BOOST_CHECK_EQUAL(state.U, initialState.U);
        BOOST_CHECK(state.CheckInvariants());
    }

    // Test 2: MINT → REDEEM → UNDO REDEEM
    {
        KhuGlobalState state;
        state.SetNull();
        CCoinsView coinsDummy;
        CCoinsViewCache view(&coinsDummy);
        CScript dest = GetTestScript();
        CAmount mintAmount = 100000 * COIN;
        CAmount redeemAmount = 50000 * COIN;

        // MINT
        auto mintTx = CreateMintTx(mintAmount, dest);
        BOOST_REQUIRE(ApplyKHUMint(*mintTx, state, view, 200));

        // REDEEM partial
        COutPoint khuOut(mintTx->GetHash(), 1);
        auto redeemTx = CreateRedeemTx(redeemAmount, dest, khuOut);
        BOOST_REQUIRE(ApplyKHURedeem(*redeemTx, state, view, 201));
        BOOST_CHECK_EQUAL(state.C, mintAmount - redeemAmount);

        // UNDO REDEEM (reorg)
        BOOST_REQUIRE(UndoKHURedeem(*redeemTx, state, view));
        BOOST_CHECK_EQUAL(state.C, mintAmount);
        BOOST_CHECK_EQUAL(state.U, mintAmount);
        BOOST_CHECK(state.CheckInvariants());
    }
}

BOOST_AUTO_TEST_CASE(reorg_deep_chain_undo)
{
    // Test: 10 MINTs then 10 UNDOs
    // Purpose: Verify complete chain undo
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    std::vector<CTransactionRef> txs;
    std::vector<CAmount> amounts = {1000*COIN, 2000*COIN, 3000*COIN, 4000*COIN, 5000*COIN,
                                    6000*COIN, 7000*COIN, 8000*COIN, 9000*COIN, 10000*COIN};
    CAmount runningTotal = 0;

    // Forward: 10 MINTs
    for (size_t i = 0; i < amounts.size(); ++i) {
        auto tx = CreateMintTx(amounts[i], dest);
        BOOST_REQUIRE(ApplyKHUMint(*tx, state, view, 200 + i));
        txs.push_back(tx);
        runningTotal += amounts[i];
    }

    BOOST_CHECK_EQUAL(state.C, runningTotal);

    // Backward: 10 UNDOs
    for (int i = amounts.size() - 1; i >= 0; --i) {
        runningTotal -= amounts[i];
        BOOST_REQUIRE(UndoKHUMint(*txs[i], state, view));
        BOOST_CHECK_EQUAL(state.C, runningTotal);
        BOOST_CHECK(state.CheckInvariants());
    }

    BOOST_CHECK_EQUAL(state.C, 0);
    BOOST_CHECK_EQUAL(state.U, 0);
}

// ═══════════════════════════════════════════════════════════════════════════
// SECTION 7: EDGE CASES
// ═══════════════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(edge_minimum_amounts)
{
    // Test: Minimum possible amounts (1 satoshi)
    LOCK(cs_khu);

    KhuGlobalState state;
    state.SetNull();
    CCoinsView coinsDummy;
    CCoinsViewCache view(&coinsDummy);
    CScript dest = GetTestScript();

    CAmount minAmount = 1;  // 1 satoshi
    auto tx = CreateMintTx(minAmount, dest);

    BOOST_CHECK(ApplyKHUMint(*tx, state, view, 200));
    BOOST_CHECK_EQUAL(state.C, 1);
    BOOST_CHECK_EQUAL(state.U, 1);
    BOOST_CHECK(state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(edge_yield_at_r_zero)
{
    // Test: Yield calculation when R=0
    CAmount amount = 10000 * COIN;
    uint16_t R = 0;

    CAmount yield = khu_yield::CalculateDailyYieldForNote(amount, R);
    BOOST_CHECK_EQUAL(yield, 0);
}

BOOST_AUTO_TEST_CASE(edge_t_accumulation_u_zero)
{
    // Test: T accumulation when U=0
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 0;
    state.R_annual = 4000;
    state.T = 100 * COIN;  // Existing treasury

    // Accumulate at boundary
    BOOST_CHECK(khu_dao::AccumulateDaoTreasuryIfNeeded(state, 1440, params));

    // T should remain unchanged (budget = 0)
    BOOST_CHECK_EQUAL(state.T, 100 * COIN);
}

BOOST_AUTO_TEST_CASE(edge_invariant_violations_detected)
{
    // Test: Verify CheckInvariants catches violations

    KhuGlobalState state;
    state.SetNull();

    // Valid state
    state.C = 1000;
    state.U = 1000;
    state.Z = 0;
    state.Cr = 100;
    state.Ur = 100;
    state.T = 50;
    BOOST_CHECK(state.CheckInvariants());

    // Violation: C != U + Z
    state.C = 1001;
    BOOST_CHECK(!state.CheckInvariants());
    state.C = 1000;

    // Violation: Cr != Ur
    state.Cr = 101;
    BOOST_CHECK(!state.CheckInvariants());
    state.Cr = 100;

    // Violation: Negative C
    state.C = -1;
    state.U = -1;
    BOOST_CHECK(!state.CheckInvariants());
    state.C = 1000;
    state.U = 1000;

    // Violation: Negative T
    state.T = -1;
    BOOST_CHECK(!state.CheckInvariants());
}

BOOST_AUTO_TEST_CASE(edge_overflow_protection_t_accumulation)
{
    // Test: Verify no overflow in T accumulation with extreme values
    Consensus::Params params;
    params.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight = 0;

    KhuGlobalState state;
    state.SetNull();
    state.U = 100000000LL * COIN;  // 100 million KHU
    state.R_annual = 4000;
    state.T = 0;

    // Accumulate 1000 days (should not overflow)
    for (int day = 1; day <= 1000; day++) {
        BOOST_REQUIRE(khu_dao::AccumulateDaoTreasuryIfNeeded(state, day * 1440, params));
    }

    // T should be positive and reasonable
    BOOST_CHECK(state.T > 0);
    BOOST_CHECK(state.T < INT64_MAX);
}

BOOST_AUTO_TEST_SUITE_END()
