// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

//
// Unit tests for KHU Phase 1-EMISSION: 6→0 PIV/year emission schedule
//
// Canonical specification: docs/02-canonical-specification.md section 5
// Implementation: src/validation.cpp GetBlockValue() and GetMasternodePayment()
//

#include "test/test_pivx.h"

#include "chainparams.h"
#include "consensus/params.h"
#include "consensus/upgrades.h"
#include "validation.h"

#include <boost/test/unit_test.hpp>

/**
 * Custom test fixture for emission tests.
 * Required to use UpdateNetworkUpgradeParameters() which only works on REGTEST.
 */
struct EmissionTestingSetup : public TestingSetup
{
    int V6_DefaultActivation;

    EmissionTestingSetup()
    {
        // Switch to REGTEST network (required for UpdateNetworkUpgradeParameters)
        SelectParams(CBaseChainParams::REGTEST);

        // Save default V6.0 activation height
        V6_DefaultActivation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

        // Set V6.0 to inactive by default (tests will activate it as needed)
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
    }

    ~EmissionTestingSetup()
    {
        // Restore default V6.0 activation height
        UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, V6_DefaultActivation);

        // Switch back to MAIN network
        SelectParams(CBaseChainParams::MAIN);
    }
};

BOOST_FIXTURE_TEST_SUITE(khu_emission_v6_tests, EmissionTestingSetup)

/**
 * Test 1: Pre-activation (legacy PIVX emission)
 *
 * Before V6.0 activation, the network should use legacy PIVX emission:
 * - GetBlockValue() returns 10 PIV (fixed after V5.5)
 * - GetMasternodePayment() returns 6 PIV (nNewMNBlockReward)
 *
 * Staker gets: 10 - 6 = 4 PIV
 */
BOOST_AUTO_TEST_CASE(test_emission_pre_activation)
{
    // V6.0 is NO_ACTIVATION_HEIGHT by default
    const int testHeight = 5000000;  // Arbitrary height after V5.6
    const Consensus::Params& consensus = Params().GetConsensus();

    // Verify V6.0 is not active
    BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

    // Legacy emission
    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    // Legacy values (after V5.5)
    BOOST_CHECK_EQUAL(blockValue, 10 * COIN);  // Fixed 10 PIV
    BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);    // Fixed 6 PIV (nNewMNBlockReward)
}

/**
 * Test 2: Post-activation Year 0 (6 PIV per compartment)
 *
 * At activation height, emission should be:
 * - reward_year = max(6 - 0, 0) = 6
 * - GetBlockValue() = 6 PIV (staker compartment)
 * - GetMasternodePayment() = 6 PIV (MN compartment)
 * - Total per block = 18 PIV (6 staker + 6 MN + 6 DAO)
 */
BOOST_AUTO_TEST_CASE(test_emission_year0)
{
    // Activate V6.0 at height 6000000
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Test at activation height (year 0, block 0)
    {
        const int testHeight = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Year 0: reward_year = 6
        BOOST_CHECK_EQUAL(blockValue, 6 * COIN);  // Staker reward
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);   // MN reward
    }

    // Test at activation + 100000 blocks (still year 0, since year transition is at +525600)
    {
        const int testHeight = activationHeight + 100000;
        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 6 * COIN);
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
    }

    // Cleanup: reset V6.0
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 3: Year 1 (5 PIV per compartment)
 *
 * After 525,600 blocks (1 year), emission should decrease:
 * - reward_year = max(6 - 1, 0) = 5
 * - GetBlockValue() = 5 PIV
 * - GetMasternodePayment() = 5 PIV
 * - Total per block = 15 PIV
 */
BOOST_AUTO_TEST_CASE(test_emission_year1)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Test at exactly year 1 boundary (activation + 525600)
    const int testHeight = activationHeight + Consensus::Params::BLOCKS_PER_YEAR;
    BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    // Year 1: reward_year = 5
    BOOST_CHECK_EQUAL(blockValue, 5 * COIN);
    BOOST_CHECK_EQUAL(mnPayment, 5 * COIN);

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 4: Year 5 (1 PIV per compartment)
 *
 * After 5 years (5 * 525,600 blocks):
 * - reward_year = max(6 - 5, 0) = 1
 * - GetBlockValue() = 1 PIV
 * - GetMasternodePayment() = 1 PIV
 * - Total per block = 3 PIV
 */
BOOST_AUTO_TEST_CASE(test_emission_year5)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const int testHeight = activationHeight + (5 * Consensus::Params::BLOCKS_PER_YEAR);

    CAmount blockValue = GetBlockValue(testHeight);
    CAmount mnPayment = GetMasternodePayment(testHeight);

    // Year 5: reward_year = 1
    BOOST_CHECK_EQUAL(blockValue, 1 * COIN);
    BOOST_CHECK_EQUAL(mnPayment, 1 * COIN);

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 5: Year 6+ (0 PIV - terminal emission)
 *
 * After 6 years, emission goes to zero and stays there:
 * - reward_year = max(6 - 6, 0) = 0
 * - GetBlockValue() = 0 PIV
 * - GetMasternodePayment() = 0 PIV
 * - Total per block = 0 PIV (terminal emission)
 */
BOOST_AUTO_TEST_CASE(test_emission_year6_plus)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    // Test year 6
    {
        const int testHeight = activationHeight + (6 * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Year 6: reward_year = 0
        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    // Test year 10 (should still be 0)
    {
        const int testHeight = activationHeight + (10 * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    // Test year 100 (should still be 0)
    {
        const int testHeight = activationHeight + (100 * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, 0);
        BOOST_CHECK_EQUAL(mnPayment, 0);
    }

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 6: Year boundary precision
 *
 * Test exact boundary conditions:
 * - Last block of year 0 should give reward_year = 6
 * - First block of year 1 should give reward_year = 5
 */
BOOST_AUTO_TEST_CASE(test_emission_boundary)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    // Last block of year 0 (block 525599)
    {
        const int testHeight = activationHeight + Consensus::Params::BLOCKS_PER_YEAR - 1;

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Should still be year 0 (525599 / 525600 = 0)
        BOOST_CHECK_EQUAL(blockValue, 6 * COIN);
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
    }

    // First block of year 1 (block 525600)
    {
        const int testHeight = activationHeight + Consensus::Params::BLOCKS_PER_YEAR;

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Should be year 1 (525600 / 525600 = 1)
        BOOST_CHECK_EQUAL(blockValue, 5 * COIN);
        BOOST_CHECK_EQUAL(mnPayment, 5 * COIN);
    }

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 7: Emission never negative
 *
 * Verify that reward_year = max(6 - year, 0) never goes negative,
 * even for very large year values.
 */
BOOST_AUTO_TEST_CASE(test_emission_never_negative)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    // Test various large year values
    const int testYears[] = {6, 7, 10, 50, 100, 1000};

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

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 8: All year progression (0 through 6)
 *
 * Comprehensive test of the full emission schedule from year 0 to year 6.
 */
BOOST_AUTO_TEST_CASE(test_emission_full_schedule)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    struct YearTest {
        int year;
        CAmount expectedReward;
    };

    const YearTest tests[] = {
        {0, 6 * COIN},
        {1, 5 * COIN},
        {2, 4 * COIN},
        {3, 3 * COIN},
        {4, 2 * COIN},
        {5, 1 * COIN},
        {6, 0},
    };

    for (const auto& test : tests) {
        const int testHeight = activationHeight + (test.year * Consensus::Params::BLOCKS_PER_YEAR);

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        BOOST_CHECK_EQUAL(blockValue, test.expectedReward);
        BOOST_CHECK_EQUAL(mnPayment, test.expectedReward);
    }

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

/**
 * Test 9: Transition from legacy to V6.0
 *
 * Test the exact moment of V6.0 activation to ensure clean transition.
 */
BOOST_AUTO_TEST_CASE(test_emission_transition)
{
    const int activationHeight = 6000000;
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, activationHeight);

    const Consensus::Params& consensus = Params().GetConsensus();

    // Block before activation (should be legacy)
    {
        const int testHeight = activationHeight - 1;
        BOOST_CHECK(!NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // Legacy emission
        BOOST_CHECK_EQUAL(blockValue, 10 * COIN);
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
    }

    // Block at activation (should be V6.0 year 0)
    {
        const int testHeight = activationHeight;
        BOOST_CHECK(NetworkUpgradeActive(testHeight, consensus, Consensus::UPGRADE_V6_0));

        CAmount blockValue = GetBlockValue(testHeight);
        CAmount mnPayment = GetMasternodePayment(testHeight);

        // V6.0 year 0 emission
        BOOST_CHECK_EQUAL(blockValue, 6 * COIN);
        BOOST_CHECK_EQUAL(mnPayment, 6 * COIN);
    }

    // Cleanup
    UpdateNetworkUpgradeParameters(Consensus::UPGRADE_V6_0, Consensus::NetworkUpgrade::NO_ACTIVATION_HEIGHT);
}

BOOST_AUTO_TEST_SUITE_END()
