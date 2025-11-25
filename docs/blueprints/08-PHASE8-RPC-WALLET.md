# Blueprint Phase 8 — RPC/Wallet KHU

**Version:** 2.0 (Fusionné)
**Date:** 2025-11-25
**Status:** EN COURS
**Branche:** khu-phase8-rpc

---

## 1. OBJECTIF

Implémenter les commandes RPC et l'infrastructure wallet permettant aux utilisateurs d'interagir avec le système KHU via CLI/RPC.

**Prérequis:** Phases 1-7 complètes ✅

**Principe:** Wallet KHU = Extension de CWallet existant, pas nouveau wallet.

---

## 2. ARCHITECTURE WALLET KHU

### 2.1 Extension CWallet PIVX

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

public:
    // Lecture balances
    CAmount GetKHUBalance() const;
    CAmount GetZKHUBalance() const;
    CAmount GetKHUPendingYield() const;

    // Gestion UTXOs KHU_T
    std::vector<COutput> AvailableKHUCoins(bool fOnlySafe = true) const;
    bool AddKHUCoin(const COutPoint& outpoint, const CKHUUTXO& khuCoin);
    bool SpendKHUCoin(const COutPoint& outpoint);
    bool SelectKHUCoins(CAmount amount, std::vector<COutput>& vCoins) const;

    // Gestion notes ZKHU
    bool GetKHUNotes(std::vector<ZKHUNoteEntry>& notes, bool includeImmature = false) const;
    bool AddKHUNote(const uint256& noteId, const ZKHUNoteData& note);
    bool SpendKHUNote(const uint256& noteId);

    // Création transactions
    bool CreateKHUTransaction(
        const std::vector<CRecipient>& vecSend,
        CWalletTx& wtxNew,
        CReserveKey& reservekey,
        CAmount& nFeeRet,
        int& nChangePosRet,
        std::string& strFailReason,
        const CCoinControl* coinControl = nullptr
    );

private:
    void UpdateKHUBalance();
};
```

### 2.2 Structure ZKHUNoteEntry (Wallet)

```cpp
/**
 * Note ZKHU stockée dans wallet
 */
struct ZKHUNoteEntry {
    uint256 noteId;           // Commitment (cm)
    CAmount amount;           // Principal staké
    uint32_t stakeHeight;     // Bloc de stake
    uint256 nullifier;        // Pour spend
    libzcash::SaplingPaymentAddress address;

    // Méthodes computed
    bool IsMature(uint32_t currentHeight) const {
        return (currentHeight - stakeHeight) >= MATURITY_BLOCKS;  // 4320
    }

    uint32_t BlocksToMaturity(uint32_t currentHeight) const {
        if (IsMature(currentHeight)) return 0;
        return MATURITY_BLOCKS - (currentHeight - stakeHeight);
    }

    CAmount GetPendingYield(uint32_t currentHeight, uint16_t R_annual) const {
        if (!IsMature(currentHeight)) return 0;
        uint32_t daysStaked = (currentHeight - stakeHeight) / BLOCKS_PER_DAY;
        return (amount * R_annual / 10000) * daysStaked / 365;
    }
};
```

### 2.3 Tracking UTXOs KHU_T

```cpp
/**
 * Ajouter UTXO KHU_T au wallet
 */
bool CWallet::AddKHUCoin(const COutPoint& outpoint, const CKHUUTXO& khuCoin) {
    LOCK(cs_wallet);

    if (!IsMine(khuCoin.scriptPubKey))
        return false;

    mapKHUCoins[outpoint] = khuCoin;
    UpdateKHUBalance();
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

    mapKHUCoins.erase(it);
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

### 2.4 Scan Blockchain pour KHU

```cpp
/**
 * Scanner blockchain pour récupérer UTXOs KHU_T
 */
bool CWallet::ScanForKHUCoins(const CBlockIndex* pindexStart) {
    LOCK2(cs_main, cs_wallet);

    const CBlockIndex* pindex = pindexStart;

    while (pindex) {
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex))
            return error("ScanForKHUCoins: failed to read block");

        for (const auto& tx : block.vtx) {
            if (IsKHUTransaction(tx)) {
                ScanKHUTransaction(tx, pindex->nHeight);
            }
        }

        pindex = chainActive.Next(pindex);
    }

    return true;
}
```

---

## 3. LISTE DES RPC

### 3.1 RPC Existants (Phases 1-6) ✅

| Commande | Description | Fichier |
|----------|-------------|---------|
| `getkhustate` | État global KHU | rpc/khu.cpp:53 |
| `getkhustatecommitment` | Commitment finality | rpc/khu.cpp:140 |
| `domccommit` | Vote DOMC (commit) | rpc/khu.cpp:223 |
| `domcreveal` | Vote DOMC (reveal) | rpc/khu.cpp:363 |

### 3.2 RPC Phase 8 — P0 (BLOQUANT TESTNET)

| Commande | Description | Complexité |
|----------|-------------|------------|
| `khubalance` | Solde KHU (T + Z + yield) | Basse |
| `khulistunspent` | Liste UTXOs KHU_T | Basse |
| `khumint` | PIV → KHU_T | Moyenne |
| `khuredeem` | KHU_T → PIV | Moyenne |
| `khulistnotes` | Liste notes ZKHU | Moyenne |
| `khustake` | KHU_T → ZKHU | Haute |
| `khuunstake` | ZKHU → KHU_T + bonus | Haute |

### 3.3 RPC Phase 8 — P1 (Post-Testnet)

| Commande | Description | Complexité |
|----------|-------------|------------|
| `khusend` | Transfer KHU_T | Moyenne |
| `khugetinfo` | Info R%, DOMC, wallet | Basse |

---

## 4. SPÉCIFICATIONS RPC DÉTAILLÉES

### 4.1 `khubalance`

```
Usage: khubalance

Result:
{
  "transparent": n,         // KHU_T en UTXOs (satoshis)
  "staked": n,              // ZKHU principal (satoshis)
  "pending_yield": n,       // Yield accumulé non réclamé
  "total": n,               // transparent + staked + pending_yield
  "immature_count": n,      // Notes < 4320 blocs
  "mature_count": n         // Notes >= 4320 blocs
}

Exemple:
> pivx-cli khubalance
```

### 4.2 `khulistunspent`

```
Usage: khulistunspent [minconf=1] [maxconf=9999999]

Result:
[
  {
    "txid": "hash",
    "vout": n,
    "address": "addr",
    "amount": n,
    "confirmations": n,
    "spendable": true|false,
    "staked": true|false
  },
  ...
]
```

### 4.3 `khumint <amount>`

```
Usage: khumint <amount>

Arguments:
  amount    (numeric, required) Montant PIV à convertir

Result:
{
  "txid": "hash",
  "amount_khu": n,
  "address": "addr",
  "fee": n
}

Validation:
- amount > 0
- amount <= solde PIV disponible
- V6 activé
```

**Implémentation:**
```cpp
UniValue khumint(const JSONRPCRequest& request) {
    LOCK2(cs_main, pwallet->cs_wallet);

    CAmount nAmount = AmountFromValue(request.params[0]);
    if (nAmount <= 0)
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Invalid amount");

    if (nAmount > pwallet->GetBalance())
        throw JSONRPCError(RPC_WALLET_INSUFFICIENT_FUNDS, "Insufficient PIV");

    // Créer TX type KHU_MINT
    CMutableTransaction tx;
    tx.nType = TxType::KHU_MINT;

    // Input: PIV à brûler
    if (!pwallet->SelectCoins(nAmount, tx.vin))
        throw JSONRPCError(RPC_WALLET_ERROR, "Failed to select coins");

    // Output 0: Proof-of-burn
    CScript burnScript;
    burnScript << OP_RETURN << ToByteVector(uint256::ZERO);
    tx.vout.push_back(CTxOut(nAmount, burnScript));

    // Output 1: KHU_T créé
    CKeyID keyID;
    pwallet->GetKeyFromPool(keyID);
    tx.vout.push_back(CTxOut(nAmount, GetScriptForDestination(keyID)));

    // Sign & broadcast...
}
```

### 4.4 `khuredeem <amount>`

```
Usage: khuredeem <amount>

Arguments:
  amount    (numeric, required) Montant KHU_T à convertir

Result:
{
  "txid": "hash",
  "amount_piv": n,
  "address": "addr",
  "fee": n
}

Validation:
- amount > 0
- amount <= solde KHU_T
- UTXOs non stakés (fStaked == false)
```

### 4.5 `khulistnotes`

```
Usage: khulistnotes [include_immature=false]

Result:
[
  {
    "note_id": "hash",
    "amount": n,
    "stake_height": n,
    "blocks_staked": n,
    "mature": true|false,
    "blocks_to_maturity": n,
    "pending_yield": n,
    "nullifier": "hash"
  },
  ...
]
```

### 4.6 `khustake <amount>`

```
Usage: khustake <amount>

Arguments:
  amount    (numeric, required) Montant KHU_T à staker

Result:
{
  "txid": "hash",
  "note_id": "hash",
  "amount": n,
  "stake_height": n,
  "maturity_height": n      // stake_height + 4320
}

Validation:
- amount > 0
- amount <= solde KHU_T
- Sapling params chargés
```

### 4.7 `khuunstake <note_id>`

```
Usage: khuunstake <note_id>

Arguments:
  note_id   (string, required) ID note ZKHU (commitment)

Result:
{
  "txid": "hash",
  "principal": n,
  "bonus": n,               // Yield R%
  "total": n,
  "stake_duration": n       // Blocs
}

Validation:
- note_id existe dans wallet
- Note mature (>= 4320 blocs)
- Nullifier non dépensé
```

### 4.8 `khusend <address> <amount>`

```
Usage: khusend <address> <amount>

Result:
{
  "txid": "hash",
  "amount": n,
  "fee": n
}
```

### 4.9 `khugetinfo`

```
Usage: khugetinfo

Result:
{
  "state": {
    "C": n, "U": n, "Cr": n, "Ur": n, "T": n,
    "invariants_ok": true
  },
  "yield": {
    "R_annual": n,              // Basis points
    "R_annual_pct": x.xx,       // Pourcentage
    "R_MAX_dynamic": n,
    "daily_rate_bps": x.xx      // R_annual / 365
  },
  "domc": {
    "cycle_id": n,
    "cycle_start": n,
    "phase": "idle|commit|reveal",
    "blocks_to_next_phase": n
  },
  "wallet": {
    "khu_transparent": n,
    "khu_staked": n,
    "pending_yield": n
  }
}
```

---

## 5. PERSISTENCE WALLET

### 5.1 Sauvegarde wallet.dat

```cpp
/**
 * Sauvegarder UTXOs KHU dans wallet.dat
 */
bool CWalletDB::WriteKHUCoin(const COutPoint& outpoint, const CKHUUTXO& khuCoin) {
    return Write(std::make_pair(std::string("khucoin"), outpoint), khuCoin);
}

bool CWalletDB::EraseKHUCoin(const COutPoint& outpoint) {
    return Erase(std::make_pair(std::string("khucoin"), outpoint));
}

/**
 * Sauvegarder notes ZKHU
 */
bool CWalletDB::WriteZKHUNote(const uint256& noteId, const ZKHUNoteEntry& note) {
    return Write(std::make_pair(std::string("zkhunote"), noteId), note);
}

bool CWalletDB::EraseZKHUNote(const uint256& noteId) {
    return Erase(std::make_pair(std::string("zkhunote"), noteId));
}
```

### 5.2 Chargement wallet.dat

```cpp
/**
 * Charger données KHU depuis wallet.dat
 */
bool CWalletDB::LoadKHUData(CWallet* pwallet) {
    Dbc* pcursor = GetCursor();
    if (!pcursor) return false;

    while (true) {
        CDataStream ssKey(SER_DISK, CLIENT_VERSION);
        CDataStream ssValue(SER_DISK, CLIENT_VERSION);

        int ret = ReadAtCursor(pcursor, ssKey, ssValue);
        if (ret == DB_NOTFOUND) break;

        std::string strType;
        ssKey >> strType;

        if (strType == "khucoin") {
            COutPoint outpoint;
            ssKey >> outpoint;
            CKHUUTXO khuCoin;
            ssValue >> khuCoin;
            pwallet->mapKHUCoins[outpoint] = khuCoin;
        }
        else if (strType == "zkhunote") {
            uint256 noteId;
            ssKey >> noteId;
            ZKHUNoteEntry note;
            ssValue >> note;
            pwallet->mapZKHUNotes[noteId] = note;
        }
    }

    pcursor->close();
    pwallet->UpdateKHUBalance();
    return true;
}
```

---

## 6. NOTIFICATIONS UI

### 6.1 Signaux Wallet

```cpp
/**
 * Signaux pour notifier UI des changements KHU
 */
class CWallet {
public:
    boost::signals2::signal<void(CWallet*, CAmount)> NotifyKHUBalanceChanged;
    boost::signals2::signal<void(CWallet*, const uint256&)> NotifyKHUTransactionChanged;
    boost::signals2::signal<void(CWallet*, const uint256&)> NotifyZKHUNoteChanged;
};
```

### 6.2 Connection Qt

```cpp
void WalletModel::subscribeToCoreSignals() {
    // KHU signals
    wallet->NotifyKHUBalanceChanged.connect(
        boost::bind(&WalletModel::updateKHUBalance, this, _1, _2)
    );
}

void WalletModel::updateKHUBalance(CWallet* wallet, CAmount newBalance) {
    Q_EMIT khuBalanceChanged(newBalance);
}
```

---

## 7. ORDRE D'IMPLÉMENTATION

```
Phase 8.1 — Lecture (Simple, pas de TX)
├── 1. khubalance
├── 2. khulistunspent
└── 3. khulistnotes

Phase 8.2 — Transactions Transparentes
├── 4. khumint
├── 5. khuredeem
└── 6. khusend

Phase 8.3 — Transactions ZKHU (Sapling)
├── 7. khustake
└── 8. khuunstake

Phase 8.4 — Agrégation
└── 9. khugetinfo
```

---

## 8. TESTS

### 8.1 Tests Unitaires

```cpp
BOOST_AUTO_TEST_SUITE(khu_wallet_tests)

// Balance tests
BOOST_AUTO_TEST_CASE(test_khu_balance_empty)
BOOST_AUTO_TEST_CASE(test_khu_balance_after_mint)
BOOST_AUTO_TEST_CASE(test_khu_balance_after_stake)

// UTXO tests
BOOST_AUTO_TEST_CASE(test_add_khu_coin)
BOOST_AUTO_TEST_CASE(test_spend_khu_coin)
BOOST_AUTO_TEST_CASE(test_available_khu_coins)

// Note tests
BOOST_AUTO_TEST_CASE(test_add_zkhu_note)
BOOST_AUTO_TEST_CASE(test_note_maturity)
BOOST_AUTO_TEST_CASE(test_pending_yield_calculation)

// RPC tests
BOOST_AUTO_TEST_CASE(test_rpc_khumint)
BOOST_AUTO_TEST_CASE(test_rpc_khuredeem)
BOOST_AUTO_TEST_CASE(test_rpc_khustake)
BOOST_AUTO_TEST_CASE(test_rpc_khuunstake)

// Integration
BOOST_AUTO_TEST_CASE(test_full_cycle_mint_stake_unstake_redeem)

BOOST_AUTO_TEST_SUITE_END()
```

### 8.2 Tests Fonctionnels

```python
# test/functional/khu_rpc.py
class KHURPCTest(PivxTestFramework):
    def run_test(self):
        # Test balance
        balance = self.nodes[0].khubalance()
        assert_equal(balance['total'], 0)

        # Test MINT
        self.nodes[0].generate(101)
        self.nodes[0].khumint(100)
        self.nodes[0].generate(1)
        balance = self.nodes[0].khubalance()
        assert_equal(balance['transparent'], 100 * COIN)

        # Test STAKE
        self.nodes[0].khustake(50)
        self.nodes[0].generate(1)
        balance = self.nodes[0].khubalance()
        assert_equal(balance['staked'], 50 * COIN)

        # Test UNSTAKE (après maturité)
        self.nodes[0].generate(4320)  # Maturité
        notes = self.nodes[0].khulistnotes()
        self.nodes[0].khuunstake(notes[0]['note_id'])
```

---

## 9. REGISTRATION RPC

### 9.1 Table des commandes

```cpp
static const CRPCCommand commands[] = {
    //  category    name                 actor              argNames
    // Existants
    {"khu", "getkhustate",           &getkhustate,           {}},
    {"khu", "getkhustatecommitment", &getkhustatecommitment, {"height"}},
    {"khu", "domccommit",            &domccommit,            {"R_proposal", "mn_outpoint"}},
    {"khu", "domcreveal",            &domcreveal,            {"R_proposal", "salt", "mn_outpoint"}},
    // Phase 8
    {"khu", "khubalance",            &khubalance,            {}},
    {"khu", "khulistunspent",        &khulistunspent,        {"minconf", "maxconf"}},
    {"khu", "khulistnotes",          &khulistnotes,          {"include_immature"}},
    {"khu", "khumint",               &khumint,               {"amount"}},
    {"khu", "khuredeem",             &khuredeem,             {"amount"}},
    {"khu", "khustake",              &khustake,              {"amount"}},
    {"khu", "khuunstake",            &khuunstake,            {"note_id"}},
    {"khu", "khusend",               &khusend,               {"address", "amount"}},
    {"khu", "khugetinfo",            &khugetinfo,            {}},
};
```

---

## 10. INTERDICTIONS

```
❌ Créer wallet KHU séparé (réutiliser CWallet)
❌ Modifier format wallet.dat (rétrocompatibilité)
❌ RPC sans validation montants
❌ Balance KHU sans scan blockchain
❌ Dépenser UTXO staké (fStaked=true)
❌ MINT sans vérifier balance PIV
❌ REDEEM sans vérifier balance KHU
❌ UNSTAKE sans vérifier maturité
❌ Transaction KHU sans fees PIV
```

---

## 11. CHECKLIST TESTNET

- [ ] Infrastructure wallet (mapKHUCoins, mapZKHUNotes)
- [ ] Persistence wallet.dat
- [ ] `khubalance` fonctionnel
- [ ] `khulistunspent` fonctionnel
- [ ] `khulistnotes` fonctionnel
- [ ] `khumint` fonctionnel + tests
- [ ] `khuredeem` fonctionnel + tests
- [ ] `khustake` fonctionnel + tests
- [ ] `khuunstake` fonctionnel + tests
- [ ] Invariants préservés après chaque RPC
- [ ] 0 régression tests existants

---

**Signature:** Claude (Senior C++)
**Date:** 2025-11-25
**Version:** 2.0 (Fusionné depuis 08-WALLET-RPC.md)
