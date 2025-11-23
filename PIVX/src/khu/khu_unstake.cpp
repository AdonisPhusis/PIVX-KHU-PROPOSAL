// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_unstake.h"

#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_utxo.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_memo.h"
#include "khu/zkhu_note.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "sapling/incrementalmerkletree.h"

bool CheckKHUUnstake(
    const CTransaction& tx,
    const CCoinsViewCache& view,
    CValidationState& state,
    const Consensus::Params& consensus,
    const KhuGlobalState& khuState,
    int nHeight)
{
    // 1. TxType check
    // TODO: Uncomment when TxType::KHU_UNSTAKE is defined
    // if (tx.nType != TxType::KHU_UNSTAKE) {
    //     return state.DoS(100, error("%s: wrong tx type", __func__),
    //                     REJECT_INVALID, "bad-unstake-type");
    // }

    // 2. Validate transaction has Sapling spend data
    if (!tx.sapData) {
        return state.DoS(100, error("%s: UNSTAKE tx missing Sapling data", __func__),
                        REJECT_INVALID, "bad-unstake-no-sapdata");
    }

    if (tx.sapData->vShieldedSpend.empty()) {
        return state.DoS(100, error("%s: UNSTAKE tx has no shielded spends", __func__),
                        REJECT_INVALID, "bad-unstake-no-spend");
    }

    // 3. Extract nullifier from Sapling spend
    const SpendDescription& saplingSpend = tx.sapData->vShieldedSpend[0];
    uint256 nullifier = saplingSpend.nullifier;

    // 4. Access ZKHU database
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        return state.DoS(100, error("%s: ZKHU database not initialized", __func__),
                        REJECT_INVALID, "bad-unstake-no-db");
    }

    // 5. Check nullifier not already spent (double-spend prevention)
    if (zkhuDB->IsNullifierSpent(nullifier)) {
        return state.DoS(100, error("%s: nullifier already spent", __func__),
                        REJECT_INVALID, "bad-unstake-nullifier-spent");
    }

    // 6. Lookup nullifier → cm mapping
    uint256 cm;
    if (!zkhuDB->ReadNullifierMapping(nullifier, cm)) {
        return state.DoS(100, error("%s: nullifier mapping not found", __func__),
                        REJECT_INVALID, "bad-unstake-no-mapping");
    }

    // 7. Read note data
    ZKHUNoteData noteData;
    if (!zkhuDB->ReadNote(cm, noteData)) {
        return state.DoS(100, error("%s: note data not found for cm=%s", __func__, cm.ToString()),
                        REJECT_INVALID, "bad-unstake-note-missing");
    }

    // 8. ⚠️ MATURITY: nHeight - noteData.nStakeStartHeight >= 4320 (sinon reject)
    if (nHeight - noteData.nStakeStartHeight < (int)ZKHU_MATURITY_BLOCKS) {
        return state.DoS(100, error("%s: maturity not reached (height=%d, start=%d, required=%d)",
                                   __func__, nHeight, noteData.nStakeStartHeight, ZKHU_MATURITY_BLOCKS),
                        REJECT_INVALID, "bad-unstake-maturity");
    }

    // 9. bonus = noteData.Ur_accumulated >= 0
    CAmount bonus = noteData.Ur_accumulated;  // ✅ PER-NOTE (NOT Ur_now - Ur_at_stake)
    if (bonus < 0) {
        return state.DoS(100, error("%s: negative bonus", __func__),
                        REJECT_INVALID, "bad-unstake-bonus-negative");
    }

    // 10. khuState.Cr >= bonus && khuState.Ur >= bonus
    if (khuState.Cr < bonus || khuState.Ur < bonus) {
        return state.DoS(100, error("%s: insufficient pool (Cr=%d, Ur=%d, bonus=%d)",
                                   __func__, khuState.Cr, khuState.Ur, bonus),
                        REJECT_INVALID, "bad-unstake-insufficient-pool");
    }

    // 11. vout[0].nValue == amount + bonus
    CAmount expectedOutput = noteData.amount + bonus;
    if (tx.vout.empty() || tx.vout[0].nValue != expectedOutput) {
        return state.DoS(100, error("%s: output amount mismatch (expected=%d, got=%d)",
                                   __func__, expectedOutput, tx.vout.empty() ? 0 : tx.vout[0].nValue),
                        REJECT_INVALID, "bad-unstake-output-amount");
    }

    // TODO Phase 6: Anchor validation
    // TODO Phase 6: zk-proof Sapling verification

    LogPrint(BCLog::KHU, "%s: UNSTAKE validation passed (height=%d, maturity=%d, bonus=%d)\n",
             __func__, nHeight, nHeight - noteData.nStakeStartHeight, bonus);
    return true;
}

bool ApplyKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO Phase 6: AssertLockHeld(cs_khu);

    // 1. Validate transaction has Sapling spend data
    if (!tx.sapData) {
        return error("%s: UNSTAKE tx missing Sapling data", __func__);
    }

    if (tx.sapData->vShieldedSpend.empty()) {
        return error("%s: UNSTAKE tx has no shielded spends", __func__);
    }

    // 2. Extract nullifier from Sapling spend
    const SpendDescription& saplingSpend = tx.sapData->vShieldedSpend[0];
    uint256 nullifier = saplingSpend.nullifier;

    // 3. Access ZKHU database
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        return error("%s: ZKHU database not initialized", __func__);
    }

    // 4. Check nullifier not already spent (double-spend prevention)
    if (zkhuDB->IsNullifierSpent(nullifier)) {
        return error("%s: nullifier already spent", __func__);
    }

    // 5. Lookup nullifier → cm mapping
    uint256 cm;
    if (!zkhuDB->ReadNullifierMapping(nullifier, cm)) {
        return error("%s: nullifier mapping not found", __func__);
    }

    // 6. Read note data
    ZKHUNoteData noteData;
    if (!zkhuDB->ReadNote(cm, noteData)) {
        return error("%s: note data not found for cm=%s", __func__, cm.ToString());
    }

    // 7. ✅ CRITICAL: Extract bonus from note (per-note)
    CAmount bonus = noteData.Ur_accumulated;  // Phase 5: =0, Phase 6+: >0

    // ✅ DOUBLE FLUX — ORDRE CRITIQUE (préserve invariants C==U, Cr==Ur):
    //
    // Phase 4: bonus=0 → effet net zéro (C+=0, U+=0, Cr-=0, Ur-=0)
    // Phase 5+: bonus>0 → transfert économique (reward pool → supply)
    //
    // RATIONALE: L'UNSTAKE retire un droit (Ur) et le matérialise en supply (U).
    //            Le collateral doit suivre (C+ pour backing, Cr- pour release).

    // 1. Supply increases (U+)
    // TODO: Add SafeAdd when available
    // if (!SafeAdd(state.U, bonus, state.U)) {
    //     return error("%s: overflow U (U=%d, bonus=%d)", __func__, state.U, bonus);
    // }
    state.U += bonus;

    // 2. Collateral increases (C+) — ADJACENT à U+
    // TODO: Add SafeAdd when available
    // if (!SafeAdd(state.C, bonus, state.C)) {
    //     return error("%s: overflow C (C=%d, bonus=%d)", __func__, state.C, bonus);
    // }
    state.C += bonus;

    // 3. Reward pool decreases (Cr-)
    if (state.Cr < bonus) {
        return error("%s: insufficient Cr (Cr=%d, bonus=%d)", __func__, state.Cr, bonus);
    }
    state.Cr -= bonus;

    // 4. Unstake rights decrease (Ur-)
    if (state.Ur < bonus) {
        return error("%s: insufficient Ur (Ur=%d, bonus=%d)", __func__, state.Ur, bonus);
    }
    state.Ur -= bonus;

    // ✅ Phase 5: bonus=0, net effect zero, but structure ready for Phase 6+

    // 8. Mark nullifier as spent (prevent double-spend)
    if (!zkhuDB->WriteNullifier(nullifier)) {
        return error("%s: failed to mark nullifier spent", __func__);
    }

    // 9. Keep nullifier mapping (needed for undo)
    // Phase 5: Keep mapping even after spend to support reorg undo
    // The nullifier spent flag prevents double-spend
    // TODO Phase 6: Consider using undo data instead

    // 10. Keep note in database for undo (Phase 5)
    // Note: In Phase 5, we keep the note so UndoKHUUnstake can read bonus
    // The nullifier spent flag prevents double-spend
    // TODO Phase 6: Use proper undo data instead of keeping notes
    // if (!zkhuDB->EraseNote(cm)) {
    //     return error("%s: failed to erase spent note", __func__);
    // }

    // 11. Create output KHU_T UTXO (amount + bonus)
    if (tx.vout.empty()) {
        return error("%s: UNSTAKE tx has no outputs", __func__);
    }

    // Verify the output amount matches expected value
    CAmount expectedOutput = noteData.amount + bonus;
    if (tx.vout[0].nValue != expectedOutput) {
        return error("%s: output amount mismatch (expected=%d, got=%d)",
                    __func__, expectedOutput, tx.vout[0].nValue);
    }

    // Add the output UTXO to the coins view
    // In ConnectBlock this happens automatically, but in Apply we must do it explicitly
    Coin newcoin(tx.vout[0], nHeight, false, false);  // not coinbase, not coinstake
    view.AddCoin(COutPoint(tx.GetHash(), 0), std::move(newcoin), false);

    // 12. ✅ CRITICAL: Verify invariants AFTER mutations
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after UNSTAKE (C=%d, U=%d, Cr=%d, Ur=%d)",
                    __func__, state.C, state.U, state.Cr, state.Ur);
    }

    LogPrint(BCLog::KHU, "%s: Applied UNSTAKE (bonus=%d, height=%d, C=%d, U=%d, Cr=%d, Ur=%d)\n",
             __func__, bonus, nHeight, state.C, state.U, state.Cr, state.Ur);

    return true;
}

bool UndoKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO Phase 6: AssertLockHeld(cs_khu);

    // 1. Validate transaction structure
    if (!tx.sapData || tx.sapData->vShieldedSpend.empty()) {
        return error("%s: invalid UNSTAKE tx in undo", __func__);
    }

    // 2. Extract nullifier from Sapling spend
    const SpendDescription& saplingSpend = tx.sapData->vShieldedSpend[0];
    uint256 nullifier = saplingSpend.nullifier;

    // 3. Access ZKHU database
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        return error("%s: ZKHU database not initialized", __func__);
    }

    // 4. Lookup nullifier → cm mapping
    uint256 cm;
    if (!zkhuDB->ReadNullifierMapping(nullifier, cm)) {
        return error("%s: nullifier mapping not found for undo", __func__);
    }

    // 5. Read note data to get the REAL bonus (CRITICAL for Phase 5)
    ZKHUNoteData noteData;
    if (!zkhuDB->ReadNote(cm, noteData)) {
        return error("%s: note data not found for cm=%s", __func__, cm.ToString());
    }

    // 6. Extract bonus from note (same as ApplyKHUUnstake)
    CAmount bonus = noteData.Ur_accumulated;  // Phase 5: may be >0
    CAmount noteAmount = noteData.amount;

    // ✅ REVERSE DOUBLE FLUX — SYMÉTRIE CRITIQUE:
    //
    // Undo operates in REVERSE order of Apply to restore exact state.

    // 1. Supply decreases (U-) — reverse of U+
    if (state.U < bonus) {
        return error("%s: underflow U (U=%d, bonus=%d)", __func__, state.U, bonus);
    }
    state.U -= bonus;

    // 2. Collateral decreases (C-) — reverse of C+
    if (state.C < bonus) {
        return error("%s: underflow C (C=%d, bonus=%d)", __func__, state.C, bonus);
    }
    state.C -= bonus;

    // 3. Reward pool increases (Cr+) — reverse of Cr-
    // TODO: Add SafeAdd when available
    // if (!SafeAdd(state.Cr, bonus, state.Cr)) {
    //     return error("%s: overflow Cr (Cr=%d, bonus=%d)", __func__, state.Cr, bonus);
    // }
    state.Cr += bonus;

    // 4. Unstake rights increase (Ur+) — reverse of Ur-
    // TODO: Add SafeAdd when available
    // if (!SafeAdd(state.Ur, bonus, state.Ur)) {
    //     return error("%s: overflow Ur (Ur=%d, bonus=%d)", __func__, state.Ur, bonus);
    // }
    state.Ur += bonus;

    // 7. Note is already in DB (we didn't erase it in Apply)
    // We just need to unmark the nullifier as spent
    // The note already has the correct Ur_accumulated and all other fields

    // 8. Nullifier mapping is already in DB (kept by Apply)
    // No need to restore it

    // 9. Unspend nullifier (erase from spent set)
    if (!zkhuDB->EraseNullifier(nullifier)) {
        return error("%s: failed to unspend nullifier", __func__);
    }

    // 10. Remove the output UTXO that was created in ApplyKHUUnstake
    COutPoint unstakeOutput(tx.GetHash(), 0);
    view.SpendCoin(unstakeOutput);

    // 10. ✅ CRITICAL: Verify invariants AFTER undo
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after undo UNSTAKE (C=%d, U=%d, Cr=%d, Ur=%d)",
                    __func__, state.C, state.U, state.Cr, state.Ur);
    }

    LogPrint(BCLog::KHU, "%s: Undone UNSTAKE (bonus=%d, C=%d, U=%d, Cr=%d, Ur=%d)\n",
             __func__, bonus, state.C, state.U, state.Cr, state.Ur);

    return true;
}
