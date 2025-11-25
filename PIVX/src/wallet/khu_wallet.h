// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_WALLET_KHU_WALLET_H
#define PIVX_WALLET_KHU_WALLET_H

#include "amount.h"
#include "khu/khu_coins.h"
#include "khu/zkhu_memo.h"
#include "primitives/transaction.h"
#include "serialize.h"
#include "uint256.h"
#include "utiltime.h"

#include <map>
#include <vector>

class CWallet;
class COutput;
class CCoinsViewCache;

/**
 * KHU Wallet Extension — Phase 8a (Transparent)
 *
 * Extension for CWallet to track KHU_T colored coin UTXOs.
 * This is NOT a separate wallet — it extends the existing CWallet.
 *
 * PRINCIPES:
 * - Réutilise l'infrastructure wallet PIVX existante
 * - Pas de logique consensus (délégué à khu_validation)
 * - Tracking UTXOs "mine" uniquement
 * - Persistence via wallet.dat (prefix "khucoin")
 *
 * Source: docs/blueprints/08-PHASE8-RPC-WALLET.md
 */

/**
 * KHUCoinEntry - Entry for wallet's KHU coin tracking
 *
 * Wrapper autour de CKHUUTXO avec métadonnées wallet.
 */
struct KHUCoinEntry {
    //! The KHU UTXO data
    CKHUUTXO coin;

    //! Transaction hash containing this output
    uint256 txhash;

    //! Output index in transaction
    uint32_t vout;

    //! Block height when confirmed (0 if unconfirmed)
    int nConfirmedHeight;

    //! Time received in wallet
    int64_t nTimeReceived;

    KHUCoinEntry() : vout(0), nConfirmedHeight(0), nTimeReceived(0) {}

    KHUCoinEntry(const CKHUUTXO& coinIn, const uint256& txhashIn, uint32_t voutIn, int heightIn)
        : coin(coinIn), txhash(txhashIn), vout(voutIn), nConfirmedHeight(heightIn), nTimeReceived(GetTime()) {}

    COutPoint GetOutPoint() const {
        return COutPoint(txhash, vout);
    }

    SERIALIZE_METHODS(KHUCoinEntry, obj) {
        READWRITE(obj.coin, obj.txhash, obj.vout, obj.nConfirmedHeight, obj.nTimeReceived);
    }
};

/**
 * ZKHUNoteEntry - Entry for wallet's ZKHU note tracking (Phase 8b)
 *
 * Tracks Sapling notes created by KHU_STAKE transactions.
 * Links to the underlying Sapling note data with ZKHU-specific metadata.
 */
struct ZKHUNoteEntry {
    //! Sapling outpoint (txid + output index)
    SaplingOutPoint op;

    //! Note commitment (cm) for identification
    uint256 cm;

    //! Decoded ZKHUMemo data
    uint32_t nStakeStartHeight;
    CAmount amount;
    CAmount Ur_accumulated;

    //! Nullifier (for tracking spends)
    uint256 nullifier;

    //! Has this note been spent (unstaked)?
    bool fSpent;

    //! Block height when confirmed
    int nConfirmedHeight;

    //! Time received in wallet
    int64_t nTimeReceived;

    ZKHUNoteEntry()
        : nStakeStartHeight(0), amount(0), Ur_accumulated(0),
          fSpent(false), nConfirmedHeight(0), nTimeReceived(0) {}

    ZKHUNoteEntry(const SaplingOutPoint& opIn, const uint256& cmIn,
                  uint32_t stakeHeight, CAmount amountIn, const uint256& nullifierIn, int heightIn)
        : op(opIn), cm(cmIn), nStakeStartHeight(stakeHeight), amount(amountIn),
          Ur_accumulated(0), nullifier(nullifierIn), fSpent(false),
          nConfirmedHeight(heightIn), nTimeReceived(GetTime()) {}

    //! Check if note is mature for unstaking (>= 4320 blocks)
    bool IsMature(int currentHeight) const {
        const uint32_t ZKHU_MATURITY_BLOCKS = 4320; // 3 days
        return !fSpent && (uint32_t)(currentHeight - nConfirmedHeight) >= ZKHU_MATURITY_BLOCKS;
    }

    //! Get blocks staked
    int GetBlocksStaked(int currentHeight) const {
        if (nConfirmedHeight == 0) return 0;
        return currentHeight - nConfirmedHeight;
    }

    SERIALIZE_METHODS(ZKHUNoteEntry, obj) {
        READWRITE(obj.op, obj.cm, obj.nStakeStartHeight, obj.amount, obj.Ur_accumulated,
                  obj.nullifier, obj.fSpent, obj.nConfirmedHeight, obj.nTimeReceived);
    }
};

/**
 * KHU Wallet Data Container
 *
 * Contains all KHU-specific data for the wallet.
 * This is embedded in CWallet, not a separate class.
 */
class KHUWalletData {
public:
    //! Map of KHU UTXOs owned by this wallet: outpoint -> entry
    std::map<COutPoint, KHUCoinEntry> mapKHUCoins;

    //! Map of ZKHU notes owned by this wallet: note_commitment -> entry (Phase 8b)
    std::map<uint256, ZKHUNoteEntry> mapZKHUNotes;

    //! Map of ZKHU nullifiers to note commitments (for spend detection)
    std::map<uint256, uint256> mapZKHUNullifiers;

    //! Cached KHU transparent balance
    CAmount nKHUBalance{0};

    //! Cached KHU staked balance (ZKHU notes)
    CAmount nKHUStaked{0};

    KHUWalletData() = default;

    //! Clear all KHU data
    void Clear() {
        mapKHUCoins.clear();
        mapZKHUNotes.clear();
        mapZKHUNullifiers.clear();
        nKHUBalance = 0;
        nKHUStaked = 0;
    }

    //! Recalculate cached balances from maps
    void UpdateBalance() {
        nKHUBalance = 0;
        nKHUStaked = 0;

        // Transparent KHU_T balance
        for (std::map<COutPoint, KHUCoinEntry>::const_iterator it = mapKHUCoins.begin();
             it != mapKHUCoins.end(); ++it) {
            const KHUCoinEntry& entry = it->second;
            if (!entry.coin.fStaked) {
                nKHUBalance += entry.coin.amount;
            }
        }

        // Staked ZKHU balance (unspent notes only)
        for (std::map<uint256, ZKHUNoteEntry>::const_iterator it = mapZKHUNotes.begin();
             it != mapZKHUNotes.end(); ++it) {
            const ZKHUNoteEntry& entry = it->second;
            if (!entry.fSpent) {
                nKHUStaked += entry.amount;
            }
        }
    }
};

/**
 * KHU Wallet Functions
 *
 * Ces fonctions opèrent sur CWallet avec son KHUWalletData intégré.
 * Elles sont déclarées ici et implémentées dans khu_wallet.cpp.
 */

//! Add a KHU coin to the wallet (returns true if it's ours)
bool AddKHUCoinToWallet(CWallet* pwallet, const COutPoint& outpoint,
                        const CKHUUTXO& coin, int nHeight);

//! Remove a spent KHU coin from the wallet
bool RemoveKHUCoinFromWallet(CWallet* pwallet, const COutPoint& outpoint);

//! Get available (unspent, non-staked) KHU coins
std::vector<COutput> GetAvailableKHUCoins(const CWallet* pwallet, int minDepth = 1);

//! Get KHU transparent balance
CAmount GetKHUBalance(const CWallet* pwallet);

//! Get KHU staked balance (Phase 8b)
CAmount GetKHUStakedBalance(const CWallet* pwallet);

/**
 * Get estimated pending yield for display purposes.
 *
 * ⚠️ WARNING: This is an APPROXIMATION for UI display only.
 * The actual yield is calculated by the consensus engine (khu_yield.cpp).
 * Do NOT use this for consensus decisions.
 *
 * @param pwallet Wallet pointer
 * @param R_annual Annual rate in basis points
 * @return Estimated pending yield (may differ from actual by ± satoshis)
 */
CAmount GetKHUPendingYieldEstimate(const CWallet* pwallet, uint16_t R_annual);

//! Scan blockchain for KHU coins belonging to this wallet
bool ScanForKHUCoins(CWallet* pwallet, int nStartHeight);

//! Process a KHU transaction for wallet tracking
void ProcessKHUTransactionForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight);

/**
 * Wallet Persistence Functions (wallet.dat)
 */

//! Write a KHU coin to wallet database
bool WriteKHUCoinToDB(CWallet* pwallet, const COutPoint& outpoint, const KHUCoinEntry& entry);

//! Erase a KHU coin from wallet database
bool EraseKHUCoinFromDB(CWallet* pwallet, const COutPoint& outpoint);

//! Load all KHU coins from wallet database
bool LoadKHUCoinsFromDB(CWallet* pwallet);

/**
 * ZKHU Note Functions (Phase 8b)
 */

//! Add a ZKHU note to the wallet (called when KHU_STAKE tx is processed)
bool AddZKHUNoteToWallet(CWallet* pwallet, const SaplingOutPoint& op, const uint256& cm,
                         const ZKHUMemo& memo, const uint256& nullifier, int nHeight);

//! Mark a ZKHU note as spent (called when KHU_UNSTAKE tx spends the nullifier)
bool MarkZKHUNoteSpent(CWallet* pwallet, const uint256& nullifier);

//! Get list of unspent ZKHU notes
std::vector<ZKHUNoteEntry> GetUnspentZKHUNotes(const CWallet* pwallet);

//! Get a specific ZKHU note by commitment
const ZKHUNoteEntry* GetZKHUNote(const CWallet* pwallet, const uint256& cm);

//! Write a ZKHU note to wallet database
bool WriteZKHUNoteToDB(CWallet* pwallet, const uint256& cm, const ZKHUNoteEntry& entry);

//! Erase a ZKHU note from wallet database
bool EraseZKHUNoteFromDB(CWallet* pwallet, const uint256& cm);

//! Process a KHU_STAKE transaction for ZKHU note tracking
void ProcessKHUStakeForWallet(CWallet* pwallet, const CTransactionRef& tx, int nHeight);

//! Process a KHU_UNSTAKE transaction to mark notes spent
void ProcessKHUUnstakeForWallet(CWallet* pwallet, const CTransactionRef& tx);

#endif // PIVX_WALLET_KHU_WALLET_H
