// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_STAKE_H
#define PIVX_KHU_STAKE_H

#include "khu/khu_state.h"
#include "primitives/transaction.h"

class CCoinsViewCache;
class CValidationState;
namespace Consensus { struct Params; }

/**
 * STAKE (KHU_T → ZKHU)
 *
 * Source: docs/blueprints/05-ZKHU-SAPLING-STAKE.md section 2
 * Phase: 4 (ZKHU Staking)
 *
 * RÈGLE FONDAMENTALE: STAKE transforme KHU_T en ZKHU (Sapling note)
 *                     État global INCHANGÉ (C, U, Cr, Ur constants)
 *                     Seul le "form" change: transparent → shielded
 */

/**
 * CheckKHUStake - Validation STAKE (Consensus Rules)
 *
 * Checks obligatoires :
 * 1. tx.nType == TxType::KHU_STAKE
 * 2. Input KHU_T UTXO existe, amount > 0
 * 3. Input KHU_T non staké (fStaked == false)
 * 4. Sapling output présent (1 note ZKHU)
 * 5. Memo 512 bytes → ZKHUMemo::Deserialize OK, magic == "ZKHU"
 * 6. nStakeStartHeight cohérent
 * 7. Aucune modification de C/U/Cr/Ur
 *
 * @param[in] tx         Transaction à valider
 * @param[in] view       UTXO set view
 * @param[in] state      Validation state (for error reporting)
 * @param[in] consensus  Consensus parameters
 * @return true if validation passes
 */
bool CheckKHUStake(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus);

/**
 * ApplyKHUStake - Application STAKE (Consensus Critical)
 *
 * Implémentation (logique) :
 * 1. SpendKHUCoin(view, prevout)
 * 2. Extraire la note ZKHU (commitment + nullifier)
 * 3. Construire ZKHUNoteData (Ur_accumulated = 0 Phase 4)
 * 4. WriteNote(cm, noteData) dans DB ZKHU
 * 5. zkhuTree.append(cm) + WriteAnchor(root, zkhuTree)
 * 6. Ne pas toucher à state.C/U/Cr/Ur
 *
 * @param[in]     tx      Transaction STAKE
 * @param[in,out] view    Coins view (mutated: spends KHU_T)
 * @param[in,out] state   KHU global state (unchanged: C, U, Cr, Ur)
 * @param[in]     nHeight Block height
 * @return true if application successful
 */
bool ApplyKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight);

/**
 * UndoKHUStake - Undo STAKE during reorg (Consensus Critical)
 *
 * Reverses ApplyKHUStake:
 * 1. Recréer l'UTXO KHU_T initial (AddKHUCoin)
 * 2. Supprimer la note ZKHU (EraseNote)
 * 3. Ne pas toucher à C/U/Cr/Ur
 *
 * @param[in]     tx      Transaction STAKE à annuler
 * @param[in,out] view    Coins view (mutated: restores KHU_T)
 * @param[in,out] state   KHU global state (unchanged)
 * @return true if undo successful
 */
bool UndoKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight);

#endif // PIVX_KHU_STAKE_H
