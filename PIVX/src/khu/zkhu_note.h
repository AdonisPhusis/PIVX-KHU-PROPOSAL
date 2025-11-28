// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_ZKHU_NOTE_H
#define PIVX_KHU_ZKHU_NOTE_H

#include "amount.h"
#include "serialize.h"
#include "uint256.h"

#include <stdint.h>

/**
 * ZKHUNoteData - Private staking note metadata
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE CRITIQUE:
 * - Ur_accumulated est PER-NOTE (pas global Ur_at_stake)
 * - Phase 4: Ur_accumulated = 0 (no yield yet)
 * - Phase 5+: Ur_accumulated > 0 (incremented by yield engine)
 */
struct ZKHUNoteData
{
    CAmount  amount;              // KHU amount staked (satoshis)
    uint32_t nStakeStartHeight;   // Stake start height
    CAmount  Ur_accumulated;      // Phase 4: always 0, Phase 5: per-note yield
    uint256  nullifier;           // Nullifier of the note
    uint256  cm;                  // Commitment (cmu)
    bool     bSpent;              // BUG #6 FIX: True if note was spent via UNSTAKE

    ZKHUNoteData()
        : amount(0), nStakeStartHeight(0), Ur_accumulated(0), bSpent(false)
    {}

    ZKHUNoteData(CAmount amountIn, uint32_t heightIn, CAmount urIn, const uint256& nullifierIn, const uint256& cmIn)
        : amount(amountIn),
          nStakeStartHeight(heightIn),
          Ur_accumulated(urIn),
          nullifier(nullifierIn),
          cm(cmIn),
          bSpent(false)
    {}

    SERIALIZE_METHODS(ZKHUNoteData, obj)
    {
        READWRITE(obj.amount, obj.nStakeStartHeight, obj.Ur_accumulated);
        READWRITE(obj.nullifier, obj.cm, obj.bSpent);
    }
};

#endif // PIVX_KHU_ZKHU_NOTE_H
