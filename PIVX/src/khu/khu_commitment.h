// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_COMMITMENT_H
#define PIVX_KHU_COMMITMENT_H

#include "bls/bls_wrapper.h"
#include "serialize.h"
#include "uint256.h"

#include <vector>

struct KhuGlobalState;

/**
 * KhuStateCommitment - LLMQ-signed commitment to KHU state
 *
 * PHASE 3 IMPLEMENTATION: Masternode Finality for KHU
 *
 * Purpose:
 * - Provides cryptographic finality for KHU state at each block
 * - Prevents state divergence across network nodes
 * - Enables reorg protection beyond simple depth checks
 *
 * Flow:
 * 1. Each block: Compute hashState = SHA256(C, U, Cr, Ur, height)
 * 2. LLMQ masternodes sign this hash
 * 3. Once quorum reached (>= 60%), state is finalized
 * 4. Finalized states cannot be reorged without quorum consensus
 *
 * Scope:
 * - Phase 3 ONLY: State finality for existing C/U/Cr/Ur values
 * - Does NOT implement: R%, Daily Yield, STAKE/UNSTAKE, DOMC
 * - Future phases will extend this foundation
 */
struct KhuStateCommitment
{
    // Block identification
    uint32_t nHeight;        // Block height of this commitment

    // State hash: SHA256(C, U, Cr, Ur, height)
    uint256 hashState;

    // LLMQ signature data
    uint256 quorumHash;              // LLMQ quorum identifier
    CBLSSignature sig;               // Aggregate BLS signature from masternodes
    std::vector<bool> signers;       // Bitfield: signers[i] = true if MN i signed

    KhuStateCommitment()
    {
        SetNull();
    }

    void SetNull()
    {
        nHeight = 0;
        hashState.SetNull();
        quorumHash.SetNull();
        sig.Reset();
        signers.clear();
    }

    bool IsNull() const
    {
        return (nHeight == 0 && hashState.IsNull());
    }

    /**
     * IsValid - Basic validation of commitment structure
     *
     * Checks:
     * - Height is non-zero
     * - State hash is non-null
     * - Signature is valid (BLS verification)
     *
     * Does NOT check quorum threshold (use HasQuorum() for that)
     */
    bool IsValid() const;

    /**
     * HasQuorum - Check if commitment has sufficient signatures
     *
     * LLMQ consensus requirement: >= 60% of quorum members must sign
     *
     * @return true if enough masternodes signed (>= 60% threshold)
     */
    bool HasQuorum() const;

    /**
     * GetHash - Compute deterministic hash of this commitment
     *
     * Used for:
     * - Database indexing
     * - Consensus validation
     * - Network propagation
     *
     * @return SHA256 hash of all commitment fields
     */
    uint256 GetHash() const;

    SERIALIZE_METHODS(KhuStateCommitment, obj)
    {
        READWRITE(obj.nHeight);
        READWRITE(obj.hashState);
        READWRITE(obj.quorumHash);
        READWRITE(obj.sig);
        READWRITE(DYNBITSET(obj.signers));
    }
};

/**
 * ComputeKHUStateHash - Calculate canonical state hash
 *
 * CRITICAL FUNCTION for Phase 3 consensus
 *
 * Computes: SHA256(C || U || Cr || Ur || height)
 *
 * This hash represents the deterministic fingerprint of KHU state
 * at a given block. LLMQ masternodes sign this hash to provide
 * cryptographic finality.
 *
 * Serialization order MUST be stable:
 * 1. C (collateral)
 * 2. U (supply)
 * 3. Cr (reward pool)
 * 4. Ur (unstake rights)
 * 5. nHeight (block height)
 *
 * @param state KHU global state to hash
 * @return SHA256 hash of state components
 */
uint256 ComputeKHUStateHash(const KhuGlobalState& state);

/**
 * CreateKHUStateCommitment - Create new commitment for current state
 *
 * Called during block processing when:
 * - State has been updated
 * - LLMQ quorum is available
 * - Block height qualifies for finality
 *
 * Flow:
 * 1. Compute hashState from current KHU state
 * 2. Request LLMQ signatures for this hash
 * 3. Collect signatures from quorum members
 * 4. Build commitment structure
 *
 * NOTE: Signature collection is asynchronous. This function may return
 * a commitment without quorum initially. Use HasQuorum() to check.
 *
 * @param state Current KHU global state
 * @param quorumHash Active LLMQ quorum identifier
 * @return New KhuStateCommitment (may not have quorum yet)
 */
KhuStateCommitment CreateKHUStateCommitment(
    const KhuGlobalState& state,
    const uint256& quorumHash
);

/**
 * VerifyKHUStateCommitment - Verify commitment signature
 *
 * Validation checks:
 * 1. State hash matches recomputed hash from state
 * 2. BLS signature is valid for the state hash
 * 3. Signature matches the claimed quorum
 * 4. Quorum threshold met (>= 60%)
 *
 * This function performs FULL cryptographic verification.
 *
 * @param commitment Commitment to verify
 * @param state KHU state being committed to
 * @return true if commitment is valid and matches state
 */
bool VerifyKHUStateCommitment(
    const KhuStateCommitment& commitment,
    const KhuGlobalState& state
);

/**
 * CheckReorgConflict - Detect state divergence during reorg
 *
 * REORG PROTECTION MECHANISM
 *
 * When a reorg is attempted, this function checks if the new chain's
 * state commitment conflicts with the existing finalized commitment.
 *
 * Scenarios:
 * 1. No commitment exists → Allow reorg (within depth limit)
 * 2. Commitment matches → Allow reorg
 * 3. Commitment differs → REJECT reorg (state divergence detected)
 *
 * This prevents nodes from accepting chains with incompatible KHU state.
 *
 * @param nHeight Block height being reorged
 * @param hashState State hash from new chain
 * @return true if reorg is safe (no conflict), false if divergence detected
 */
bool CheckReorgConflict(uint32_t nHeight, const uint256& hashState);

#endif // PIVX_KHU_COMMITMENT_H
