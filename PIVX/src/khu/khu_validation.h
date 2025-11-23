// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_VALIDATION_H
#define PIVX_KHU_VALIDATION_H

#include <memory>

class CBlock;
class CBlockIndex;
class CCoinsViewCache;
class CValidationState;
class CKHUStateDB;
class CKHUCommitmentDB;
struct KhuGlobalState;

namespace Consensus {
    struct Params;
}

/**
 * ProcessKHUBlock - Process KHU state transitions for a block
 *
 * PHASE 1 IMPLEMENTATION:
 * - Loads previous state
 * - Creates new state with updated height/hash
 * - Validates invariants
 * - Persists state to DB
 *
 * FUTURE PHASES (NOT IMPLEMENTED YET):
 * - Phase 2: MINT/REDEEM operations
 * - Phase 3: Daily YIELD application
 * - Phase 4: UNSTAKE bonus
 * - Phase 5: DOMC governance
 *
 * @param block The block to process
 * @param pindex Block index
 * @param view Coins view cache
 * @param state Validation state (for errors)
 * @param consensusParams Consensus parameters
 * @return true if KHU processing succeeded
 */
bool ProcessKHUBlock(const CBlock& block,
                     CBlockIndex* pindex,
                     CCoinsViewCache& view,
                     CValidationState& state,
                     const Consensus::Params& consensusParams);

/**
 * DisconnectKHUBlock - Rollback KHU state during reorg
 *
 * PHASE 1 IMPLEMENTATION:
 * - Simply erases state at this height
 * - Previous state remains intact
 *
 * FUTURE PHASES:
 * - Reverse MINT/REDEEM operations
 * - Reverse daily yield
 * - Validate reorg depth (<= 12 blocks)
 *
 * @param pindex Block index to disconnect
 * @param state Validation state
 * @return true if disconnect succeeded
 */
bool DisconnectKHUBlock(CBlockIndex* pindex,
                       CValidationState& state);

/**
 * InitKHUStateDB - Initialize the KHU state database
 *
 * Called during node startup. Creates the database if it doesn't exist.
 *
 * @param nCacheSize DB cache size
 * @param fReindex If true, wipe and recreate DB
 * @return true on success
 */
bool InitKHUStateDB(size_t nCacheSize, bool fReindex);

/**
 * GetKHUStateDB - Get global KHU state database instance
 *
 * @return Pointer to KHU state DB (may be nullptr if not initialized)
 */
CKHUStateDB* GetKHUStateDB();

/**
 * GetCurrentKHUState - Get KHU state at chain tip
 *
 * @param state Output parameter for state
 * @return true if state loaded successfully
 */
bool GetCurrentKHUState(KhuGlobalState& state);

/**
 * InitKHUCommitmentDB - Initialize the KHU commitment database
 *
 * PHASE 3: Called during node startup. Creates the commitment DB.
 *
 * @param nCacheSize DB cache size
 * @param fReindex If true, wipe and recreate DB
 * @return true on success
 */
bool InitKHUCommitmentDB(size_t nCacheSize, bool fReindex);

/**
 * GetKHUCommitmentDB - Get global KHU commitment database instance
 *
 * PHASE 3: Returns the commitment DB for state finality operations.
 *
 * @return Pointer to KHU commitment DB (may be nullptr if not initialized)
 */
CKHUCommitmentDB* GetKHUCommitmentDB();

#endif // PIVX_KHU_VALIDATION_H
