// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_UNSTAKE_H
#define PIVX_KHU_UNSTAKE_H

#include "khu/khu_state.h"
#include "primitives/transaction.h"

class CCoinsViewCache;
class CValidationState;
namespace Consensus { struct Params; }

/**
 * UNSTAKE (ZKHU → KHU_T)
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md section 3
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE FONDAMENTALE: UNSTAKE retire ZKHU et crée KHU_T + bonus
 *                     ✅ DOUBLE FLUX: C+=B, U+=B, Cr-=B, Ur-=B (préserve invariants)
 *                     ✅ Phase 4: B=0 (effet net zéro), structure prête pour B>0
 *                     ✅ Maturity: 4320 blocks (3 days) enforced
 */

/**
 * ZKHU_MATURITY_BLOCKS - Mandatory maturity period
 */
static constexpr uint32_t ZKHU_MATURITY_BLOCKS = 4320;  // 3 days (90s per block)

/**
 * CUnstakeKHUPayload - Payload for KHU_UNSTAKE transactions
 *
 * Contains the note commitment (cm) so consensus can look up the note
 * directly without relying on nullifier mapping.
 *
 * Phase 5/8 fix: The Sapling nullifier is derived differently than our
 * deterministic nullifier, so we pass cm explicitly.
 */
struct CUnstakeKHUPayload {
    uint256 cm;  // Note commitment to unstake

    CUnstakeKHUPayload() : cm() {}
    explicit CUnstakeKHUPayload(const uint256& cmIn) : cm(cmIn) {}

    SERIALIZE_METHODS(CUnstakeKHUPayload, obj) {
        READWRITE(obj.cm);
    }

    std::string ToString() const;
};

/**
 * GetUnstakeKHUPayload - Extract payload from UNSTAKE transaction
 */
bool GetUnstakeKHUPayload(const CTransaction& tx, CUnstakeKHUPayload& payload);

/**
 * CheckKHUUnstake - Validation UNSTAKE (Consensus Rules)
 *
 * Checks obligatoires :
 * 1. tx.nType == TxType::KHU_UNSTAKE
 * 2. Nullifier présent et not spent
 * 3. Anchor présent dans zkhuTree (namespace 'K')
 * 4. zk-proof Sapling valide
 * 5. Décoder memo ZKHU → ZKHUMemo
 * 6. ⚠️ MATURITY: nHeight - memo.nStakeStartHeight >= 4320 (sinon reject)
 * 7. bonus = note.Ur_accumulated >= 0
 * 8. khuState.Cr >= bonus && khuState.Ur >= bonus
 * 9. vout[0].nValue == amount + bonus
 *
 * @param[in] tx         Transaction à valider
 * @param[in] view       UTXO set view
 * @param[in] state      Validation state (for error reporting)
 * @param[in] consensus  Consensus parameters
 * @param[in] khuState   KHU global state (for pool checks)
 * @param[in] nHeight    Current block height
 * @return true if validation passes
 */
bool CheckKHUUnstake(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus,
    const KhuGlobalState& khuState,
    int nHeight);

/**
 * ApplyKHUUnstake - Application UNSTAKE (Consensus Critical)
 *
 * RÈGLE ATOMIQUE CRITIQUE — DOUBLE FLUX:
 *   CAmount bonus = noteData.Ur_accumulated;  // ✅ Per-note (NOT Ur_now - Ur_at_stake)
 *
 *   state.U += bonus;   // Supply increases
 *   state.C += bonus;   // Collateral increases
 *   state.Cr -= bonus;  // Pool decreases
 *   state.Ur -= bonus;  // Rights decrease
 *
 * Phase 4: bonus = 0 → effet net zéro (mais structure correcte)
 * Phase 5+: bonus > 0 → transfert économique réel (Cr/Ur → C/U)
 *
 * Side effects:
 * 1. Mark nullifier as spent (prevent double-spend)
 * 2. Create output KHU_T UTXO (amount + bonus)
 * 3. Verify invariants AFTER mutations (C==U, Cr==Ur)
 *
 * @param[in]     tx      Transaction UNSTAKE
 * @param[in,out] view    Coins view (mutated: creates KHU_T UTXO)
 * @param[in,out] state   KHU global state (mutated: C+, U+, Cr-, Ur-)
 * @param[in]     nHeight Block height
 * @return true if application successful
 */
bool ApplyKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight);

/**
 * UndoKHUUnstake - Undo UNSTAKE during reorg (Consensus Critical)
 *
 * RÈGLE CRITIQUE — SYMÉTRIE DOUBLE FLUX:
 *   CAmount bonus = noteData.Ur_accumulated;  // Same bonus as Apply
 *
 *   state.U -= bonus;   // Supply decreases
 *   state.C -= bonus;   // Collateral decreases
 *   state.Cr += bonus;  // Pool increases
 *   state.Ur += bonus;  // Rights increase
 *
 * Reverses ApplyKHUUnstake:
 * 1. Unspend nullifier (EraseNullifier)
 * 2. Remove output KHU_T UTXO (SpendKHUCoin)
 * 3. Reverse double flux (C-, U-, Cr+, Ur+)
 * 4. Verify invariants AFTER undo (C==U, Cr==Ur)
 *
 * @param[in]     tx      Transaction UNSTAKE à annuler
 * @param[in,out] view    Coins view (mutated: removes KHU_T UTXO)
 * @param[in,out] state   KHU global state (mutated: reverse double flux)
 * @return true if undo successful
 */
bool UndoKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight);

#endif // PIVX_KHU_UNSTAKE_H
