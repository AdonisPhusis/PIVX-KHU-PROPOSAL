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
#include "sapling/saplingscriptpubkeyman.h"
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

    // STEP 1: Always check if any input spends our tracked KHU UTXOs
    // This handles ALL transaction types including regular transfers via khusend
    for (const auto& vin : tx->vin) {
        if (pwallet->khuData.mapKHUCoins.count(vin.prevout)) {
            RemoveKHUCoinFromWallet(pwallet, vin.prevout);
        }
    }

    // STEP 2: Process KHU-specific transaction types for output creation
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
    // KHU_REDEEM and KHU_STAKE only spend (no KHU outputs created)
    // Their inputs are already handled in STEP 1 above
}

bool ScanForKHUCoins(CWallet* pwallet, int nStartHeight)
{
    LOCK2(cs_main, pwallet->cs_wallet);

    LogPrint(BCLog::KHU, "ScanForKHUCoins: Starting scan from height %d\n", nStartHeight);

    // Clear existing KHU coins before full rescan
    if (nStartHeight == 0) {
        pwallet->khuData.Clear();
        // Also clear from database
        WalletBatch batch(pwallet->GetDBHandle());
        // Note: Full clear would need cursor iteration; for now, coins are
        // individually erased via RemoveKHUCoinFromWallet when spent
    }

    const CBlockIndex* pindex = chainActive[nStartHeight];
    if (!pindex) {
        LogPrintf("ERROR: ScanForKHUCoins: Invalid start height %d\n", nStartHeight);
        return false;
    }

    int nScanned = 0;
    int nKHUTxProcessed = 0;

    while (pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex)) {
            LogPrintf("ERROR: ScanForKHUCoins: Failed to read block at height %d\n", pindex->nHeight);
            return false;
        }

        for (const auto& tx : block.vtx) {
            // Process ALL transactions to detect:
            // 1. KHU-specific transactions (MINT, REDEEM, STAKE, UNSTAKE)
            // 2. Regular transactions that spend our tracked KHU UTXOs (via khusend)
            bool isKHUTx = (tx->nType >= CTransaction::TxType::KHU_MINT &&
                           tx->nType <= CTransaction::TxType::KHU_UNSTAKE);

            // For non-KHU transactions, only process if we have KHU coins
            // that might be spent (optimization)
            if (isKHUTx || !pwallet->khuData.mapKHUCoins.empty()) {
                ProcessKHUTransactionForWallet(pwallet, tx, pindex->nHeight);
                if (isKHUTx) nKHUTxProcessed++;
            }
        }

        nScanned++;
        if (nScanned % 10000 == 0) {
            LogPrint(BCLog::KHU, "ScanForKHUCoins: Scanned %d blocks (height %d), %d KHU coins tracked\n",
                     nScanned, pindex->nHeight, pwallet->khuData.mapKHUCoins.size());
        }

        pindex = chainActive.Next(pindex);
    }

    // Update final balances
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::KHU, "ScanForKHUCoins: Complete. Scanned %d blocks, %d KHU tx, found %d coins, balance=%d\n",
             nScanned, nKHUTxProcessed, pwallet->khuData.mapKHUCoins.size(), pwallet->khuData.nKHUBalance);

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

    // NOTE: KHU coins are now loaded automatically during wallet load
    // via ReadKeyValue() in walletdb.cpp, which handles "khucoin" records.
    //
    // This function is kept for explicit calls to finalize/refresh balances.
    // The actual loading happens in WalletBatch::LoadWallet() -> ReadKeyValue().

    if (pwallet->khuData.mapKHUCoins.empty()) {
        LogPrint(BCLog::KHU, "LoadKHUCoinsFromDB: No KHU coins in wallet\n");
        return true;
    }

    // Recalculate cached balances from loaded coins
    pwallet->khuData.UpdateBalance();

    LogPrint(BCLog::KHU, "LoadKHUCoinsFromDB: Finalized %d KHU coins, balance=%d, staked=%d\n",
             pwallet->khuData.mapKHUCoins.size(),
             pwallet->khuData.nKHUBalance,
             pwallet->khuData.nKHUStaked);

    return true;
}

// ============================================================================
// ZKHU Note Functions (Phase 8b)
// ============================================================================

bool AddZKHUNoteToWallet(CWallet* pwallet, const SaplingOutPoint& op, const uint256& cm,
                         const ZKHUMemo& memo, const uint256& nullifier, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    // Check if already tracked
    if (pwallet->khuData.mapZKHUNotes.count(cm)) {
        LogPrint(BCLog::KHU, "AddZKHUNoteToWallet: Already tracking note %s\n",
                 cm.GetHex().substr(0, 16));
        return true;
    }

    // Create entry
    ZKHUNoteEntry entry(op, cm, memo.nStakeStartHeight, memo.amount, nullifier, nHeight);

    // Add to map
    pwallet->khuData.mapZKHUNotes[cm] = entry;

    // Add nullifier mapping
    if (!nullifier.IsNull()) {
        pwallet->khuData.mapZKHUNullifiers[nullifier] = cm;
    }

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Persist to database
    if (!WriteZKHUNoteToDB(pwallet, cm, entry)) {
        LogPrintf("ERROR: AddZKHUNoteToWallet: Failed to persist to DB\n");
        return false;
    }

    LogPrint(BCLog::KHU, "AddZKHUNoteToWallet: Added note %s amount=%d stakeHeight=%d\n",
             cm.GetHex().substr(0, 16), memo.amount, memo.nStakeStartHeight);

    return true;
}

bool MarkZKHUNoteSpent(CWallet* pwallet, const uint256& nullifier)
{
    LOCK(pwallet->cs_wallet);

    // Find note by nullifier
    auto nullIt = pwallet->khuData.mapZKHUNullifiers.find(nullifier);
    if (nullIt == pwallet->khuData.mapZKHUNullifiers.end()) {
        return false; // Not our note
    }

    const uint256& cm = nullIt->second;
    auto noteIt = pwallet->khuData.mapZKHUNotes.find(cm);
    if (noteIt == pwallet->khuData.mapZKHUNotes.end()) {
        return false;
    }

    // Mark as spent
    noteIt->second.fSpent = true;

    // Update cached balance
    pwallet->khuData.UpdateBalance();

    // Update database
    if (!WriteZKHUNoteToDB(pwallet, cm, noteIt->second)) {
        LogPrintf("ERROR: MarkZKHUNoteSpent: Failed to update DB\n");
    }

    LogPrint(BCLog::KHU, "MarkZKHUNoteSpent: Note %s marked spent\n",
             cm.GetHex().substr(0, 16));

    return true;
}

std::vector<ZKHUNoteEntry> GetUnspentZKHUNotes(const CWallet* pwallet)
{
    LOCK(pwallet->cs_wallet);

    std::vector<ZKHUNoteEntry> result;
    for (std::map<uint256, ZKHUNoteEntry>::const_iterator it = pwallet->khuData.mapZKHUNotes.begin();
         it != pwallet->khuData.mapZKHUNotes.end(); ++it) {
        if (!it->second.fSpent) {
            result.push_back(it->second);
        }
    }
    return result;
}

const ZKHUNoteEntry* GetZKHUNote(const CWallet* pwallet, const uint256& cm)
{
    LOCK(pwallet->cs_wallet);

    auto it = pwallet->khuData.mapZKHUNotes.find(cm);
    if (it == pwallet->khuData.mapZKHUNotes.end()) {
        return nullptr;
    }
    return &it->second;
}

bool WriteZKHUNoteToDB(CWallet* pwallet, const uint256& cm, const ZKHUNoteEntry& entry)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.WriteZKHUNote(cm, entry);
}

bool EraseZKHUNoteFromDB(CWallet* pwallet, const uint256& cm)
{
    WalletBatch batch(pwallet->GetDBHandle());
    return batch.EraseZKHUNote(cm);
}

void ProcessKHUStakeForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight)
{
    LOCK(pwallet->cs_wallet);

    if (tx->nType != CTransaction::TxType::KHU_STAKE) return;
    if (!tx->IsShieldedTx()) return;

    // Get the Sapling script pub key manager
    SaplingScriptPubKeyMan* saplingMan = pwallet->GetSaplingScriptPubKeyMan();
    if (!saplingMan) return;

    // Process each Sapling output
    const uint256& txhash = tx->GetHash();

    for (size_t i = 0; i < tx->sapData->vShieldedOutput.size(); ++i) {
        const OutputDescription& output = tx->sapData->vShieldedOutput[i];
        SaplingOutPoint op(txhash, i);

        // Check if this output belongs to us
        auto it = pwallet->mapWallet.find(txhash);
        if (it == pwallet->mapWallet.end()) continue;

        const CWalletTx& wtx = it->second;
        auto noteIt = wtx.mapSaplingNoteData.find(op);
        if (noteIt == wtx.mapSaplingNoteData.end()) continue;

        const SaplingNoteData& nd = noteIt->second;
        if (!nd.IsMyNote()) continue;

        // Try to decode the memo as ZKHUMemo
        if (!nd.memo) continue;

        std::array<unsigned char, 512> memoBytes;
        std::copy(nd.memo->begin(), nd.memo->end(), memoBytes.begin());
        ZKHUMemo memo = ZKHUMemo::Deserialize(memoBytes);

        // Verify ZKHU magic
        if (memcmp(memo.magic, "ZKHU", 4) != 0) continue;

        // Get nullifier
        uint256 nullifier;
        if (nd.nullifier) {
            nullifier = *nd.nullifier;
        }

        // Add to wallet
        AddZKHUNoteToWallet(pwallet, op, output.cmu, memo, nullifier, nHeight);
    }
}

void ProcessKHUUnstakeForWallet(CWallet* pwallet, const CTransactionRef& tx)
{
    LOCK(pwallet->cs_wallet);

    if (tx->nType != CTransaction::TxType::KHU_UNSTAKE) return;
    if (!tx->IsShieldedTx()) return;

    // Check each Sapling spend's nullifier
    for (const SpendDescription& spend : tx->sapData->vShieldedSpend) {
        // Mark the note as spent if it's ours
        MarkZKHUNoteSpent(pwallet, spend.nullifier);
    }
}
