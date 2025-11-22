# 08 — WALLET & RPC BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit l'**implémentation wallet KHU** et les **commandes RPC**.

**Wallet KHU réutilise l'infrastructure PIVX existante avec tracking spécifique pour UTXOs KHU_T.**

---

## 1. ARCHITECTURE WALLET KHU

### 1.1 Integration avec CWallet PIVX

**KHU wallet = Extension de CWallet existant, pas nouveau wallet.**

```cpp
/**
 * Extension CWallet pour KHU
 * src/wallet/wallet.h
 */
class CWallet {
    // ═══════════════════════════════════════════
    // EXISTANT (PIVX standard)
    // ═══════════════════════════════════════════
    std::map<uint256, CWalletTx> mapWallet;
    std::set<COutPoint> setLockedCoins;

    // ═══════════════════════════════════════════
    // NOUVEAU (KHU-specific)
    // ═══════════════════════════════════════════

    //! Map des UTXOs KHU_T possédés
    std::map<COutPoint, CKHUUTXO> mapKHUCoins;

    //! Balance KHU_T totale
    CAmount nKHUBalance;

    //! Balance KHU_T stakée (ZKHU)
    CAmount nKHUStaked;

    //! Notes ZKHU possédées
    std::map<uint256, ZKHUNoteData> mapZKHUNotes;

    /**
     * Calculer balance KHU_T (transparent)
     */
    CAmount GetKHUBalance() const;

    /**
     * Calculer balance ZKHU (staked)
     */
    CAmount GetZKHUBalance() const;

    /**
     * Obtenir UTXOs KHU_T spendable
     */
    std::vector<COutput> AvailableKHUCoins(bool fOnlySafe = true) const;

    /**
     * Créer transaction KHU
     */
    bool CreateKHUTransaction(
        const std::vector<CRecipient>& vecSend,
        CWalletTx& wtxNew,
        CReserveKey& reservekey,
        CAmount& nFeeRet,
        int& nChangePosRet,
        std::string& strFailReason,
        const CCoinControl* coinControl = nullptr
    );
};
```

### 1.2 Tracking UTXOs KHU_T

```cpp
/**
 * Ajouter UTXO KHU_T au wallet
 * src/wallet/wallet.cpp
 */
bool CWallet::AddKHUCoin(const COutPoint& outpoint, const CKHUUTXO& khuCoin) {
    LOCK(cs_wallet);

    // Vérifier que c'est notre adresse
    if (!IsMine(khuCoin.scriptPubKey))
        return false;

    // Ajouter à map
    mapKHUCoins[outpoint] = khuCoin;

    // Mettre à jour balance
    UpdateKHUBalance();

    // Notifier UI
    NotifyKHUBalanceChanged(nKHUBalance);

    return true;
}

/**
 * Marquer UTXO KHU_T comme dépensé
 */
bool CWallet::SpendKHUCoin(const COutPoint& outpoint) {
    LOCK(cs_wallet);

    auto it = mapKHUCoins.find(outpoint);
    if (it == mapKHUCoins.end())
        return false;

    // Retirer de map
    mapKHUCoins.erase(it);

    // Mettre à jour balance
    UpdateKHUBalance();

    return true;
}

/**
 * Calculer balance totale KHU_T
 */
void CWallet::UpdateKHUBalance() {
    LOCK(cs_wallet);

    nKHUBalance = 0;
    nKHUStaked = 0;

    for (const auto& [outpoint, khuCoin] : mapKHUCoins) {
        if (khuCoin.fStaked) {
            nKHUStaked += khuCoin.amount;
        } else {
            nKHUBalance += khuCoin.amount;
        }
    }
}
```

### 1.3 Scan Blockchain pour KHU

```cpp
/**
 * Scanner blockchain pour récupérer UTXOs KHU_T
 * Appelé au démarrage wallet ou rescan
 */
bool CWallet::ScanForKHUCoins(const CBlockIndex* pindexStart) {
    LOCK2(cs_main, cs_wallet);

    const CBlockIndex* pindex = pindexStart;

    while (pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex))
            return error("ScanForKHUCoins: failed to read block");

        // Scanner transactions du bloc
        for (const auto& tx : block.vtx) {
            // Vérifier si transaction contient KHU
            if (IsKHUTransaction(tx)) {
                ScanKHUTransaction(tx, pindex->nHeight);
            }
        }

        pindex = chainActive.Next(pindex);
    }

    return true;
}

/**
 * Scanner transaction KHU spécifique
 */
void CWallet::ScanKHUTransaction(const CTransaction& tx, uint32_t nHeight) {
    // Scanner outputs KHU_T
    for (size_t i = 0; i < tx.vout.size(); i++) {
        const CTxOut& out = tx.vout[i];

        if (IsKHUOutput(out) && IsMine(out.scriptPubKey)) {
            // C'est un UTXO KHU_T qui nous appartient
            CKHUUTXO khuCoin;
            khuCoin.amount = out.nValue;
            khuCoin.scriptPubKey = out.scriptPubKey;
            khuCoin.nHeight = nHeight;
            khuCoin.fIsKHU = true;
            khuCoin.fStaked = false;

            COutPoint outpoint(tx.GetHash(), i);
            AddKHUCoin(outpoint, khuCoin);
        }
    }

    // Scanner inputs KHU_T (dépensés)
    for (const auto& in : tx.vin) {
        if (mapKHUCoins.count(in.prevout)) {
            SpendKHUCoin(in.prevout);
        }
    }
}
```

---

## 2. COMMANDES RPC

### 2.1 getkhubalance

```cpp
/**
 * RPC: getkhubalance
 * Retourne balance KHU_T (transparent + staked)
 */
UniValue getkhubalance(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() > 0)
        throw std::runtime_error(
            "getkhubalance\n"
            "\nRetourne le solde KHU du wallet.\n"
            "\nRésultat:\n"
            "{\n"
            "  \"transparent\": xxx.xxx,    (numeric) Balance KHU_T transparent\n"
            "  \"staked\": xxx.xxx,         (numeric) Balance ZKHU staked\n"
            "  \"total\": xxx.xxx           (numeric) Balance totale\n"
            "}\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount transparent = pwallet->GetKHUBalance();
    CAmount staked = pwallet->GetZKHUBalance();
    CAmount total = transparent + staked;

    UniValue result(UniValue::VOBJ);
    result.pushKV("transparent", ValueFromAmount(transparent));
    result.pushKV("staked", ValueFromAmount(staked));
    result.pushKV("total", ValueFromAmount(total));

    return result;
}
```

### 2.2 mintkhu

```cpp
/**
 * RPC: mintkhu <amount>
 * Convertir PIV → KHU_T (1:1)
 */
UniValue mintkhu(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "mintkhu amount\n"
            "\nConvertir PIV en KHU (ratio 1:1).\n"
            "\nArguments:\n"
            "1. amount      (numeric, required) Montant PIV à convertir\n"
            "\nRésultat:\n"
            "{\n"
            "  \"txid\": \"xxx\",           (string) Transaction ID\n"
            "  \"amount_khu\": xxx.xxx,    (numeric) Montant KHU créé\n"
            "  \"fee\": xxx.xxx            (numeric) Fee PIV payée\n"
            "}\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // Parse amount
    CAmount nAmount = AmountFromValue(request.params[0]);

    if (nAmount <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    // Vérifier balance PIV suffisante
    CAmount nBalance = pwallet->GetBalance();
    if (nAmount > nBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient PIV balance");

    // Créer transaction MINT
    CMutableTransaction tx;
    tx.nType = TxType::KHU_MINT;

    // Input: PIV à brûler
    if (!pwallet->SelectCoins(nAmount, tx.vin))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to select coins");

    // Output 0: PIV proof-of-burn
    CScript burnScript;
    burnScript << OP_RETURN << ToByteVector(uint256::ZERO);
    tx.vout.push_back(CTxOut(nAmount, burnScript));

    // Output 1: KHU_T créé
    CKeyID keyID;
    if (!pwallet->GetKeyFromPool(keyID))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out");

    CScript khuScript = GetScriptForDestination(keyID);
    tx.vout.push_back(CTxOut(nAmount, khuScript));

    // Signer transaction
    if (!pwallet->SignTransaction(tx))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction");

    // Broadcast
    CTransactionRef txRef = MakeTransactionRef(tx);
    uint256 txid;
    if (!pwallet->CommitTransaction(txRef, {}, {}, txid))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to commit transaction");

    // Résultat
    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("amount_khu", ValueFromAmount(nAmount));
    result.pushKV("fee", ValueFromAmount(tx.GetValueOut() - nAmount));

    return result;
}
```

### 2.3 redeemkhu

```cpp
/**
 * RPC: redeemkhu <amount>
 * Convertir KHU_T → PIV (1:1)
 */
UniValue redeemkhu(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "redeemkhu amount\n"
            "\nConvertir KHU en PIV (ratio 1:1).\n"
            "\nArguments:\n"
            "1. amount      (numeric, required) Montant KHU à convertir\n"
            "\nRésultat:\n"
            "{\n"
            "  \"txid\": \"xxx\",           (string) Transaction ID\n"
            "  \"amount_piv\": xxx.xxx,    (numeric) Montant PIV reçu\n"
            "  \"fee\": xxx.xxx            (numeric) Fee PIV payée\n"
            "}\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount nAmount = AmountFromValue(request.params[0]);

    if (nAmount <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    // Vérifier balance KHU_T suffisante
    CAmount nKHUBalance = pwallet->GetKHUBalance();
    if (nAmount > nKHUBalance)
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient KHU balance");

    // Créer transaction REDEEM
    CMutableTransaction tx;
    tx.nType = TxType::KHU_REDEEM;

    // Sélectionner UTXOs KHU_T
    std::vector<COutput> vKHUCoins = pwallet->AvailableKHUCoins();
    CAmount nSelected = 0;

    for (const auto& out : vKHUCoins) {
        if (nSelected >= nAmount)
            break;

        tx.vin.push_back(CTxIn(out.tx->GetHash(), out.i));
        nSelected += out.tx->vout[out.i].nValue;
    }

    if (nSelected < nAmount)
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to select enough KHU coins");

    // Output: PIV reçu
    CKeyID keyID;
    if (!pwallet->GetKeyFromPool(keyID))
        throw JSONRPCError(RPC_WALLET_KEYPOOL_RAN_OUT, "Keypool ran out");

    CScript pivScript = GetScriptForDestination(keyID);
    tx.vout.push_back(CTxOut(nAmount, pivScript));

    // Change KHU si nécessaire
    if (nSelected > nAmount) {
        CAmount nChange = nSelected - nAmount;
        CKeyID changeKey;
        pwallet->GetKeyFromPool(changeKey);
        CScript changeScript = GetScriptForDestination(changeKey);
        tx.vout.push_back(CTxOut(nChange, changeScript));
    }

    // Signer et broadcast
    if (!pwallet->SignTransaction(tx))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to sign transaction");

    CTransactionRef txRef = MakeTransactionRef(tx);
    uint256 txid;
    if (!pwallet->CommitTransaction(txRef, {}, {}, txid))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to commit transaction");

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("amount_piv", ValueFromAmount(nAmount));

    return result;
}
```

### 2.4 sendkhu

```cpp
/**
 * RPC: sendkhu <address> <amount>
 * Envoyer KHU_T à une adresse
 */
UniValue sendkhu(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "sendkhu \"address\" amount\n"
            "\nEnvoyer KHU_T à une adresse.\n"
            "\nArguments:\n"
            "1. address     (string, required) Adresse destinataire\n"
            "2. amount      (numeric, required) Montant KHU à envoyer\n"
            "\nRésultat:\n"
            "\"txid\"       (string) Transaction ID\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    // Parse adresse
    CTxDestination dest = DecodeDestination(request.params[0].get_str());
    if (!IsValidDestination(dest))
        throw JSONRPCError(RPC_INVALID_ADDRESS_OR_KEY, "Invalid address");

    // Parse montant
    CAmount nAmount = AmountFromValue(request.params[1]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    // Vérifier balance
    if (nAmount > pwallet->GetKHUBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient KHU balance");

    // Créer transaction
    std::vector<CRecipient> vecSend;
    CRecipient recipient = {GetScriptForDestination(dest), nAmount, false};
    vecSend.push_back(recipient);

    CWalletTx wtxNew;
    CReserveKey reservekey(pwallet);
    CAmount nFeeRet;
    int nChangePosRet = -1;
    std::string strFailReason;

    if (!pwallet->CreateKHUTransaction(vecSend, wtxNew, reservekey, nFeeRet, nChangePosRet, strFailReason))
        throw JSONRPCError(RPC_WALLET_ERROR, strFailReason);

    // Broadcast
    if (!pwallet->CommitTransaction(wtxNew, reservekey))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to commit transaction");

    return wtxNew.GetHash().GetHex();
}
```

### 2.5 listkhuunspent

```cpp
/**
 * RPC: listkhuunspent
 * Lister UTXOs KHU_T non dépensés
 */
UniValue listkhuunspent(const JSONRPCRequest& request) {
    if (request.fHelp)
        throw std::runtime_error(
            "listkhuunspent\n"
            "\nRetourne liste des UTXOs KHU_T non dépensés.\n"
            "\nRésultat:\n"
            "[\n"
            "  {\n"
            "    \"txid\": \"xxx\",\n"
            "    \"vout\": n,\n"
            "    \"address\": \"xxx\",\n"
            "    \"amount\": xxx.xxx,\n"
            "    \"confirmations\": n,\n"
            "    \"staked\": true|false\n"
            "  },\n"
            "  ...\n"
            "]\n"
        );

    LOCK2(cs_main, pwallet->cs_wallet);

    UniValue results(UniValue::VARR);

    for (const auto& [outpoint, khuCoin] : pwallet->mapKHUCoins) {
        UniValue entry(UniValue::VOBJ);

        entry.pushKV("txid", outpoint.hash.GetHex());
        entry.pushKV("vout", (int)outpoint.n);

        CTxDestination dest;
        if (ExtractDestination(khuCoin.scriptPubKey, dest))
            entry.pushKV("address", EncodeDestination(dest));

        entry.pushKV("amount", ValueFromAmount(khuCoin.amount));

        int nDepth = chainActive.Height() - khuCoin.nHeight + 1;
        entry.pushKV("confirmations", nDepth);
        entry.pushKV("staked", khuCoin.fStaked);

        results.push_back(entry);
    }

    return results;
}
```

### 2.6 getkhustate

```cpp
/**
 * RPC: getkhustate
 * Obtenir état global KHU
 */
UniValue getkhustate(const JSONRPCRequest& request) {
    if (request.fHelp)
        throw std::runtime_error(
            "getkhustate\n"
            "\nRetourne l'état global KHU.\n"
            "\nRésultat:\n"
            "{\n"
            "  \"C\": xxx.xxx,             (numeric) Collateral total\n"
            "  \"U\": xxx.xxx,             (numeric) Supply totale KHU\n"
            "  \"Cr\": xxx.xxx,            (numeric) Reward pool collateral\n"
            "  \"Ur\": xxx.xxx,            (numeric) Reward pool rights\n"
            "  \"R_annual\": 500,          (numeric) Yield rate (bp)\n"
            "  \"R_percent\": 5.00,        (numeric) Yield rate (%)\n"
            "  \"R_MAX_dynamic\": 3000,    (numeric) Max yield (bp)\n"
            "  \"last_yield_height\": n,   (numeric) Last yield update\n"
            "  \"height\": n,              (numeric) Current height\n"
            "  \"invariants_ok\": true     (boolean) Invariants valid\n"
            "}\n"
        );

    LOCK(cs_main);

    KhuGlobalState state;
    if (!LoadKhuState(chainActive.Height(), state))
        throw JSONRPCError(RPC_INTERNAL_ERROR, "Failed to load KHU state");

    UniValue result(UniValue::VOBJ);
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("R_annual", state.R_annual);
    result.pushKV("R_percent", (double)state.R_annual / 100.0);
    result.pushKV("R_MAX_dynamic", state.R_MAX_dynamic);
    result.pushKV("last_yield_height", (int)state.last_yield_update_height);
    result.pushKV("height", (int)state.nHeight);
    result.pushKV("invariants_ok", state.CheckInvariants());

    return result;
}
```

---

## 3. REGISTRATION RPC

### 3.1 Fichier : src/rpc/khu_rpc.cpp

```cpp
#include "rpc/server.h"
#include "wallet/wallet.h"
#include "khu/khu_state.h"

/**
 * Table RPC commands KHU
 */
static const CRPCCommand commands[] = {
    //  category    name                actor (function)        argNames
    //  ----------- ------------------- ----------------------  ----------
    {"khu",        "getkhubalance",    &getkhubalance,         {}},
    {"khu",        "mintkhu",          &mintkhu,               {"amount"}},
    {"khu",        "redeemkhu",        &redeemkhu,             {"amount"}},
    {"khu",        "sendkhu",          &sendkhu,               {"address","amount"}},
    {"khu",        "listkhuunspent",   &listkhuunspent,        {}},
    {"khu",        "getkhustate",      &getkhustate,           {}},
    {"khu",        "stakekhu",         &stakekhu,              {"amount"}},
    {"khu",        "unstakekhu",       &unstakekhu,            {"nullifier"}},
};

/**
 * Enregistrer commandes RPC KHU
 */
void RegisterKHURPCCommands(CRPCTable& t) {
    for (const auto& command : commands)
        t.appendCommand(command.name, &command);
}
```

### 3.2 Appel dans src/rpc/register.h

```cpp
void RegisterKHURPCCommands(CRPCTable& t);

// Dans RegisterAllCoreRPCCommands() :
void static RegisterAllCoreRPCCommands(CRPCTable& t) {
    RegisterBlockchainRPCCommands(t);
    RegisterMasternodeRPCCommands(t);
    RegisterWalletRPCCommands(t);
    RegisterKHURPCCommands(t);  // ← AJOUTER
    // ...
}
```

---

## 4. PERSISTENCE WALLET

### 4.1 Sauvegarde Wallet KHU

```cpp
/**
 * Sauvegarder données KHU dans wallet.dat
 * src/wallet/walletdb.cpp
 */
bool CWalletDB::WriteKHUCoin(const COutPoint& outpoint, const CKHUUTXO& khuCoin) {
    return Write(std::make_pair(std::string("khucoin"), outpoint), khuCoin);
}

bool CWalletDB::EraseKHUCoin(const COutPoint& outpoint) {
    return Erase(std::make_pair(std::string("khucoin"), outpoint));
}

/**
 * Charger données KHU depuis wallet.dat
 */
bool CWalletDB::LoadKHUCoins(CWallet* pwallet) {
    Dbc* pcursor = GetCursor();
    if (!pcursor)
        return false;

    while (true) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);

        int ret = ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND)
            break;

        std::string strType;
        ssKey >> strType;

        if (strType == "khucoin") {
            COutPoint outpoint;
            ssKey >> outpoint;

            CKHUUTXO khuCoin;
            ssValue >> khuCoin;

            pwallet->mapKHUCoins[outpoint] = khuCoin;
        }
    }

    pcursor->close();
    pwallet->UpdateKHUBalance();

    return true;
}
```

---

## 5. NOTIFICATIONS UI

### 5.1 Signaux Wallet KHU

```cpp
/**
 * Signaux pour notifier UI des changements KHU
 * src/wallet/wallet.h
 */
class CWallet {
public:
    boost::signals2::signal<void (CWallet* wallet, CAmount newBalance)> NotifyKHUBalanceChanged;
    boost::signals2::signal<void (CWallet* wallet, const uint256& txid)> NotifyKHUTransactionChanged;
};

/**
 * Émission signaux
 */
void CWallet::UpdateKHUBalance() {
    CAmount oldBalance = nKHUBalance;

    // Recalculer balance
    nKHUBalance = 0;
    for (const auto& [outpoint, khuCoin] : mapKHUCoins) {
        if (!khuCoin.fStaked)
            nKHUBalance += khuCoin.amount;
    }

    // Notifier si changement
    if (nKHUBalance != oldBalance) {
        NotifyKHUBalanceChanged(this, nKHUBalance);
    }
}
```

### 5.2 Connection UI (Qt)

```cpp
/**
 * Connection signaux wallet → UI
 * src/qt/walletmodel.cpp
 */
void WalletModel::subscribeToCoreSignals() {
    // Existant (PIV)
    wallet->NotifyStatusChanged.connect(...);
    wallet->NotifyAddressBookChanged.connect(...);

    // Nouveau (KHU)
    wallet->NotifyKHUBalanceChanged.connect(
        boost::bind(&WalletModel::updateKHUBalance, this, _1, _2)
    );

    wallet->NotifyKHUTransactionChanged.connect(
        boost::bind(&WalletModel::updateKHUTransaction, this, _1, _2)
    );
}

void WalletModel::updateKHUBalance(CWallet* wallet, CAmount newBalance) {
    Q_EMIT khuBalanceChanged(newBalance);
}
```

---

## 6. TESTS

### 6.1 Tests Unitaires : src/test/khu_wallet_tests.cpp

```cpp
#include <boost/test/unit_test.hpp>
#include "wallet/wallet.h"

BOOST_AUTO_TEST_SUITE(khu_wallet_tests)

BOOST_AUTO_TEST_CASE(test_khu_balance)
{
    CWallet wallet;

    // Initial balance = 0
    BOOST_CHECK_EQUAL(wallet.GetKHUBalance(), 0);

    // Ajouter UTXO KHU_T
    COutPoint outpoint(uint256S("abc"), 0);
    CKHUUTXO khuCoin;
    khuCoin.amount = 100 * COIN;
    khuCoin.fStaked = false;

    wallet.AddKHUCoin(outpoint, khuCoin);

    // Balance = 100 KHU
    BOOST_CHECK_EQUAL(wallet.GetKHUBalance(), 100 * COIN);
}

BOOST_AUTO_TEST_CASE(test_available_khu_coins)
{
    CWallet wallet;

    // Ajouter 3 UTXOs
    for (int i = 0; i < 3; i++) {
        COutPoint outpoint(uint256S("abc"), i);
        CKHUUTXO khuCoin;
        khuCoin.amount = 10 * COIN;
        khuCoin.fStaked = false;
        wallet.AddKHUCoin(outpoint, khuCoin);
    }

    // Vérifier available coins
    std::vector<COutput> vCoins = wallet.AvailableKHUCoins();
    BOOST_CHECK_EQUAL(vCoins.size(), 3);
}

BOOST_AUTO_TEST_SUITE_END()
```

### 6.2 Tests Fonctionnels : test/functional/khu_wallet.py

```python
#!/usr/bin/env python3
from test_framework.test_framework import PivxTestFramework
from test_framework.util import assert_equal

class KHUWalletTest(PivxTestFramework):
    def run_test(self):
        node = self.nodes[0]

        # Test 1: Balance initiale
        balance = node.getkhubalance()
        assert_equal(balance['total'], 0)

        # Test 2: MINT
        node.generate(101)  # Maturity coinbase
        node.mintkhu(100)
        node.generate(1)

        balance = node.getkhubalance()
        assert_equal(balance['transparent'], 100)

        # Test 3: Send KHU
        addr = node.getnewaddress()
        txid = node.sendkhu(addr, 50)
        node.generate(1)

        balance = node.getkhubalance()
        assert_equal(balance['transparent'], 50)

        # Test 4: List unspent
        unspent = node.listkhuunspent()
        assert len(unspent) > 0

if __name__ == '__main__':
    KHUWalletTest().main()
```

---

## 7. INTERDICTIONS

```
❌ Créer wallet KHU séparé (réutiliser CWallet)
❌ Modifier wallet.dat format (rétrocompatibilité)
❌ RPC sans validation montants
❌ Balance KHU sans scan blockchain
❌ Dépenser UTXO staké (fStaked=true)
❌ MINT sans vérifier balance PIV
❌ REDEEM sans vérifier balance KHU
❌ Transaction KHU sans fees PIV
```

---

## 8. RÉFÉRENCES

**Blueprints liés :**
- `02-KHU-COLORED-COIN.md` — Structure CKHUUTXO
- `03-MINT-REDEEM.md` — Transactions MINT/REDEEM

**Fichiers source :**
- `src/wallet/wallet.h` — Extension CWallet
- `src/rpc/khu_rpc.cpp` — Commandes RPC
- `src/wallet/walletdb.cpp` — Persistence

---

## 9. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales :**

1. **Réutilisation CWallet** : Extension, pas nouveau wallet
2. **RPC standard** : Format JSON-RPC compatible PIVX
3. **Persistence wallet.dat** : Sauvegarde UTXOs KHU
4. **Validation stricte** : Vérifier balances avant opérations
5. **UI notifications** : Signaux boost pour updates

**Statut :** ACTIF pour implémentation Phase 7-8

---

**FIN DU BLUEPRINT WALLET & RPC**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
