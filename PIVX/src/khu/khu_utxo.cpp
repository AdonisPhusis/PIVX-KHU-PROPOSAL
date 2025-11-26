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

    // Vérifier que le coin n'existe pas déjà
    auto it = mapKHUUTXOs.find(outpoint);
    if (it != mapKHUUTXOs.end() && !it->second.IsSpent()) {
        return error("AddKHUCoin: coin already exists at %s", outpoint.ToString());
    }

    // Ajouter le coin
    mapKHUUTXOs[outpoint] = coin;

    LogPrint(BCLog::KHU, "AddKHUCoin: added %s KHU at %s (height %d)\n",
             FormatMoney(coin.amount), outpoint.ToString(), coin.nHeight);

    return true;
}

bool SpendKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint)
{
    LOCK(cs_khu_utxos);

    auto it = mapKHUUTXOs.find(outpoint);
    if (it == mapKHUUTXOs.end()) {
        return error("SpendKHUCoin: coin not found at %s", outpoint.ToString());
    }

    if (it->second.IsSpent()) {
        return error("SpendKHUCoin: coin already spent at %s", outpoint.ToString());
    }

    // Marquer comme dépensé
    it->second.Clear();

    LogPrint(BCLog::KHU, "SpendKHUCoin: spent KHU at %s\n", outpoint.ToString());

    return true;
}

bool GetKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint, CKHUUTXO& coin)
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
