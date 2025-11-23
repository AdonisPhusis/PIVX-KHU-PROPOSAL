// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_stake.h"

#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "hash.h"
#include "khu/khu_utxo.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_memo.h"
#include "khu/zkhu_note.h"
#include "logging.h"
#include "primitives/transaction.h"
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
    // TODO Phase 6: AssertLockHeld(cs_khu);

    // 1. Validate transaction has Sapling data
    if (!tx.sapData) {
        return error("%s: STAKE tx missing Sapling data", __func__);
    }

    if (tx.sapData->vShieldedOutput.empty()) {
        return error("%s: STAKE tx has no shielded outputs", __func__);
    }

    // 2. Spend input KHU_T UTXO
    if (tx.vin.empty()) {
        return error("%s: STAKE tx has no inputs", __func__);
    }

    const COutPoint& prevout = tx.vin[0].prevout;
    const Coin& coin = view.AccessCoin(prevout);
    if (coin.IsSpent()) {
        return error("%s: input already spent", __func__);
    }

    CAmount amount = coin.out.nValue;
    view.SpendCoin(prevout);  // Consume the KHU_T UTXO

    // 3. Extract Sapling output (commitment)
    const OutputDescription& saplingOut = tx.sapData->vShieldedOutput[0];
    uint256 cm = saplingOut.cmu;  // Commitment = noteId

    // 4. Calculate deterministic nullifier (Phase 5 simplification)
    // Phase 6+: Use real Sapling nullifier derivation
    CHashWriter ss(SER_GETHASH, 0);
    ss << cm;
    ss << std::string("ZKHU-NULLIFIER-V1");
    uint256 nullifier = ss.GetHash();

    // 5. Create ZKHU note data (Ur_accumulated = 0 in Phase 5)
    ZKHUNoteData noteData(
        amount,
        nHeight,      // nStakeStartHeight
        0,            // Ur_accumulated = 0 (Phase 5: no yield yet)
        nullifier,
        cm
    );

    // 6. Write to ZKHU database
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        return error("%s: ZKHU database not initialized", __func__);
    }

    if (!zkhuDB->WriteNote(cm, noteData)) {
        return error("%s: failed to write note to DB", __func__);
    }

    // 7. Write nullifier → cm mapping (for UNSTAKE lookup)
    if (!zkhuDB->WriteNullifierMapping(nullifier, cm)) {
        return error("%s: failed to write nullifier mapping", __func__);
    }

    // TODO Phase 6: Update Merkle tree
    // zkhuTree.append(cm);
    // uint256 newRoot = zkhuTree.root();
    // if (!zkhuDB->WriteAnchor(newRoot, zkhuTree)) {
    //     return error("%s: failed to write anchor", __func__);
    // }

    // 8. ✅ CRITICAL: Ne pas toucher à state.C/U/Cr/Ur
    // Phase 4/5: STAKE is pure form conversion (T→Z), no economic effect

    // Verify invariants (should still hold since state unchanged)
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after STAKE", __func__);
    }

    LogPrint(BCLog::KHU, "%s: Applied STAKE at height %d (cm=%s, amount=%d)\n",
             __func__, nHeight, cm.ToString(), amount);

    return true;
}

bool UndoKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO Phase 6: AssertLockHeld(cs_khu);

    // 1. Validate transaction structure
    if (!tx.sapData || tx.sapData->vShieldedOutput.empty()) {
        return error("%s: invalid STAKE tx in undo", __func__);
    }

    if (tx.vin.empty()) {
        return error("%s: STAKE tx has no inputs in undo", __func__);
    }

    // 2. Extract commitment (note ID)
    uint256 cm = tx.sapData->vShieldedOutput[0].cmu;

    // 3. Read note to get amount (for UTXO recreation)
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        return error("%s: ZKHU database not initialized", __func__);
    }

    ZKHUNoteData noteData;
    if (!zkhuDB->ReadNote(cm, noteData)) {
        return error("%s: failed to read note for undo", __func__);
    }

    // 4. Recreate the KHU_T UTXO (undo the spend)
    const COutPoint& prevout = tx.vin[0].prevout;
    CScript scriptPubKey;  // TODO Phase 6: Extract from original UTXO
    Coin coin;
    coin.out = CTxOut(noteData.amount, scriptPubKey);
    coin.nHeight = nHeight - 1;
    view.AddCoin(prevout, std::move(coin), false);

    // 5. Erase ZKHU note from database
    if (!zkhuDB->EraseNote(cm)) {
        return error("%s: failed to erase note", __func__);
    }

    // 6. Erase nullifier mapping
    if (!zkhuDB->EraseNullifierMapping(noteData.nullifier)) {
        return error("%s: failed to erase nullifier mapping", __func__);
    }

    // 7. Ne pas toucher à C/U/Cr/Ur (no state mutations to undo)

    // Verify invariants
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after undo STAKE", __func__);
    }

    LogPrint(BCLog::KHU, "%s: Undone STAKE at height %d (cm=%s)\n",
             __func__, nHeight, cm.ToString());

    return true;
}
