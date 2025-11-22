# 08 — KHU HTLC / GATEWAY BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit l'architecture **HTLC** (Hash Time Lock Contracts) pour KHU et l'architecture **Gateway** (atomic swaps cross-chain).

**RÉVÉLATION ARCHITECTURALE FONDAMENTALE :**

```
KHU = UNITÉ DE COMPTE INVISIBLE (comme le mètre)

❌ User n'a PAS besoin de wallet KHU pour swap !
✅ Gateway utilise KHU pour calculer équivalences (invisible)
✅ Swap direct BTC ↔ USDC via HTLC standard
✅ HTLC KHU = HTLC Bitcoin standard (AUCUN code spécial !)
```

---

## 1. RÈGLES FONDAMENTALES HTLC

### 1.1 HTLC KHU = HTLC Bitcoin Standard

```
✅ PIVX supporte DÉJÀ les scripts HTLC (Bitcoin-compatible)
✅ KHU = Token UTXO standard (comme PIV)
✅ HTLC KHU fonctionne EXACTEMENT comme HTLC PIV
❌ AUCUNE implémentation HTLC spéciale nécessaire pour KHU
```

**Raison :** KHU est un token UTXO standard. Les scripts HTLC Bitcoin fonctionnent sur n'importe quel UTXO (PIV ou KHU).

### 1.2 Opcodes Bitcoin Standard (Déjà dans PIVX)

```cpp
OP_IF                    // Conditionnel
OP_ELSE                  // Alternative
OP_ENDIF                 // Fin conditionnel
OP_HASH160               // Hash du secret (SHA256 + RIPEMD160)
OP_EQUALVERIFY           // Vérification égalité
OP_CHECKLOCKTIMEVERIFY   // Timelock (CLTV)
OP_CHECKSIG              // Vérification signature
OP_DROP                  // Suppression stack
```

**C'est TOUT ce qu'il faut pour faire des HTLC !**

Ça marche pour PIV → Ça marche pour KHU automatiquement !

### 1.3 Template HTLC Standard

**Script HTLC canonique (Bitcoin-compatible) :**

```cpp
OP_IF
    // Branche CLAIM (reveal secret)
    <locktime> OP_CHECKLOCKTIMEVERIFY OP_DROP
    OP_HASH160 <hash160(secret)> OP_EQUALVERIFY
    <receiver_pubkey> OP_CHECKSIG
OP_ELSE
    // Branche REFUND (timeout)
    <timeout> OP_CHECKLOCKTIMEVERIFY OP_DROP
    <sender_pubkey> OP_CHECKSIG
OP_ENDIF
```

**Ce script fonctionne pour :**
- ✅ UTXO PIV
- ✅ UTXO KHU
- ✅ UTXO Bitcoin
- ✅ UTXO Litecoin
- ✅ Tout UTXO Bitcoin-compatible

**Aucune modification nécessaire pour KHU !**

---

## 2. KHU = UNITÉ DE COMPTE INVISIBLE

### 2.1 Analogie Parfaite : Le Mètre

**Le MÈTRE comme unité de mesure :**

```
❌ Tu n'as pas besoin de "posséder des mètres"
✅ Le mètre sert juste à mesurer

Exemple :
  Table A = 2 mètres
  Table B = 3 mètres
  → Tu peux comparer les tables (A < B)
  → Mais tu n'as pas de "stock de mètres" quelque part
```

**KHU comme unité de compte :**

```
❌ Alice n'a pas besoin de "posséder du KHU"
✅ KHU sert juste à calculer équivalences

Exemple :
  1 BTC = 50,000 KHU (rate market)
  1 USDC = 0.5 KHU (rate market)
  → 1 BTC = combien USDC ?
  → 50,000 KHU ÷ 0.5 KHU/USDC = 100,000 USDC

  Mais Alice n'a JAMAIS eu de KHU !
  KHU a juste servi à calculer !
```

### 2.2 Exemple Swap BTC → USDC (Sans KHU)

**Alice veut échanger 1 BTC contre USDC :**

```
┌─────────────────────────────────────────────────┐
│ ÉTAPE 1 : Gateway calcule (invisible pour Alice)│
└─────────────────────────────────────────────────┘

Gateway:
  1 BTC = 50,000 KHU (market rate)
  1 USDC = 0.5 KHU (market rate)

  Donc Alice devrait recevoir :
  50,000 KHU ÷ 0.5 KHU/USDC = 100,000 USDC

┌─────────────────────────────────────────────────┐
│ ÉTAPE 2 : Gateway trouve contrepartie           │
└─────────────────────────────────────────────────┘

Bob veut échanger 100,000 USDC contre BTC
Bob a créé HTLC USDC avec :
  • Lock : 100,000 USDC
  • Hash : 0x9a8b... (coordonné par Gateway)
  • Timeout : T2

┌─────────────────────────────────────────────────┐
│ ÉTAPE 3 : Alice crée HTLC Bitcoin               │
└─────────────────────────────────────────────────┘

Alice crée HTLC BTC avec :
  • Lock : 1 BTC
  • Hash : 0x9a8b... (même hash que Bob !)
  • Timeout : T1 (T1 > T2 pour sécurité)

┌─────────────────────────────────────────────────┐
│ ÉTAPE 4 : Alice révèle secret                   │
└─────────────────────────────────────────────────┘

Alice broadcast transaction USDC :
  • Reveal secret : 0x1234...
  • Claim : 100,000 USDC

Secret est maintenant PUBLIC on-chain !

┌─────────────────────────────────────────────────┐
│ ÉTAPE 5 : Bob utilise secret                    │
└─────────────────────────────────────────────────┘

Bob broadcast transaction BTC :
  • Use secret : 0x1234... (copié de tx Alice)
  • Claim : 1 BTC

┌─────────────────────────────────────────────────┐
│ RÉSULTAT : Atomic swap complété !               │
└─────────────────────────────────────────────────┘

Alice : 1 BTC → 100,000 USDC ✅
Bob : 100,000 USDC → 1 BTC ✅

KHU n'apparaît NULLE PART dans ces transactions !
KHU a juste servi au Gateway pour calculer 1 BTC = 100k USDC
```

---

## 3. QUI A BESOIN DE KHU ? (OPTIONNEL)

### 3.1 Cas d'Usage KHU

**KHU wallet est OPTIONNEL. Seulement pour :**

#### Cas 1 : Market Makers / LP Avancés

```
Bob veut faire du market making :
  • Il a 10 BTC
  • Il a 1M USDC
  • Il a 500 ZEC

Au lieu de créer HTLC séparés :
  1. Mint KHU depuis ses assets
  2. Créer UN pool KHU diversifié
  3. Plus flexible pour matching

Mais c'est OPTIONNEL !
99% des swaps n'ont pas besoin de ça !
```

#### Cas 2 : Utilisateurs PIVX (Staking, Governance)

```
Quelqu'un qui veut :
  • Staker en ZKHU (privacy + yield R%)
  • Participer à la governance PIVX (DOMC)
  • Utiliser features natives PIVX

→ A besoin de KHU wallet
→ Mint PIV → KHU
→ Stake KHU_T → ZKHU
```

#### Cas 3 : Hedge Contre Volatilité

```
Quelqu'un qui veut :
  • Sortir de la volatilité BTC/ZEC
  • Mais rester dans écosystème décentralisé
  • Pas convertir en USDC (centralisé)

→ Peut détenir KHU (plus stable que BTC)
```

### 3.2 Pour Swaps Simples : KHU PAS NÉCESSAIRE !

```
Alice veut BTC → USDC
Bob veut USDC → BTC

Alice n'a PAS besoin de :
  ❌ Wallet KHU
  ❌ Balance KHU
  ❌ Mint KHU
  ❌ Rien en KHU !

Alice a juste besoin de :
  ✅ Son wallet Bitcoin (Ledger, Trezor, etc.)
  ✅ Instructions HTLC du Gateway
  ✅ Créer HTLC Bitcoin
  ✅ C'est tout !
```

---

## 4. ARCHITECTURE GATEWAY

### 4.1 Version Minimale (Swaps Simples)

```
┌─────────────────────────────────────────────┐
│  KHU GATEWAY (Minimal Version)              │
├─────────────────────────────────────────────┤
│                                             │
│  ❌ PAS de wallet KHU intégré               │
│  ❌ User n'a pas besoin de KHU              │
│                                             │
│  ✅ Interface swap                          │
│  ✅ Watchers (BTC, ETH, USDC, ZEC...)       │
│  ✅ Matching engine                         │
│  ✅ Price discovery (KHU unité de compte)   │
│  ✅ Génère instructions HTLC                │
│                                             │
│  User connecte :                            │
│  • Bitcoin wallet (Ledger, etc.)           │
│  • Ethereum wallet (MetaMask)              │
│  • Zcash wallet, etc.                       │
│                                             │
│  Swap direct BTC ↔ USDC                     │
│  KHU invisible (calcul interne uniquement)  │
└─────────────────────────────────────────────┘
```

**Composants Gateway minimal :**

1. **Watchers (Blockchain Monitors)**
   - Bitcoin watcher
   - Ethereum watcher
   - Zcash watcher
   - Etc.

2. **Matching Engine**
   - Order book
   - Price discovery (via KHU unité de compte)
   - Match Alice (BTC) ↔ Bob (USDC)

3. **HTLC Instructions Generator**
   - Génère script HTLC pour Alice (Bitcoin)
   - Génère script HTLC pour Bob (Ethereum)
   - Coordonne hash lock (même hash)
   - Coordonne time locks (T1 > T2)

4. **Secret Propagation Monitor**
   - Détecte quand secret révélé
   - Notifie contrepartie

### 4.2 Version Avancée (Optionnelle)

```
┌─────────────────────────────────────────────┐
│  KHU GATEWAY (Advanced Version)             │
├─────────────────────────────────────────────┤
│                                             │
│  ✅ Light Wallet KHU (optionnel)            │
│     Pour ceux qui veulent :                 │
│     • Staker en ZKHU                        │
│     • Faire du LP complexe                  │
│     • Tenir du KHU                          │
│                                             │
│  ✅ Simple swaps (sans KHU)                 │
│  ✅ Advanced features (avec KHU)            │
│  ✅ PIVX Core connection                    │
│  ✅ Governance (DOMC voting)                │
│                                             │
└─────────────────────────────────────────────┘
```

---

## 5. CODE NÉCESSAIRE PIVX CORE V6

### 5.1 Ce Qui Est Nécessaire (~900 lignes total)

#### Fichier 1 : khu_token.cpp (~500 lignes)

```cpp
/**
 * KHU Token Management
 * KHU = Token UTXO 1:1 avec PIV
 */

class CKHUToken {
public:
    /**
     * Mint KHU depuis PIV (1:1)
     * Lock PIV, créer KHU_T UTXO
     */
    bool MintKHU(CKeyID keyId, CAmount amount) {
        // 1. Vérifier balance PIV
        // 2. Lock PIV
        // 3. Créer UTXO KHU_T
        // 4. Update global state (C += amount, U += amount)
        // 5. Check invariants (C == U)
        return true;
    }

    /**
     * Burn KHU vers PIV (1:1)
     * Burn KHU_T, unlock PIV
     */
    bool BurnKHU(CKeyID keyId, CAmount amount) {
        // 1. Vérifier balance KHU_T
        // 2. Burn UTXO KHU_T
        // 3. Unlock PIV
        // 4. Update global state (C -= amount, U -= amount)
        // 5. Check invariants (C == U)
        return true;
    }

    /**
     * Transfer KHU_T (UTXO standard)
     * Fonctionne comme PIV transfer
     */
    bool TransferKHU(CKeyID from, CKeyID to, CAmount amount) {
        // Standard UTXO transaction
        // Aucune différence avec PIV transfer
        return true;
    }

    /**
     * Get KHU_T balance
     */
    CAmount GetKHUBalance(CKeyID keyId) const {
        // Somme des UTXO KHU_T pour cette adresse
        return balance;
    }

    /**
     * Check invariants (C == U)
     */
    bool CheckInvariants() const {
        return (total_collateral == total_supply);
    }
};
```

#### Fichier 2 : khu_rpc.cpp (~300 lignes)

```cpp
/**
 * RPC Commands pour KHU
 */

// Mint PIV → KHU
UniValue mintkhu(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 1)
        throw std::runtime_error(
            "mintkhu amount\n"
            "Lock PIV and mint KHU (1:1)\n"
        );

    CAmount amount = AmountFromValue(request.params[0]);

    bool success = pkhutokenstate->MintKHU(keyId, amount);

    UniValue result(UniValue::VOBJ);
    result.pushKV("success", success);
    result.pushKV("amount", ValueFromAmount(amount));
    result.pushKV("txid", txid.GetHex());

    return result;
}

// Burn KHU → PIV
UniValue burnkhu(const JSONRPCRequest& request) {
    // Similar to mintkhu
}

// Send KHU_T
UniValue sendkhu(const JSONRPCRequest& request) {
    // Standard UTXO transfer
    // Même pattern que sendtoaddress pour PIV
}

// Get KHU balance
UniValue getkhublance(const JSONRPCRequest& request) {
    CAmount balance = pkhutokenstate->GetKHUBalance(keyId);

    UniValue result(UniValue::VOBJ);
    result.pushKV("balance", ValueFromAmount(balance));

    return result;
}

// ═══════════════════════════════════════════════════════
// HTLC RPC Commands
// NOTE : Probablement DÉJÀ existants pour PIV !
//        Si oui, ils fonctionnent automatiquement pour KHU
// ═══════════════════════════════════════════════════════

// Create HTLC
UniValue createhtlc(const JSONRPCRequest& request) {
    if (request.fHelp || request.params.size() != 5)
        throw std::runtime_error(
            "createhtlc amount currency hash timeout receiver\n"
            "Create Hash Time Lock Contract\n"
            "Works for PIV and KHU (same code)\n"
        );

    CAmount amount = AmountFromValue(request.params[0]);
    std::string currency = request.params[1].get_str(); // "PIV" or "KHU"
    uint256 hash = ParseHashV(request.params[2], "hash");
    uint32_t timeout = request.params[3].get_int();
    CTxDestination receiver = DecodeDestination(request.params[4].get_str());

    // Créer script HTLC standard (Bitcoin-compatible)
    CScript htlcScript = CreateHTLCScript(hash, timeout, receiver);

    // Créer transaction
    CMutableTransaction tx;
    // ... add inputs/outputs ...

    // Broadcast
    uint256 txid = SendTransaction(tx);

    UniValue result(UniValue::VOBJ);
    result.pushKV("txid", txid.GetHex());
    result.pushKV("htlc_script", HexStr(htlcScript));

    return result;
}

// Redeem HTLC (reveal secret)
UniValue redeemhtlc(const JSONRPCRequest& request) {
    // Reveal secret to claim funds
}

// Refund HTLC (timeout)
UniValue refundhtlc(const JSONRPCRequest& request) {
    // Refund after timeout
}
```

#### Fichier 3 : Modifications Consensus (~100 lignes)

```cpp
/**
 * Modifications dans validation.cpp
 */

// Vérification transactions KHU
bool CheckKHUTransaction(const CTransaction& tx, CValidationState& state) {
    // Vérifier type transaction (MINT, BURN, TRANSFER)
    // Vérifier montants
    // Vérifier signatures
    return true;
}

// Vérification invariants dans blocs
bool CheckBlock(const CBlock& block, CValidationState& state) {
    // ... checks existants ...

    // Nouveau : vérifier invariant KHU
    if (NetworkUpgradeActive(height, Consensus::UPGRADE_KHU)) {
        if (!pkhutokenstate->CheckInvariants()) {
            return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation");
        }
    }

    return true;
}
```

### 5.2 Ce Qui N'Est PAS Nécessaire

```cpp
❌ AUCUN code HTLC spécial pour KHU

// Ces fonctions N'EXISTENT PAS car inutiles :
CreateKHUHTLC();        // ❌ Utiliser createhtlc() existant
RedeemKHUHTLC();        // ❌ Utiliser redeemhtlc() existant
RefundKHUHTLC();        // ❌ Utiliser refundhtlc() existant
ValidateKHUHTLC();      // ❌ Validation HTLC standard suffit
KHUHTLCScript();        // ❌ Script Bitcoin standard suffit

// Les HTLC KHU utilisent le MÊME code que les HTLC PIV !
```

---

## 6. SCRIPT HTLC DÉTAILLÉ

### 6.1 Script HTLC Canonique

**Template complet :**

```cpp
CScript CreateHTLCScript(uint256 hash, uint32_t timeout,
                         CPubKey receiver, CPubKey sender) {
    CScript script;

    script << OP_IF;

    // Branche CLAIM (receiver reveals secret)
    script << timeout << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    script << OP_HASH160 << ToByteVector(hash) << OP_EQUALVERIFY;
    script << ToByteVector(receiver) << OP_CHECKSIG;

    script << OP_ELSE;

    // Branche REFUND (sender reclaims after timeout)
    script << timeout + REFUND_DELAY << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    script << ToByteVector(sender) << OP_CHECKSIG;

    script << OP_ENDIF;

    return script;
}
```

### 6.2 Transaction CLAIM (Reveal Secret)

```cpp
/**
 * Receiver claims funds by revealing secret
 */
CMutableTransaction CreateClaimTransaction(
    uint256 htlc_txid,
    uint256 secret,
    CKey receiver_key
) {
    CMutableTransaction tx;

    // Input: HTLC output
    tx.vin.push_back(CTxIn(htlc_txid, 0));

    // Output: receiver address
    tx.vout.push_back(CTxOut(amount, receiver_script));

    // Witness: secret + signature + "1" (TRUE branch)
    CScript witness;
    witness << ToByteVector(secret);
    witness << ToByteVector(signature);
    witness << OP_TRUE;  // Select IF branch

    tx.vin[0].scriptWitness = witness;

    return tx;
}
```

### 6.3 Transaction REFUND (Timeout)

```cpp
/**
 * Sender reclaims funds after timeout
 */
CMutableTransaction CreateRefundTransaction(
    uint256 htlc_txid,
    CKey sender_key
) {
    CMutableTransaction tx;

    // Input: HTLC output
    tx.vin.push_back(CTxIn(htlc_txid, 0));

    // Output: sender address
    tx.vout.push_back(CTxOut(amount, sender_script));

    // Witness: signature + "0" (FALSE branch)
    CScript witness;
    witness << ToByteVector(signature);
    witness << OP_FALSE;  // Select ELSE branch

    tx.vin[0].scriptWitness = witness;

    // Set nLockTime to timeout (pour CLTV)
    tx.nLockTime = timeout + REFUND_DELAY + 1;

    return tx;
}
```

---

## 7. FLOW ATOMIC SWAP COMPLET

### 7.1 Setup Swap (Alice BTC → Bob USDC)

```
1. Alice veut échanger : 1 BTC → USDC
2. Bob veut échanger : 100,000 USDC → BTC

3. Gateway calcule (KHU invisible) :
   • 1 BTC = 50,000 KHU
   • 100,000 USDC = 50,000 KHU
   • → MATCH !

4. Gateway génère :
   • Hash lock : H = SHA256(secret)
   • Secret : 0x1234... (random 32 bytes)
   • Time locks : T_btc = 144 blocs, T_usdc = 72 blocs
```

### 7.2 Alice Crée HTLC Bitcoin

```bash
# Alice broadcast HTLC Bitcoin
bitcoin-cli createhtlc \
  amount=1.0 \
  hash=0x9a8b... \
  timeout=144 \
  receiver=<Bob_BTC_address>

# HTLC Bitcoin créé
# Txid : 0xabc123...
# Lock : 1 BTC
```

### 7.3 Bob Crée HTLC Ethereum (USDC)

```javascript
// Bob broadcast HTLC Ethereum (smart contract)
await htlcContract.create({
  amount: 100000 * 1e6,  // 100k USDC (6 decimals)
  hash: '0x9a8b...',
  timeout: 72 blocks,
  receiver: alice_eth_address
});

// HTLC USDC créé
// Contract address : 0xdef456...
// Lock : 100,000 USDC
```

### 7.4 Alice Révèle Secret (Claim USDC)

```javascript
// Alice broadcast claim transaction Ethereum
await htlcContract.claim({
  secret: '0x1234...'  // REVEAL !
});

// Secret maintenant PUBLIC on-chain !
// Alice reçoit : 100,000 USDC
```

### 7.5 Bob Utilise Secret (Claim BTC)

```bash
# Bob a vu le secret dans la tx Ethereum d'Alice
# Bob broadcast claim transaction Bitcoin
bitcoin-cli redeemhtlc \
  txid=0xabc123... \
  secret=0x1234...

# Bob reçoit : 1 BTC
```

### 7.6 Résultat Final

```
✅ Atomic swap complété !

Alice :
  Avant : 1 BTC
  Après : 100,000 USDC

Bob :
  Avant : 100,000 USDC
  Après : 1 BTC

KHU n'a JAMAIS été échangé !
KHU a juste servi au Gateway pour calculer équivalence.
```

---

## 8. INTERDICTIONS ABSOLUES

### 8.1 Code HTLC

```cpp
❌ JAMAIS créer code HTLC spécial pour KHU
❌ JAMAIS modifier opcodes Bitcoin standard
❌ JAMAIS utiliser autre algo que SHA256 pour hashlock
❌ JAMAIS utiliser timestamp pour timelock (block height uniquement)
❌ JAMAIS permettre HTLC Z→Z (ZKHU → ZKHU)
❌ JAMAIS brûler KHU via HTLC (transfer ownership only)
```

### 8.2 Gateway

```cpp
❌ JAMAIS mettre price oracle on-chain
❌ JAMAIS calculer khu_equivalent on-chain
❌ JAMAIS forcer user à avoir wallet KHU pour swap simple
❌ JAMAIS mélanger logique Gateway off-chain avec consensus on-chain
❌ JAMAIS utiliser format HTLC propriétaire (Bitcoin standard uniquement)
```

### 8.3 Transactions

```
❌ Z→Z HTLC (ZKHU → ZKHU)
❌ KHU→PIV direct via HTLC (pipeline mint/burn uniquement)
❌ HTLC sans timelock
❌ HTLC avec metadata on-chain
❌ HTLC cross-chain sans Gateway (coordination nécessaire)
```

---

## 9. CHECKLIST IMPLÉMENTATION PHASE 7

### 9.1 PIVX Core v6

- [ ] KHU Token implémenté (mint/burn/transfer)
- [ ] Invariants C==U vérifiés dans ConnectBlock
- [ ] RPC mintkhu/burnkhu/sendkhu implémentés
- [ ] RPC createhtlc/redeemhtlc/refundhtlc (vérifier si existant pour PIV)
- [ ] Script HTLC Bitcoin standard
- [ ] Validation HTLC standard (pas de code spécial KHU)
- [ ] Tests unitaires khu_token
- [ ] Tests unitaires khu_htlc
- [ ] Tests fonctionnels htlc.py

### 9.2 Gateway (Off-Chain)

- [ ] Watchers blockchain (BTC, ETH, ZEC, etc.)
- [ ] Matching engine
- [ ] Price discovery (KHU unité de compte)
- [ ] Order book
- [ ] HTLC instructions generator
- [ ] Secret propagation monitor
- [ ] Interface web (optionnel)
- [ ] Light wallet KHU (optionnel - version avancée)

### 9.3 Tests End-to-End

- [ ] Swap BTC → USDC (sans wallet KHU)
- [ ] Swap USDC → BTC (sans wallet KHU)
- [ ] Swap BTC → ZEC (sans wallet KHU)
- [ ] Timeout refund scenario (Alice ne révèle pas secret)
- [ ] Double-spend prevention (secret déjà utilisé)
- [ ] Gateway matching (Alice ↔ Bob)
- [ ] KHU unité de compte (calculs corrects)

---

## 10. RÉFÉRENCES

**Documents liés :**
- `01-blueprint-master-flow.md` — Section 1.3.6 (lois HTLC)
- `02-canonical-specification.md` — Section HTLC (spec)
- `03-architecture-overview.md` — Section 11 (HTLC cross-chain)
- `06-protocol-reference.md` — Section 17 (code C++ HTLC)

**Standards externes :**
- BIP 199 : Hashed Timelock Contracts
- BIP 65 : CHECKLOCKTIMEVERIFY
- Bitcoin Wiki : Atomic Swaps

---

## 11. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **HTLC KHU = HTLC Bitcoin standard** (aucun code spécial)
2. **KHU = unité de compte invisible** (user n'a pas besoin de wallet KHU)
3. **Gateway off-chain** (price discovery, matching, order book)
4. **PIVX Core on-chain** (HTLC execution, atomicity)
5. **Aucune logique off-chain on-chain** (pas de price oracle on-chain)

Toute modification doit :
1. Être approuvée par architecte
2. Être documentée dans DIVERGENCES.md
3. Mettre à jour version et date

**Statut :** ACTIF pour implémentation Phase 7 (après Phases 1-6 validées)

---

**FIN DU BLUEPRINT KHU HTLC / GATEWAY**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
