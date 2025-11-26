// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_stake.h"

#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "hash.h"
#include "khu/khu_coins.h"
#include "khu/khu_utxo.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_memo.h"
#include "khu/zkhu_note.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "sapling/incrementalmerkletree.h"
#include "sync.h"
#include "utilmoneystr.h"

// External lock (defined in khu_validation.cpp)
extern RecursiveMutex cs_khu;

bool CheckKHUStake(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus)
{
    // 1. TxType check
    if (tx.nType != CTransaction::TxType::KHU_STAKE) {
        return state.DoS(100, error("%s: wrong tx type (got %d, expected KHU_STAKE)",
                                   __func__, (int)tx.nType),
                        REJECT_INVALID, "bad-stake-type");
    }

    // 2. Input KHU_T UTXO exists, amount > 0
    if (tx.vin.empty()) {
        return state.DoS(100, error("%s: no inputs", __func__),
                        REJECT_INVALID, "bad-stake-no-inputs");
    }

    // 3. Verify input is KHU_T UTXO and get amount
    const COutPoint& prevout = tx.vin[0].prevout;
    CKHUUTXO khuCoin;
    if (!GetKHUCoin(view, prevout, khuCoin)) {
        return state.DoS(100, error("%s: input not KHU_T at %s", __func__, prevout.ToString()),
                        REJECT_INVALID, "bad-stake-input-type");
    }

    // 4. Verify amount > 0
    if (khuCoin.amount <= 0) {
        return state.DoS(100, error("%s: invalid amount %d", __func__, khuCoin.amount),
                        REJECT_INVALID, "bad-stake-amount");
    }

    // 5. Input KHU_T not already staked (fStaked == false)
    if (khuCoin.fStaked) {
        return state.DoS(100, error("%s: input already staked at %s", __func__, prevout.ToString()),
                        REJECT_INVALID, "bad-stake-already-staked");
    }

    // 6. Sapling output present (exactly 1 note ZKHU)
    if (!tx.sapData) {
        return state.DoS(100, error("%s: missing Sapling data", __func__),
                        REJECT_INVALID, "bad-stake-no-sapdata");
    }

    if (tx.sapData->vShieldedOutput.size() != 1) {
        return state.DoS(100, error("%s: must have exactly 1 shielded output (got %zu)",
                                   __func__, tx.sapData->vShieldedOutput.size()),
                        REJECT_INVALID, "bad-stake-output-count");
    }

    // 7. Transparent outputs allowed only for change (KHU_T change back to user)
    // All transparent outputs must be to KHU addresses (P2PKH with KHU flag)
    // This allows partial stakes with change
    for (const auto& out : tx.vout) {
        if (out.nValue <= 0) {
            return state.DoS(100, error("%s: invalid transparent output value", __func__),
                            REJECT_INVALID, "bad-stake-output-value");
        }
        // Note: Change outputs will be tracked as KHU_T by consensus rules
        // They inherit KHU status from the inputs
    }

    LogPrint(BCLog::KHU, "%s: STAKE validation passed (amount=%d)\n", __func__, khuCoin.amount);
    return true;
}

bool ApplyKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // CRITICAL: cs_khu MUST be held to prevent race conditions
    AssertLockHeld(cs_khu);

    // 1. Validate transaction has Sapling data
    if (!tx.sapData) {
        return error("%s: STAKE tx missing Sapling data", __func__);
    }

    if (tx.sapData->vShieldedOutput.empty()) {
        return error("%s: STAKE tx has no shielded outputs", __func__);
    }

    // 2. Get input amount from the transaction outputs
    // NOTE: Standard tx validation already spent the UTXO from the view.
    // We get the amount from the Sapling output + fee calculation
    if (tx.vin.empty()) {
        return error("%s: STAKE tx has no inputs", __func__);
    }

    // 2. Get the staked amount from Sapling valueBalance
    // valueBalance = sum(spends) - sum(outputs)
    // For STAKE: no spends, one output → valueBalance = -stakedAmount
    // So stakedAmount = -valueBalance
    CAmount amount = -tx.sapData->valueBalance;
    if (amount <= 0) {
        return error("%s: invalid stake amount from valueBalance: %d", __func__, amount);
    }

    LogPrint(BCLog::KHU, "%s: Stake amount from valueBalance: %d satoshis\n", __func__, amount);

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

    // 8. ✅ CRITICAL: Spend KHU inputs from mapKHUUTXOs
    // STAKE tx has KHU_T inputs that need to be spent in consensus tracking
    for (const auto& in : tx.vin) {
        CKHUUTXO khuCoin;
        if (GetKHUCoin(view, in.prevout, khuCoin)) {
            // This is a KHU input - spend it from tracking
            if (!SpendKHUCoin(view, in.prevout)) {
                return error("%s: failed to spend KHU coin at %s", __func__, in.prevout.ToString());
            }
            LogPrint(BCLog::KHU, "%s: spent KHU input %s:%d value=%s\n",
                     __func__, in.prevout.hash.ToString().substr(0,16).c_str(), in.prevout.n,
                     FormatMoney(khuCoin.amount));
        }
        // Non-KHU inputs (PIV fee) are skipped - they're not in mapKHUUTXOs
    }

    // 9. ✅ CRITICAL: Add KHU_T change output to mapKHUUTXOs (if any)
    // STAKE tx may have a KHU change output at index 0 (before Sapling outputs)
    // The wallet's khustake creates: output[0] = KHU change, then Sapling data
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        const CTxOut& out = tx.vout[i];
        // Skip dust outputs and OP_RETURN
        if (out.nValue > 0 && !out.scriptPubKey.IsUnspendable()) {
            // This could be KHU change - add it to tracking
            CKHUUTXO newCoin(out.nValue, out.scriptPubKey, nHeight);
            newCoin.fIsKHU = true;
            newCoin.fStaked = false;
            newCoin.nStakeStartHeight = 0;

            COutPoint khuOutpoint(tx.GetHash(), i);
            if (!AddKHUCoin(view, khuOutpoint, newCoin)) {
                return error("%s: failed to add KHU change coin", __func__);
            }
            LogPrint(BCLog::KHU, "%s: created KHU change %s:%d value=%s\n",
                     __func__, khuOutpoint.hash.ToString().substr(0,16).c_str(), khuOutpoint.n,
                     FormatMoney(out.nValue));
        }
    }

    // 10. ✅ CRITICAL: STAKE = form conversion (KHU_T → ZKHU)
    // Mutations atomiques: U -= amount, Z += amount
    // C reste inchangé (collateral total identique)
    // Invariant C == U + Z préservé car: C = (U - amount) + (Z + amount)

    if (state.U < amount) {
        return error("%s: insufficient U for STAKE (U=%d, amount=%d)", __func__, state.U, amount);
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE (U et Z ensemble)
    // ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    // ═══════════════════════════════════════════════════════════
    state.U -= amount;  // Retirer du transparent
    state.Z += amount;  // Ajouter au shielded

    // Verify invariants
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after STAKE (C=%d U=%d Z=%d)",
                    __func__, state.C, state.U, state.Z);
    }

    LogPrint(BCLog::KHU, "%s: Applied STAKE at height %d (cm=%s, amount=%d, U=%d, Z=%d)\n",
             __func__, nHeight, cm.ToString(), amount, state.U, state.Z);

    return true;
}

bool UndoKHUStake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // CRITICAL: cs_khu MUST be held to prevent race conditions
    AssertLockHeld(cs_khu);

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

    // 4. ✅ CRITICAL: Remove KHU change outputs that were added in ApplyKHUStake
    // This reverses the AddKHUCoin() calls for change outputs
    for (size_t i = 0; i < tx.vout.size(); ++i) {
        const CTxOut& out = tx.vout[i];
        if (out.nValue > 0 && !out.scriptPubKey.IsUnspendable()) {
            COutPoint khuOutpoint(tx.GetHash(), i);
            if (HaveKHUCoin(view, khuOutpoint)) {
                if (!SpendKHUCoin(view, khuOutpoint)) {
                    return error("%s: failed to remove KHU change coin at %s", __func__, khuOutpoint.ToString());
                }
                LogPrint(BCLog::KHU, "%s: removed KHU change %s:%d\n",
                         __func__, khuOutpoint.hash.ToString().substr(0,16).c_str(), khuOutpoint.n);
            }
        }
    }

    // 4b. NOTE: KHU input restoration requires proper undo data storage (Phase 6+).
    // For Phase 2, the KHU inputs were marked spent in ApplyKHUStake but cannot be
    // fully restored here. The standard UTXO view restores them via ApplyTxInUndo(),
    // and subsequent operations should use wallet data for KHU selection.

    // 5. Erase ZKHU note from database
    if (!zkhuDB->EraseNote(cm)) {
        return error("%s: failed to erase note", __func__);
    }

    // 6. Erase nullifier mapping
    if (!zkhuDB->EraseNullifierMapping(noteData.nullifier)) {
        return error("%s: failed to erase nullifier mapping", __func__);
    }

    // 7. ✅ CRITICAL: Reverse STAKE mutations (U += amount, Z -= amount)
    CAmount amount = noteData.amount;

    if (state.Z < amount) {
        return error("%s: insufficient Z for undo STAKE (Z=%d, amount=%d)", __func__, state.Z, amount);
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE REVERSE (U et Z ensemble)
    // ═══════════════════════════════════════════════════════════
    state.U += amount;  // Restaurer au transparent
    state.Z -= amount;  // Retirer du shielded

    // Verify invariants
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after undo STAKE (C=%d U=%d Z=%d)",
                    __func__, state.C, state.U, state.Z);
    }

    LogPrint(BCLog::KHU, "%s: Undone STAKE at height %d (cm=%s, amount=%d, U=%d, Z=%d)\n",
             __func__, nHeight, cm.ToString(), amount, state.U, state.Z);

    return true;
}
