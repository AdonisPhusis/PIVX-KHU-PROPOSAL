// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_STATE_H
#define PIVX_KHU_STATE_H

#include "amount.h"
#include "logging.h"
#include "serialize.h"
#include "uint256.h"

#include <stdint.h>

/**
 * KhuGlobalState - Global state for KHU colored coin system
 *
 * This struct represents the canonical state of the KHU system at a given block height.
 * It tracks the two dual systems: C/U (collateral/supply) and Cr/Ur (reward pool).
 *
 * INVARIANTS (SACRED):
 * - C == U (collateral equals supply, except genesis where both are 0)
 * - Cr == Ur (reward collateral equals unstake rights, except genesis where both are 0)
 * - T >= 0 (DAO Treasury must be non-negative)
 *
 * These invariants MUST be preserved after every block operation.
 */
struct KhuGlobalState
{
    // Main circulation (C/U system)
    CAmount C;   // Collateral (PIV burned backing KHU_T)
    CAmount U;   // Supply (KHU_T in circulation)

    // Reward circulation (Cr/Ur system)
    CAmount Cr;  // Reward collateral (pool for staking rewards)
    CAmount Ur;  // Unstake rights (total accumulated yield across all stakers)

    // DAO Treasury (Phase 6.3)
    CAmount T;   // DAO Treasury internal pool

    // Governance parameters
    uint32_t R_annual;        // Annual yield rate (basis points: 1500 = 15.00%)
    uint32_t R_MAX_dynamic;   // Maximum allowed R% voted by DOMC (basis points)
    uint32_t last_yield_update_height;  // Last block where daily yield was applied

    // DOMC Governance (Phase 6.2) - Scalaires uniquement
    uint32_t domc_cycle_start;           // Height where current DOMC cycle started
    uint32_t domc_cycle_length;          // 172800 blocks (constant)
    uint32_t domc_commit_phase_start;    // cycle_start + 132480
    uint32_t domc_reveal_deadline;       // cycle_start + 152640

    // NOTE: DOMC votes (commits/reveals) sont dans CKHUDomcDB (pas ici)
    // NOTE: Staked notes sont dans DB ZKHU (pas ici)

    // Block linkage
    uint32_t nHeight;      // Block height of this state
    uint256 hashBlock;     // Block hash for this state
    uint256 hashPrevState; // Hash of previous state (for chain validation)

    KhuGlobalState()
    {
        SetNull();
    }

    void SetNull()
    {
        C = 0;
        U = 0;
        Cr = 0;
        Ur = 0;
        T = 0;
        R_annual = 0;
        R_MAX_dynamic = 0;
        last_yield_update_height = 0;
        domc_cycle_start = 0;
        domc_cycle_length = 0;
        domc_commit_phase_start = 0;
        domc_reveal_deadline = 0;
        nHeight = 0;
        hashBlock.SetNull();
        hashPrevState.SetNull();
    }

    bool IsNull() const
    {
        return (nHeight == 0 && hashBlock.IsNull());
    }

    /**
     * CheckInvariants - Verify the sacred KHU invariants
     *
     * RULES:
     * 1. C == U (except genesis where both are 0)
     * 2. Cr == Ur (except genesis where both are 0)
     * 3. T >= 0 (DAO Treasury must be non-negative)
     * 4. All amounts must be non-negative
     *
     * @return true if all invariants hold, false otherwise
     */
    bool CheckInvariants() const
    {
        // All amounts must be non-negative (including T)
        if (C < 0 || U < 0 || Cr < 0 || Ur < 0 || T < 0) {
            return false;
        }

        // C/U invariant: either both 0 (genesis) or C == U
        bool cu_ok = (U == 0 && C == 0) || (C == U);

        // Cr/Ur invariant: either both 0 (genesis) or Cr == Ur
        bool crur_ok = (Ur == 0 && Cr == 0) || (Cr == Ur);

        // ALARM: Log invariant violations for debugging
        if (!cu_ok || !crur_ok) {
            LogPrintf("KHU INVARIANT VIOLATION: C=%lld U=%lld Cr=%lld Ur=%lld T=%lld\n",
                      (long long)C, (long long)U, (long long)Cr, (long long)Ur, (long long)T);
        }

        return cu_ok && crur_ok;
    }

    /**
     * GetHash - Compute deterministic hash of this state
     *
     * Used for state chain validation and consensus.
     * All fields are serialized in canonical order.
     */
    uint256 GetHash() const;

    SERIALIZE_METHODS(KhuGlobalState, obj)
    {
        READWRITE(obj.C);
        READWRITE(obj.U);
        READWRITE(obj.Cr);
        READWRITE(obj.Ur);
        READWRITE(obj.T);
        READWRITE(obj.R_annual);
        READWRITE(obj.R_MAX_dynamic);
        READWRITE(obj.last_yield_update_height);
        READWRITE(obj.domc_cycle_start);
        READWRITE(obj.domc_cycle_length);
        READWRITE(obj.domc_commit_phase_start);
        READWRITE(obj.domc_reveal_deadline);
        READWRITE(obj.nHeight);
        READWRITE(obj.hashBlock);
        READWRITE(obj.hashPrevState);
    }
};

#endif // PIVX_KHU_STATE_H
