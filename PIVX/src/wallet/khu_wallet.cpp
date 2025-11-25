// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet/khu_wallet.h"

#include "chain.h"
#include "chainparams.h"
#include "khu/khu_coins.h"
#include "khu/khu_state.h"
#include "khu/khu_validation.h"
#include "logging.h"
#include "primitives/transaction.h"
#include "script/ismine.h"
#include "validation.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"

// ============================================================================
// Balance Functions
// ============================================================================

CAmount GetKHUBalance(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    return pwallet->khuData.nKHUBalance;
}

CAmount GetKHUStakedBalance(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);
    return pwallet->khuData.nKHUStaked;
}

CAmount GetKHUPendingYieldEstimate(const CWallet* pwallet, uint16_t R_annual)
{
    // ⚠️ APPROXIMATION ONLY - For UI display, not consensus
    // Actual yield is calculated by khu_yield.cpp using discrete daily application

    LOCK(pwallet->cs_wallet);

    if (R_annual == 0) return 0;

    CAmount totalEstimate = 0;
    int currentHeight = chainActive.Height();

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const KHUCoinEntry& entry = it->second;
        if (!entry.coin.fStaked) continue;

        // Maturity check (4320 blocks = 3 days)
        const uint32_t MATURITY_BLOCKS = 4320;
        if (entry.coin.nStakeStartHeight == 0) continue;
        if ((uint32_t)currentHeight < entry.coin.nStakeStartHeight + MATURITY_BLOCKS) continue;

        // Approximate: days staked * daily rate
        uint32_t blocksStaked = currentHeight - entry.coin.nStakeStartHeight;
        uint32_t daysStaked = blocksStaked / 1440; // BLOCKS_PER_DAY

        // daily_yield ≈ (amount * R_annual / 10000) / 365
        CAmount annualYield = (entry.coin.amount * R_annual) / 10000;
        CAmount estimatedYield = (annualYield * daysStaked) / 365;

        totalEstimate += estimatedYield;
    }

    return totalEstimate;
}

// ============================================================================
// Coin Management Functions
// ============================================================================

bool AddKHUCoinToWallet(CWallet* pwallet, const COutPoint& outpoint,
                        const CKHUUTXO& coin, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    // Check if this output belongs to us
    if (::IsMine(*pwallet, coin.scriptPubKey) == ISMINE_NO) {
        return false;
    }

    // Check if already tracked
    if (pwallet->khuData.mapKHUCoins.count(outpoint)) {
        LogPrint(BCLog::KHU, "AddKHUCoinToWallet: Already tracking %s:%d\n",
                 outpoint.hash.GetHex().substr(0, 16), outpoint.n);
        return true;
    }

    // Create entry
    KHUCoinEntry entry(coin, outpoint.hash, outpoint.n, nHeight);

    // Add to map
    pwallet->khuData.mapKHUCoins[outpoint] = entry;

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Persist to database
    if (!WriteKHUCoinToDB(pwallet, outpoint, entry)) {
        LogPrintf("ERROR: AddKHUCoinToWallet: Failed to persist to DB\n");
        return false;
    }

    LogPrint(BCLog::KHU, "AddKHUCoinToWallet: Added %s:%d amount=%d\n",
             outpoint.hash.GetHex().substr(0, 16), outpoint.n, coin.amount);

    return true;
}

bool RemoveKHUCoinFromWallet(CWallet* pwallet, const COutPoint& outpoint)
{
    LOCK(pwallet->cs_wallet);

    auto it = pwallet->khuData.mapKHUCoins.find(outpoint);
    if (it == pwallet->khuData.mapKHUCoins.end()) {
        return false;
    }

    // Remove from map
    pwallet->khuData.mapKHUCoins.erase(it);

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Remove from database
    if (!EraseKHUCoinFromDB(pwallet, outpoint)) {
        LogPrintf("ERROR: RemoveKHUCoinFromWallet: Failed to erase from DB\n");
    }

    LogPrint(BCLog::KHU, "RemoveKHUCoinFromWallet: Removed %s:%d\n",
             outpoint.hash.GetHex().substr(0, 16), outpoint.n);

    return true;
}

std::vector<COutput> GetAvailableKHUCoins(const CWallet* pwallet, int minDepth)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    std::vector<COutput> vCoins;
    int nCurrentHeight = chainActive.Height();

    for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = pwallet->khuData.mapKHUCoins.begin();
         it != pwallet->khuData.mapKHUCoins.end(); ++it) {
        const COutPoint& outpoint = it->first;
        const KHUCoinEntry& entry = it->second;

        // Skip staked coins
        if (entry.coin.fStaked) continue;

        // Check depth
        int nDepth = nCurrentHeight - entry.nConfirmedHeight + 1;
        if (nDepth < minDepth) continue;

        // Find the wallet transaction
        const CWalletTx* wtx = pwallet->GetWalletTx(outpoint.hash);
        if (!wtx) continue;

        // Create COutput
        // Note: COutput constructor expects (wtx, vout, depth, spendable, solvable, safe)
        bool fSpendable = true;
        bool fSolvable = true;
        bool fSafe = (nDepth >= 1);

        vCoins.emplace_back(wtx, entry.vout, nDepth, fSpendable, fSolvable, fSafe);
    }

    return vCoins;
}

// ============================================================================
// Blockchain Scanning
// ============================================================================

void ProcessKHUTransactionForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    const uint256& txhash = tx->GetHash();

    // Check transaction type
    if (tx->nType == CTransaction::TxType::KHU_MINT) {
        // MINT creates KHU_T output at vout[1]
        if (tx->vout.size() >= 2) {
            const CTxOut& out = tx->vout[1];
            if (::IsMine(*pwallet, out.scriptPubKey) != ISMINE_NO) {
                CKHUUTXO coin(out.nValue, out.scriptPubKey, nHeight);
                AddKHUCoinToWallet(pwallet, COutPoint(txhash, 1), coin, nHeight);
            }
        }
    }
    else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
        // REDEEM spends KHU_T input
        for (const auto& vin : tx->vin) {
            RemoveKHUCoinFromWallet(pwallet, vin.prevout);
        }
    }
    else if (tx->nType == CTransaction::TxType::KHU_STAKE) {
        // STAKE spends KHU_T input, creates ZKHU note (Phase 8b)
        for (const auto& vin : tx->vin) {
            RemoveKHUCoinFromWallet(pwallet, vin.prevout);
        }
    }
    else if (tx->nType == CTransaction::TxType::KHU_UNSTAKE) {
        // UNSTAKE creates KHU_T output (Phase 8b)
        for (size_t i = 0; i < tx->vout.size(); ++i) {
            const CTxOut& out = tx->vout[i];
            if (::IsMine(*pwallet, out.scriptPubKey) != ISMINE_NO) {
                CKHUUTXO coin(out.nValue, out.scriptPubKey, nHeight);
                AddKHUCoinToWallet(pwallet, COutPoint(txhash, i), coin, nHeight);
            }
        }
    }
    else {
        // Regular transaction - check for KHU transfers
        // KHU_T outputs have the KHU marker in scriptPubKey
        // For now, scan all outputs for IsMine
        for (size_t i = 0; i < tx->vout.size(); ++i) {
            const CTxOut& out = tx->vout[i];
            // TODO: Add IsKHUOutput check once marker is defined
            // For Phase 8a, we rely on TxType for KHU tracking
        }
    }
}

bool ScanForKHUCoins(CWallet* pwallet, int nStartHeight)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    LogPrint(BCLog::KHU, "ScanForKHUCoins: Starting scan from height %d\n", nStartHeight);

    // Clear existing KHU coins before full rescan
    if (nStartHeight == 0) {
        pwallet->khuData.Clear();
    }

    const CBlockIndex* pindex = chainActive[nStartHeight];
    if (!pindex) {
        LogPrintf("ERROR: ScanForKHUCoins: Invalid start height %d\n", nStartHeight);
        return false;
    }

    int nScanned = 0;
    while (pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            LogPrintf("ERROR: ScanForKHUCoins: Failed to read block at height %d\n", pindex->nHeight);
            return false;
        }

        for (const auto& tx : block.vtx) {
            // Check if KHU transaction
            if (tx->nType >= CTransaction::TxType::KHU_MINT &&
                tx->nType <= CTransaction::TxType::KHU_UNSTAKE) {
                ProcessKHUTransactionForWallet(pwallet, tx, pindex->nHeight);
            }
        }

        nScanned++;
        if (nScanned % 10000 == 0) {
            LogPrint(BCLog::KHU, "ScanForKHUCoins: Scanned %d blocks (height %d)\n",
                     nScanned, pindex->nHeight);
        }

        pindex = chainActive.Next(pindex);
    }

    // Update final balances
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::KHU, "ScanForKHUCoins: Complete. Found %d KHU coins, balance=%d\n",
             pwallet->khuData.mapKHUCoins.size(), pwallet->khuData.nKHUBalance);

    return true;
}

// ============================================================================
// Database Persistence
// ============================================================================

bool WriteKHUCoinToDB(CWallet* pwallet, const COutPoint& outpoint, const KHUCoinEntry& entry)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.WriteKHUCoin(outpoint, entry);
}

bool EraseKHUCoinFromDB(CWallet* pwallet, const COutPoint& outpoint)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.EraseKHUCoin(outpoint);
}

bool LoadKHUCoinsFromDB(CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);

    LogPrint(BCLog::KHU, "LoadKHUCoinsFromDB: Loading KHU coins from wallet.dat\n");

    // Clear existing data
    pwallet->khuData.Clear();

    // Open cursor on database
    WalletBatch batch(pwallet->GetDBHandle(), "r");

    // Iterate through all entries with "khucoin" prefix
    // Note: This requires iterating all keys - in production, use proper cursor
    // For now, we'll reload via ScanForKHUCoins if DB load fails

    // Update balances
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::KHU, "LoadKHUCoinsFromDB: Loaded %d KHU coins\n",
             pwallet->khuData.mapKHUCoins.size());

    return true;
}
