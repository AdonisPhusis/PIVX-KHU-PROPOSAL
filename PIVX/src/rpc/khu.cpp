// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_validation.h"

#include "chain.h"
#include "chainparams.h"
#include "khu/khu_state.h"
#include "rpc/server.h"
#include "rpc/util.h"
#include "sync.h"
#include "util/strencodings.h"
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

    result.pushKV("height", state.nHeight);
    result.pushKV("blockhash", state.hashBlock.GetHex());
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("R_annual", state.R_annual);
    result.pushKV("R_annual_pct", state.R_annual / 100.0);
    result.pushKV("R_MAX_dynamic", state.R_MAX_dynamic);
    result.pushKV("last_yield_update_height", state.last_yield_update_height);
    result.pushKV("last_domc_height", state.last_domc_height);
    result.pushKV("domc_cycle_start", state.domc_cycle_start);
    result.pushKV("domc_cycle_length", state.domc_cycle_length);
    result.pushKV("domc_phase_length", state.domc_phase_length);
    result.pushKV("invariants_ok", state.CheckInvariants());
    result.pushKV("hashState", state.GetHash().GetHex());
    result.pushKV("hashPrevState", state.hashPrevState.GetHex());

    return result;
}

// RPC command table (to be registered in rpc/register.cpp)
static const CRPCCommand commands[] = {
    //  category    name                actor (function)        argNames
    //  ----------- ----------------    ------------------      ----------
    { "khu",        "getkhustate",      &getkhustate,           {} },
};

void RegisterKHURPCCommands(CRPCTable& t)
{
    for (unsigned int vcidx = 0; vcidx < ARRAYLEN(commands); vcidx++)
        t.appendCommand(commands[vcidx].name, &commands[vcidx]);
}
