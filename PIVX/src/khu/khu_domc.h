// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_DOMC_H
#define PIVX_KHU_DOMC_H

#include "amount.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

// Forward declarations
struct KhuGlobalState;
namespace Consensus { struct Params; }

/**
 * DOMC (Decentralized Open Monetary Committee)
 *
 * Phase 6.2: Masternode governance for R% (annual yield rate)
 *
 * ARCHITECTURE:
 * - Commit-reveal voting every 172800 blocks (4 months)
 * - Votes stored in CKHUDomcDB (NOT in KhuGlobalState)
 * - Result: median(R) clamped to R_MAX_dynamic
 * - No minimum quorum (v1): ≥1 vote → apply median, 0 votes → R unchanged
 *
 * CYCLE PHASES:
 * 1. Normal phase: 0 → 132480 blocks
 * 2. Commit phase: 132480 → 152640 blocks (20160 blocks = ~2 weeks)
 * 3. Reveal phase: 152640 → 172800 blocks (20160 blocks = ~2 weeks)
 * 4. Finalization: At 172800 → calculate median, start new cycle
 */

namespace khu_domc {

// DOMC cycle parameters (consensus-critical)
static const uint32_t DOMC_CYCLE_LENGTH = 172800;      // 4 months (172800 blocks)
static const uint32_t DOMC_COMMIT_OFFSET = 132480;     // Start commit phase at 132480
static const uint32_t DOMC_REVEAL_OFFSET = 152640;     // Start reveal phase at 152640
static const uint32_t DOMC_COMMIT_DURATION = 20160;    // Commit window: 20160 blocks (~2 weeks)
static const uint32_t DOMC_REVEAL_DURATION = 20160;    // Reveal window: 20160 blocks (~2 weeks)

// R% limits (basis points: 1500 = 15.00%)
static const uint16_t R_MIN = 0;       // Minimum R%: 0.00%
static const uint16_t R_MAX = 5000;    // Absolute maximum R%: 50.00%
static const uint16_t R_DEFAULT = 1500; // Default R% at genesis: 15.00%

/**
 * DomcCommit - Masternode commit for R% vote
 *
 * Phase 1 (commit): Masternode publishes hash(R_proposal || salt)
 * Prevents front-running and collusion.
 */
struct DomcCommit
{
    uint256 hashCommit;          // Hash of (R_proposal || salt)
    COutPoint mnOutpoint;        // Masternode collateral outpoint (identity)
    uint32_t nCycleId;           // Cycle ID (cycle_start height)
    uint32_t nCommitHeight;      // Block height of commit
    std::vector<unsigned char> vchSig; // Masternode signature

    DomcCommit()
    {
        SetNull();
    }

    void SetNull()
    {
        hashCommit.SetNull();
        mnOutpoint.SetNull();
        nCycleId = 0;
        nCommitHeight = 0;
        vchSig.clear();
    }

    bool IsNull() const
    {
        return hashCommit.IsNull();
    }

    /**
     * GetHash - Unique identifier for this commit
     * Used as key in CKHUDomcDB
     */
    uint256 GetHash() const;

    SERIALIZE_METHODS(DomcCommit, obj)
    {
        READWRITE(obj.hashCommit);
        READWRITE(obj.mnOutpoint);
        READWRITE(obj.nCycleId);
        READWRITE(obj.nCommitHeight);
        READWRITE(obj.vchSig);
    }
};

/**
 * DomcReveal - Masternode reveal for R% vote
 *
 * Phase 2 (reveal): Masternode reveals R_proposal and salt
 * Must match previously committed hash(R_proposal || salt)
 */
struct DomcReveal
{
    uint16_t nRProposal;         // Proposed R% (basis points: 1500 = 15.00%)
    uint256 salt;                // Random salt (for commit hash)
    COutPoint mnOutpoint;        // Masternode collateral outpoint (must match commit)
    uint32_t nCycleId;           // Cycle ID (must match commit)
    uint32_t nRevealHeight;      // Block height of reveal
    std::vector<unsigned char> vchSig; // Masternode signature

    DomcReveal()
    {
        SetNull();
    }

    void SetNull()
    {
        nRProposal = 0;
        salt.SetNull();
        mnOutpoint.SetNull();
        nCycleId = 0;
        nRevealHeight = 0;
        vchSig.clear();
    }

    bool IsNull() const
    {
        return salt.IsNull();
    }

    /**
     * GetHash - Unique identifier for this reveal
     * Used as key in CKHUDomcDB
     */
    uint256 GetHash() const;

    /**
     * GetCommitHash - Calculate hash(R_proposal || salt)
     * Must match DomcCommit.hashCommit for validation
     */
    uint256 GetCommitHash() const;

    SERIALIZE_METHODS(DomcReveal, obj)
    {
        READWRITE(obj.nRProposal);
        READWRITE(obj.salt);
        READWRITE(obj.mnOutpoint);
        READWRITE(obj.nCycleId);
        READWRITE(obj.nRevealHeight);
        READWRITE(obj.vchSig);
    }
};

/**
 * GetCurrentCycleId - Calculate cycle ID for given height
 *
 * Cycle ID = domc_cycle_start (height of cycle start)
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return Cycle start height (cycle ID)
 */
uint32_t GetCurrentCycleId(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * IsDomcCycleBoundary - Check if current height is DOMC cycle boundary
 *
 * Cycle boundary = height where cycle ends and new cycle starts
 * At boundary: finalize previous cycle, calculate median(R), start new cycle
 *
 * @param nHeight Current block height
 * @param nActivationHeight KHU V6.0 activation height
 * @return true if (nHeight - activation) % 172800 == 0
 */
bool IsDomcCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight);

/**
 * IsDomcCommitPhase - Check if current height is in commit phase
 *
 * Commit phase: [cycle_start + 132480, cycle_start + 152640)
 * Duration: 20160 blocks (~2 weeks)
 *
 * @param nHeight Current block height
 * @param cycleStart Cycle start height
 * @return true if in commit phase
 */
bool IsDomcCommitPhase(uint32_t nHeight, uint32_t cycleStart);

/**
 * IsDomcRevealPhase - Check if current height is in reveal phase
 *
 * Reveal phase: [cycle_start + 152640, cycle_start + 172800)
 * Duration: 20160 blocks (~2 weeks)
 *
 * @param nHeight Current block height
 * @param cycleStart Cycle start height
 * @return true if in reveal phase
 */
bool IsDomcRevealPhase(uint32_t nHeight, uint32_t cycleStart);

/**
 * CalculateDomcMedian - Calculate median R% from valid reveals
 *
 * V1 RULE (no minimum quorum):
 * - If 0 valid reveals → return current R_annual (no change)
 * - If ≥1 valid reveals → return clamped median(R)
 *
 * Clamping: median ≤ R_MAX_dynamic (governance safety limit)
 *
 * @param cycleId Cycle ID to finalize
 * @param currentR Current R_annual (fallback if 0 votes)
 * @param R_MAX_dynamic Maximum allowed R% (clamp limit)
 * @return New R_annual (basis points)
 */
uint16_t CalculateDomcMedian(
    uint32_t cycleId,
    uint16_t currentR,
    uint16_t R_MAX_dynamic
);

/**
 * InitializeDomcCycle - Initialize new DOMC cycle in state
 *
 * Called at cycle boundary in ConnectBlock.
 * Updates domc_cycle_start, domc_commit_phase_start, domc_reveal_deadline.
 *
 * @param state [IN/OUT] Global state to update
 * @param nHeight Current block height (cycle start)
 */
void InitializeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight
);

/**
 * FinalizeDomcCycle - Finalize previous cycle and update R_annual
 *
 * Called at cycle boundary in ConnectBlock (BEFORE InitializeDomcCycle).
 * 1. Collect valid reveals from previous cycle
 * 2. Calculate median(R) with clamping
 * 3. Update state.R_annual
 *
 * @param state [IN/OUT] Global state to update (R_annual)
 * @param nHeight Current block height (cycle boundary)
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on error
 */
bool FinalizeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

/**
 * UndoFinalizeDomcCycle - Undo DOMC cycle finalization during reorg
 *
 * Called in DisconnectBlock when a cycle boundary block is disconnected.
 * Restores R_annual to its value before FinalizeDomcCycle was called.
 *
 * CRITICAL for reorg safety: Without this, R_annual changes are irreversible
 * and cause state divergence during chain reorganizations.
 *
 * @param state [IN/OUT] Global state to restore (R_annual)
 * @param nHeight Current block height (cycle boundary being disconnected)
 * @param consensusParams Consensus parameters (for activation height)
 * @return true on success, false on error
 */
bool UndoFinalizeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
);

} // namespace khu_domc

#endif // PIVX_KHU_DOMC_H
