// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_utxo.h"

#include "coins.h"
#include "khu/khu_statedb.h"
#include "sync.h"
#include "util/system.h"
#include "utilmoneystr.h"

#include <unordered_map>

// Phase 2: Simple in-memory KHU UTXO tracking
// Future phases will integrate with CCoinsViewCache properly
static RecursiveMutex cs_khu_utxos;
static std::unordered_map<COutPoint, CKHUUTXO, SaltedOutpointHasher> mapKHUUTXOs GUARDED_BY(cs_khu_utxos);

bool AddKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint, const CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    LogPrint(BCLog::KHU, "%s: adding %s KHU at %s:%d (height %d)\n",
             __func__, FormatMoney(coin.amount), outpoint.hash.ToString().substr(0,16).c_str(),
             outpoint.n, coin.nHeight);

    // Vérifier que le coin n'existe pas déjà
    auto it = mapKHUUTXOs.find(outpoint);
    if (it != mapKHUUTXOs.end()) {
        bool isSpent = it->second.IsSpent();
        LogPrint(BCLog::KHU, "%s: outpoint=%s already exists (spent=%d)\n",
                 __func__, outpoint.ToString(), isSpent);
        if (!isSpent) {
            return error("%s: coin already exists and not spent at %s", __func__, outpoint.ToString());
        }
    }

    // Ajouter le coin
    mapKHUUTXOs[outpoint] = coin;

    LogPrint(BCLog::KHU, "%s: added %s KHU at %s\n",
             __func__, FormatMoney(coin.amount), outpoint.ToString());

    return true;
}

bool SpendKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint)
{
    LOCK(cs_khu_utxos);

    LogPrint(BCLog::KHU, "%s: looking for %s:%d\n",
              __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        LogPrint(BCLog::KHU, "%s: coin not found for %s:%d\n",
                 __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return error("%s: coin not found at %s", __func__, outpoint.ToString());
    }

    if (it->second.IsSpent()) {
        LogPrint(BCLog::KHU, "SpendKHUCoin: coin already spent for %s:%d\n",
                 outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return error("SpendKHUCoin: coin already spent at %s", outpoint.ToString());
    }

    LogPrint(BCLog::KHU, "SpendKHUCoin: spending %s:%d value=%s\n",
             outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n, FormatMoney(it->second.amount));

    // Marquer comme dépensé
    it->second.Clear();

    return true;
}

bool GetKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint, CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    LogPrint(BCLog::KHU, "%s: looking for %s:%d\n",
              __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        // Not found is normal for PIV inputs - only log at debug level
        LogPrint(BCLog::KHU, "%s: coin not found for %s:%d\n",
                 __func__, outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return false;
    }

    if (it->second.IsSpent()) {
        LogPrint(BCLog::KHU, "GetKHUCoin: coin spent for %s:%d\n",
                 outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n);
        return false;
    }

    coin = it->second;
    LogPrint(BCLog::KHU, "GetKHUCoin: found %s:%d value=%s\n",
             outpoint.hash.ToString().substr(0,16).c_str(), outpoint.n, FormatMoney(coin.amount));
    return true;
}

bool HaveKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint)
{
    LOCK(cs_khu_utxos);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        return false;
    }

    return !it->second.IsSpent();
}

bool GetKHUCoinFromTracking(const COutPoint& outpoint, CKHUUTXO& coin)
{
    LOCK(cs_khu_utxos);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        return false;
    }

    if (it->second.IsSpent()) {
        return false;
    }

    coin = it->second;
    return true;
}
