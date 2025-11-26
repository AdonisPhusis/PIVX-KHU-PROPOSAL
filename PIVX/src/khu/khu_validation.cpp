// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_validation.h"

#include "chain.h"
#include "consensus/params.h"
#include "khu/khu_commitment.h"
#include "khu/khu_commitmentdb.h"
#include "khu/khu_dao.h"
#include "khu/khu_domc.h"
#include "khu/khu_domcdb.h"
#include "khu/khu_domc_tx.h"
#include "khu/khu_mint.h"
#include "khu/khu_redeem.h"
#include "khu/khu_stake.h"
#include "khu/khu_state.h"
#include "khu/khu_statedb.h"
#include "khu/khu_unstake.h"
#include "khu/khu_yield.h"
#include "khu/zkhu_db.h"
#include "primitives/block.h"
#include "sync.h"
#include "util/system.h"
#include "validation.h"

#include <memory>

// Global KHU state database
static std::unique_ptr<CKHUStateDB> pkhustatedb;

// Global KHU commitment database (Phase 3: Masternode Finality)
static std::unique_ptr<CKHUCommitmentDB> pkhucommitmentdb;

// Global ZKHU database (Phase 4/5: Sapling notes, nullifiers, anchors)
static std::unique_ptr<CZKHUTreeDB> pzkhudb;

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

bool InitZKHUDB(size_t nCacheSize, bool fReindex)
{
    LOCK(cs_khu);

    try {
        pzkhudb.reset();
        pzkhudb = std::make_unique<CZKHUTreeDB>(nCacheSize, false, fReindex);
        LogPrint(BCLog::KHU, "KHU: Initialized ZKHU database (Phase 4/5 Sapling)\n");
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: Failed to initialize ZKHU database: %s\n", e.what());
        return false;
    }
}

CZKHUTreeDB* GetZKHUDB()
{
    return pzkhudb.get();
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
                     const Consensus::Params& consensusParams,
                     bool fJustCheck)
{
    LOCK(cs_khu);

    const int nHeight = pindex->nHeight;
    // Get hash from block directly - pindex->phashBlock may be nullptr during TestBlockValidity
    const uint256 hashBlock = block.GetHash();

    LogPrint(BCLog::KHU, "ProcessKHUBlock: height=%d, fJustCheck=%d, block=%s\n",
             nHeight, fJustCheck, hashBlock.ToString().substr(0, 16));

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

    LogPrint(BCLog::KHU, "ProcessKHUBlock: Before processing - C=%d U=%d Cr=%d Ur=%d (height=%d)\n",
             prevState.C, prevState.U, prevState.Cr, prevState.Ur, nHeight);

    // PHASE 6: Canonical order (CONSENSUS CRITICAL)
    // STEP 1: DOMC cycle boundary — Phase 6.2 (DOMC Governance)
    // STEP 2: DAO Treasury — Phase 6.3 (DAO Pool)
    // STEP 3: Daily Yield — Phase 6.1 (TODO)
    // STEP 4: KHU Transactions — Phase 2-4 (MINT/REDEEM/STAKE/UNSTAKE) + Phase 6.2 (DOMC)
    // STEP 5: Block Reward — Future
    // STEP 6: CheckInvariants()
    // STEP 7: PersistState()

    // STEP 0: Update R_MAX_dynamic based on year since activation
    // Formula: R_MAX_dynamic = max(400, 3700 - year × 100)
    // Decreases by 1% per year from 37% to 4% floor
    khu_domc::UpdateRMaxDynamic(newState, nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    // STEP 1: DOMC cycle boundary (Phase 6.2)
    // At cycle boundary: finalize previous cycle (calculate median R), initialize new cycle
    if (khu_domc::IsDomcCycleBoundary(nHeight, consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight)) {
        // Finalize previous cycle: calculate median(R) from reveals, update R_annual
        if (!khu_domc::FinalizeDomcCycle(newState, nHeight, consensusParams)) {
            return validationState.Error("domc-finalize-failed");
        }

        // Initialize new cycle: update cycle_start, commit_phase_start, reveal_deadline
        khu_domc::InitializeDomcCycle(newState, nHeight);

        LogPrint(BCLog::KHU, "ProcessKHUBlock: DOMC cycle boundary at height %u, R_annual=%u (%.2f%%), R_MAX=%u (%.2f%%)\n",
                 nHeight, newState.R_annual, newState.R_annual / 100.0,
                 newState.R_MAX_dynamic, newState.R_MAX_dynamic / 100.0);
    }

    // STEP 2: DAO Treasury accumulation (Phase 6.3)
    // Budget calculated on INITIAL state (before yield/transactions)
    if (!khu_dao::AccumulateDaoTreasuryIfNeeded(newState, nHeight, consensusParams)) {
        return validationState.Error("dao-treasury-failed");
    }

    // STEP 3: Daily Yield distribution (Phase 6.1)
    // Apply daily yield to all mature staked notes (every 1440 blocks)
    // This updates Ur += total_yield (Cr remains unchanged)
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if (khu_yield::ShouldApplyDailyYield(nHeight, V6_activation, newState.last_yield_update_height)) {
        if (!khu_yield::ApplyDailyYield(newState, nHeight, V6_activation)) {
            return validationState.Error("daily-yield-failed");
        }

        LogPrint(BCLog::KHU, "ProcessKHUBlock: Applied daily yield at height %u, Ur=%d\n",
                 nHeight, newState.Ur);
    }

    // STEP 4: Process KHU transactions
    // Note: Basic transaction validation was done by CheckSpecialTx
    // Here we apply transactions to update newState and view.
    // DB writes are conditional on !fJustCheck (handled inside Apply* functions or here)
    int nKHUTxCount = 0;
    for (const auto& tx : block.vtx) {
        if (tx->nType == CTransaction::TxType::KHU_MINT) {
            nKHUTxCount++;
            // ApplyKHUMint modifies global KHU UTXO map - only call when !fJustCheck
            // Transaction structure validation was already done by CheckSpecialTx
            if (!fJustCheck) {
                if (!ApplyKHUMint(*tx, newState, view, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU MINT at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_MINT tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            nKHUTxCount++;
            // ApplyKHURedeem modifies global KHU UTXO map - only call when !fJustCheck
            // Transaction structure validation was already done by CheckSpecialTx
            if (!fJustCheck) {
                if (!ApplyKHURedeem(*tx, newState, view, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU REDEEM at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_REDEEM tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_STAKE) {
            // Phase 4: KHU_T → ZKHU (state unchanged: C, U, Cr, Ur)
            // ApplyKHUStake writes to ZKHU DB, so only call when !fJustCheck
            // For fJustCheck=true, we validate structure but skip DB writes
            nKHUTxCount++;
            if (!fJustCheck) {
                if (!ApplyKHUStake(*tx, view, newState, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU STAKE at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_STAKE tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_UNSTAKE) {
            // Phase 4: ZKHU → KHU_T + bonus (double flux: C+, U+, Cr-, Ur-)
            // ApplyKHUUnstake reads from ZKHU DB and modifies state
            // For fJustCheck=true, skip since it needs prior STAKE data
            nKHUTxCount++;
            if (!fJustCheck) {
                if (!ApplyKHUUnstake(*tx, view, newState, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply KHU UNSTAKE at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_UNSTAKE tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_COMMIT) {
            // Phase 6.2: DOMC commit vote (Hash(R || salt))
            // Validation runs in both paths, DB write only when !fJustCheck
            nKHUTxCount++;
            if (!ValidateDomcCommitTx(*tx, validationState, newState, nHeight, consensusParams)) {
                return false; // validationState already set
            }
            if (!fJustCheck) {
                if (!ApplyDomcCommitTx(*tx, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply DOMC COMMIT at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_DOMC_COMMIT tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_REVEAL) {
            // Phase 6.2: DOMC reveal vote (R + salt)
            // Validation runs in both paths, DB write only when !fJustCheck
            nKHUTxCount++;
            if (!ValidateDomcRevealTx(*tx, validationState, newState, nHeight, consensusParams)) {
                return false; // validationState already set
            }
            if (!fJustCheck) {
                if (!ApplyDomcRevealTx(*tx, nHeight)) {
                    return validationState.Error(strprintf("Failed to apply DOMC REVEAL at height %d", nHeight));
                }
            }
            LogPrint(BCLog::KHU, "ProcessKHUBlock: KHU_DOMC_REVEAL tx %s (fJustCheck=%d)\n",
                     tx->GetHash().ToString().substr(0, 16), fJustCheck);
        }
    }
    LogPrint(BCLog::KHU, "ProcessKHUBlock: Processed %d KHU transactions at height %d\n", nKHUTxCount, nHeight);

    // Verify invariants (CRITICAL)
    if (!newState.CheckInvariants()) {
        LogPrint(BCLog::KHU, "ProcessKHUBlock: FAIL - Invariants violated at height %d (C=%d U=%d Cr=%d Ur=%d)\n",
                 nHeight, newState.C, newState.U, newState.Cr, newState.Ur);
        return validationState.Error(strprintf("KHU invariants violated at height %d", nHeight));
    }

    LogPrint(BCLog::KHU, "ProcessKHUBlock: After processing - C=%d U=%d Cr=%d Ur=%d (height=%d, fJustCheck=%d)\n",
             newState.C, newState.U, newState.Cr, newState.Ur, nHeight, fJustCheck);

    // Persist state to database ONLY when not just checking
    if (!fJustCheck) {
        if (!db->WriteKHUState(nHeight, newState)) {
            LogPrint(BCLog::KHU, "ProcessKHUBlock: FAIL - Write state failed at height %d\n", nHeight);
            return validationState.Error(strprintf("Failed to write KHU state at height %d", nHeight));
        }
        LogPrint(BCLog::KHU, "ProcessKHUBlock: SUCCESS - Persisted state at height %d\n", nHeight);
    } else {
        LogPrint(BCLog::KHU, "ProcessKHUBlock: SUCCESS - Validated state at height %d (fJustCheck=true, no persist)\n", nHeight);
    }

    return true;
}

bool DisconnectKHUBlock(const CBlock& block,
                       CBlockIndex* pindex,
                       CValidationState& validationState,
                       CCoinsViewCache& view,
                       KhuGlobalState& khuState,
                       const Consensus::Params& consensusParams)
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

    // PHASE 4: Undo KHU transactions in REVERSE order
    // This restores the exact state by reversing all mutations from ProcessKHUBlock
    for (int i = block.vtx.size() - 1; i >= 0; i--) {
        const auto& tx = block.vtx[i];

        if (tx->nType == CTransaction::TxType::KHU_UNSTAKE) {
            // Reverse double flux: C-, U-, Cr+, Ur+ (opposite of Apply)
            if (!UndoKHUUnstake(*tx, view, khuState, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-unstake-failed",
                    strprintf("Failed to undo KHU UNSTAKE at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_STAKE) {
            // Restore KHU_T UTXO, erase ZKHU note (state unchanged: C, U, Cr, Ur)
            if (!UndoKHUStake(*tx, view, khuState, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-stake-failed",
                    strprintf("Failed to undo KHU STAKE at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_REVEAL) {
            // Phase 6.2: Undo DOMC reveal vote (erase from DB)
            if (!UndoDomcRevealTx(*tx, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-domc-reveal-failed",
                    strprintf("Failed to undo DOMC REVEAL at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_DOMC_COMMIT) {
            // Phase 6.2: Undo DOMC commit vote (erase from DB)
            if (!UndoDomcCommitTx(*tx, nHeight)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-domc-commit-failed",
                    strprintf("Failed to undo DOMC COMMIT at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_MINT) {
            // Phase 2: Undo MINT (C-, U-)
            if (!UndoKHUMint(*tx, khuState, view)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-mint-failed",
                    strprintf("Failed to undo KHU MINT at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        } else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            // Phase 2: Undo REDEEM (C+, U+)
            if (!UndoKHURedeem(*tx, khuState, view)) {
                return validationState.Invalid(false, REJECT_INVALID, "khu-undo-redeem-failed",
                    strprintf("Failed to undo KHU REDEEM at height %d (tx %s)",
                              nHeight, tx->GetHash().ToString()));
            }
        }
    }

    // PHASE 6: Undo Daily Yield (Phase 6.1)
    // Must be undone AFTER transactions, BEFORE DOMC/DAO (reverse order of Connect)
    uint32_t V6_activation = consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
    if (khu_yield::ShouldApplyDailyYield(nHeight, V6_activation, khuState.last_yield_update_height)) {
        if (!khu_yield::UndoDailyYield(khuState, nHeight, V6_activation)) {
            return validationState.Invalid(false, REJECT_INVALID, "undo-daily-yield-failed",
                strprintf("Failed to undo daily yield at height %d", nHeight));
        }

        LogPrint(BCLog::KHU, "DisconnectKHUBlock: Undid daily yield at height %u, Ur=%d\n",
                 nHeight, khuState.Ur);
    }

    // PHASE 6: Undo DOMC cycle finalization (Phase 6.2)
    // Must be undone AFTER Daily Yield, BEFORE DAO (reverse order of Connect)
    // At cycle boundary: undo R_annual update from previous cycle finalization
    if (khu_domc::IsDomcCycleBoundary(nHeight, V6_activation)) {
        if (!khu_domc::UndoFinalizeDomcCycle(khuState, nHeight, consensusParams)) {
            return validationState.Invalid(false, REJECT_INVALID, "undo-domc-cycle-failed",
                strprintf("Failed to undo DOMC cycle finalization at height %d", nHeight));
        }

        LogPrint(BCLog::KHU, "DisconnectKHUBlock: Undid DOMC cycle finalization at height %u, R_annual=%u\n",
                 nHeight, khuState.R_annual);
    }

    // PHASE 6: Undo DAO Treasury (Phase 6.3)
    // Must be undone AFTER DOMC (reverse order of Connect)
    if (!khu_dao::UndoDaoTreasuryIfNeeded(khuState, nHeight, consensusParams)) {
        return validationState.Invalid(false, REJECT_INVALID, "undo-dao-treasury-failed",
            strprintf("Failed to undo DAO treasury at height %d", nHeight));
    }

    // Verify invariants after UNDO operations (CRITICAL: ensures state integrity)
    if (!khuState.CheckInvariants()) {
        return validationState.Invalid(false, REJECT_INVALID, "khu-undo-invariant-failed",
            strprintf("KHU invariants violated after undo at height %d (C=%d U=%d Cr=%d Ur=%d)",
                      nHeight, khuState.C, khuState.U, khuState.Cr, khuState.Ur));
    }

    // Erase state at this height (previous state remains intact)
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

    LogPrint(BCLog::KHU, "KHU: Disconnected block %d (undone %zu transactions)\n", nHeight, block.vtx.size());

    return true;
}
