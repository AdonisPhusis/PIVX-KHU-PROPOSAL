// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Wallet RPC Commands — Phase 8a
 *
 * Wallet-dependent RPC commands for KHU operations.
 * These are registered via RegisterWalletRPCCommands.
 */

#include "chain.h"
#include "chainparams.h"
#include "key_io.h"
#include "khu/khu_state.h"
#include "khu/khu_validation.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "script/sign.h"
#include "sync.h"
#include "txmempool.h"
#include "utilmoneystr.h"
#include "util/validation.h"
#include "validation.h"
#include "wallet/khu_wallet.h"
#include "wallet/rpcwallet.h"
#include "wallet/wallet.h"

#include <univalue.h>

/**
 * khubalance - Get KHU wallet balance
 */
static UniValue khubalance(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "khubalance\n"
            "\nReturns the KHU balance for this wallet (Phase 8a).\n"
            "\nResult:\n"
            "{\n"
            "  \"transparent\": n,         (numeric) KHU_T balance in satoshis\n"
            "  \"staked\": n,              (numeric) ZKHU staked balance in satoshis\n"
            "  \"pending_yield_estimated\": n,  (numeric) Estimated pending yield (approximation!)\n"
            "  \"total\": n,               (numeric) Total KHU balance\n"
            "  \"utxo_count\": n,          (numeric) Number of KHU UTXOs\n"
            "  \"note_count\": n           (numeric) Number of ZKHU notes\n"
            "}\n"
            "\nNote: pending_yield_estimated is an APPROXIMATION for display purposes.\n"
            "The actual yield is calculated by the consensus engine.\n"
            "\nExamples:\n"
            + HelpExampleCli("khubalance", "")
            + HelpExampleRpc("khubalance", "")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount nTransparent = GetKHUBalance(pwallet);
    CAmount nStaked = GetKHUStakedBalance(pwallet);

    KhuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    CAmount nPendingYield = GetKHUPendingYieldEstimate(pwallet, R_annual);

    UniValue result(UniValue::VOBJ);
    result.pushKV("transparent", ValueFromAmount(nTransparent));
    result.pushKV("staked", ValueFromAmount(nStaked));
    result.pushKV("pending_yield_estimated", ValueFromAmount(nPendingYield));
    result.pushKV("total", ValueFromAmount(nTransparent + nStaked + nPendingYield));
    result.pushKV("utxo_count", (int64_t)pwallet->khuData.mapKHUCoins.size());
    result.pushKV("note_count", 0); // Phase 8b: ZKHU notes

    return result;
}

/**
 * khulistunspent - List unspent KHU UTXOs
 */
static UniValue khulistunspent(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 2) {
        throw std::runtime_error(
            "khulistunspent ( minconf maxconf )\n"
            "\nReturns a list of unspent KHU_T UTXOs (Phase 8a).\n"
            "\nArguments:\n"
            "1. minconf    (numeric, optional, default=1) Minimum confirmations\n"
            "2. maxconf    (numeric, optional, default=9999999) Maximum confirmations\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"hash\",         (string) Transaction ID\n"
            "    \"vout\": n,              (numeric) Output index\n"
            "    \"address\": \"addr\",      (string) Destination address\n"
            "    \"amount\": n,            (numeric) Amount in satoshis\n"
            "    \"confirmations\": n,     (numeric) Number of confirmations\n"
            "    \"spendable\": true|false,(boolean) Can be spent\n"
            "    \"staked\": true|false    (boolean) Is staked as ZKHU\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("khulistunspent", "")
            + HelpExampleCli("khulistunspent", "6 9999999")
            + HelpExampleRpc("khulistunspent", "6, 9999999")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    int nMinDepth = 1;
    int nMaxDepth = 9999999;

    if (request.params.size() > 0) {
        nMinDepth = request.params[0].get_int();
    }
    if (request.params.size() > 1) {
        nMaxDepth = request.params[1].get_int();
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();

    UniValue results(UniValue::VARR);

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const KHUCoinEntry& entry = it->second;

        int nDepth = nCurrentHeight - entry.nConfirmedHeight + 1;

        if (nDepth < nMinDepth || nDepth > nMaxDepth) continue;

        CTxDestination dest;
        std::string address = "unknown";
        if (ExtractDestination(entry.coin.scriptPubKey, dest)) {
            address = EncodeDestination(dest);
        }

        UniValue obj(UniValue::VOBJ);
        obj.pushKV("txid", entry.txhash.GetHex());
        obj.pushKV("vout", (int64_t)entry.vout);
        obj.pushKV("address", address);
        obj.pushKV("amount", ValueFromAmount(entry.coin.amount));
        obj.pushKV("confirmations", nDepth);
        obj.pushKV("spendable", entry.coin.IsSpendable());
        obj.pushKV("staked", entry.coin.fStaked);

        results.push_back(obj);
    }

    return results;
}

/**
 * khumint - Mint KHU_T from PIV
 */
static UniValue khumint(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "khumint amount\n"
            "\nMint KHU_T from PIV (Phase 8a).\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount of PIV to convert to KHU_T\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",          (string) Transaction ID\n"
            "  \"amount_khu\": n,         (numeric) KHU_T minted (satoshis)\n"
            "  \"fee\": n                 (numeric) Transaction fee (satoshis)\n"
            "}\n"
            "\nNote: The invariant C == U is maintained. PIV is burned and KHU_T is created.\n"
            "\nExamples:\n"
            + HelpExampleCli("khumint", "100")
            + HelpExampleRpc("khumint", "100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    CAmount nBalance = pwallet->GetBalance().m_mine_trusted;
    if (nAmount > nBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV balance. Have: %s, Need: %s",
                     FormatMoney(nBalance), FormatMoney(nAmount)));
    }

    CAmount nFee = 10000; // 0.0001 PIV minimum fee

    CAmount nTotalRequired = nAmount + nFee;
    if (nTotalRequired > nBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for amount + fee. Have: %s, Need: %s",
                     FormatMoney(nBalance), FormatMoney(nTotalRequired)));
    }

    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    CTxDestination dest = newKey.GetID();

    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::LEGACY;
    mtx.nType = CTransaction::TxType::KHU_MINT;

    // Output 0: OP_RETURN burn marker
    CScript burnScript;
    burnScript << OP_RETURN;
    mtx.vout.push_back(CTxOut(0, burnScript));

    // Output 1: KHU_T output
    CScript khuScript = GetScriptForDestination(dest);
    mtx.vout.push_back(CTxOut(nAmount, khuScript));

    // Select coins for input
    std::vector<COutput> vAvailableCoins;
    pwallet->AvailableCoins(&vAvailableCoins);

    CAmount nValueIn = 0;
    for (const COutput& out : vAvailableCoins) {
        if (nValueIn >= nTotalRequired) break;

        mtx.vin.push_back(CTxIn(out.tx->GetHash(), out.i));
        nValueIn += out.tx->tx->vout[out.i].nValue;
    }

    if (nValueIn < nTotalRequired) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Unable to select sufficient coins");
    }

    // Output 2: Change back to PIV (if any)
    CAmount nChange = nValueIn - nAmount - nFee;
    if (nChange > 0) {
        CPubKey changeKey;
        if (!pwallet->GetKeyFromPool(changeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for change");
        }
        CScript changeScript = GetScriptForDestination(changeKey.GetID());
        mtx.vout.push_back(CTxOut(nChange, changeScript));
    }

    // Sign transaction
    for (size_t i = 0; i < mtx.vin.size(); ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        const CWalletTx* wtx = pwallet->GetWalletTx(outpoint.hash);
        if (!wtx) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Input transaction not found");
        }

        const CScript& scriptPubKey = wtx->tx->vout[outpoint.n].scriptPubKey;
        CAmount amount = wtx->tx->vout[outpoint.n].nValue;

        SignatureData sigdata;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, SIGVERSION_BASE, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing transaction failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Broadcast transaction
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));
    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount_khu", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));

    return result;
}

/**
 * khuredeem - Redeem KHU_T back to PIV
 */
static UniValue khuredeem(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "khuredeem amount\n"
            "\nRedeem KHU_T back to PIV (Phase 8a).\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount of KHU_T to convert back to PIV\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",          (string) Transaction ID\n"
            "  \"amount_piv\": n,         (numeric) PIV redeemed (satoshis)\n"
            "  \"fee\": n                 (numeric) Transaction fee (satoshis)\n"
            "}\n"
            "\nNote: The invariant C == U is maintained. KHU_T is burned and PIV is released.\n"
            "\nExamples:\n"
            + HelpExampleCli("khuredeem", "100")
            + HelpExampleRpc("khuredeem", "100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    CAmount nKHUBalance = GetKHUBalance(pwallet);
    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU_T balance. Have: %s, Need: %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    CAmount nFee = 10000; // 0.0001 PIV
    if (nAmount <= nFee) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Amount must be greater than fee (%s)", FormatMoney(nFee)));
    }

    // Select KHU UTXOs
    CAmount nValueIn = 0;
    std::vector<COutPoint> vInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fStaked) continue;
        if (nValueIn >= nAmount) break;

        vInputs.push_back(outpoint);
        nValueIn += entry.coin.amount;
    }

    if (nValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    CTxDestination dest = newKey.GetID();

    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::LEGACY;
    mtx.nType = CTransaction::TxType::KHU_REDEEM;

    // Add inputs
    for (const COutPoint& outpoint : vInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
    }

    // Output 0: PIV output
    CScript pivScript = GetScriptForDestination(dest);
    mtx.vout.push_back(CTxOut(nAmount - nFee, pivScript));

    // Output 1: KHU_T change (if any)
    CAmount nChange = nValueIn - nAmount;
    if (nChange > 0) {
        CPubKey changeKey;
        if (!pwallet->GetKeyFromPool(changeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for change");
        }
        CScript changeScript = GetScriptForDestination(changeKey.GetID());
        mtx.vout.push_back(CTxOut(nChange, changeScript));
    }

    // Sign transaction (sign KHU inputs)
    for (size_t i = 0; i < mtx.vin.size(); ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }

        const CScript& scriptPubKey = it->second.coin.scriptPubKey;
        CAmount amount = it->second.coin.amount;

        SignatureData sigdata;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, SIGVERSION_BASE, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing transaction failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Broadcast transaction
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));
    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount_piv", ValueFromAmount(nAmount - nFee));
    result.pushKV("fee", ValueFromAmount(nFee));

    return result;
}

// KHU Wallet RPC command table
static const CRPCCommand khuWalletCommands[] = {
    //  category    name                      actor (function)            okSafe  argNames
    //  ----------- ------------------------  ------------------------    ------  ----------
    { "khu",        "khubalance",             &khubalance,                true,   {} },
    { "khu",        "khulistunspent",         &khulistunspent,            true,   {"minconf", "maxconf"} },
    { "khu",        "khumint",                &khumint,                   false,  {"amount"} },
    { "khu",        "khuredeem",              &khuredeem,                 false,  {"amount"} },
};

void RegisterKHUWalletRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(khuWalletCommands); vcidx++)
        t.appendCommand(khuWalletCommands[vcidx].name, &khuWalletCommands[vcidx]);
}
