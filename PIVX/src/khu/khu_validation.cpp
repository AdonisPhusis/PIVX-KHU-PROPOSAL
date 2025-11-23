// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_validation.h"

#include "chain.h"
#include "consensus/params.h"
#include "khu/khu_commitment.h"
#include "khu/khu_commitmentdb.h"
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"
#include "khu/khu_state.h"
#include "khu/khu_statedb.h"
#include "primitives/block.h"
#include "sync.h"
#include "util/system.h"
#include "validation.h"

#include <memory>

// Global KHU state database
static std::unique_ptr<CKHUStateDB> pkhustatedb;

// Global KHU commitment database (Phase 3: Masternode Finality)
static std::unique_ptr<CKHUCommitmentDB> pkhucommitmentdb;

// KHU state lock (protects state transitions)
static RecursiveMutex cs_khu;

bool InitKHUStateDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pkhustatedb.reset();
        pkhustatedb = std::make_unique<CKHUStateDB>(nCacheSize, false, fReindex);
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize KHU state database: %s\n", e.what());
        return false;
    }
}

CKHUStateDB* GetKHUStateDB()
{
    return pkhustatedb.get();
}

bool InitKHUCommitmentDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pkhucommitmentdb.reset();
        pkhucommitmentdb = std::make_unique<CKHUCommitmentDB>(nCacheSize, false, fReindex);
        LogPrint(BCLog::KHU, "KHU: Initialized commitment database (Phase 3 Finality)\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize KHU commitment database: %s\n", e.what());
        return false;
    }
}

CKHUCommitmentDB* GetKHUCommitmentDB()
{
    return pkhucommitmentdb.get();
}

bool GetCurrentKHUState(KhuGlobalState& state)
{
    LOCK(cs_main);

    CBlockIndex* pindex = chainActive.Tip();
    if (!pindex) {
        return false;
    }

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return false;
    }

    return db->ReadKHUState(pindex->nHeight, state);
}

bool ProcessKHUBlock(const CBlock& block,
                     CBlockIndex* pindex,
                     CCoinsViewCache& view,
                     CValidationState& validationState,
                     const Consensus::Params& consensusParams)
{
    LOCK(cs_khu);

    const int nHeight = pindex->nHeight;
    const uint256 hashBlock = pindex->GetBlockHash();

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return validationState.Error("khu-db-not-initialized");
    }

    // Load previous state (or genesis if first KHU block)
    KhuGlobalState prevState;
    if (nHeight > 0) {
        if (!db->ReadKHUState(nHeight - 1, prevState)) {
            // If previous state doesn't exist, use genesis state
            // This happens at KHU activation height
            prevState.SetNull();
            prevState.nHeight = nHeight - 1;
        } else {
            // ✅ FIX CVE-KHU-2025-002: Vérifier les invariants de l'état chargé
            // CRITICAL: Without this check, a corrupted DB with invalid state (C != U)
            // would be loaded and used as the base for all future blocks, permanently
            // breaking the sacred invariants.
            if (!prevState.CheckInvariants()) {
                return validationState.Error(strprintf(
                    "khu-corrupted-prev-state: Previous state at height %d has invalid invariants (C=%d U=%d Cr=%d Ur=%d)",
                    nHeight - 1, prevState.C, prevState.U, prevState.Cr, prevState.Ur));
            }
        }
    } else {
        // Genesis block
        prevState.SetNull();
        prevState.nHeight = -1;
    }

    // Create new state (copy from previous)
    KhuGlobalState newState = prevState;

    // Update block linkage
    newState.nHeight = nHeight;
    newState.hashBlock = hashBlock;
    newState.hashPrevState = prevState.GetHash();

    // PHASE 2: Process MINT/REDEEM transactions
    // Ordre canonique immuable (cf. blueprints):
    // 1. ApplyDailyYieldIfNeeded() — Phase 3 (non implémenté)
    // 2. ProcessKHUTransactions() — Phase 2 (MINT/REDEEM)
    // 3. ApplyBlockReward() — Future
    // 4. CheckInvariants()
    // 5. PersistState()

    for (const auto& tx : block.vtx) {
        if (tx->nType == CTransaction::TxType::KHU_MINT) {
            if (!ApplyKHUMint(*tx, newState, view, nHeight)) {
                return validationState.Error(strprintf("Failed to apply KHU MINT at height %d", nHeight));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            if (!ApplyKHURedeem(*tx, newState, view, nHeight)) {
                return validationState.Error(strprintf("Failed to apply KHU REDEEM at height %d", nHeight));
            }
        }
    }

    // Verify invariants (CRITICAL)
    if (!newState.CheckInvariants()) {
        return validationState.Error(strprintf("KHU invariants violated at height %d", nHeight));
    }

    // Persist state to database
    if (!db->WriteKHUState(nHeight, newState)) {
        return validationState.Error(strprintf("Failed to write KHU state at height %d", nHeight));
    }

    LogPrint(BCLog::NET, "KHU: Processed block %d, C=%d U=%d Cr=%d Ur=%d\n",
             nHeight, newState.C, newState.U, newState.Cr, newState.Ur);

    return true;
}

bool DisconnectKHUBlock(CBlockIndex* pindex,
                       CValidationState& validationState)
{
    LOCK(cs_khu);

    const int nHeight = pindex->nHeight;

    CKHUStateDB* db = GetKHUStateDB();
    if (!db) {
        return validationState.Error("khu-db-not-initialized");
    }

    // PHASE 3: Check cryptographic finality via commitments (V6_0+ only)
    // NOTE: DisconnectKHUBlock is only called if NetworkUpgradeActive(V6_0) in validation.cpp
    // but we double-check here for clarity and safety
    CKHUCommitmentDB* commitmentDB = GetKHUCommitmentDB();
    if (commitmentDB) {
        uint32_t latestFinalized = commitmentDB->GetLatestFinalizedHeight();

        // Cannot reorg finalized blocks with quorum commitments
        if (nHeight <= latestFinalized) {
            LogPrint(BCLog::KHU, "KHU: Rejecting reorg of finalized block %d (latest finalized: %d)\n",
                     nHeight, latestFinalized);
            return validationState.Error(strprintf(
                "khu-reorg-finalized: Cannot reorg block %d (finalized at %d with LLMQ commitment)",
                nHeight, latestFinalized));
        }
    }

    // PHASE 1/3: Validate reorg depth (12 blocks maximum)
    // This is a CONSENSUS RULE for KHU state integrity
    const int KHU_FINALITY_DEPTH = 12;  // LLMQ finality depth

    CBlockIndex* pindexTip = chainActive.Tip();
    if (pindexTip) {
        int reorgDepth = pindexTip->nHeight - nHeight;
        if (reorgDepth > KHU_FINALITY_DEPTH) {
            LogPrint(BCLog::KHU, "KHU: Rejecting reorg depth %d (max %d blocks)\n",
                     reorgDepth, KHU_FINALITY_DEPTH);
            return validationState.Error(strprintf(
                "khu-reorg-too-deep: KHU reorg depth %d exceeds maximum %d blocks",
                reorgDepth, KHU_FINALITY_DEPTH));
        }
    }

    // Erase state at this height
    if (!db->EraseKHUState(nHeight)) {
        return validationState.Error(strprintf("Failed to erase KHU state at height %d", nHeight));
    }

    // Phase 3: Also erase commitment if present (non-finalized)
    if (commitmentDB && commitmentDB->HaveCommitment(nHeight)) {
        if (!commitmentDB->EraseCommitment(nHeight)) {
            LogPrint(BCLog::KHU, "KHU: Warning - failed to erase commitment at height %d during reorg\n", nHeight);
            // Non-fatal - continue with reorg
        }
    }

    LogPrint(BCLog::KHU, "KHU: Disconnected block %d\n", nHeight);

    return true;
}
