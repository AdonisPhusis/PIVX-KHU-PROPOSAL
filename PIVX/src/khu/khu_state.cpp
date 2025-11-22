// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_state.h"

#include "hash.h"
#include "streams.h"

uint256 KhuGlobalState::GetHash() const
{
    CHashWriter ss(SER_GETHASH, 0);
    ss << *this;
    return ss.GetHash();
}
