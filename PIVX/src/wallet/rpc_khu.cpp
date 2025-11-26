// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * KHU Wallet RPC Commands — Phase 8a/8b
 *
 * Wallet-dependent RPC commands for KHU operations.
 * These are registered via RegisterWalletRPCCommands.
 *
 * Phase 8a: Transparent KHU_T operations (khumint, khuredeem, khusend)
 * Phase 8b: ZKHU staking operations (khustake, khuunstake, khuliststaked)
 */

#include "chain.h"
#include "chainparams.h"
#include "key_io.h"
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"
#include "khu/khu_state.h"
#include "khu/khu_unstake.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_memo.h"
#include "streams.h"
#include "primitives/transaction.h"
#include "rpc/server.h"
#include "sapling/key_io_sapling.h"
#include "sapling/saplingscriptpubkeyman.h"
#include "sapling/transaction_builder.h"
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
            "\nReturns the KHU and PIV balance for this wallet.\n"
            "\nResult:\n"
            "{\n"
            "  \"khu\": {                   (object) KHU balance details\n"
            "    \"transparent\": n,        (numeric) KHU_T balance\n"
            "    \"staked\": n,             (numeric) ZKHU staked balance\n"
            "    \"pending_yield_estimated\": n,  (numeric) Estimated pending yield\n"
            "    \"total\": n,              (numeric) Total KHU balance\n"
            "    \"utxo_count\": n,         (numeric) Number of KHU UTXOs\n"
            "    \"note_count\": n          (numeric) Number of ZKHU notes\n"
            "  },\n"
            "  \"piv\": {                   (object) PIV balance (for fees)\n"
            "    \"available\": n,          (numeric) PIV available for fees\n"
            "    \"immature\": n,           (numeric) PIV immature (coinbase)\n"
            "    \"locked\": n              (numeric) PIV locked (collateral)\n"
            "  }\n"
            "}\n"
            "\nNote: pending_yield_estimated is an APPROXIMATION for display purposes.\n"
            "PIV 'available' is used to pay transaction fees for KHU operations.\n"
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

    // KHU balances
    CAmount nTransparent = GetKHUBalance(pwallet);
    CAmount nStaked = GetKHUStakedBalance(pwallet);

    KhuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    CAmount nPendingYield = GetKHUPendingYieldEstimate(pwallet, R_annual);

    // PIV balances (for fees)
    CAmount nPIVAvailable = pwallet->GetAvailableBalance();
    CAmount nPIVImmature = pwallet->GetImmatureBalance();
    CAmount nPIVLocked = pwallet->GetLockedCoins();

    // Build KHU object
    UniValue khuObj(UniValue::VOBJ);
    khuObj.pushKV("transparent", ValueFromAmount(nTransparent));
    khuObj.pushKV("staked", ValueFromAmount(nStaked));
    khuObj.pushKV("pending_yield_estimated", ValueFromAmount(nPendingYield));
    khuObj.pushKV("total", ValueFromAmount(nTransparent + nStaked + nPendingYield));
    khuObj.pushKV("utxo_count", (int64_t)pwallet->khuData.mapKHUCoins.size());
    khuObj.pushKV("note_count", (int64_t)GetUnspentZKHUNotes(pwallet).size());

    // Build PIV object
    UniValue pivObj(UniValue::VOBJ);
    pivObj.pushKV("available", ValueFromAmount(nPIVAvailable));
    pivObj.pushKV("immature", ValueFromAmount(nPIVImmature));
    pivObj.pushKV("locked", ValueFromAmount(nPIVLocked));

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("khu", khuObj);
    result.pushKV("piv", pivObj);

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
    mtx.nVersion = CTransaction::TxVersion::SAPLING;  // KHU txs require Sapling version
    mtx.nType = CTransaction::TxType::KHU_MINT;

    // Output 1: KHU_T output (prepare script first for payload)
    CScript khuScript = GetScriptForDestination(dest);

    // Create and serialize payload (required for special txs)
    CMintKHUPayload payload(nAmount, khuScript);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    // Output 0: OP_RETURN burn marker
    CScript burnScript;
    burnScript << OP_RETURN;
    mtx.vout.push_back(CTxOut(0, burnScript));

    // Output 1: KHU_T output
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
        // Use SIGVERSION_SAPLING for Sapling version transactions
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion, false)) {
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

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from KHU amount
    // ═══════════════════════════════════════════════════════════════════════

    CAmount nKHUBalance = GetKHUBalance(pwallet);
    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU_T balance. Have: %s, Need: %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    CAmount nFee = 10000; // 0.0001 PIV (paid from separate PIV input)

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select KHU UTXOs (for redeem amount only)
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fStaked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);
    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    CScript pivFeeScript;
    bool foundPIVInput = false;
    CAmount bestExcess = std::numeric_limits<CAmount>::max();

    for (const COutput& coin : vPIVCoins) {
        if (coin.tx->tx->IsShieldedTx()) continue;
        CAmount value = coin.Value();
        if (value >= nFee) {
            CAmount excess = value - nFee;
            if (excess < bestExcess) {
                pivFeeInput = COutPoint(coin.tx->GetHash(), coin.i);
                nPIVInputValue = value;
                pivFeeScript = coin.tx->tx->vout[coin.i].scriptPubKey;
                foundPIVInput = true;
                bestExcess = excess;
                if (excess == 0) break;
            }
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for fee payment");
    }

    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out");
    }
    CTxDestination dest = newKey.GetID();

    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::SAPLING;
    mtx.nType = CTransaction::TxType::KHU_REDEEM;

    CScript pivScript = GetScriptForDestination(dest);

    // Create and serialize payload
    CRedeemKHUPayload payload(nAmount, pivScript);
    CDataStream ds(SER_NETWORK, PROTOCOL_VERSION);
    ds << payload;
    mtx.extraPayload = std::vector<uint8_t>(ds.begin(), ds.end());

    LogPrint(BCLog::KHU, "khuredeem: %zu KHU inputs for amount=%s\n",
              vKHUInputs.size(), FormatMoney(nAmount));

    // Add KHU inputs first
    for (const COutPoint& outpoint : vKHUInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
    }
    size_t nKHUInputCount = mtx.vin.size();

    // Add PIV input for fee
    mtx.vin.push_back(CTxIn(pivFeeInput));

    // Output 0: PIV output (full amount - fee comes from PIV input)
    mtx.vout.push_back(CTxOut(nAmount, pivScript));

    // Output 1: KHU_T change (if any)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey khuChangeKey;
        if (!pwallet->GetKeyFromPool(khuChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        CScript khuChangeScript = GetScriptForDestination(khuChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nKHUChange, khuChangeScript));
    }

    // Output 2: PIV change (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        CScript pivChangeScript = GetScriptForDestination(pivChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nPIVChange, pivChangeScript));
    }

    // Sign KHU inputs
    for (size_t i = 0; i < nKHUInputCount; ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }

        const CScript& scriptPubKey = it->second.coin.scriptPubKey;
        CAmount amount = it->second.coin.amount;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing KHU input failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Sign PIV input (last input)
    {
        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, nKHUInputCount, nPIVInputValue, SIGHASH_ALL),
                             pivFeeScript, sigdata, sigversion, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing PIV fee input failed");
        }
        UpdateTransaction(mtx, nKHUInputCount, sigdata);
    }

    // Broadcast transaction
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));

    LogPrint(BCLog::KHU, "khuredeem: broadcasting tx %s\n",
              txRef->GetHash().ToString().substr(0,16).c_str());

    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount_piv", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("fee_source", "separate_piv_input");

    return result;
}

/**
 * khugetinfo - Get comprehensive KHU wallet and network info
 */
static UniValue khugetinfo(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "khugetinfo\n"
            "\nReturns comprehensive KHU wallet and network information.\n"
            "\nResult:\n"
            "{\n"
            "  \"wallet\": {\n"
            "    \"khu_transparent\": n,    (numeric) KHU_T balance\n"
            "    \"khu_staked\": n,         (numeric) ZKHU staked balance\n"
            "    \"khu_total\": n,          (numeric) Total KHU balance\n"
            "    \"utxo_count\": n,         (numeric) Number of KHU UTXOs\n"
            "    \"note_count\": n          (numeric) Number of ZKHU notes\n"
            "  },\n"
            "  \"network\": {\n"
            "    \"height\": n,             (numeric) Current block height\n"
            "    \"C\": n,                  (numeric) Total collateral (PIV backing KHU)\n"
            "    \"U\": n,                  (numeric) Total KHU_T supply\n"
            "    \"Cr\": n,                 (numeric) Reward pool\n"
            "    \"Ur\": n,                 (numeric) Unstake rights\n"
            "    \"T\": n,                  (numeric) DAO Treasury\n"
            "    \"R_annual_pct\": x.xx,    (numeric) Annual yield rate %\n"
            "    \"invariants_ok\": true|false\n"
            "  },\n"
            "  \"activation\": {\n"
            "    \"khu_active\": true|false,(boolean) Is KHU system active\n"
            "    \"activation_height\": n,  (numeric) V6 activation height\n"
            "    \"blocks_until_active\": n (numeric) Blocks until activation (0 if active)\n"
            "  }\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("khugetinfo", "")
            + HelpExampleRpc("khugetinfo", "")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Wallet info
    CAmount nTransparent = GetKHUBalance(pwallet);
    CAmount nStaked = GetKHUStakedBalance(pwallet);

    UniValue wallet(UniValue::VOBJ);
    wallet.pushKV("khu_transparent", ValueFromAmount(nTransparent));
    wallet.pushKV("khu_staked", ValueFromAmount(nStaked));
    wallet.pushKV("khu_total", ValueFromAmount(nTransparent + nStaked));
    wallet.pushKV("utxo_count", (int64_t)pwallet->khuData.mapKHUCoins.size());
    wallet.pushKV("note_count", (int64_t)GetUnspentZKHUNotes(pwallet).size());

    // Network info
    KhuGlobalState state;
    UniValue network(UniValue::VOBJ);

    if (GetCurrentKHUState(state)) {
        network.pushKV("height", (int64_t)state.nHeight);
        network.pushKV("C", ValueFromAmount(state.C));
        network.pushKV("U", ValueFromAmount(state.U));
        network.pushKV("Cr", ValueFromAmount(state.Cr));
        network.pushKV("Ur", ValueFromAmount(state.Ur));
        network.pushKV("T", ValueFromAmount(state.T));
        network.pushKV("R_annual_pct", state.R_annual / 100.0);
        network.pushKV("invariants_ok", state.CheckInvariants());
    } else {
        network.pushKV("height", chainActive.Height());
        network.pushKV("C", 0);
        network.pushKV("U", 0);
        network.pushKV("Cr", 0);
        network.pushKV("Ur", 0);
        network.pushKV("T", 0);
        network.pushKV("R_annual_pct", 0.0);
        network.pushKV("invariants_ok", true);
    }

    // Activation info
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    int nCurrentHeight = chainActive.Height();
    bool fActive = (uint32_t)nCurrentHeight >= V6_activation;
    int nBlocksUntil = fActive ? 0 : (int)(V6_activation - nCurrentHeight);

    UniValue activation(UniValue::VOBJ);
    activation.pushKV("khu_active", fActive);
    activation.pushKV("activation_height", (int64_t)V6_activation);
    activation.pushKV("blocks_until_active", nBlocksUntil);

    // Build result
    UniValue result(UniValue::VOBJ);
    result.pushKV("wallet", wallet);
    result.pushKV("network", network);
    result.pushKV("activation", activation);

    return result;
}

/**
 * khusend - Send KHU_T to an address
 *
 * Creates a KHU transfer transaction (not MINT/REDEEM, just transfer).
 * Note: This is a simple transparent transfer of KHU_T UTXOs.
 */
static UniValue khusend(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() < 2 || request.params.size() > 3) {
        throw std::runtime_error(
            "khusend \"address\" amount ( \"comment\" )\n"
            "\nSend KHU_T to a given address.\n"
            "\nArguments:\n"
            "1. \"address\"    (string, required) The PIVX address to send to\n"
            "2. amount         (numeric, required) The amount of KHU_T to send\n"
            "3. \"comment\"    (string, optional) A comment (not stored on chain)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",      (string) Transaction ID\n"
            "  \"amount\": n,          (numeric) Amount sent\n"
            "  \"fee\": n,             (numeric) Transaction fee\n"
            "  \"to\": \"address\"     (string) Recipient address\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("khusend", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg34fk\" 100")
            + HelpExampleRpc("khusend", "\"DMJRSsuU9zfyrvxVaAEFQqK4MxZg34fk\", 100")
        );
    }

    CWallet* const pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    // Parse address
    std::string strAddress = request.params[0].get_str();
    CTxDestination dest = DecodeDestination(strAddress);
    if (!IsValidDestination(dest)) {
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid PIVX address");
    }

    // Parse amount
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check V6 activation
    uint32_t V6_activation = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if ((uint32_t)chainActive.Height() < V6_activation) {
        throw JSONRPCError(RPC_INVALID_REQUEST,
            strprintf("KHU not active until block %u (current: %d)", V6_activation, chainActive.Height()));
    }

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from KHU amount
    // ═══════════════════════════════════════════════════════════════════════

    // Check KHU balance (fee is separate in PIV)
    CAmount nKHUBalance = GetKHUBalance(pwallet);
    CAmount nFee = 10000; // 0.0001 PIV

    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU_T balance. Have: %s, Need: %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);

    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    bool foundPIVInput = false;

    // Sort by value ascending to find smallest suitable UTXO
    std::sort(vPIVCoins.begin(), vPIVCoins.end(),
        [](const COutput& a, const COutput& b) {
            return a.tx->tx->vout[a.i].nValue < b.tx->tx->vout[b.i].nValue;
        });

    for (const COutput& out : vPIVCoins) {
        CAmount value = out.tx->tx->vout[out.i].nValue;
        if (value >= nFee) {
            pivFeeInput = COutPoint(out.tx->tx->GetHash(), out.i);
            nPIVInputValue = value;
            foundPIVInput = true;
            break;
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for transaction fee");
    }

    // Select KHU UTXOs (for amount only, NOT fee)
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fStaked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Create transaction
    // Note: This is a simple KHU_T transfer - no type change
    CMutableTransaction mtx;
    mtx.nVersion = CTransaction::TxVersion::LEGACY;
    mtx.nType = CTransaction::TxType::NORMAL; // Regular transfer, not MINT/REDEEM

    // Add KHU inputs
    for (const COutPoint& outpoint : vKHUInputs) {
        mtx.vin.push_back(CTxIn(outpoint));
    }
    size_t nKHUInputCount = mtx.vin.size();

    // Add PIV input for fee
    mtx.vin.push_back(CTxIn(pivFeeInput));

    // Output 0: KHU_T to recipient (full amount - fee is separate)
    CScript recipientScript = GetScriptForDestination(dest);
    mtx.vout.push_back(CTxOut(nAmount, recipientScript));

    // Output 1: KHU_T change (if any)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey changeKey;
        if (!pwallet->GetKeyFromPool(changeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        CScript changeScript = GetScriptForDestination(changeKey.GetID());
        mtx.vout.push_back(CTxOut(nKHUChange, changeScript));
    }

    // Output 2: PIV change (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        CScript pivChangeScript = GetScriptForDestination(pivChangeKey.GetID());
        mtx.vout.push_back(CTxOut(nPIVChange, pivChangeScript));
    }

    // Sign KHU inputs
    for (size_t i = 0; i < nKHUInputCount; ++i) {
        const COutPoint& outpoint = mtx.vin[i].prevout;
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }

        const CScript& scriptPubKey = it->second.coin.scriptPubKey;
        CAmount amount = it->second.coin.amount;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, i, amount, SIGHASH_ALL),
                             scriptPubKey, sigdata, sigversion, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing KHU input failed");
        }
        UpdateTransaction(mtx, i, sigdata);
    }

    // Sign PIV input (last input)
    {
        size_t pivInputIdx = nKHUInputCount;
        const CWalletTx* wtx = pwallet->GetWalletTx(pivFeeInput.hash);
        if (!wtx) {
            throw JSONRPCError(RPC_WALLET_ERROR, "PIV input transaction not found");
        }
        const CScript& pivScriptPubKey = wtx->tx->vout[pivFeeInput.n].scriptPubKey;

        SignatureData sigdata;
        SigVersion sigversion = mtx.isSaplingVersion() ? SIGVERSION_SAPLING : SIGVERSION_BASE;
        if (!ProduceSignature(MutableTransactionSignatureCreator(pwallet, &mtx, pivInputIdx, nPIVInputValue, SIGHASH_ALL),
                             pivScriptPubKey, sigdata, sigversion, false)) {
            throw JSONRPCError(RPC_WALLET_ERROR, "Signing PIV fee input failed");
        }
        UpdateTransaction(mtx, pivInputIdx, sigdata);
    }

    // Broadcast
    CTransactionRef txRef = MakeTransactionRef(std::move(mtx));
    CValidationState state;

    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("to", strAddress);

    return result;
}

/**
 * khurescan - Rescan blockchain for KHU coins
 *
 * Usage: khurescan [startheight]
 *
 * Rescans the blockchain for KHU transactions belonging to this wallet.
 * Useful after importing keys or if wallet is out of sync.
 */
static UniValue khurescan(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "khurescan ( startheight )\n"
            "\nRescan blockchain for KHU transactions belonging to this wallet.\n"
            "\nArguments:\n"
            "1. startheight    (numeric, optional, default=0) Block height to start scanning from\n"
            "\nResult:\n"
            "{\n"
            "  \"scanned_blocks\": n,     (numeric) Number of blocks scanned\n"
            "  \"khu_coins_found\": n,    (numeric) Number of KHU coins found\n"
            "  \"khu_balance\": n.nnn,    (numeric) KHU transparent balance\n"
            "  \"khu_staked\": n.nnn,     (numeric) KHU staked balance\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("khurescan", "")
            + HelpExampleCli("khurescan", "100000")
            + HelpExampleRpc("khurescan", "100000")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    // Get start height
    int nStartHeight = 0;
    if (!request.params[0].isNull()) {
        nStartHeight = request.params[0].get_int();
        if (nStartHeight < 0) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid start height (must be >= 0)");
        }
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();
    if (nStartHeight > nCurrentHeight) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Start height %d is greater than current height %d", nStartHeight, nCurrentHeight));
    }

    // Perform scan
    if (!ScanForKHUCoins(pwallet, nStartHeight)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to scan for KHU coins");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("scanned_blocks", nCurrentHeight - nStartHeight + 1);
    result.pushKV("khu_coins_found", (int)pwallet->khuData.mapKHUCoins.size());
    result.pushKV("khu_balance", ValueFromAmount(pwallet->khuData.nKHUBalance));
    result.pushKV("khu_staked", ValueFromAmount(pwallet->khuData.nKHUStaked));

    return result;
}

/**
 * khustake - Stake KHU_T to ZKHU (Phase 8b)
 *
 * Converts KHU_T transparent coins to ZKHU private staking notes.
 * The staked amount earns yield based on R_annual (DOMC governed).
 *
 * IMPORTANT: STAKE is a form conversion only (T→Z), no economic effect.
 *            The yield is accumulated per-note and paid at UNSTAKE.
 */
static UniValue khustake(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "khustake amount\n"
            "\nStake KHU_T transparent coins to ZKHU private staking notes.\n"
            "\nArguments:\n"
            "1. amount    (numeric, required) Amount to stake (in KHU)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",           (string) Transaction ID\n"
            "  \"amount\": n,              (numeric) Amount staked\n"
            "  \"stake_height\": n,        (numeric) Stake start height\n"
            "  \"maturity_height\": n,     (numeric) Height when unstake is allowed\n"
            "  \"note_commitment\": \"hash\" (string) ZKHU note commitment (cm)\n"
            "  \"sapling_address\": \"addr\" (string) ZKHU destination address\n"
            "}\n"
            "\nNotes:\n"
            "- Minimum maturity: 4320 blocks (3 days) before unstaking\n"
            "- Yield accumulates based on R_annual (currently governed by DOMC)\n"
            "- STAKE is a form conversion only - C, U, Cr, Ur unchanged\n"
            "\nExamples:\n"
            + HelpExampleCli("khustake", "100")
            + HelpExampleRpc("khustake", "100")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    EnsureWalletIsUnlocked(pwallet);

    // Parse amount
    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Amount must be positive");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check KHU activation
    int nCurrentHeight = chainActive.Height();
    const Consensus::Params& consensus = Params().GetConsensus();

    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V6_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "KHU system not yet activated");
    }

    // Check Sapling activation
    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V5_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Sapling not yet activated (required for ZKHU)");
    }

    // Check KHU balance (fee is paid in PIV, not KHU - per CLAUDE.md §2.1)
    CAmount nKHUBalance = GetKHUBalance(pwallet);
    // Sapling STAKE transactions are ~1200-1500 bytes, need ~15000 satoshis at 10 sat/byte
    CAmount nFee = 15000; // 0.00015 PIV

    if (nAmount > nKHUBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient KHU balance: have %s, need %s",
                     FormatMoney(nKHUBalance), FormatMoney(nAmount)));
    }

    // Check PIV balance for fee
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select KHU_T UTXOs (for stake amount only)
    CAmount nKHUValueIn = 0;
    std::vector<COutPoint> vKHUInputs;

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        if (entry.coin.fStaked) continue;
        if (nKHUValueIn >= nAmount) break;

        vKHUInputs.push_back(outpoint);
        nKHUValueIn += entry.coin.amount;
    }

    if (nKHUValueIn < nAmount) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "Unable to select sufficient KHU UTXOs");
    }

    // Select PIV UTXO for fee (smallest one >= fee to minimize change)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);
    COutPoint pivFeeInput;
    CAmount nPIVInputValue = 0;
    CScript pivFeeScript;
    bool foundPIVInput = false;
    CAmount bestExcess = std::numeric_limits<CAmount>::max();

    for (const COutput& coin : vPIVCoins) {
        if (coin.tx->tx->IsShieldedTx()) continue; // Skip shielded
        CAmount value = coin.Value();
        if (value >= nFee) {
            CAmount excess = value - nFee;
            if (excess < bestExcess) {
                pivFeeInput = COutPoint(coin.tx->GetHash(), coin.i);
                nPIVInputValue = value;
                pivFeeScript = coin.tx->tx->vout[coin.i].scriptPubKey;
                foundPIVInput = true;
                bestExcess = excess;
                if (excess == 0) break; // Perfect match
            }
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for fee payment");
    }

    // Generate or get Sapling address for ZKHU note
    libzcash::SaplingPaymentAddress saplingAddr;
    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan || !saplingMan->IsEnabled()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            "Sapling not enabled in wallet. Run 'upgradetohd' first.");
    }

    saplingAddr = pwallet->GenerateNewSaplingZKey();

    // Get output viewing key (OVK) for the note
    uint256 ovk = saplingMan->getCommonOVK();

    // Create ZKHUMemo with stake metadata
    uint32_t nStakeHeight = nCurrentHeight + 1; // Note will be in next block
    const uint32_t ZKHU_MATURITY_BLOCKS = 4320; // 3 days

    ZKHUMemo memo;
    memcpy(memo.magic, "ZKHU", 4);
    memo.version = 1;
    memo.nStakeStartHeight = nStakeHeight;
    memo.amount = nAmount;
    memo.Ur_accumulated = 0; // Initial stake, no accumulated yield yet

    std::array<unsigned char, 512> memoBytes = memo.Serialize();

    // Build transaction using TransactionBuilder
    TransactionBuilder builder(consensus, pwallet);
    builder.SetFee(nFee);
    builder.SetType(CTransaction::TxType::KHU_STAKE);  // Set type BEFORE building

    // Add KHU_T transparent inputs
    for (const COutPoint& outpoint : vKHUInputs) {
        auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
        if (it == pwallet->khuData.mapKHUCoins.end()) {
            throw JSONRPCError(RPC_WALLET_ERROR, "KHU input not found");
        }
        builder.AddTransparentInput(outpoint, it->second.coin.scriptPubKey, it->second.coin.amount);
    }

    // Add PIV transparent input (for fee)
    builder.AddTransparentInput(pivFeeInput, pivFeeScript, nPIVInputValue);

    // Add Sapling output (ZKHU note) with ZKHUMemo
    builder.AddSaplingOutput(ovk, saplingAddr, nAmount, memoBytes);

    // IMPORTANT: Output ordering for wallet tracking (per CLAUDE.md §2.1)
    // vout[0] = KHU change (tracked as KHU)
    // vout[1] = PIV change (NOT tracked as KHU)

    // Calculate KHU change (fee is paid from PIV, not KHU)
    CAmount nKHUChange = nKHUValueIn - nAmount;
    if (nKHUChange > 0) {
        CPubKey khuChangeKey;
        if (!pwallet->GetKeyFromPool(khuChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU change");
        }
        builder.AddTransparentOutput(khuChangeKey.GetID(), nKHUChange);
    }

    // Calculate PIV change (PIV input minus fee)
    CAmount nPIVChange = nPIVInputValue - nFee;
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, true)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        builder.AddTransparentOutput(pivChangeKey.GetID(), nPIVChange);
    }

    // Build with dummy signatures first (to calculate size)
    TransactionBuilderResult buildResult = builder.Build(true);
    if (buildResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to build stake transaction: %s", buildResult.GetError()));
    }

    // Clear dummy signatures/proofs before creating real ones
    // (required for TransactionBuilder workflow)
    builder.ClearProofsAndSignatures();

    // Now prove and sign
    TransactionBuilderResult proveResult = builder.ProveAndSign();
    if (proveResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to prove/sign stake transaction: %s", proveResult.GetError()));
    }

    CTransaction finalTx = proveResult.GetTxOrThrow();

    // Transaction type was already set via builder.SetType() before Build()
    // DO NOT modify the transaction after Build() - it would invalidate Sapling proofs
    CTransactionRef txRef = MakeTransactionRef(finalTx);

    // Broadcast transaction
    CValidationState state;
    if (!AcceptToMemoryPool(mempool, state, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(state)));
    }

    // Get note commitment from the Sapling output
    std::string noteCommitment = "pending"; // Note commitment is derived from the output
    if (txRef->IsShieldedTx() && txRef->sapData->vShieldedOutput.size() > 0) {
        noteCommitment = txRef->sapData->vShieldedOutput[0].cmu.GetHex();
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("amount", ValueFromAmount(nAmount));
    result.pushKV("stake_height", (int64_t)nStakeHeight);
    result.pushKV("maturity_height", (int64_t)(nStakeHeight + ZKHU_MATURITY_BLOCKS));
    result.pushKV("note_commitment", noteCommitment);
    result.pushKV("sapling_address", KeyIO::EncodePaymentAddress(saplingAddr));

    return result;
}

/**
 * khuunstake - Unstake ZKHU to KHU_T (Phase 8b)
 *
 * Converts ZKHU private staking notes back to KHU_T transparent coins.
 * The unstaked amount includes accumulated yield based on R_annual.
 *
 * IMPORTANT: UNSTAKE applies DOUBLE FLUX:
 *            C += bonus, U += bonus, Cr -= bonus, Ur -= bonus
 *            This preserves invariants while transferring yield from reward pool.
 */
static UniValue khuunstake(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 1) {
        throw std::runtime_error(
            "khuunstake ( \"note_commitment\" )\n"
            "\nUnstake ZKHU private staking notes back to KHU_T transparent coins.\n"
            "\nArguments:\n"
            "1. note_commitment  (string, optional) Specific note to unstake (default: oldest mature note)\n"
            "\nResult:\n"
            "{\n"
            "  \"txid\": \"hash\",           (string) Transaction ID\n"
            "  \"principal\": n,           (numeric) Original staked amount\n"
            "  \"yield_bonus\": n,         (numeric) Accumulated yield bonus\n"
            "  \"total\": n,               (numeric) Total amount received (principal + bonus)\n"
            "  \"stake_duration_blocks\": n,(numeric) How long the note was staked\n"
            "  \"stake_duration_days\": n  (numeric) Approximate days staked\n"
            "}\n"
            "\nNotes:\n"
            "- Requires 4320 blocks maturity (3 days minimum stake)\n"
            "- Yield is calculated based on R_annual and stake duration\n"
            "- UNSTAKE applies DOUBLE FLUX: C+, U+, Cr-, Ur- (preserves invariants)\n"
            "\nExamples:\n"
            + HelpExampleCli("khuunstake", "")
            + HelpExampleCli("khuunstake", "\"abc123...\"")
            + HelpExampleRpc("khuunstake", "")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    EnsureWalletIsUnlocked(pwallet);

    LOCK2(cs_main, pwallet->cs_wallet);

    // Check KHU activation
    int nCurrentHeight = chainActive.Height();
    const Consensus::Params& consensus = Params().GetConsensus();

    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V6_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "KHU system not yet activated");
    }

    // Check Sapling activation
    if (!consensus.NetworkUpgradeActive(nCurrentHeight, Consensus::UPGRADE_V5_0)) {
        throw JSONRPCError(RPC_INVALID_REQUEST, "Sapling not yet activated (required for ZKHU)");
    }

    const uint32_t ZKHU_MATURITY_BLOCKS = 4320;
    // Sapling UNSTAKE transactions are ~1200-1500 bytes, need ~15000 satoshis at 10 sat/byte
    CAmount nFee = 15000; // 0.00015 PIV for Sapling tx

    // ═══════════════════════════════════════════════════════════════════════
    // CLAUDE.md §2.1: "Tous les frais KHU sont payés en PIV non-bloqué"
    // Fee is paid from separate PIV input, NOT from yield/principal
    // ═══════════════════════════════════════════════════════════════════════

    // Check PIV balance for fee (separate from KHU)
    CAmount nPIVBalance = pwallet->GetAvailableBalance();
    if (nFee > nPIVBalance) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            strprintf("Insufficient PIV for fee: have %s, need %s",
                     FormatMoney(nPIVBalance), FormatMoney(nFee)));
    }

    // Select PIV UTXO for fee (smallest one >= fee)
    std::vector<COutput> vPIVCoins;
    pwallet->AvailableCoins(&vPIVCoins);

    COutPoint pivFeeInput;
    CScript pivFeeScript;
    CAmount nPIVInputValue = 0;
    bool foundPIVInput = false;

    // Sort by value ascending to find smallest suitable UTXO
    std::sort(vPIVCoins.begin(), vPIVCoins.end(),
        [](const COutput& a, const COutput& b) {
            return a.tx->tx->vout[a.i].nValue < b.tx->tx->vout[b.i].nValue;
        });

    for (const COutput& out : vPIVCoins) {
        CAmount value = out.tx->tx->vout[out.i].nValue;
        if (value >= nFee) {
            pivFeeInput = COutPoint(out.tx->tx->GetHash(), out.i);
            pivFeeScript = out.tx->tx->vout[out.i].scriptPubKey;
            nPIVInputValue = value;
            foundPIVInput = true;
            break;
        }
    }

    if (!foundPIVInput) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No suitable PIV UTXO found for transaction fee");
    }

    // Find the ZKHU note to unstake
    ZKHUNoteEntry* targetNote = nullptr;
    uint256 targetCm;

    if (!request.params[0].isNull()) {
        // User specified a specific note
        targetCm = uint256S(request.params[0].get_str());
        auto it = pwallet->khuData.mapZKHUNotes.find(targetCm);
        if (it == pwallet->khuData.mapZKHUNotes.end()) {
            throw JSONRPCError(RPC_INVALID_PARAMETER, "Note commitment not found in wallet");
        }
        targetNote = &it->second;
    } else {
        // Find oldest mature note
        int oldestHeight = INT_MAX;
        for (auto& pair : pwallet->khuData.mapZKHUNotes) {
            ZKHUNoteEntry& entry = pair.second;
            if (entry.fSpent) continue;
            if (!entry.IsMature(nCurrentHeight)) continue;

            if (entry.nConfirmedHeight < oldestHeight) {
                oldestHeight = entry.nConfirmedHeight;
                targetNote = &entry;
                targetCm = pair.first;
            }
        }
    }

    if (!targetNote) {
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS,
            "No mature ZKHU notes available for unstaking. "
            "Notes require 4320 blocks (3 days) maturity.");
    }

    if (targetNote->fSpent) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Note has already been spent");
    }

    if (!targetNote->IsMature(nCurrentHeight)) {
        int blocksRemaining = ZKHU_MATURITY_BLOCKS - targetNote->GetBlocksStaked(nCurrentHeight);
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("Note not mature yet. %d blocks remaining (approximately %.1f days)",
                     blocksRemaining, blocksRemaining / 1440.0));
    }

    // Get the corresponding Sapling note from wallet using GetNotes()
    // This properly decrypts the note and retrieves the correct rcm (randomness)
    const SaplingOutPoint& saplingOp = targetNote->op;
    std::vector<SaplingOutPoint> saplingOutpoints = {saplingOp};
    std::vector<SaplingNoteEntry> saplingEntries;

    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Sapling not enabled in wallet");
    }

    saplingMan->GetNotes(saplingOutpoints, saplingEntries);

    if (saplingEntries.empty()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            "Could not retrieve Sapling note data. The note may not belong to this wallet "
            "or the wallet may need to be rescanned.");
    }

    const SaplingNoteEntry& noteEntry = saplingEntries[0];
    const libzcash::SaplingNote& note = noteEntry.note;  // Contains correct rcm!
    libzcash::SaplingPaymentAddress saplingAddr = noteEntry.address;
    CAmount principal = note.value();

    // Get the spending key
    libzcash::SaplingExtendedSpendingKey sk;
    if (!pwallet->GetSaplingExtendedSpendingKey(saplingAddr, sk)) {
        throw JSONRPCError(RPC_WALLET_ERROR, "Spending key not found for note address");
    }

    // Calculate yield bonus based on stake duration and R_annual
    KhuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    int blocksStaked = targetNote->GetBlocksStaked(nCurrentHeight);
    uint32_t daysStaked = blocksStaked / 1440;

    CAmount yieldBonus = 0;
    if (R_annual > 0 && daysStaked > 0) {
        // Linear yield calculation: principal * R_annual/10000 * daysStaked/365
        CAmount annualYield = (principal * R_annual) / 10000;
        yieldBonus = (annualYield * daysStaked) / 365;
    }

    // CLAUDE.md §2.1: Fee is separate in PIV, NOT deducted from KHU output
    CAmount totalKHUOutput = principal + yieldBonus;
    if (totalKHUOutput <= 0) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Output amount would be zero or negative");
    }

    // PIV change output (if any)
    CAmount nPIVChange = nPIVInputValue - nFee;

    // Get witness and anchor for the note
    std::vector<SaplingOutPoint> ops = {saplingOp};
    std::vector<Optional<SaplingWitness>> witnesses;
    uint256 anchor;
    saplingMan->GetSaplingNoteWitnesses(ops, witnesses, anchor);

    if (witnesses.empty() || !witnesses[0]) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            "Missing witness for ZKHU note. The witness cache may be incomplete. "
            "Try restarting the wallet or running a rescan.");
    }

    // Build transaction using TransactionBuilder
    TransactionBuilder builder(consensus, pwallet);
    builder.SetFee(nFee);
    builder.SetType(CTransaction::TxType::KHU_UNSTAKE);  // Set type BEFORE building

    // Create and serialize UNSTAKE payload with note commitment (Phase 5/8 fix)
    // This allows consensus to look up the note directly by cm without nullifier mapping
    CUnstakeKHUPayload unstakePayload(targetCm);
    CDataStream payloadStream(SER_NETWORK, PROTOCOL_VERSION);
    payloadStream << unstakePayload;
    builder.SetExtraPayload(std::vector<uint8_t>(payloadStream.begin(), payloadStream.end()));

    // Add the Sapling spend (ZKHU note)
    builder.AddSaplingSpend(sk.expsk, note, anchor, witnesses[0].get());

    // Add PIV transparent input for fee (CLAUDE.md §2.1)
    builder.AddTransparentInput(pivFeeInput, pivFeeScript, nPIVInputValue);

    // Add transparent KHU_T output (principal + yield - NO fee deduction)
    CPubKey newKey;
    if (!pwallet->GetKeyFromPool(newKey, false)) {
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for KHU output");
    }
    builder.AddTransparentOutput(newKey.GetID(), totalKHUOutput);

    // Add PIV change output if needed
    if (nPIVChange > 0) {
        CPubKey pivChangeKey;
        if (!pwallet->GetKeyFromPool(pivChangeKey, false)) {
            throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Error: Keypool ran out for PIV change");
        }
        builder.AddTransparentOutput(pivChangeKey.GetID(), nPIVChange);
    }

    // Build with dummy signatures first
    TransactionBuilderResult buildResult = builder.Build(true);
    if (buildResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to build unstake transaction: %s", buildResult.GetError()));
    }

    // Clear dummy signatures/proofs before creating real ones
    builder.ClearProofsAndSignatures();

    // Prove and sign
    TransactionBuilderResult proveResult = builder.ProveAndSign();
    if (proveResult.IsError()) {
        throw JSONRPCError(RPC_WALLET_ERROR,
            strprintf("Failed to prove/sign unstake transaction: %s", proveResult.GetError()));
    }

    CTransaction finalTx = proveResult.GetTxOrThrow();

    // Transaction type was already set via builder.SetType() before Build()
    // DO NOT modify the transaction after Build() - it would invalidate Sapling proofs
    CTransactionRef txRef = MakeTransactionRef(finalTx);

    // Broadcast transaction
    CValidationState validationState;
    if (!AcceptToMemoryPool(mempool, validationState, txRef, false, nullptr)) {
        throw JSONRPCError(RPC_TRANSACTION_REJECTED,
            strprintf("Transaction rejected: %s", FormatStateMessage(validationState)));
    }

    // Mark the note as spent locally (consensus will verify)
    MarkZKHUNoteSpent(pwallet, targetNote->nullifier);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txRef->GetHash().GetHex());
    result.pushKV("principal", ValueFromAmount(principal));
    result.pushKV("yield_bonus", ValueFromAmount(yieldBonus));
    result.pushKV("total", ValueFromAmount(totalKHUOutput)); // Full amount (fee is separate in PIV)
    result.pushKV("fee", ValueFromAmount(nFee));
    result.pushKV("stake_duration_blocks", blocksStaked);
    result.pushKV("stake_duration_days", (double)daysStaked);

    return result;
}

/**
 * khuliststaked - List staked ZKHU notes (Phase 8b)
 *
 * Lists all ZKHU notes owned by this wallet with their staking status.
 */
static UniValue khuliststaked(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() > 0) {
        throw std::runtime_error(
            "khuliststaked\n"
            "\nList all staked ZKHU notes belonging to this wallet.\n"
            "\nResult:\n"
            "[\n"
            "  {\n"
            "    \"note_commitment\": \"hash\",  (string) Note commitment (cm)\n"
            "    \"amount\": n,                (numeric) Staked amount\n"
            "    \"stake_height\": n,          (numeric) Stake start height\n"
            "    \"blocks_staked\": n,         (numeric) Blocks since stake\n"
            "    \"is_mature\": true|false,    (boolean) Can be unstaked (>= 4320 blocks)\n"
            "    \"estimated_yield\": n        (numeric) Estimated yield bonus\n"
            "  },\n"
            "  ...\n"
            "]\n"
            "\nExamples:\n"
            + HelpExampleCli("khuliststaked", "")
            + HelpExampleRpc("khuliststaked", "")
        );
    }

    CWallet* pwallet = GetWalletForJSONRPCRequest(request);
    if (!pwallet) {
        throw JSONRPCError(RPC_WALLET_NOT_FOUND, "Wallet not found");
    }

    LOCK2(cs_main, pwallet->cs_wallet);

    int nCurrentHeight = chainActive.Height();
    const uint32_t ZKHU_MATURITY_BLOCKS = 4320;

    // Get current R_annual for yield estimation
    KhuGlobalState state;
    uint16_t R_annual = 0;
    if (GetCurrentKHUState(state)) {
        R_annual = state.R_annual;
    }

    UniValue results(UniValue::VARR);

    // Iterate through wallet's ZKHU notes
    std::vector<ZKHUNoteEntry> unspentNotes = GetUnspentZKHUNotes(pwallet);

    for (const ZKHUNoteEntry& entry : unspentNotes) {
        int blocksStaked = entry.GetBlocksStaked(nCurrentHeight);
        bool isMature = entry.IsMature(nCurrentHeight);

        // Estimate yield (approximation for display)
        CAmount estimatedYield = 0;
        if (R_annual > 0 && blocksStaked >= (int)ZKHU_MATURITY_BLOCKS) {
            uint32_t daysStaked = blocksStaked / 1440;
            CAmount annualYield = (entry.amount * R_annual) / 10000;
            estimatedYield = (annualYield * daysStaked) / 365;
        }

        UniValue noteObj(UniValue::VOBJ);
        noteObj.pushKV("note_commitment", entry.cm.GetHex());
        noteObj.pushKV("amount", ValueFromAmount(entry.amount));
        noteObj.pushKV("stake_height", (int64_t)entry.nStakeStartHeight);
        noteObj.pushKV("blocks_staked", blocksStaked);
        noteObj.pushKV("is_mature", isMature);
        noteObj.pushKV("estimated_yield", ValueFromAmount(estimatedYield));

        results.push_back(noteObj);
    }

    return results;
}

// KHU Wallet RPC command table
static const CRPCCommand khuWalletCommands[] = {
    //  category    name                      actor (function)            okSafe  argNames
    //  ----------- ------------------------  ------------------------    ------  ----------
    // Phase 8a - Transparent KHU_T operations
    { "khu",        "khubalance",             &khubalance,                true,   {} },
    { "khu",        "khulistunspent",         &khulistunspent,            true,   {"minconf", "maxconf"} },
    { "khu",        "khumint",                &khumint,                   false,  {"amount"} },
    { "khu",        "khuredeem",              &khuredeem,                 false,  {"amount"} },
    { "khu",        "khugetinfo",             &khugetinfo,                true,   {} },
    { "khu",        "khusend",                &khusend,                   false,  {"address", "amount", "comment"} },
    { "khu",        "khurescan",              &khurescan,                 false,  {"startheight"} },
    // Phase 8b - ZKHU staking operations (Sapling)
    { "khu",        "khustake",               &khustake,                  false,  {"amount"} },
    { "khu",        "khuunstake",             &khuunstake,                false,  {"note_commitment"} },
    { "khu",        "khuliststaked",          &khuliststaked,             true,   {} },
};

void RegisterKHUWalletRPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(khuWalletCommands); vcidx++)
        t.appendCommand(khuWalletCommands[vcidx].name, &khuWalletCommands[vcidx]);
}
