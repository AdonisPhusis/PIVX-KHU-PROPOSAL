// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_STATEDB_H
#define PIVX_KHU_STATEDB_H

#include "dbwrapper.h"
#include "khu/khu_state.h"

#include <stdint.h>

/**
 * CKHUStateDB - LevelDB persistence layer for KHU global state
 *
 * Database keys:
 * - 'K' + 'S' + height -> KhuGlobalState
 *
 * The database stores KHU state snapshots at each block height.
 * This allows for efficient state retrieval and reorg handling.
 */
class CKHUStateDB : public CDBWrapper
{
public:
    explicit CKHUStateDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CKHUStateDB(const CKHUStateDB&);
    void operator=(const CKHUStateDB&);

public:
    /**
     * WriteKHUState - Persist KHU state for a given height
     *
     * @param nHeight Block height
     * @param state KHU state to write
     * @return true on success, false on failure
     */
    bool WriteKHUState(int nHeight, const KhuGlobalState& state);

    /**
     * ReadKHUState - Read KHU state at a given height
     *
     * @param nHeight Block height
     * @param state Output parameter for state
     * @return true if state exists, false otherwise
     */
    bool ReadKHUState(int nHeight, KhuGlobalState& state);

    /**
     * ExistsKHUState - Check if state exists at height
     *
     * @param nHeight Block height
     * @return true if state exists
     */
    bool ExistsKHUState(int nHeight);

    /**
     * EraseKHUState - Delete state at height (used during reorg)
     *
     * @param nHeight Block height
     * @return true on success
     */
    bool EraseKHUState(int nHeight);

    /**
     * LoadKHUState_OrGenesis - Load state or return genesis state
     *
     * If state doesn't exist at height, returns genesis state (all zeros).
     * This is used during activation of KHU upgrade.
     *
     * @param nHeight Block height
     * @return KHU state (existing or genesis)
     */
    KhuGlobalState LoadKHUState_OrGenesis(int nHeight);
};

#endif // PIVX_KHU_STATEDB_H
