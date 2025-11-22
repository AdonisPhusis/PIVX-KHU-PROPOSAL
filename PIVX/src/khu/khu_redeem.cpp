// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_redeem.h"

#include "coins.h"
#include "consensus/validation.h"
#include "khu/khu_coins.h"
#include "khu/khu_state.h"
#include "logging.h"
#include "script/standard.h"
#include "sync.h"
#include "util/system.h"
#include "validation.h"

// External lock (defined in khu_validation.cpp)
extern RecursiveMutex cs_khu;

std::string CRedeemKHUPayload::ToString() const
{
    return strprintf("CRedeemKHUPayload(amount=%s, dest=%s)",
                     FormatMoney(amount),
                     EncodeDestination(dest));
}

bool GetRedeemKHUPayload(const CTransaction& tx, CRedeemKHUPayload& payload)
{
    if (tx.nType != CTransaction::TxType::KHU_REDEEM) {
        return false;
    }

    // Phase 2: Payload in extraPayload (PIVX special tx pattern)
    if (!tx.extraPayload || tx.extraPayload->empty()) {
        return false;
    }

    try {
        CDataStream ds(*tx.extraPayload, SER_NETWORK, PROTOCOL_VERSION);
        ds >> payload;
        return true;
    } catch (const std::exception& e) {
        LogPrint(BCLog::KHU, "ERROR: GetRedeemKHUPayload: %s\n", e.what());
        return false;
    }
}

bool CheckKHURedeem(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view)
{
    // 1. Vérifier TxType
    if (tx.nType != CTransaction::TxType::KHU_REDEEM) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-type",
                             "Transaction type is not KHU_REDEEM");
    }

    // 2. Vérifier payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-missing-payload",
                             "Failed to extract REDEEM payload");
    }

    // 3. Vérifier montant > 0
    if (payload.amount <= 0) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-amount",
                             strprintf("Invalid REDEEM amount: %d", payload.amount));
    }

    // 4. Vérifier inputs KHU_T suffisants
    // TODO Phase 2: Implement view.GetKHUCoin() to check KHU_T UTXOs
    // For now, we do basic validation that inputs exist
    CAmount total_input = 0;
    for (const auto& in : tx.vin) {
        const Coin& coin = view.AccessCoin(in.prevout);
        if (coin.IsSpent()) {
            return state.Invalid(false, REJECT_INVALID, "khu-redeem-missing-input",
                                 strprintf("Input not found: %s", in.prevout.ToString()));
        }

        // TODO Phase 2: Check if coin is KHU_T (not regular PIV)
        // TODO Phase 2: Check if KHU_T is staked (fStaked must be false)

        total_input += coin.out.nValue;
    }

    if (total_input < payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-insufficient-khu",
                             strprintf("Insufficient KHU_T: need %s, have %s",
                                       FormatMoney(payload.amount),
                                       FormatMoney(total_input)));
    }

    // 5. Vérifier au moins 1 output (PIV)
    if (tx.vout.empty()) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-no-outputs",
                             "REDEEM requires at least 1 output");
    }

    // 6. Vérifier output 0 = PIV (amount == payload.amount)
    if (tx.vout[0].nValue != payload.amount) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-amount-mismatch",
                             strprintf("PIV amount %s != payload %s",
                                       FormatMoney(tx.vout[0].nValue),
                                       FormatMoney(payload.amount)));
    }

    // 7. Vérifier destination valide
    if (!IsValidDestination(payload.dest)) {
        return state.Invalid(false, REJECT_INVALID, "khu-redeem-invalid-destination",
                             "Destination is not valid");
    }

    // 8. Vérifier collateral disponible (checked in ApplyKHURedeem with state access)
    // This is a consensus check that requires access to KHU global state

    return true;
}

bool ApplyKHURedeem(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view, uint32_t nHeight)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return error("ApplyKHURedeem: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("ApplyKHURedeem: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. Vérifier collateral suffisant
    if (state.C < amount || state.U < amount) {
        return error("ApplyKHURedeem: Insufficient C/U (C=%d U=%d amount=%d)",
                     state.C, state.U, amount);
    }

    // 4. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble)
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    //                       PAS D'INSTRUCTIONS ENTRE LES DEUX
    //    Source: docs/blueprints/03-MINT-REDEEM.md section 4.1
    // ═════════════════════════════════════════════════════════
    state.C -= amount;  // Diminuer collateral
    state.U -= amount;  // Diminuer supply

    // 5. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("ApplyKHURedeem: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 6. Dépenser UTXO KHU_T
    // TODO Phase 2: Implement view.SpendKHUCoin() for each input
    // For now, inputs are spent as regular PIV UTXOs

    // 7. Log
    LogPrint(BCLog::KHU, "ApplyKHURedeem: amount=%s C=%s U=%s height=%d\n",
             FormatMoney(amount),
             FormatMoney(state.C),
             FormatMoney(state.U),
             nHeight);

    return true;
}

bool UndoKHURedeem(const CTransaction& tx, KhuGlobalState& state, CCoinsViewCache& view)
{
    // ⚠️ CRITICAL: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    // 1. Extract payload
    CRedeemKHUPayload payload;
    if (!GetRedeemKHUPayload(tx, payload)) {
        return error("UndoKHURedeem: Failed to extract payload");
    }

    const CAmount amount = payload.amount;

    // 2. Vérifier invariants AVANT mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHURedeem: Pre-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 3. ═════════════════════════════════════════════════════════
    //    DOUBLE MUTATION ATOMIQUE (C et U ensemble) - REVERSE
    //    ⚠️ RÈGLE CRITIQUE: Ces deux lignes doivent être ADJACENTES
    // ═════════════════════════════════════════════════════════
    state.C += amount;  // Augmenter collateral
    state.U += amount;  // Augmenter supply

    // 4. Vérifier invariants APRÈS mutation
    if (!state.CheckInvariants()) {
        return error("UndoKHURedeem: Post-invariant violation (C=%d U=%d Cr=%d Ur=%d)",
                     state.C, state.U, state.Cr, state.Ur);
    }

    // 5. Restaurer UTXO KHU_T
    // TODO Phase 2: Implement view.AddKHUCoin() to restore spent KHU_T

    // 6. Log
    LogPrint(BCLog::KHU, "UndoKHURedeem: amount=%s C=%s U=%s\n",
             FormatMoney(amount),
             FormatMoney(state.C),
             FormatMoney(state.U));

    return true;
}
