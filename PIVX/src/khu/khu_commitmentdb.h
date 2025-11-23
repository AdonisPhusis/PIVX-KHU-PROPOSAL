// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_COMMITMENTDB_H
#define PIVX_KHU_COMMITMENTDB_H

#include "dbwrapper.h"
#include "khu/khu_commitment.h"

#include <stdint.h>

/**
 * CKHUCommitmentDB - LevelDB persistence layer for KHU state commitments
 *
 * PHASE 3 IMPLEMENTATION: Masternode Finality Storage
 *
 * Database keys:
 * - 'K' + 'C' + height -> KhuStateCommitment
 * - 'K' + 'L' -> uint32_t (latest finalized height)
 *
 * Purpose:
 * - Store LLMQ-signed commitments for KHU state at each block
 * - Track latest finalized height for reorg protection
 * - Enable fast lookup of state commitments during validation
 *
 * The database stores commitments separately from state to:
 * 1. Allow state without commitment (before quorum reached)
 * 2. Optimize reorg checks (no need to load full state)
 * 3. Separate concerns (state vs consensus finality)
 */
class CKHUCommitmentDB : public CDBWrapper
{
public:
    explicit CKHUCommitmentDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CKHUCommitmentDB(const CKHUCommitmentDB&);
    void operator=(const CKHUCommitmentDB&);

public:
    /**
     * WriteCommitment - Persist state commitment for a given height
     *
     * Called when LLMQ quorum is reached for a block's KHU state.
     * Once written, the commitment provides cryptographic finality.
     *
     * @param nHeight Block height
     * @param commitment State commitment to write
     * @return true on success, false on failure
     */
    bool WriteCommitment(uint32_t nHeight, const KhuStateCommitment& commitment);

    /**
     * ReadCommitment - Read state commitment at a given height
     *
     * @param nHeight Block height
     * @param commitment Output parameter for commitment
     * @return true if commitment exists, false otherwise
     */
    bool ReadCommitment(uint32_t nHeight, KhuStateCommitment& commitment);

    /**
     * HaveCommitment - Check if commitment exists at height
     *
     * Fast check without loading full commitment data.
     *
     * @param nHeight Block height
     * @return true if commitment exists and is finalized
     */
    bool HaveCommitment(uint32_t nHeight);

    /**
     * EraseCommitment - Delete commitment at height (used during reorg)
     *
     * REORG SAFETY: Only erase commitments when:
     * - Height > latest finalized height
     * - Reorg is within allowed depth (12 blocks)
     *
     * @param nHeight Block height
     * @return true on success
     */
    bool EraseCommitment(uint32_t nHeight);

    /**
     * GetLatestFinalizedHeight - Get height of most recent finalized commitment
     *
     * REORG PROTECTION: Blocks at or below this height cannot be reorged
     * without quorum consensus on new chain.
     *
     * @return Height of latest finalized block (0 if none)
     */
    uint32_t GetLatestFinalizedHeight();

    /**
     * SetLatestFinalizedHeight - Update latest finalized height
     *
     * Called when a new commitment is written with quorum.
     * This marks all blocks up to this height as cryptographically final.
     *
     * @param nHeight New latest finalized height
     * @return true on success
     */
    bool SetLatestFinalizedHeight(uint32_t nHeight);

    /**
     * IsFinalizedAt - Check if a specific height is finalized
     *
     * A height is finalized if:
     * 1. Commitment exists at this height
     * 2. Commitment has valid quorum (>= 60%)
     *
     * @param nHeight Block height to check
     * @return true if height is finalized
     */
    bool IsFinalizedAt(uint32_t nHeight);
};

#endif // PIVX_KHU_COMMITMENTDB_H
