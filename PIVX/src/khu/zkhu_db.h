// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_ZKHU_DB_H
#define PIVX_KHU_ZKHU_DB_H

#include "dbwrapper.h"
#include "khu/zkhu_note.h"
#include "sapling/incrementalmerkletree.h"
#include "uint256.h"

/**
 * CZKHUTreeDB - ZKHU Database (namespace 'K')
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md section 4
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE CRITIQUE: ZKHU utilise namespace 'K', Shield utilise 'S'/'s'
 * Aucun chevauchement de clés entre ZKHU et Shield.
 *
 * Key Prefixes:
 * - 'K' + 'A' + anchor → SaplingMerkleTree (ZKHU anchor)
 * - 'K' + 'N' + nullifier → bool (ZKHU nullifier spent flag)
 * - 'K' + 'T' + note_id → ZKHUNoteData (ZKHU note metadata)
 * - 'K' + 'L' + nullifier → cm (nullifier→commitment mapping for UNSTAKE)
 */
class CZKHUTreeDB : public CDBWrapper
{
public:
    explicit CZKHUTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

private:
    CZKHUTreeDB(const CZKHUTreeDB&);
    void operator=(const CZKHUTreeDB&);

public:
    /**
     * Anchor operations
     */
    bool WriteAnchor(const uint256& anchor, const SaplingMerkleTree& tree);
    bool ReadAnchor(const uint256& anchor, SaplingMerkleTree& tree) const;

    /**
     * Nullifier operations
     */
    bool WriteNullifier(const uint256& nullifier);
    bool IsNullifierSpent(const uint256& nullifier) const;
    bool EraseNullifier(const uint256& nullifier);

    /**
     * Note operations
     */
    bool WriteNote(const uint256& noteId, const ZKHUNoteData& data);
    bool ReadNote(const uint256& noteId, ZKHUNoteData& data) const;
    bool EraseNote(const uint256& noteId);

    /**
     * Nullifier → Commitment mapping (for UNSTAKE lookup)
     * Phase 5: Required for ApplyKHUUnstake to find note from nullifier
     */
    bool WriteNullifierMapping(const uint256& nullifier, const uint256& cm);
    bool ReadNullifierMapping(const uint256& nullifier, uint256& cm) const;
    bool EraseNullifierMapping(const uint256& nullifier);
};

#endif // PIVX_KHU_ZKHU_DB_H
