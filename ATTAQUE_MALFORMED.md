# 🔴 ATTAQUE TRANSACTIONS MALFORMÉES - BYPASS CHECKS

## VECTEUR #1: Tx MINT avec Payload Modifié

### Attaque: Minter Plus que Brûlé

**Transaction Normale:**
```
Input:  100 PIV (burned)
Payload: amount = 100 PIV
Output: 100 KHU_T
```

**Transaction Malformée:**
```
Input:  100 PIV (burned)
Payload: amount = 1000 PIV  // ⚠️ MENTIR sur le montant
Output: 1000 KHU_T
```

**Exploitation:**
```cpp
// ApplyKHUMint() ligne 138
const CAmount amount = payload.amount;  // Lit du payload (non vérifié!)

// Ligne 152-153
state.C += amount;  // C += 1000
state.U += amount;  // U += 1000
```

**Résultat:**
```
Brûlé: 100 PIV
Collateral augmenté: 1000 PIV  // ⚠️ ARBITRAGE!
```

**Question:** Y a-t-il une vérification entre PIV brûlé et payload.amount?

**Analyse Code:**

Cherchons dans khu_mint.cpp si on vérifie que le montant brûlé == payload.amount

---

## VECTEUR #2: Double MINT (Transaction avec 2 MINT)

### Attaque: Plusieurs OP_RETURN MINT

**Transaction:**
```
Input: 100 PIV
Output 0: OP_RETURN MINT 100
Output 1: OP_RETURN MINT 100
```

**Exploitation:**
```cpp
// ProcessKHUBlock() ligne 134
for (const auto& tx : block.vtx) {
    if (tx->nType == CTransaction::TxType::KHU_MINT) {
        ApplyKHUMint(*tx, ...);
    }
}
```

**Question:** Si tx contient 2 OP_RETURN MINT, est-elle traitée 2x?

**Réponse:** NON, car la boucle parcourt les transactions, pas les outputs.

Mais dans ApplyKHUMint(), si on extrait PLUSIEURS payloads?

---

## VECTEUR #3: MINT + REDEEM dans Même Transaction

### Attaque: Cancel Out

**Transaction:**
```
nType = KHU_MINT (priorisé)
OP_RETURN: MINT 100
OP_RETURN: REDEEM 100
```

**Exploitation:**
```cpp
// ProcessKHUBlock()
if (tx->nType == CTransaction::TxType::KHU_MINT) {
    ApplyKHUMint(*tx, ...);  // C += 100, U += 100
}
// ⚠️ Transaction déjà traitée, ne passera pas dans REDEEM
```

**Résultat:** Mintné 100 sans brûler

**Défense:** Une transaction a UN SEUL nType, donc soit MINT soit REDEEM, pas les deux ✅

---

## VECTEUR #4: Payload avec Montant Négatif

### Attaque: Underflow via Montant Négatif

**Transaction MINT:**
```
Payload: amount = -100 PIV
```

**Exploitation:**
```cpp
// ApplyKHUMint() ligne 152-153
state.C += (-100);  // C -= 100
state.U += (-100);  // U -= 100
```

**Résultat:** REDEEM déguisé en MINT!

**Défense:** Vérification dans ValidateKHUMint()?

Cherchons:
```cpp
// khu_mint.cpp
bool ValidateKHUMint(...) {
    // Vérifier amount > 0?
}
```

Sans voir le code complet, **POTENTIELLEMENT VULNÉRABLE** si pas de check amount > 0

---

## VECTEUR #5: Transaction sans Inputs (Coinbase Fake)

### Attaque: MINT sans Brûler PIV

**Transaction:**
```
Inputs: [] (vide, comme coinbase)
nType: KHU_MINT
Payload: amount = 1000000 PIV
```

**Exploitation:**
- Pas de PIV brûlé
- Mais claim minter 1M PIV

**Défense:**
```cpp
// ValidateKHUMint() ligne 38 (supposé)
if (tx.vin.empty()) {
    return state.Invalid(..., "khu-mint-no-inputs");
}
```

Si ce check existe → ✅ BLOQUÉ
Si pas de check → ❌ VULNÉRABLE

---

## VECTEUR #6: Replay Attack

### Attaque: Réutiliser Même Transaction MINT

**Scénario:**
1. Alice mint 100 PIV → KHU_T
2. Transaction diffusée, confirmée
3. Attaquant rejette transaction dans nouveau bloc

**Exploitation:**
- Première inclusion: C += 100, U += 100
- Deuxième inclusion: C += 100, U += 100
- Invariants OK mais double mint!

**Défense:** UTXO double-spend protection Bitcoin standard
- Inputs dépensés une seule fois ✅
- Deuxième tentative = double spend → REJETÉ ✅

---

## VECTEUR #7: Modification Payload en Mémoire

### Attaque: Race Condition sur Payload

**Code:**
```cpp
// ApplyKHUMint() ligne 133
CMintKHUPayload payload;
if (!GetMintKHUPayload(tx, payload)) {
    return error(...);
}

const CAmount amount = payload.amount;  // Ligne 138

// ... du code entre temps ...

state.C += amount;  // Ligne 152 - utilise amount
state.U += amount;  // Ligne 153
```

**Attaque:**
1. GetMintKHUPayload() extrait amount = 100
2. Entre ligne 138 et 152, attaquant modifie `payload.amount` en mémoire
3. `amount` variable locale → pas modifiable
4. **ATTAQUE ÉCHOUE** ✅

**Défense:** Variable locale `amount` (copie), pas de référence

---

## VECTEUR #8: Integer Truncation

### Attaque: Payload avec CAmount > 64 bits

**Payload Malformé:**
```
amount = 0xFFFFFFFFFFFFFFFF0000  // 80 bits
```

**Désérialisation:**
```cpp
payload.amount = (CAmount) raw_data;  // Truncation à 64 bits?
```

**Résultat:** Montant tronqué, incohérence

**Défense:** Dépend de la sérialisation CAmount
- Si protocole limite à 64 bits → ✅ SAFE
- Si pas de limite → ⚠️ POTENTIEL

---

## RÉSUMÉ ATTAQUES MALFORMED

| Vecteur | Bloquée | Défense | Sévérité |
|---------|---------|---------|----------|
| Montant payload > brûlé | ❓ UNKNOWN | À vérifier | **CRITIQUE** |
| Double OP_RETURN | ✅ OUI | Loop sur tx, pas outputs | LOW |
| MINT+REDEEM même tx | ✅ OUI | nType unique | LOW |
| Montant négatif | ❓ UNKNOWN | À vérifier ValidateKHUMint | **HAUTE** |
| Sans inputs | ❓ UNKNOWN | À vérifier ValidateKHUMint | **CRITIQUE** |
| Replay attack | ✅ OUI | UTXO protection | LOW |
| Modification mémoire | ✅ OUI | Variable locale | LOW |
| Integer truncation | ❓ UNKNOWN | Dépend sérialisation | MOYENNE |

---

## ACTIONS REQUISES

**VÉRIFICATION CODE EFFECTUÉE:**

### Résultats Audit CheckKHUMint() (khu_mint.cpp:51-125)

✅ **LIGNE 67-70: Amount > 0 VÉRIFIÉ**
```cpp
if (payload.amount <= 0) {
    return state.Invalid(..., "khu-mint-invalid-amount");
}
```

✅ **LIGNE 73-88: Inputs PIV Suffisants VÉRIFIÉ**
```cpp
CAmount total_input = 0;
for (const auto& in : tx.vin) {
    const Coin& coin = view.AccessCoin(in.prevout);
    total_input += coin.out.nValue;
}
if (total_input < payload.amount) {
    return state.Invalid(..., "khu-mint-insufficient-funds");
}
```

✅ **LIGNE 103-108: PIV Brûlé == Payload VÉRIFIÉ**
```cpp
if (tx.vout[0].nValue != payload.amount) {
    return state.Invalid(..., "khu-mint-burn-mismatch");
}
```

✅ **LIGNE 111-116: KHU_T Amount == Payload VÉRIFIÉ**
```cpp
if (tx.vout[1].nValue != payload.amount) {
    return state.Invalid(..., "khu-mint-amount-mismatch");
}
```

### Résultats Audit CheckKHURedeem() (khu_redeem.cpp:51-121)

✅ **LIGNE 67-70: Amount > 0 VÉRIFIÉ**
```cpp
if (payload.amount <= 0) {
    return state.Invalid(..., "khu-redeem-invalid-amount");
}
```

✅ **LIGNE 73-95: Inputs KHU_T Suffisants VÉRIFIÉ**
```cpp
CAmount total_input = 0;
for (const auto& in : tx.vin) {
    CKHUUTXO khuCoin;
    if (!GetKHUCoin(view, in.prevout, khuCoin)) {
        return state.Invalid(..., "khu-redeem-missing-input");
    }
    total_input += khuCoin.amount;
}
if (total_input < payload.amount) {
    return state.Invalid(..., "khu-redeem-insufficient-khu");
}
```

✅ **LIGNE 82-85: Cannot Redeem Staked KHU VÉRIFIÉ**
```cpp
if (khuCoin.fStaked) {
    return state.Invalid(..., "khu-redeem-staked-khu");
}
```

✅ **LIGNE 104-109: PIV Output == Payload VÉRIFIÉ**
```cpp
if (tx.vout[0].nValue != payload.amount) {
    return state.Invalid(..., "khu-redeem-amount-mismatch");
}
```

✅ **LIGNE 143-146 (ApplyKHURedeem): Sufficient C/U VÉRIFIÉ**
```cpp
if (state.C < amount || state.U < amount) {
    return error("ApplyKHURedeem: Insufficient C/U");
}
```

---

## CONCLUSION ATTAQUES MALFORMED

**TOUTES les vérifications critiques sont PRÉSENTES et FONCTIONNELLES.**

**Sécurité:** ✅ EXCELLENTE - La plupart des attaques de transactions malformées sont BLOQUÉES.
