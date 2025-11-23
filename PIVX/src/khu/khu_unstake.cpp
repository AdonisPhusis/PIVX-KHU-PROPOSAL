// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_unstake.h"

#include "coins.h"
#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_utxo.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_memo.h"
#include "khu/zkhu_note.h"
#include "logging.h"
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

    // 2. Nullifier présent et not spent
    // TODO: Extract nullifier from tx.vShieldedSpend[0]
    // uint256 nullifier = tx.vShieldedSpend[0].nullifier;
    // if (zkhuDB.IsNullifierSpent(nullifier)) {
    //     return state.DoS(100, error("%s: nullifier already spent", __func__),
    //                     REJECT_INVALID, "bad-unstake-nullifier-spent");
    // }

    // 3. Anchor présent dans zkhuTree
    // TODO: Extract anchor from tx.vShieldedSpend[0]
    // uint256 anchor = tx.vShieldedSpend[0].anchor;
    // SaplingMerkleTree tree;
    // if (!zkhuDB.ReadAnchor(anchor, tree)) {
    //     return state.DoS(100, error("%s: anchor not found", __func__),
    //                     REJECT_INVALID, "bad-unstake-anchor");
    // }

    // 4. zk-proof Sapling valide
    // TODO: Verify tx.vShieldedSpend[0].zkproof using libsodium/sapling verification

    // 5. Décoder memo ZKHU → ZKHUMemo
    // TODO: Decrypt encCiphertext and deserialize
    // ZKHUMemo memo;
    // try {
    //     std::array<unsigned char, 512> memoBytes = DecryptMemo(...);
    //     memo = ZKHUMemo::Deserialize(memoBytes);
    //     if (memcmp(memo.magic, "ZKHU", 4) != 0) {
    //         return state.DoS(100, error("%s: invalid memo magic", __func__),
    //                         REJECT_INVALID, "bad-unstake-memo-magic");
    //     }
    // } catch (const std::exception& e) {
    //     return state.DoS(100, error("%s: memo deserialize failed: %s", __func__, e.what()),
    //                     REJECT_INVALID, "bad-unstake-memo-format");
    // }

    // 6. ⚠️ MATURITY: nHeight - memo.nStakeStartHeight >= 4320 (sinon reject)
    // TODO: Uncomment when memo extraction works
    // if (nHeight - memo.nStakeStartHeight < ZKHU_MATURITY_BLOCKS) {
    //     return state.DoS(100, error("%s: maturity not reached (height=%d, start=%d, required=%d)",
    //                                __func__, nHeight, memo.nStakeStartHeight, ZKHU_MATURITY_BLOCKS),
    //                     REJECT_INVALID, "bad-unstake-maturity");
    // }

    // 7. bonus = note.Ur_accumulated >= 0
    // TODO: Retrieve note from DB
    // ZKHUNoteData noteData;
    // if (!zkhuDB.ReadNote(nullifier, noteData)) {
    //     return state.DoS(100, error("%s: note data not found", __func__),
    //                     REJECT_INVALID, "bad-unstake-note-missing");
    // }
    //
    // CAmount bonus = noteData.Ur_accumulated;  // ✅ PER-NOTE (NOT Ur_now - Ur_at_stake)
    // if (bonus < 0) {
    //     return state.DoS(100, error("%s: negative bonus", __func__),
    //                     REJECT_INVALID, "bad-unstake-bonus-negative");
    // }

    // 8. khuState.Cr >= bonus && khuState.Ur >= bonus
    // TODO: Uncomment when bonus is available
    // if (khuState.Cr < bonus || khuState.Ur < bonus) {
    //     return state.DoS(100, error("%s: insufficient pool (Cr=%d, Ur=%d, bonus=%d)",
    //                                __func__, khuState.Cr, khuState.Ur, bonus),
    //                     REJECT_INVALID, "bad-unstake-insufficient-pool");
    // }

    // 9. vout[0].nValue == amount + bonus
    // TODO: Uncomment when note and bonus are available
    // CAmount expectedOutput = noteData.amount + bonus;
    // if (tx.vout.empty() || tx.vout[0].nValue != expectedOutput) {
    //     return state.DoS(100, error("%s: output amount mismatch (expected=%d, got=%d)",
    //                                __func__, expectedOutput, tx.vout.empty() ? 0 : tx.vout[0].nValue),
    //                     REJECT_INVALID, "bad-unstake-output-amount");
    // }

    LogPrint(BCLog::KHU, "%s: UNSTAKE validation passed (height=%d, TODO: full implementation)\n",
             __func__, nHeight);
    return true;
}

bool ApplyKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // TODO: AssertLockHeld(cs_khu);

    // Retrieve note data
    // TODO: Extract nullifier and retrieve from DB
    // uint256 nullifier = tx.vShieldedSpend[0].nullifier;
    // ZKHUNoteData noteData;
    // if (!zkhuDB.ReadNote(nullifier, noteData)) {
    //     return error("%s: note data not found", __func__);
    // }

    // ✅ CRITICAL: Extract bonus from note (per-note)
    // CAmount bonus = noteData.Ur_accumulated;  // Phase 4: =0, Phase 5+: >0

    // For skeleton, assume bonus = 0 (Phase 4)
    CAmount bonus = 0;  // TODO: Replace with noteData.Ur_accumulated when available

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

    // ✅ Phase 4 comment: bonus=0, net effect zero, but structure ready for Phase 5

    // 5. Mark nullifier as spent (prevent double-spend)
    // TODO: Uncomment when zkhuDB is accessible
    // if (!zkhuDB.WriteNullifier(nullifier)) {
    //     return error("%s: failed to mark nullifier spent", __func__);
    // }

    // 6. Create output KHU_T UTXO (amount + bonus)
    // TODO: Implement
    // AddKHUCoin(view, tx.vout[0], noteData.amount + bonus);

    // 7. ✅ CRITICAL: Verify invariants AFTER mutations
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
    // TODO: AssertLockHeld(cs_khu);

    // Retrieve note data (must still exist for undo)
    // TODO: Extract nullifier and retrieve from DB
    // uint256 nullifier = tx.vShieldedSpend[0].nullifier;
    // ZKHUNoteData noteData;
    // if (!zkhuDB.ReadNote(nullifier, noteData)) {
    //     return error("%s: note data not found", __func__);
    // }

    // ✅ CRITICAL: Extract same bonus as Apply
    // CAmount bonus = noteData.Ur_accumulated;

    // For skeleton, assume bonus = 0 (Phase 4)
    CAmount bonus = 0;  // TODO: Replace with noteData.Ur_accumulated when available

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

    // 5. Unspend nullifier (restore spendable state)
    // TODO: Uncomment when zkhuDB is accessible
    // if (!zkhuDB.EraseNullifier(nullifier)) {
    //     return error("%s: failed to unspend nullifier", __func__);
    // }

    // 6. Remove output KHU_T UTXO
    // TODO: Implement
    // SpendKHUCoin(view, tx.vout[0]);

    // 7. ✅ CRITICAL: Verify invariants AFTER undo
    if (!state.CheckInvariants()) {
        return error("%s: invariant violation after undo UNSTAKE (C=%d, U=%d, Cr=%d, Ur=%d)",
                    __func__, state.C, state.U, state.Cr, state.Ur);
    }

    LogPrint(BCLog::KHU, "%s: Undone UNSTAKE (bonus=%d, C=%d, U=%d, Cr=%d, Ur=%d)\n",
             __func__, bonus, state.C, state.U, state.Cr, state.Ur);

    return true;
}
