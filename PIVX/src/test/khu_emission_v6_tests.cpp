// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU V6.0: ZERO BLOCK REWARD (immediate)
//
// Post-V6, block reward = 0 PIV. Economy governed by R% (yield) and T (treasury).
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

/**
 * Custom test fixture for V6 emission tests.
 */
struct V6EmissionTestingSetup : public TestingSetup
{
    int V6_DefaultActivation;

    V6EmissionTestingSetup()
    {
        SelectParams(CBaseChainParams::REGTEST);
        V6_DefaultActivation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }

    ~V6EmissionTestingSetup()
    {
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, V6_DefaultActivation);
        SelectParams(CBaseChainParams::MAIN);
    }
};

BOOST_FIXTURE_TEST_SUITE(khu_emission_v6_tests, V6EmissionTestingSetup)

/**
 * Test 1: Pre-activation (legacy PIVX emission)
 *
 * Before V6.0 activation, the network uses legacy PIVX emission:
 * - GetBlockValue() returns 10 PIV (fixed after V5.5)
 * - GetMasternodePayment() returns 6 PIV
 */
BOOST_AUTO_TEST_CASE(test_emission_pre_activation)
{
    const int testHeight = 5000000;
    const Consensus::Params& consensus = Params().GetConsensus();

    BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    BOOST_CHECK_EQUAL(blockValue, 10 * COIN);
    BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
}

/**
 * Test 2: Post-activation - ZERO IMMEDIATE
 *
 * At V6 activation, block reward becomes 0 PIV IMMEDIATELY.
 * No progressive schedule. No transition period.
 */
BOOST_AUTO_TEST_CASE(test_emission_v6_zero_immediate)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // At activation height: ZERO
    {
        const int testHeight = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    // After activation: still ZERO
    {
        const int testHeight = activationHeight + 100000;
        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 3: Year 1+ - still ZERO
 *
 * After 1 year, block reward remains 0 PIV.
 * Economy is governed by R% (yield) not block rewards.
 */
BOOST_AUTO_TEST_CASE(test_emission_year1_still_zero)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const int testHeight = activationHeight + Consensus::Params::BLOCKS_PER_YEAR;

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    BOOST_CHECK_EQUAL(blockValue, 0);
    BOOST_CHECK_EQUAL(mnPayment, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 4: Year 10+ - still ZERO
 *
 * After 10 years, block reward is still 0 PIV.
 */
BOOST_AUTO_TEST_CASE(test_emission_year10_still_zero)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const int testHeight = activationHeight + (10 * Consensus::Params::BLOCKS_PER_YEAR);

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    BOOST_CHECK_EQUAL(blockValue, 0);
    BOOST_CHECK_EQUAL(mnPayment, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 5: Year 100+ - perpetually ZERO
 *
 * After 100 years, block reward is still 0 PIV forever.
 */
BOOST_AUTO_TEST_CASE(test_emission_year100_perpetual_zero)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const int testHeight = activationHeight + (100 * Consensus::Params::BLOCKS_PER_YEAR);

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    BOOST_CHECK_EQUAL(blockValue, 0);
    BOOST_CHECK_EQUAL(mnPayment, 0);

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 6: Transition boundary - exact moment
 *
 * Test the exact moment of V6.0 activation:
 * - Block X-1: Legacy (10 PIV)
 * - Block X:   V6 (0 PIV) <- IMMEDIATE
 */
BOOST_AUTO_TEST_CASE(test_emission_transition_boundary)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Block BEFORE activation: legacy
    {
        const int testHeight = activationHeight - 1;
        BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 10 * COIN);  // Legacy
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);    // Legacy
    }

    // Block AT activation: V6 ZERO
    {
        const int testHeight = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 0);  // V6: ZERO
        BOOST_CHECK_EQUAL(mnPayment, 0);   // V6: ZERO
    }

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 7: Emission never negative
 *
 * Block reward is always >= 0 (never negative).
 */
BOOST_AUTO_TEST_CASE(test_emission_never_negative)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const int testYears[] = {0, 1, 5, 10, 50, 100, 1000};

    for (int year : testYears) {
        const int testHeight = activationHeight + (year * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Should be exactly 0, never negative
        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
        BOOST_CHECK(blockValue >= 0);
        BOOST_CHECK(mnPayment >= 0);
    }

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 8: Full schedule - all zeros post-V6
 *
 * Comprehensive test: all years post-V6 have zero block reward.
 */
BOOST_AUTO_TEST_CASE(test_emission_full_schedule_all_zeros)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    // Test years 0 through 33 (R% goes from 40% to 7%)
    for (int year = 0; year <= 33; year++) {
        const int testHeight = activationHeight + (year * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Block reward is ALWAYS 0 post-V6
        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

BOOST_AUTO_TEST_SUITE_END()
