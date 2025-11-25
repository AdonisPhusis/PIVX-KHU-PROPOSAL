# Blueprint Phase 8 — RPC/Wallet KHU

**Version:** 1.0
**Date:** 2025-11-25
**Status:** EN COURS
**Branche:** khu-phase8-rpc

---

## 1. OBJECTIF

Implémenter les commandes RPC permettant aux utilisateurs d'interagir avec le système KHU via CLI/RPC.

**Prérequis:** Phases 1-7 complètes ✅

---

## 2. LISTE DES RPC À IMPLÉMENTER

### 2.1 RPC Existants (Phases 1-6)

| Commande | Status | Fichier |
|----------|--------|---------|
| `getkhustate` | ✅ | rpc/khu.cpp:53 |
| `getkhustatecommitment` | ✅ | rpc/khu.cpp:140 |
| `domccommit` | ✅ | rpc/khu.cpp:223 |
| `domcreveal` | ✅ | rpc/khu.cpp:363 |

### 2.2 RPC Phase 8 — Priorité P0 (BLOQUANT TESTNET)

| Commande | Description | Complexité |
|----------|-------------|------------|
| `khumint` | PIV → KHU_T | Moyenne |
| `khuredeem` | KHU_T → PIV | Moyenne |
| `khustake` | KHU_T → ZKHU | Haute |
| `khuunstake` | ZKHU → KHU_T + bonus | Haute |

### 2.3 RPC Phase 8 — Priorité P1

| Commande | Description | Complexité |
|----------|-------------|------------|
| `khubalance` | Solde KHU (T + Z) | Basse |
| `khulistnotes` | Liste notes ZKHU | Moyenne |
| `khulistunspent` | Liste UTXOs KHU_T | Basse |

### 2.4 RPC Phase 8 — Priorité P2

| Commande | Description | Complexité |
|----------|-------------|------------|
| `khusend` | Transfer KHU_T | Moyenne |
| `khugetinfo` | Info R%, cycle DOMC | Basse |

---

## 3. SPÉCIFICATIONS DÉTAILLÉES

### 3.1 `khumint <amount>`

**Fonction:** Convertit PIV en KHU_T (1:1)

```
Usage: khumint <amount>

Arguments:
  amount    (numeric, required) Montant en PIV à convertir

Result:
{
  "txid": "hash",           // Transaction ID
  "amount": n,              // Montant minté (satoshis)
  "address": "addr",        // Adresse KHU_T destination
  "fee": n                  // Fee payé
}

Exemple:
> pivx-cli khumint 100
```

**Implémentation:**
1. Vérifier solde PIV suffisant
2. Créer TX type KHU_MINT
3. Input: UTXO PIV standard
4. Output: KHU_T UTXO (marqué comme colored coin)
5. Broadcast et retourner txid

**Validation:**
- amount > 0
- amount <= solde PIV disponible
- V6 activé

---

### 3.2 `khuredeem <amount>`

**Fonction:** Convertit KHU_T en PIV (1:1)

```
Usage: khuredeem <amount>

Arguments:
  amount    (numeric, required) Montant KHU_T à convertir

Result:
{
  "txid": "hash",
  "amount": n,
  "address": "addr",        // Adresse PIV destination
  "fee": n
}

Exemple:
> pivx-cli khuredeem 50
```

**Implémentation:**
1. Vérifier solde KHU_T suffisant
2. Sélectionner UTXOs KHU_T (coin selection)
3. Créer TX type KHU_REDEEM
4. Input: KHU_T UTXOs
5. Output: PIV standard + change KHU_T si nécessaire
6. Broadcast

**Validation:**
- amount > 0
- amount <= solde KHU_T disponible
- UTXOs KHU_T non stakés (fStaked == false)

---

### 3.3 `khustake <amount>`

**Fonction:** Convertit KHU_T en ZKHU (privacy + yield)

```
Usage: khustake <amount>

Arguments:
  amount    (numeric, required) Montant KHU_T à staker

Result:
{
  "txid": "hash",
  "note_id": "hash",        // Commitment de la note ZKHU
  "amount": n,
  "stake_height": n,        // Hauteur de début stake
  "maturity_height": n      // Hauteur de maturité (stake_height + 4320)
}

Exemple:
> pivx-cli khustake 1000
```

**Implémentation:**
1. Vérifier solde KHU_T suffisant
2. Générer clés Sapling (ivk, ovk, d)
3. Créer note ZKHU avec amount
4. Créer TX type KHU_STAKE
5. Input: KHU_T UTXO
6. Output: Sapling shielded output (1 note)
7. Stocker note dans wallet
8. Broadcast

**Validation:**
- amount > 0
- amount <= solde KHU_T
- Sapling params chargés

---

### 3.4 `khuunstake <note_id>`

**Fonction:** Convertit ZKHU en KHU_T + bonus yield

```
Usage: khuunstake <note_id>

Arguments:
  note_id   (string, required) ID de la note ZKHU (commitment hash)

Result:
{
  "txid": "hash",
  "principal": n,           // Montant principal
  "bonus": n,               // Yield accumulé
  "total": n,               // principal + bonus
  "address": "addr",        // Adresse KHU_T destination
  "stake_duration": n       // Durée en blocs
}

Exemple:
> pivx-cli khuunstake "abc123..."
```

**Implémentation:**
1. Lire note depuis wallet
2. Vérifier maturité (>= 4320 blocs)
3. Calculer bonus = Ur_accumulated de la note
4. Créer TX type KHU_UNSTAKE
5. Input: Sapling spend (nullifier)
6. Output: KHU_T UTXO (principal + bonus)
7. Broadcast

**Validation:**
- note_id existe dans wallet
- note mature (current_height - stake_height >= 4320)
- nullifier non dépensé

---

### 3.5 `khubalance`

**Fonction:** Affiche solde KHU total

```
Usage: khubalance

Result:
{
  "transparent": n,         // KHU_T en UTXOs
  "staked": n,              // ZKHU en notes (principal)
  "pending_yield": n,       // Yield accumulé non réclamé
  "total": n,               // transparent + staked
  "immature_count": n       // Nombre de notes < 4320 blocs
}

Exemple:
> pivx-cli khubalance
```

**Implémentation:**
1. Scanner UTXOs KHU_T du wallet
2. Scanner notes ZKHU du wallet
3. Calculer yield pending pour chaque note mature
4. Agréger et retourner

---

### 3.6 `khulistnotes`

**Fonction:** Liste les notes ZKHU du wallet

```
Usage: khulistnotes [include_immature=false]

Arguments:
  include_immature  (boolean, optional) Inclure notes immatures

Result:
[
  {
    "note_id": "hash",
    "amount": n,
    "stake_height": n,
    "current_height": n,
    "blocks_staked": n,
    "mature": true|false,
    "blocks_to_maturity": n,    // 0 si mature
    "pending_yield": n,
    "nullifier": "hash"
  },
  ...
]

Exemple:
> pivx-cli khulistnotes true
```

---

### 3.7 `khulistunspent`

**Fonction:** Liste les UTXOs KHU_T

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

Exemple:
> pivx-cli khulistunspent
```

---

### 3.8 `khusend <address> <amount>`

**Fonction:** Envoie KHU_T à une adresse

```
Usage: khusend <address> <amount>

Arguments:
  address   (string, required) Adresse KHU destination
  amount    (numeric, required) Montant à envoyer

Result:
{
  "txid": "hash",
  "amount": n,
  "fee": n
}
```

---

### 3.9 `khugetinfo`

**Fonction:** Informations détaillées KHU

```
Usage: khugetinfo

Result:
{
  "state": {
    "C": n, "U": n, "Cr": n, "Ur": n, "T": n
  },
  "yield": {
    "R_annual": n,
    "R_annual_pct": x.xx,
    "R_MAX_dynamic": n,
    "daily_rate_pct": x.xxxx
  },
  "domc": {
    "cycle_id": n,
    "cycle_start": n,
    "phase": "idle|commit|reveal|finalize",
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

## 4. PLAN D'IMPLÉMENTATION

### Phase 8.1 — Core RPC (P0)

| Étape | Tâche | Fichiers | Durée Est. |
|-------|-------|----------|------------|
| 8.1.1 | `khumint` | rpc/khu.cpp, wallet/wallet.cpp | - |
| 8.1.2 | `khuredeem` | rpc/khu.cpp, wallet/wallet.cpp | - |
| 8.1.3 | `khubalance` | rpc/khu.cpp | - |
| 8.1.4 | `khulistunspent` | rpc/khu.cpp | - |
| 8.1.5 | Tests unitaires P0 | test/rpc_khu_tests.cpp | - |

### Phase 8.2 — ZKHU RPC (P0)

| Étape | Tâche | Fichiers | Durée Est. |
|-------|-------|----------|------------|
| 8.2.1 | `khustake` | rpc/khu.cpp, wallet/wallet.cpp, sapling/* | - |
| 8.2.2 | `khuunstake` | rpc/khu.cpp, wallet/wallet.cpp | - |
| 8.2.3 | `khulistnotes` | rpc/khu.cpp | - |
| 8.2.4 | Tests unitaires ZKHU | test/rpc_khu_tests.cpp | - |

### Phase 8.3 — Extras (P1/P2)

| Étape | Tâche | Fichiers |
|-------|-------|----------|
| 8.3.1 | `khusend` | rpc/khu.cpp |
| 8.3.2 | `khugetinfo` | rpc/khu.cpp |
| 8.3.3 | Tests complets | test/rpc_khu_tests.cpp |
| 8.3.4 | Documentation | doc/khu-rpc.md |

---

## 5. DÉPENDANCES WALLET

### 5.1 Modifications wallet/wallet.h

```cpp
// KHU wallet functions
bool GetKHUBalance(CAmount& transparent, CAmount& staked, CAmount& pendingYield) const;
bool SelectKHUCoins(CAmount amount, std::vector<COutput>& vCoins) const;
bool GetKHUNotes(std::vector<ZKHUNoteEntry>& notes, bool includeImmature = false) const;
bool AddKHUNote(const uint256& noteId, const ZKHUNoteData& note);
bool SpendKHUNote(const uint256& noteId);
```

### 5.2 Structure ZKHUNoteEntry (wallet)

```cpp
struct ZKHUNoteEntry {
    uint256 noteId;           // Commitment
    CAmount amount;           // Principal
    uint32_t stakeHeight;     // Bloc de stake
    uint256 nullifier;        // Pour spend
    libzcash::SaplingPaymentAddress address;

    // Computed
    bool IsMature(uint32_t currentHeight) const;
    CAmount GetPendingYield(uint32_t currentHeight, uint16_t R_annual) const;
};
```

---

## 6. ORDRE D'IMPLÉMENTATION RECOMMANDÉ

```
1. khubalance       (lecture seule, simple)
2. khulistunspent   (lecture seule, simple)
3. khumint          (TX simple, pas de Sapling)
4. khuredeem        (TX simple, pas de Sapling)
5. khulistnotes     (lecture ZKHU)
6. khustake         (TX Sapling, complexe)
7. khuunstake       (TX Sapling, complexe)
8. khusend          (optionnel)
9. khugetinfo       (agrégation)
```

---

## 7. TESTS REQUIS

### 7.1 Tests Unitaires

```cpp
BOOST_AUTO_TEST_SUITE(rpc_khu_tests)

// P0 Tests
BOOST_AUTO_TEST_CASE(test_khumint_basic)
BOOST_AUTO_TEST_CASE(test_khumint_insufficient_funds)
BOOST_AUTO_TEST_CASE(test_khuredeem_basic)
BOOST_AUTO_TEST_CASE(test_khuredeem_insufficient_khu)
BOOST_AUTO_TEST_CASE(test_khubalance)
BOOST_AUTO_TEST_CASE(test_khulistunspent)

// ZKHU Tests
BOOST_AUTO_TEST_CASE(test_khustake_basic)
BOOST_AUTO_TEST_CASE(test_khuunstake_mature)
BOOST_AUTO_TEST_CASE(test_khuunstake_immature_rejected)
BOOST_AUTO_TEST_CASE(test_khulistnotes)

// Integration
BOOST_AUTO_TEST_CASE(test_full_cycle_mint_stake_unstake_redeem)

BOOST_AUTO_TEST_SUITE_END()
```

### 7.2 Tests Fonctionnels

```python
# test/functional/khu_rpc.py
def test_mint_redeem_roundtrip():
def test_stake_unstake_with_yield():
def test_balance_updates():
```

---

## 8. CHECKLIST AVANT TESTNET

- [ ] `khumint` fonctionnel + tests
- [ ] `khuredeem` fonctionnel + tests
- [ ] `khustake` fonctionnel + tests
- [ ] `khuunstake` fonctionnel + tests
- [ ] `khubalance` fonctionnel
- [ ] `khulistnotes` fonctionnel
- [ ] `khulistunspent` fonctionnel
- [ ] Invariants préservés après chaque RPC
- [ ] Documentation RPC complète
- [ ] 0 régression tests existants

---

**Signature:** Claude (Senior C++)
**Date:** 2025-11-25
