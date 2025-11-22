// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_statedb.h"

#include "util/system.h"

static const char DB_KHU_STATE = 'K';
static const char DB_KHU_STATE_PREFIX = 'S';

CKHUStateDB::CKHUStateDB(size_t nCacheSize, bool fMemory, bool fWipe) :
    CDBWrapper(GetDataDir() / "khu" / "state", nCacheSize, fMemory, fWipe)
{
}

bool CKHUStateDB::WriteKHUState(int nHeight, const KhuGlobalState& state)
{
    return Write(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)), state);
}

bool CKHUStateDB::ReadKHUState(int nHeight, KhuGlobalState& state)
{
    return Read(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)), state);
}

bool CKHUStateDB::ExistsKHUState(int nHeight)
{
    return Exists(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)));
}

bool CKHUStateDB::EraseKHUState(int nHeight)
{
    return Erase(std::make_pair(DB_KHU_STATE, std::make_pair(DB_KHU_STATE_PREFIX, nHeight)));
}

KhuGlobalState CKHUStateDB::LoadKHUState_OrGenesis(int nHeight)
{
    KhuGlobalState state;

    if (ReadKHUState(nHeight, state)) {
        return state;
    }

    // Return genesis state if not found
    state.SetNull();
    state.nHeight = nHeight;
    return state;
}
