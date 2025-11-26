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
#include "sync.h"

#include <limits>

// External lock (defined in khu_validation.cpp)
extern RecursiveMutex cs_khu;

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
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

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

    // 7. ✅ CRITICAL: Extract P (principal) and Y (yield) from note
    CAmount P = noteData.amount;              // Principal staked
    CAmount Y = noteData.Ur_accumulated;      // Yield accumulated

    // ═══════════════════════════════════════════════════════════════════════
    // 5 MUTATIONS ATOMIQUES — ORDRE CRITIQUE (préserve invariants C==U+Z, Cr==Ur)
    //
    // Avant:  C = U + Z,  Cr = Ur
    // Après:  C' = U' + Z',  Cr' = Ur'
    //
    // Mutations:
    //   Z  -= P        →  Z' = Z - P
    //   U  += P + Y    →  U' = U + P + Y
    //   C  += Y        →  C' = C + Y
    //   Cr -= Y        →  Cr' = Cr - Y
    //   Ur -= Y        →  Ur' = Ur - Y
    //
    // Vérification invariant C == U + Z:
    //   C' = C + Y
    //   U' + Z' = (U + P + Y) + (Z - P) = U + Z + Y = C + Y = C' ✓
    // ═══════════════════════════════════════════════════════════════════════

    // Pre-checks: underflow/overflow protection
    if (state.Z < P) {
        return error("%s: insufficient Z (Z=%d, P=%d)", __func__, state.Z, P);
    }
    if (state.U > (std::numeric_limits<CAmount>::max() - P - Y)) {
        return error("%s: overflow would occur on U (U=%d, P=%d, Y=%d)", __func__, state.U, P, Y);
    }
    if (state.C > (std::numeric_limits<CAmount>::max() - Y)) {
        return error("%s: overflow would occur on C (C=%d, Y=%d)", __func__, state.C, Y);
    }
    if (state.Cr < Y) {
        return error("%s: insufficient Cr (Cr=%d, Y=%d)", __func__, state.Cr, Y);
    }
    if (state.Ur < Y) {
        return error("%s: insufficient Ur (Ur=%d, Y=%d)", __func__, state.Ur, Y);
    }

    // ═══════════════════════════════════════════════════════════
    // 5 MUTATIONS ATOMIQUES — AUCUN CODE ENTRE CES LIGNES
    // ═══════════════════════════════════════════════════════════
    state.Z  -= P;          // (1) Principal retiré du shielded
    state.U  += P + Y;      // (2) Principal + Yield vers transparent
    state.C  += Y;          // (3) Yield ajoute au collateral (nouveau PIV)
    state.Cr -= Y;          // (4) Yield consommé du pool
    state.Ur -= Y;          // (5) Yield consommé des droits

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

    // 11. Create output KHU_T UTXO (P + Y)
    if (tx.vout.empty()) {
        return error("%s: UNSTAKE tx has no outputs", __func__);
    }

    // Verify the output amount matches expected value
    CAmount expectedOutput = P + Y;
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
        return error("%s: invariant violation after UNSTAKE (C=%d, U=%d, Z=%d, Cr=%d, Ur=%d)",
                    __func__, state.C, state.U, state.Z, state.Cr, state.Ur);
    }

    LogPrint(BCLog::KHU, "%s: Applied UNSTAKE (P=%d, Y=%d, height=%d, C=%d, U=%d, Z=%d, Cr=%d, Ur=%d)\n",
             __func__, P, Y, nHeight, state.C, state.U, state.Z, state.Cr, state.Ur);

    return true;
}

bool UndoKHUUnstake(
    const CTransaction& tx,
    CCoinsViewCache& view,
    KhuGlobalState& state,
    int nHeight)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

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

    // 5. Read note data to get P and Y (same values as ApplyKHUUnstake)
    ZKHUNoteData noteData;
    if (!zkhuDB->ReadNote(cm, noteData)) {
        return error("%s: note data not found for cm=%s", __func__, cm.ToString());
    }

    // 6. Extract P (principal) and Y (yield) from note
    CAmount P = noteData.amount;
    CAmount Y = noteData.Ur_accumulated;

    // ═══════════════════════════════════════════════════════════════════════
    // REVERSE 5 MUTATIONS — SYMÉTRIE CRITIQUE
    //
    // Apply did:       Z -= P,  U += P+Y,  C += Y,  Cr -= Y,  Ur -= Y
    // Undo must do:    Z += P,  U -= P+Y,  C -= Y,  Cr += Y,  Ur += Y
    // ═══════════════════════════════════════════════════════════════════════

    // Pre-checks: underflow/overflow protection
    if (state.U < P + Y) {
        return error("%s: underflow U (U=%d, P=%d, Y=%d)", __func__, state.U, P, Y);
    }
    if (state.C < Y) {
        return error("%s: underflow C (C=%d, Y=%d)", __func__, state.C, Y);
    }
    if (state.Z > (std::numeric_limits<CAmount>::max() - P)) {
        return error("%s: overflow would occur on Z (Z=%d, P=%d)", __func__, state.Z, P);
    }
    if (state.Cr > (std::numeric_limits<CAmount>::max() - Y)) {
        return error("%s: overflow would occur on Cr (Cr=%d, Y=%d)", __func__, state.Cr, Y);
    }
    if (state.Ur > (std::numeric_limits<CAmount>::max() - Y)) {
        return error("%s: overflow would occur on Ur (Ur=%d, Y=%d)", __func__, state.Ur, Y);
    }

    // ═══════════════════════════════════════════════════════════
    // 5 MUTATIONS ATOMIQUES REVERSE — AUCUN CODE ENTRE CES LIGNES
    // ═══════════════════════════════════════════════════════════
    state.Z  += P;          // (1) Reverse: Principal restauré au shielded
    state.U  -= P + Y;      // (2) Reverse: Principal + Yield retiré du transparent
    state.C  -= Y;          // (3) Reverse: Yield retiré du collateral
    state.Cr += Y;          // (4) Reverse: Yield restauré au pool
    state.Ur += Y;          // (5) Reverse: Yield restauré aux droits

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
        return error("%s: invariant violation after undo UNSTAKE (C=%d, U=%d, Z=%d, Cr=%d, Ur=%d)",
                    __func__, state.C, state.U, state.Z, state.Cr, state.Ur);
    }

    LogPrint(BCLog::KHU, "%s: Undone UNSTAKE (P=%d, Y=%d, C=%d, U=%d, Z=%d, Cr=%d, Ur=%d)\n",
             __func__, P, Y, state.C, state.U, state.Z, state.Cr, state.Ur);

    return true;
}
