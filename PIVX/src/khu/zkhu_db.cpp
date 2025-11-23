// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/zkhu_db.h"

#include "util/system.h"

// ZKHU namespace key prefixes
static constexpr char DB_ZKHU_ANCHOR = 'A';      // 'K' + 'A' + anchor → SaplingMerkleTree
static constexpr char DB_ZKHU_NULLIFIER = 'N';  // 'K' + 'N' + nullifier → bool
static constexpr char DB_ZKHU_NOTE = 'T';       // 'K' + 'T' + note_id → ZKHUNoteData
static constexpr char DB_ZKHU_LOOKUP = 'L';     // 'K' + 'L' + nullifier → cm

// Master namespace for all ZKHU data
static constexpr char DB_ZKHU_NAMESPACE = 'K';

CZKHUTreeDB::CZKHUTreeDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "khu" / "zkhu", nCacheSize, fMemory, fWipe)
{
}

// ========== Anchor Operations ==========

bool CZKHUTreeDB::WriteAnchor(const uint256& anchor, const SaplingMerkleTree& tree)
{
    return Write(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_ANCHOR, anchor)), tree);
}

bool CZKHUTreeDB::ReadAnchor(const uint256& anchor, SaplingMerkleTree& tree) const
{
    return Read(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_ANCHOR, anchor)), tree);
}

// ========== Nullifier Operations ==========

bool CZKHUTreeDB::WriteNullifier(const uint256& nullifier)
{
    return Write(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NULLIFIER, nullifier)), true);
}

bool CZKHUTreeDB::IsNullifierSpent(const uint256& nullifier) const
{
    bool spent = false;
    Read(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NULLIFIER, nullifier)), spent);
    return spent;
}

bool CZKHUTreeDB::EraseNullifier(const uint256& nullifier)
{
    return Erase(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NULLIFIER, nullifier)));
}

// ========== Note Operations ==========

bool CZKHUTreeDB::WriteNote(const uint256& noteId, const ZKHUNoteData& data)
{
    return Write(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NOTE, noteId)), data);
}

bool CZKHUTreeDB::ReadNote(const uint256& noteId, ZKHUNoteData& data) const
{
    return Read(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NOTE, noteId)), data);
}

bool CZKHUTreeDB::EraseNote(const uint256& noteId)
{
    return Erase(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_NOTE, noteId)));
}

// ========== Nullifier → Commitment Mapping Operations ==========

bool CZKHUTreeDB::WriteNullifierMapping(const uint256& nullifier, const uint256& cm)
{
    return Write(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_LOOKUP, nullifier)), cm);
}

bool CZKHUTreeDB::ReadNullifierMapping(const uint256& nullifier, uint256& cm) const
{
    return Read(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_LOOKUP, nullifier)), cm);
}

bool CZKHUTreeDB::EraseNullifierMapping(const uint256& nullifier)
{
    return Erase(std::make_pair(DB_ZKHU_NAMESPACE, std::make_pair(DB_ZKHU_LOOKUP, nullifier)));
}
