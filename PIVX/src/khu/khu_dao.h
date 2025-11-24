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

// Constants
static const uint32_t DAO_CYCLE_LENGTH = 172800;  // 4 months (172800 blocks)

/**
 * Check if current height is a DAO cycle boundary
 *
 * DAO budget accumulation happens every 172800 blocks (4 months).
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if (nHeight - activation) % 172800 == 0
 */
bool IsDaoCycleBoundary(
    uint32_t nHeight,
    uint32_t nActivationHeight
);

/**
 * Calculate DAO budget from current global state
 *
 * Formula: 0.5% × (U + Ur) = (U + Ur) × 5 / 1000
 *
 * Uses 128-bit arithmetic to prevent overflow.
 *
 * @param state Current global state (uses U and Ur)
 * @return DAO budget in satoshis (0 on overflow)
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
