// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_DAO_H
#define PIVX_KHU_DAO_H

#include "amount.h"
#include "consensus/params.h"

// Forward declarations
struct KhuGlobalState;

namespace khu_dao {

// ============================================================================
// Constants (consensus-critical)
// ============================================================================

/**
 * DAO Treasury accumulates 2% annual, calculated daily (same trigger as yield).
 * Formula: T_daily = (U + Ur) / T_DAILY_DIVISOR
 * Where T_DAILY_DIVISOR = 10000 / (200 / 365) = 182500
 *
 * Verification: 2% annual = 200 basis points
 *   daily_rate = 200 / 365 / 10000 = 1 / 182500
 *   T_daily = (U + Ur) * (1 / 182500) = (U + Ur) / 182500
 */
static const uint32_t DAO_CYCLE_LENGTH = 1440;    // Daily (same as yield)
static const int64_t T_DAILY_DIVISOR = 182500;    // (U+Ur) / 182500 = daily T (2% annual)

/**
 * Check if current height is a DAO cycle boundary (daily)
 *
 * DAO Treasury accumulation happens every 1440 blocks (daily, same as yield).
 * Unified with ApplyDailyUpdatesIfNeeded() in ConnectBlock.
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if daily yield should be applied
 */
bool IsDaoCycleBoundary(
    uint32_t nHeight,
    uint32_t nActivationHeight
);

/**
 * Calculate daily DAO Treasury budget from current global state
 *
 * FORMULA CONSENSUS (2% annual):
 *   T_daily = (U + Ur) / 182500
 *
 * Example:
 * - U + Ur = 1,000,000 KHU (1e14 satoshis)
 * - T_daily = 1e14 / 182500 = 547,945,205 satoshis (~547.9 KHU/day)
 * - Annual: 547.9 × 365 = 199,985 KHU ≈ 2% of 1M
 *
 * Uses 128-bit arithmetic to prevent overflow.
 *
 * @param state Current global state (uses U and Ur)
 * @return Daily DAO budget in satoshis (0 on overflow)
 */
CAmount CalculateDAOBudget(
    const KhuGlobalState& state
);

/**
 * Accumulate DAO treasury if at cycle boundary
 *
 * CONSENSUS CRITICAL: Must be called FIRST in ConnectBlock (before yield).
 * Budget is calculated on INITIAL state (U+Ur before any modifications).
 *
 * @param state [IN/OUT] Global state to modify (T increases)
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on overflow
 */
bool AccumulateDaoTreasuryIfNeeded(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * Undo DAO treasury accumulation (for DisconnectBlock)
 *
 * Reverses AccumulateDaoTreasuryIfNeeded by subtracting the budget from T.
 *
 * @param state [IN/OUT] Global state to restore
 * @param nHeight Current block height
 * @param consensusParams Consensus parameters
 * @return true on success, false on underflow
 */
bool UndoDaoTreasuryIfNeeded(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

} // namespace khu_dao

#endif // PIVX_KHU_DAO_H
