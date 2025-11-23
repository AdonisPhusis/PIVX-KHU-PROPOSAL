// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_commitmentdb.h"

#include "util/system.h"

// Database key prefixes for KHU commitments
static const char DB_KHU_COMMITMENT = 'K';          // KHU namespace
static const char DB_KHU_COMMITMENT_PREFIX = 'C';   // Commitment data
static const char DB_KHU_LATEST_FINALIZED = 'L';    // Latest finalized height

CKHUCommitmentDB::CKHUCommitmentDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "khu" / "commitments", nCacheSize, fMemory, fWipe)
{
}

bool CKHUCommitmentDB::WriteCommitment(uint32_t nHeight, const KhuStateCommitment& commitment)
{
    // Verify commitment is valid before writing
    if (!commitment.IsValid()) {
        LogPrint(BCLog::KHU, "KHU: Refusing to write invalid commitment at height %d\n", nHeight);
        return false;
    }

    // Write commitment to DB
    bool result = Write(std::make_pair(DB_KHU_COMMITMENT, std::make_pair(DB_KHU_COMMITMENT_PREFIX, nHeight)), commitment);

    if (result) {
        // If commitment has quorum, update latest finalized height
        if (commitment.HasQuorum()) {
            uint32_t currentLatest = GetLatestFinalizedHeight();
            if (nHeight > currentLatest) {
                SetLatestFinalizedHeight(nHeight);
            }
            LogPrint(BCLog::KHU, "KHU: Finalized commitment at height %d: %s\n",
                     nHeight, commitment.hashState.ToString());
        } else {
            LogPrint(BCLog::KHU, "KHU: Wrote commitment without quorum at height %d\n", nHeight);
        }
    }

    return result;
}

bool CKHUCommitmentDB::ReadCommitment(uint32_t nHeight, KhuStateCommitment& commitment)
{
    return Read(std::make_pair(DB_KHU_COMMITMENT, std::make_pair(DB_KHU_COMMITMENT_PREFIX, nHeight)), commitment);
}

bool CKHUCommitmentDB::HaveCommitment(uint32_t nHeight)
{
    return Exists(std::make_pair(DB_KHU_COMMITMENT, std::make_pair(DB_KHU_COMMITMENT_PREFIX, nHeight)));
}

bool CKHUCommitmentDB::EraseCommitment(uint32_t nHeight)
{
    // Safety check: Don't erase finalized commitments
    uint32_t latestFinalized = GetLatestFinalizedHeight();
    if (nHeight <= latestFinalized) {
        LogPrint(BCLog::KHU, "KHU: Cannot erase finalized commitment at height %d (latest finalized: %d)\n",
                 nHeight, latestFinalized);
        return false;
    }

    bool result = Erase(std::make_pair(DB_KHU_COMMITMENT, std::make_pair(DB_KHU_COMMITMENT_PREFIX, nHeight)));

    if (result) {
        LogPrint(BCLog::KHU, "KHU: Erased commitment at height %d during reorg\n", nHeight);
    }

    return result;
}

uint32_t CKHUCommitmentDB::GetLatestFinalizedHeight()
{
    uint32_t nHeight = 0;
    Read(std::make_pair(DB_KHU_COMMITMENT, DB_KHU_LATEST_FINALIZED), nHeight);
    return nHeight;
}

bool CKHUCommitmentDB::SetLatestFinalizedHeight(uint32_t nHeight)
{
    return Write(std::make_pair(DB_KHU_COMMITMENT, DB_KHU_LATEST_FINALIZED), nHeight);
}

bool CKHUCommitmentDB::IsFinalizedAt(uint32_t nHeight)
{
    // Check if commitment exists
    KhuStateCommitment commitment;
    if (!ReadCommitment(nHeight, commitment)) {
        return false;
    }

    // Check if commitment has quorum
    return commitment.HasQuorum();
}
