// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_domc.h"
#include "khu/khu_domcdb.h"
#include "khu/khu_state.h"
#include "khu/khu_statedb.h"
#include "khu/khu_validation.h"
#include "consensus/params.h"
#include "hash.h"
#include "logging.h"

#include <algorithm>

namespace khu_domc {

// ============================================================================
// DomcCommit implementation
// ============================================================================

uint256 DomcCommit::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << hashCommit;
    ss << mnOutpoint;
    ss << nCycleId;
    ss << nCommitHeight;
    return ss.GetHash();
}

// ============================================================================
// DomcReveal implementation
// ============================================================================

uint256 DomcReveal::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    ss << mnOutpoint;
    ss << nCycleId;
    ss << nRevealHeight;
    return ss.GetHash();
}

uint256 DomcReveal::GetCommitHash() const
{
    // Commit hash = Hash(R_proposal || salt)
    CHashWriter ss(SER_GETHASH, 0);
    ss << nRProposal;
    ss << salt;
    return ss.GetHash();
}

// ============================================================================
// Cycle management functions
// ============================================================================

uint32_t GetCurrentCycleId(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight < nActivationHeight) {
        return 0; // Before activation
    }

    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    uint32_t cycle_number = blocks_since_activation / DOMC_CYCLE_LENGTH;
    return nActivationHeight + (cycle_number * DOMC_CYCLE_LENGTH);
}

bool IsDomcCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    // Before activation: not a boundary
    if (nHeight < nActivationHeight) {
        return false;
    }

    // First cycle starts at activation height
    if (nHeight == nActivationHeight) {
        return true;
    }

    // Subsequent cycles: every DOMC_CYCLE_LENGTH blocks
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DOMC_CYCLE_LENGTH) == 0;
}

bool IsDomcCommitPhase(uint32_t nHeight, uint32_t cycleStart)
{
    if (nHeight < cycleStart) {
        return false;
    }

    uint32_t offset = nHeight - cycleStart;
    return (offset >= DOMC_COMMIT_OFFSET && offset < DOMC_REVEAL_OFFSET);
}

bool IsDomcRevealPhase(uint32_t nHeight, uint32_t cycleStart)
{
    if (nHeight < cycleStart) {
        return false;
    }

    uint32_t offset = nHeight - cycleStart;
    return (offset >= DOMC_REVEAL_OFFSET && offset < DOMC_CYCLE_LENGTH);
}

// ============================================================================
// Median calculation (consensus-critical)
// ============================================================================

uint16_t CalculateDomcMedian(
    uint32_t cycleId,
    uint16_t currentR,
    uint16_t R_MAX_dynamic
)
{
    // Get DOMC database
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: CalculateDomcMedian: DOMC DB not initialized\n");
        return currentR; // Fallback: keep current R
    }

    // Collect all valid reveals for this cycle
    std::vector<DomcReveal> reveals;
    if (!domcDB->GetRevealsForCycle(cycleId, reveals)) {
        LogPrint(BCLog::KHU, "CalculateDomcMedian: No reveals found for cycle %u\n", cycleId);
        return currentR; // No reveals → keep current R
    }

    // Extract R proposals from valid reveals
    std::vector<uint16_t> proposals;
    proposals.reserve(reveals.size());

    for (const auto& reveal : reveals) {
        // Verify reveal matches a commit
        DomcCommit commit;
        if (!domcDB->ReadCommit(reveal.mnOutpoint, cycleId, commit)) {
            LogPrint(BCLog::KHU, "CalculateDomcMedian: No commit found for reveal (MN=%s)\n",
                     reveal.mnOutpoint.ToString());
            continue; // Skip invalid reveal
        }

        // Verify commit hash matches reveal
        if (commit.hashCommit != reveal.GetCommitHash()) {
            LogPrint(BCLog::KHU, "CalculateDomcMedian: Commit hash mismatch (MN=%s)\n",
                     reveal.mnOutpoint.ToString());
            continue; // Skip invalid reveal
        }

        // Valid reveal → add to proposals
        proposals.push_back(reveal.nRProposal);
    }

    // V1 RULE: No minimum quorum
    if (proposals.empty()) {
        LogPrint(BCLog::KHU, "CalculateDomcMedian: No valid proposals for cycle %u (keeping R=%u)\n",
                 cycleId, currentR);
        return currentR; // 0 valid votes → keep current R
    }

    // Calculate median (floor index)
    std::sort(proposals.begin(), proposals.end());
    uint16_t median = proposals[proposals.size() / 2];

    // Clamp to R_MAX_dynamic (governance safety limit)
    if (median > R_MAX_dynamic) {
        LogPrint(BCLog::KHU, "CalculateDomcMedian: Clamping median %u to R_MAX %u\n",
                 median, R_MAX_dynamic);
        median = R_MAX_dynamic;
    }

    LogPrint(BCLog::KHU, "CalculateDomcMedian: Cycle %u → %zu valid votes, median R=%u (clamped to %u)\n",
             cycleId, proposals.size(), median, R_MAX_dynamic);

    return median;
}

// ============================================================================
// Cycle initialization/finalization
// ============================================================================

void InitializeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight
)
{
    state.domc_cycle_start = nHeight;
    state.domc_cycle_length = DOMC_CYCLE_LENGTH;
    state.domc_commit_phase_start = nHeight + DOMC_COMMIT_OFFSET;
    state.domc_reveal_deadline = nHeight + DOMC_REVEAL_OFFSET;

    LogPrint(BCLog::KHU, "InitializeDomcCycle: New cycle at height %u\n", nHeight);
    LogPrint(BCLog::KHU, "  Commit phase: %u - %u\n",
             state.domc_commit_phase_start, state.domc_reveal_deadline - 1);
    LogPrint(BCLog::KHU, "  Reveal phase: %u - %u\n",
             state.domc_reveal_deadline, nHeight + DOMC_CYCLE_LENGTH - 1);
}

bool FinalizeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Calculate previous cycle ID
    uint32_t prevCycleId = nHeight - DOMC_CYCLE_LENGTH;

    if (prevCycleId < consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight) {
        // First cycle after activation → no previous cycle to finalize
        LogPrint(BCLog::KHU, "FinalizeDomcCycle: First cycle, no previous cycle to finalize\n");
        return true;
    }

    LogPrint(BCLog::KHU, "FinalizeDomcCycle: Finalizing cycle %u at height %u\n",
             prevCycleId, nHeight);

    // Calculate new R_annual from median of previous cycle
    uint16_t old_R = state.R_annual;
    uint16_t new_R = CalculateDomcMedian(prevCycleId, old_R, state.R_MAX_dynamic);

    if (new_R != old_R) {
        LogPrint(BCLog::KHU, "FinalizeDomcCycle: R_annual updated: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R, new_R, old_R / 100.0, new_R / 100.0);
        state.R_annual = new_R;
    } else {
        LogPrint(BCLog::KHU, "FinalizeDomcCycle: R_annual unchanged: %u (%.2f%%)\n",
                 old_R, old_R / 100.0);
    }

    return true;
}

bool UndoFinalizeDomcCycle(
    KhuGlobalState& state,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Calculate previous cycle ID (the cycle that was finalized at nHeight)
    uint32_t prevCycleId = nHeight - DOMC_CYCLE_LENGTH;

    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;

    if (prevCycleId < V6_activation) {
        // First cycle boundary after activation
        // FinalizeDomcCycle returned early without changing state
        // Nothing to undo
        LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: First cycle boundary, no state changes to undo\n");
        return true;
    }

    // To undo FinalizeDomcCycle, we need to restore ALL DOMC state fields to what they
    // were BEFORE FinalizeDomcCycle was called. The correct way to do this is to read
    // the state from the previous cycle boundary (nHeight - DOMC_CYCLE_LENGTH).

    // Calculate the height of the previous cycle boundary
    uint32_t prevCycleBoundary = nHeight - DOMC_CYCLE_LENGTH;

    // Read state from previous cycle boundary
    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        LogPrintf("ERROR: UndoFinalizeDomcCycle: State DB not initialized\n");
        return false;
    }

    KhuGlobalState prevState;
    if (!db->ReadKHUState(prevCycleBoundary, prevState)) {
        // Edge case: If previous state doesn't exist (shouldn't happen in valid chain),
        // fall back to defaults
        LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Cannot read state at height %u, falling back to defaults\n",
                 prevCycleBoundary);
        state.R_annual = R_DEFAULT;
        state.R_MAX_dynamic = R_MAX;
        // domc_cycle_* fields will be reinitialized by InitializeDomcCycle
        return true;
    }

    // Restore ALL DOMC-related state fields from previous state
    uint16_t old_R = state.R_annual;
    uint16_t restored_R = prevState.R_annual;
    uint16_t old_R_MAX = state.R_MAX_dynamic;
    uint16_t restored_R_MAX = prevState.R_MAX_dynamic;

    // Restore R_annual
    if (restored_R != old_R) {
        LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Restoring R_annual: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R, restored_R, old_R / 100.0, restored_R / 100.0);
        state.R_annual = restored_R;
    }

    // Restore R_MAX_dynamic
    if (restored_R_MAX != old_R_MAX) {
        LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Restoring R_MAX_dynamic: %u → %u (%.2f%% → %.2f%%)\n",
                 old_R_MAX, restored_R_MAX, old_R_MAX / 100.0, restored_R_MAX / 100.0);
        state.R_MAX_dynamic = restored_R_MAX;
    }

    // Restore DOMC cycle tracking fields
    // Note: These will be re-initialized by InitializeDomcCycle, but we restore them
    // here for consistency and to maintain exact state reversibility
    state.domc_cycle_start = prevState.domc_cycle_start;
    state.domc_commit_phase_start = prevState.domc_commit_phase_start;
    state.domc_reveal_deadline = prevState.domc_reveal_deadline;

    LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Restored DOMC cycle fields (start=%u, commit_start=%u, reveal_deadline=%u)\n",
             state.domc_cycle_start, state.domc_commit_phase_start, state.domc_reveal_deadline);

    // Clean up commits and reveals from the cycle being undone
    // The cycle being undone is the one that started at nHeight - DOMC_CYCLE_LENGTH
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (domcDB) {
        // Clear all commits/reveals for this cycle from DB
        // This prevents stale data from affecting future cycles after reorg
        if (!domcDB->EraseCycleData(prevCycleId)) {
            LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Warning - failed to erase cycle data for cycle %u\n",
                     prevCycleId);
            // Non-critical: continue even if cleanup fails
        } else {
            LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Cleaned up commits/reveals for cycle %u\n",
                     prevCycleId);
        }
    } else {
        LogPrint(BCLog::KHU, "UndoFinalizeDomcCycle: Warning - DOMC DB not initialized, skipping cleanup\n");
    }

    return true;
}

// ============================================================================
// R_MAX_dynamic calculation
// ============================================================================

uint16_t CalculateRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight)
{
    // Before activation: return initial value
    if (nHeight < nActivationHeight) {
        return R_MAX_DYNAMIC_INITIAL;
    }

    // Calculate year since activation
    // year = (nHeight - activation) / BLOCKS_PER_YEAR
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    uint32_t year = blocks_since_activation / 525600; // BLOCKS_PER_YEAR

    // FORMULA CONSENSUS: R_MAX_dynamic = max(400, 3700 - year × 100)
    int32_t calculated = R_MAX_DYNAMIC_INITIAL - (year * R_MAX_DYNAMIC_DECAY);

    // Clamp to minimum (floor at 4%)
    if (calculated < R_MAX_DYNAMIC_MIN) {
        return R_MAX_DYNAMIC_MIN;
    }

    return static_cast<uint16_t>(calculated);
}

void UpdateRMaxDynamic(
    KhuGlobalState& state,
    uint32_t nHeight,
    uint32_t nActivationHeight
)
{
    uint16_t newRMax = CalculateRMaxDynamic(nHeight, nActivationHeight);

    if (newRMax != state.R_MAX_dynamic) {
        LogPrint(BCLog::KHU, "UpdateRMaxDynamic: R_MAX_dynamic updated %u → %u (%.2f%% → %.2f%%) at height %u\n",
                 state.R_MAX_dynamic, newRMax,
                 state.R_MAX_dynamic / 100.0, newRMax / 100.0,
                 nHeight);
        state.R_MAX_dynamic = newRMax;
    }
}

} // namespace khu_domc
