// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_NOTES_H
#define PIVX_KHU_NOTES_H

#include "amount.h"
#include "sapling/sapling.h"
#include "serialize.h"
#include "uint256.h"

#include <array>
#include <stdint.h>

/**
 * ZKHUNoteData - Private staking note metadata
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md section 3.2
 * Phase: 4 (ZKHU Staking)
 *
 * Cette structure représente les métadonnées d'une note ZKHU stakée.
 * Chaque note ZKHU est une note Sapling avec 512 bytes de memo custom.
 *
 * RÈGLE CRITIQUE:
 * - Ur_accumulated est PER-NOTE (pas global Ur_at_stake)
 * - Phase 4: Ur_accumulated = 0 (no yield yet)
 * - Phase 5+: Ur_accumulated > 0 (incremented by yield engine)
 */
struct ZKHUNoteData
{
    CAmount amount;                // Montant KHU_T staké (atoms)
    uint32_t nStakeStartHeight;    // Block height où STAKE a eu lieu
    int64_t Ur_accumulated;        // Reward accumulated per-note (Phase 4: =0, Phase 5+: >0)
    uint256 nullifier;             // Sapling nullifier (unique, prevents double-spend)
    uint256 cm;                    // Sapling commitment (public, in Merkle tree)

    ZKHUNoteData()
    {
        SetNull();
    }

    ZKHUNoteData(CAmount amountIn, uint32_t heightIn, int64_t urIn, const uint256& nullifierIn, const uint256& cmIn)
        : amount(amountIn),
          nStakeStartHeight(heightIn),
          Ur_accumulated(urIn),
          nullifier(nullifierIn),
          cm(cmIn)
    {}

    void SetNull()
    {
        amount = 0;
        nStakeStartHeight = 0;
        Ur_accumulated = 0;
        nullifier.SetNull();
        cm.SetNull();
    }

    bool IsNull() const
    {
        return (amount == 0 && nStakeStartHeight == 0 && nullifier.IsNull());
    }

    /**
     * GetBonus - Calcule le bonus UNSTAKE pour cette note
     *
     * Phase 4: bonus = 0 (Ur_accumulated = 0)
     * Phase 5+: bonus = Ur_accumulated (incremented by daily yield)
     *
     * ⚠️ CRITICAL: Bonus is PER-NOTE, NOT global (Ur_now - Ur_at_stake)
     */
    CAmount GetBonus() const
    {
        return Ur_accumulated;
    }

    SERIALIZE_METHODS(ZKHUNoteData, obj)
    {
        READWRITE(obj.amount);
        READWRITE(obj.nStakeStartHeight);
        READWRITE(obj.Ur_accumulated);  // ✅ PAS Ur_at_stake !
        READWRITE(obj.nullifier);
        READWRITE(obj.cm);
    }
};

/**
 * ZKHUMemo - 512-byte Sapling memo for ZKHU notes
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md section 3.2
 * Phase: 4
 *
 * Format (512 bytes total):
 * - [0:4]   Magic "ZKHU" (4 bytes)
 * - [4:5]   Version (1 byte, currently 1)
 * - [5:9]   nStakeStartHeight (4 bytes LE)
 * - [9:17]  amount (8 bytes LE)
 * - [17:25] Ur_accumulated (8 bytes LE) — ✅ PAS Ur_at_stake !
 * - [25:512] Padding zeros (487 bytes)
 *
 * Total: 4 + 1 + 4 + 8 + 8 + 487 = 512 bytes
 */
struct ZKHUMemo
{
    static constexpr size_t MEMO_SIZE = ZC_MEMO_SIZE;  // 512 bytes
    static constexpr size_t MAGIC_SIZE = 4;
    static constexpr char MAGIC[5] = "ZKHU";
    static constexpr uint8_t VERSION = 1;

    std::array<unsigned char, MEMO_SIZE> data;

    ZKHUMemo()
    {
        data.fill(0);
    }

    /**
     * Encode - Créer memo à partir de ZKHUNoteData
     *
     * ⚠️ CRITICAL: Memo contains Ur_accumulated (per-note), NOT Ur_at_stake (global snapshot)
     */
    static ZKHUMemo Encode(const ZKHUNoteData& noteData);

    /**
     * Decode - Extraire ZKHUNoteData du memo
     *
     * @return Optional<ZKHUNoteData> (None if invalid magic/version)
     */
    static Optional<ZKHUNoteData> Decode(const std::array<unsigned char, MEMO_SIZE>& memo, const uint256& nullifier, const uint256& cm);

    /**
     * Validate - Vérifier magic et version
     */
    bool Validate() const;

    // Accessors
    const unsigned char* raw() const { return data.data(); }
    size_t size() const { return MEMO_SIZE; }

    SERIALIZE_METHODS(ZKHUMemo, obj)
    {
        READWRITE(obj.data);
    }
};

/**
 * Constants
 */
static constexpr uint32_t ZKHU_MATURITY_BLOCKS = 4320;  // 3 days (90s per block)

#endif // PIVX_KHU_NOTES_H
