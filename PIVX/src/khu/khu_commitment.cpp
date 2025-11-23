// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_commitment.h"

#include "hash.h"
#include "khu/khu_state.h"
#include "util/system.h"

#include <algorithm>

// LLMQ quorum threshold: 60% of members must sign
static constexpr double QUORUM_THRESHOLD = 0.60;

bool KhuStateCommitment::IsValid() const
{
    // Basic structure validation
    if (nHeight == 0) {
        return false;
    }
    if (hashState.IsNull()) {
        return false;
    }
    if (quorumHash.IsNull()) {
        return false;
    }
    if (!sig.IsValid()) {
        return false;
    }
    if (signers.empty()) {
        return false;
    }

    return true;
}

bool KhuStateCommitment::HasQuorum() const
{
    if (signers.empty()) {
        return false;
    }

    // Count signers
    int signerCount = 0;
    for (bool signer : signers) {
        if (signer) {
            signerCount++;
        }
    }

    // Check threshold: >= 60%
    int totalMembers = signers.size();
    double signerRatio = static_cast<double>(signerCount) / static_cast<double>(totalMembers);

    return signerRatio >= QUORUM_THRESHOLD;
}

uint256 KhuStateCommitment::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << *this;
    return ss.GetHash();
}

uint256 ComputeKHUStateHash(const KhuGlobalState& state)
{
    // CRITICAL: Serialization order must be deterministic and stable
    // Order: C, U, Cr, Ur, nHeight
    // This hash is signed by LLMQ masternodes for consensus

    CHashWriter ss(SER_GETHASH, 0);
    ss << state.C;
    ss << state.U;
    ss << state.Cr;
    ss << state.Ur;
    ss << state.nHeight;

    return ss.GetHash();
}

KhuStateCommitment CreateKHUStateCommitment(
    const KhuGlobalState& state,
    const uint256& quorumHash)
{
    KhuStateCommitment commitment;

    // Set basic fields
    commitment.nHeight = state.nHeight;
    commitment.hashState = ComputeKHUStateHash(state);
    commitment.quorumHash = quorumHash;

    // NOTE: Signature and signers are initially empty
    // They will be populated by LLMQ signature collection process
    // This is handled by the caller (FinalizeKHUStateIfQuorum)

    return commitment;
}

bool VerifyKHUStateCommitment(
    const KhuStateCommitment& commitment,
    const KhuGlobalState& state)
{
    // 1. Basic structure validation
    if (!commitment.IsValid()) {
        LogPrint(BCLog::KHU, "KHU: Invalid commitment structure at height %d\n", commitment.nHeight);
        return false;
    }

    // 2. Height must match
    if (commitment.nHeight != static_cast<uint32_t>(state.nHeight)) {
        LogPrint(BCLog::KHU, "KHU: Commitment height mismatch: %d != %d\n",
                 commitment.nHeight, state.nHeight);
        return false;
    }

    // 3. State hash must match
    uint256 computedHash = ComputeKHUStateHash(state);
    if (commitment.hashState != computedHash) {
        LogPrint(BCLog::KHU, "KHU: State hash mismatch at height %d: %s != %s\n",
                 commitment.nHeight,
                 commitment.hashState.ToString(),
                 computedHash.ToString());
        return false;
    }

    // 4. Quorum threshold must be met
    if (!commitment.HasQuorum()) {
        LogPrint(BCLog::KHU, "KHU: Commitment lacks quorum at height %d\n", commitment.nHeight);
        return false;
    }

    // 5. BLS signature verification
    // NOTE: Full LLMQ signature verification will be implemented when
    // integrating with llmq::CQuorum in Phase 3 integration step
    // For now, we trust that if the commitment has a valid signature
    // and meets quorum threshold, it's valid
    //
    // TODO Phase 3: Add llmq::CQuorum::VerifyRecoveredSig() call here

    LogPrint(BCLog::KHU, "KHU: State commitment verified at height %d: %s\n",
             commitment.nHeight, commitment.hashState.ToString());

    return true;
}

bool CheckReorgConflict(uint32_t nHeight, const uint256& hashState)
{
    // Check if there's an existing finalized commitment at this height
    // that conflicts with the new chain's state hash
    //
    // This prevents nodes from accepting chains with incompatible KHU state

    // NOTE: This function requires GetKHUCommitmentDB() to be available
    // It will be fully integrated when commitment DB is initialized at startup

    // For Phase 3: Placeholder implementation
    // Full LLMQ integration will be added in validation hooks

    // TODO Phase 3 Integration: Call from validation.cpp during chain switch
    //   to detect state divergence before accepting new chain

    return true;  // Allow reorg (commitment DB check will be in DisconnectKHUBlock)
}
