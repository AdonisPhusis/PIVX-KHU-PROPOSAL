// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_dao.h"
#include "khu/khu_state.h"
#include "consensus/consensus.h"
#include "logging.h"

#include <boost/multiprecision/cpp_int.hpp>

using namespace boost::multiprecision;

namespace khu_dao {

bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}

CAmount CalculateDAOBudget(const KhuGlobalState& state)
{
    // Use 128-bit arithmetic to prevent overflow
    int128_t U = state.U;
    int128_t Ur = state.Ur;
    int128_t total = U + Ur;

    if (total < 0) {
        LogPrintf("ERROR: CalculateDAOBudget: negative total U=%lld Ur=%lld\n",
                  (long long)state.U, (long long)state.Ur);
        return 0;
    }

    // budget = total × 5 / 1000 = 0.5%
    int128_t budget = (total * 5) / 1000;

    if (budget < 0 || budget > MAX_MONEY) {
        LogPrintf("ERROR: CalculateDAOBudget: overflow budget=%s (U=%lld Ur=%lld)\n",
                  budget.str().c_str(), (long long)state.U, (long long)state.Ur);
        return 0;
    }

    return static_cast<CAmount>(budget);
}

bool AccumulateDaoTreasuryIfNeeded(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    if (!IsDaoCycleBoundary(nHeight, consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight)) {
        return true; // Not at boundary, nothing to do
    }

    LogPrint(BCLog::KHU, "AccumulateDaoTreasury: height=%u, U=%lld, Ur=%lld, T_before=%lld\n",
             nHeight, (long long)state.U, (long long)state.Ur, (long long)state.T);

    CAmount budget = CalculateDAOBudget(state);

    if (budget <= 0) {
        LogPrint(BCLog::KHU, "AccumulateDaoTreasury: budget=%lld (skipping)\n", (long long)budget);
        return true; // Nothing to accumulate
    }

    // Add budget to T
    int128_t new_T = static_cast<int128_t>(state.T) + static_cast<int128_t>(budget);

    if (new_T < 0 || new_T > MAX_MONEY) {
        LogPrintf("ERROR: AccumulateDaoTreasury overflow: T=%lld, budget=%lld\n",
                  (long long)state.T, (long long)budget);
        return false;
    }

    state.T = static_cast<CAmount>(new_T);

    LogPrint(BCLog::KHU, "AccumulateDaoTreasury: budget=%lld, T_after=%lld\n",
             (long long)budget, (long long)state.T);

    return true;
}

bool UndoDaoTreasuryIfNeeded(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    if (!IsDaoCycleBoundary(nHeight, consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight)) {
        return true; // Nothing to undo
    }

    LogPrint(BCLog::KHU, "UndoDaoTreasury: height=%u, T_before=%lld\n",
             nHeight, (long long)state.T);

    CAmount budget = CalculateDAOBudget(state);

    // Subtract budget from T
    int128_t new_T = static_cast<int128_t>(state.T) - static_cast<int128_t>(budget);

    if (new_T < 0) {
        LogPrintf("ERROR: UndoDaoTreasury underflow: T=%lld, budget=%lld\n",
                  (long long)state.T, (long long)budget);
        return false;
    }

    state.T = static_cast<CAmount>(new_T);

    LogPrint(BCLog::KHU, "UndoDaoTreasury: budget=%lld, T_after=%lld\n",
             (long long)budget, (long long)state.T);

    return true;
}

} // namespace khu_dao
