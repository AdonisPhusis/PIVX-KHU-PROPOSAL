// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_validation.h"

#include "chain.h"
#include "chainparams.h"
#include "khu/khu_commitment.h"
#include "khu/khu_commitmentdb.h"
#include "khu/khu_state.h"
#include "rpc/server.h"
#include "sync.h"
#include "validation.h"

#include <univalue.h>

/**
 * getkhustate - Get current KHU global state
 *
 * Returns the KHU state at the current chain tip, including:
 * - C/U (collateral/supply)
 * - Cr/Ur (reward pool)
 * - R% governance parameters
 * - Block linkage info
 * - Invariant validation status
 *
 * Usage:
 *   getkhustate
 *
 * Returns:
 * {
 *   "height": n,           (numeric) Block height
 *   "blockhash": "hash",   (string) Block hash
 *   "C": n,                (numeric) Collateral (PIV burned)
 *   "U": n,                (numeric) Supply (KHU_T in circulation)
 *   "Cr": n,               (numeric) Reward collateral
 *   "Ur": n,               (numeric) Unstake rights
 *   "R_annual": n,         (numeric) Annual yield rate (centièmes)
 *   "R_annual_pct": x.xx,  (numeric) Annual yield rate (percentage)
 *   "R_MAX_dynamic": n,    (numeric) Maximum R% allowed
 *   "invariants_ok": true|false,  (boolean) Invariants validation
 *   "hashState": "hash",   (string) Hash of this state
 *   "hashPrevState": "hash" (string) Hash of previous state
 * }
 */
static UniValue getkhustate(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 0) {
        throw std::runtime_error(
            "getkhustate\n"
            "\nReturns the current KHU global state.\n"
            "\nResult:\n"
            "{\n"
            "  \"height\": n,           (numeric) Block height\n"
            "  \"blockhash\": \"hash\",   (string) Block hash\n"
            "  \"C\": n,                (numeric) Collateral (PIV burned backing KHU_T)\n"
            "  \"U\": n,                (numeric) Supply (KHU_T in circulation)\n"
            "  \"Cr\": n,               (numeric) Reward collateral pool\n"
            "  \"Ur\": n,               (numeric) Unstake rights (accumulated yield)\n"
            "  \"R_annual\": n,         (numeric) Annual yield rate (centièmes: 2555 = 25.55%)\n"
            "  \"R_annual_pct\": x.xx,  (numeric) Annual yield rate (percentage)\n"
            "  \"R_MAX_dynamic\": n,    (numeric) Maximum R% allowed by DOMC\n"
            "  \"last_yield_update_height\": n, (numeric) Last yield update block\n"
            "  \"last_domc_height\": n, (numeric) Last DOMC cycle completion\n"
            "  \"invariants_ok\": true|false,  (boolean) Are invariants satisfied?\n"
            "  \"hashState\": \"hash\",   (string) Hash of this state\n"
            "  \"hashPrevState\": \"hash\" (string) Hash of previous state\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getkhustate", "")
            + HelpExampleRpc("getkhustate", "")
        );
    }

    LOCK(cs_main);

    KhuGlobalState state;
    if (!GetCurrentKHUState(state)) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Unable to load KHU state");
    }

    UniValue result(UniValue::VOBJ);

    result.pushKV("height", (int64_t)state.nHeight);
    result.pushKV("blockhash", state.hashBlock.GetHex());
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("T", ValueFromAmount(state.T));
    result.pushKV("R_annual", (int64_t)state.R_annual);
    result.pushKV("R_annual_pct", state.R_annual / 100.0);
    result.pushKV("R_MAX_dynamic", (int64_t)state.R_MAX_dynamic);
    result.pushKV("last_yield_update_height", (int64_t)state.last_yield_update_height);
    result.pushKV("domc_cycle_start", (int64_t)state.domc_cycle_start);
    result.pushKV("domc_cycle_length", (int64_t)state.domc_cycle_length);
    result.pushKV("domc_commit_phase_start", (int64_t)state.domc_commit_phase_start);
    result.pushKV("domc_reveal_deadline", (int64_t)state.domc_reveal_deadline);
    result.pushKV("invariants_ok", state.CheckInvariants());
    result.pushKV("hashState", state.GetHash().GetHex());
    result.pushKV("hashPrevState", state.hashPrevState.GetHex());

    return result;
}

/**
 * getkhustatecommitment - Get KHU state commitment at a block height
 *
 * PHASE 3: Masternode Finality
 *
 * Returns the LLMQ-signed commitment for KHU state at the specified height.
 * Commitments provide cryptographic finality for KHU state, preventing
 * state divergence across the network.
 *
 * Usage:
 *   getkhustatecommitment height
 *
 * Arguments:
 * 1. height    (numeric, required) The block height
 *
 * Returns:
 * {
 *   "height": n,             (numeric) Block height
 *   "hashState": "hash",     (string) State hash (SHA256 of C, U, Cr, Ur, height)
 *   "quorumHash": "hash",    (string) LLMQ quorum identifier
 *   "signature": "hex",      (string) BLS aggregate signature
 *   "signers": n,            (numeric) Number of masternodes who signed
 *   "quorumSize": n,         (numeric) Total quorum members
 *   "finalized": true|false, (boolean) Has quorum threshold (>= 60%)
 *   "commitmentHash": "hash" (string) Hash of the commitment itself
 * }
 */
static UniValue getkhustatecommitment(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 1) {
        throw std::runtime_error(
            "getkhustatecommitment height\n"
            "\nReturns KHU state commitment for a given block height (Phase 3: Masternode Finality).\n"
            "\nArguments:\n"
            "1. height    (numeric, required) The block height\n"
            "\nResult:\n"
            "{\n"
            "  \"height\": n,             (numeric) Block height\n"
            "  \"hashState\": \"hash\",     (string) State hash (SHA256 of C, U, Cr, Ur, height)\n"
            "  \"quorumHash\": \"hash\",    (string) LLMQ quorum identifier\n"
            "  \"signature\": \"hex\",      (string) BLS aggregate signature\n"
            "  \"signers\": n,            (numeric) Number of masternodes who signed\n"
            "  \"quorumSize\": n,         (numeric) Total quorum members\n"
            "  \"finalized\": true|false, (boolean) Has quorum threshold (>= 60%)\n"
            "  \"commitmentHash\": \"hash\" (string) Hash of the commitment itself\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("getkhustatecommitment", "1000000")
            + HelpExampleRpc("getkhustatecommitment", "1000000")
        );
    }

    uint32_t nHeight = request.params[0].get_int();

    CKHUCommitmentDB* commitmentDB = GetKHUCommitmentDB();
    if (!commitmentDB) {
        throw JSONRPCError(RPC_INTERNAL_ERROR, "KHU commitment database not initialized");
    }

    KhuStateCommitment commitment;
    if (!commitmentDB->ReadCommitment(nHeight, commitment)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          strprintf("No commitment found at height %d", nHeight));
    }

    // Count signers
    int signerCount = 0;
    for (bool signer : commitment.signers) {
        if (signer) {
            signerCount++;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", (int)commitment.nHeight);
    result.pushKV("hashState", commitment.hashState.GetHex());
    result.pushKV("quorumHash", commitment.quorumHash.GetHex());
    result.pushKV("signature", commitment.sig.ToString());
    result.pushKV("signers", signerCount);
    result.pushKV("quorumSize", (int)commitment.signers.size());
    result.pushKV("finalized", commitment.HasQuorum());
    result.pushKV("commitmentHash", commitment.GetHash().GetHex());

    return result;
}

// RPC command table (to be registered in rpc/register.cpp)
static const CRPCCommand commands[] = {
    //  category    name                      actor (function)            argNames
    //  ----------- ------------------------  ------------------------    ----------
    { "khu",        "getkhustate",            &getkhustate,               {} },
    { "khu",        "getkhustatecommitment",  &getkhustatecommitment,     {"height"} },
};

void RegisterKHURPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
