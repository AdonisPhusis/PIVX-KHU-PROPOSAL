// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef PIVX_KHU_UTXO_H
#define PIVX_KHU_UTXO_H

#include "khu/khu_coins.h"
#include "primitives/transaction.h"

class CCoinsViewCache;

/**
 * KHU UTXO Tracking Extensions for CCoinsViewCache
 *
 * PHASE 2: MINT/REDEEM operations
 *
 * These functions extend CCoinsViewCache to track KHU_T colored coin UTXOs.
 * KHU_T UTXOs are stored separately from regular PIV UTXOs for isolation.
 *
 * RÈGLES:
 * - KHU_T = colored coin UTXO (structure similaire à Coin)
 * - Namespace LevelDB 'K' + 'U' (isolation)
 * - fStaked = false pour Phase 2 (STAKE/UNSTAKE = Phase 3)
 */

/**
 * AddKHUCoin - Add a KHU_T UTXO to the cache
 *
 * Called by ApplyKHUMint() to create new KHU_T UTXO.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint (txid + vout)
 * @param coin KHU UTXO to add
 * @return true on success
 */
bool AddKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint, const CKHUUTXO& coin);

/**
 * SpendKHUCoin - Mark a KHU_T UTXO as spent
 *
 * Called by ApplyKHURedeem() to consume KHU_T UTXO.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint to spend
 * @return true if coin existed and was spent
 */
bool SpendKHUCoin(CCoinsViewCache& view, const COutPoint& outpoint);

/**
 * GetKHUCoin - Retrieve a KHU_T UTXO from the cache
 *
 * Called by CheckKHURedeem() to validate inputs.
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint
 * @param coin Output parameter for KHU UTXO
 * @return true if coin exists
 */
bool GetKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint, CKHUUTXO& coin);

/**
 * HaveKHUCoin - Check if a KHU_T UTXO exists
 *
 * @param view Coins view cache
 * @param outpoint Transaction outpoint
 * @return true if coin exists and is unspent
 */
bool HaveKHUCoin(const CCoinsViewCache& view, const COutPoint& outpoint);

/**
 * GetKHUCoinFromTracking - Retrieve KHU_T UTXO from global tracking map
 *
 * Used when the CCoinsViewCache may not have the coin (e.g., after
 * standard tx validation spent it, but KHU tracking still has it).
 *
 * @param outpoint Transaction outpoint
 * @param coin Output parameter for KHU UTXO
 * @return true if coin exists and is unspent
 */
bool GetKHUCoinFromTracking(const COutPoint& outpoint, CKHUUTXO& coin);

#endif // PIVX_KHU_UTXO_H
