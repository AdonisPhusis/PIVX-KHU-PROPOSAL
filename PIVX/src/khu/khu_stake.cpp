// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_stake.h"

#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_utxo.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_memo.h"
#include "khu/zkhu_note.h"
#include "logging.h"
#include "sapling/incrementalmerkletree.h"

bool CheckKHUStake(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus)
{
    // 1. TxType check
    // TODO: Uncomment when TxType::KHU_STAKE is defined in transaction.h
    // if (tx.nType != TxType::KHU_STAKE) {
    //     return state.DoS(100, error("%s: wrong tx type", __func__),
    //                     REJECT_INVALID, "bad-stake-type");
    // }

    // 2. Input KHU_T UTXO exists, amount > 0
    if (tx.vin.empty()) {
        return state.DoS(100, error("%s: no inputs", __func__),
                        REJECT_INVALID, "bad-stake-no-inputs");
    }

    // TODO: Verify input is KHU_T UTXO
    // const Coin& coin = view.AccessCoin(tx.vin[0].prevout);
    // if (coin.IsSpent() || !IsKHUCoin(coin.out)) {
    //     return state.DoS(100, error("%s: input not KHU_T", __func__),
    //                     REJECT_INVALID, "bad-stake-input-type");
    // }

    // TODO: Verify amount > 0
    // CAmount amount = coin.out.nValue;
    // if (!consensus.MoneyRange(amount)) {
    //     return state.DoS(100, error("%s: invalid amount", __func__),
    //                     REJECT_INVALID, "bad-stake-amount");
    // }

    // 3. Input KHU_T not already staked (fStaked == false)
    // TODO: Check CKHUUTXO::fStaked flag
    // if (khuUtxo.fStaked) {
    //     return state.DoS(100, error("%s: input already staked", __func__),
    //                     REJECT_INVALID, "bad-stake-already-staked");
    // }

    // 4. Sapling output present (1 note ZKHU)
    // TODO: Check tx.vShieldedOutput has exactly 1 output
    // if (tx.vShieldedOutput.size() != 1) {
    //     return state.DoS(100, error("%s: must have 1 shielded output", __func__),
    //                     REJECT_INVALID, "bad-stake-output-count");
    // }

    // 5. Memo 512 bytes → ZKHUMemo::Deserialize OK, magic == "ZKHU"
    // TODO: Decrypt memo and validate
    // try {
    //     ZKHUMemo memo = ZKHUMemo::Deserialize(memoBytes);
    //     if (memcmp(memo.magic, "ZKHU", 4) != 0) {
    //         return state.DoS(100, error("%s: invalid memo magic", __func__),
    //                         REJECT_INVALID, "bad-stake-memo-magic");
    //     }
    // } catch (const std::exception& e) {
    //     return state.DoS(100, error("%s: memo deserialize failed: %s", __func__, e.what()),
    //                     REJECT_INVALID, "bad-stake-memo-format");
    // }

    // 6. nStakeStartHeight cohérent
    // TODO: Verify memo.nStakeStartHeight is reasonable (e.g., close to current height)

    LogPrint(BCLog::KHU, "%s: STAKE validation passed (TODO: full implementation)\n", __func__);
    return true;
}

bool ApplyKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO: AssertLockHeld(cs_khu);

    // 1. SpendKHUCoin(view, prevout)
    // TODO: Implement
    // SpendKHUCoin(view, tx.vin[0].prevout);

    // 2. Extraire la note ZKHU (commitment + nullifier)
    // TODO: Extract from tx.vShieldedOutput[0]
    // const OutputDescription& out = tx.vShieldedOutput[0];
    // uint256 cm = out.cmu;
    // uint256 nullifier = CalculateNullifier(...);  // Sapling nullifier derivation

    // 3. Construire ZKHUNoteData (Ur_accumulated = 0 Phase 4)
    // TODO: Get amount from input UTXO
    // CAmount amount = ...;
    // ZKHUNoteData noteData(
    //     amount,
    //     nHeight,  // nStakeStartHeight
    //     0,        // Ur_accumulated = 0 (Phase 4)
    //     nullifier,
    //     cm
    // );

    // 4. WriteNote(cm, noteData) dans DB ZKHU
    // TODO: Access global zkhuDB and write
    // if (!zkhuDB.WriteNote(cm, noteData)) {
    //     return error("%s: failed to write note", __func__);
    // }

    // 5. zkhuTree.append(cm) + WriteAnchor(root, zkhuTree)
    // TODO: Access global zkhuTree and append
    // zkhuTree.append(cm);
    // uint256 newRoot = zkhuTree.root();
    // if (!zkhuDB.WriteAnchor(newRoot, zkhuTree)) {
    //     return error("%s: failed to write anchor", __func__);
    // }

    // 6. ✅ CRITICAL: Ne pas toucher à state.C/U/Cr/Ur
    // Phase 4: STAKE is pure form conversion (T→Z), no economic effect

    // Verify invariants (should still hold since state unchanged)
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after STAKE", __func__);
    }

    LogPrint(BCLog::KHU, "%s: Applied STAKE (height=%d, TODO: full implementation)\n",
             __func__, nHeight);

    return true;
}

bool UndoKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO: AssertLockHeld(cs_khu);

    // 1. Recréer l'UTXO KHU_T initial (AddKHUCoin)
    // TODO: Implement
    // AddKHUCoin(view, tx.vin[0].prevout, amount);

    // 2. Supprimer la note ZKHU (EraseNote)
    // TODO: Extract cm from tx.vShieldedOutput[0]
    // uint256 cm = tx.vShieldedOutput[0].cmu;
    // if (!zkhuDB.EraseNote(cm)) {
    //     return error("%s: failed to erase note", __func__);
    // }

    // 3. Ne pas toucher à C/U/Cr/Ur
    // (No state mutations to undo)

    // Verify invariants
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after undo STAKE", __func__);
    }

    LogPrint(BCLog::KHU, "%s: Undone STAKE (TODO: full implementation)\n", __func__);

    return true;
}
