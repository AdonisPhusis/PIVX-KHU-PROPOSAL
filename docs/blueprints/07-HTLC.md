# 07 — CONDITIONAL SCRIPTS (Phase 7)

**Date:** 2025-11-25
**Version:** 2.0
**Status:** MINIMAL SPEC
**Priorité:** NON-BLOQUANT

---

## OBJECTIF

Exposer des **scripts conditionnels standards** (P2SH) via helpers minimalistes.

---

## 1. SPECIFICATION

### 1.1 Script Standard (BIP-199 compatible)

```
OP_IF
    OP_SIZE <32> OP_EQUALVERIFY
    OP_SHA256 <hashlock> OP_EQUALVERIFY
    OP_DUP OP_HASH160 <pubkeyhash_A>
OP_ELSE
    <timelock> OP_CHECKLOCKTIMEVERIFY OP_DROP
    OP_DUP OP_HASH160 <pubkeyhash_B>
OP_ENDIF
OP_EQUALVERIFY
OP_CHECKSIG
```

### 1.2 Paramètres

| Param | Type | Description |
|-------|------|-------------|
| `hashlock` | uint256 | SHA256(secret), 32 bytes |
| `timelock` | uint32 | Block height (absolu) |
| `pubkeyhash_A` | CKeyID | Destinataire si secret révélé |
| `pubkeyhash_B` | CKeyID | Destinataire si timeout |

### 1.3 Dépense

**Avec secret (branche A):**
```
<signature> <pubkey> <secret_32bytes> OP_TRUE <redeemScript>
```

**Après timeout (branche B):**
```
<signature> <pubkey> OP_FALSE <redeemScript>
nLockTime >= timelock
nSequence < 0xFFFFFFFF
```

---

## 2. FICHIERS

```
src/script/conditional.h     # ~40 lignes
src/script/conditional.cpp   # ~80 lignes
src/rpc/conditional.cpp      # ~100 lignes
```

**Total: ~220 lignes**

---

## 3. INTERFACE (src/script/conditional.h)

```cpp
#ifndef PIVX_SCRIPT_CONDITIONAL_H
#define PIVX_SCRIPT_CONDITIONAL_H

#include "script/script.h"
#include "pubkey.h"
#include "uint256.h"

/**
 * Conditional Script (hash + timelock)
 * Standard P2SH, compatible BTC/LTC/DASH/ZEC
 */

// Create conditional script
CScript CreateConditionalScript(
    const uint256& hashlock,
    uint32_t timelock,
    const CKeyID& destA,
    const CKeyID& destB
);

// Decode conditional script
bool DecodeConditionalScript(
    const CScript& script,
    uint256& hashlock,
    uint32_t& timelock,
    CKeyID& destA,
    CKeyID& destB
);

// Check if script is conditional
bool IsConditionalScript(const CScript& script);

// Create spend scripts
CScript CreateConditionalSpendA(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const std::vector<unsigned char>& secret,
    const CScript& redeemScript
);

CScript CreateConditionalSpendB(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const CScript& redeemScript
);

#endif // PIVX_SCRIPT_CONDITIONAL_H
```

---

## 4. IMPLEMENTATION (src/script/conditional.cpp)

```cpp
#include "script/conditional.h"
#include "hash.h"

CScript CreateConditionalScript(
    const uint256& hashlock,
    uint32_t timelock,
    const CKeyID& destA,
    const CKeyID& destB)
{
    CScript script;

    // Branch A: secret + signature
    script << OP_IF;
    script << OP_SIZE << 32 << OP_EQUALVERIFY;
    script << OP_SHA256;
    script << ToByteVector(hashlock);
    script << OP_EQUALVERIFY;
    script << OP_DUP << OP_HASH160;
    script << ToByteVector(destA);

    // Branch B: timeout + signature
    script << OP_ELSE;
    script << CScriptNum(timelock);
    script << OP_CHECKLOCKTIMEVERIFY << OP_DROP;
    script << OP_DUP << OP_HASH160;
    script << ToByteVector(destB);

    script << OP_ENDIF;
    script << OP_EQUALVERIFY;
    script << OP_CHECKSIG;

    return script;
}

bool IsConditionalScript(const CScript& script)
{
    uint256 h; uint32_t t; CKeyID a, b;
    return DecodeConditionalScript(script, h, t, a, b);
}

bool DecodeConditionalScript(
    const CScript& script,
    uint256& hashlock,
    uint32_t& timelock,
    CKeyID& destA,
    CKeyID& destB)
{
    // Minimal parsing: check structure and extract params
    std::vector<unsigned char> data;
    opcodetype opcode;
    CScript::const_iterator it = script.begin();

    // Expected: OP_IF OP_SIZE 32 OP_EQUALVERIFY OP_SHA256 <32bytes> ...
    if (!script.GetOp(it, opcode) || opcode != OP_IF) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_SIZE) return false;
    if (!script.GetOp(it, opcode, data) || CScriptNum(data, true).getint() != 32) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_SHA256) return false;
    if (!script.GetOp(it, opcode, data) || data.size() != 32) return false;
    memcpy(hashlock.begin(), data.data(), 32);

    if (!script.GetOp(it, opcode) || opcode != OP_EQUALVERIFY) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_DUP) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_HASH160) return false;
    if (!script.GetOp(it, opcode, data) || data.size() != 20) return false;
    destA = CKeyID(uint160(data));

    if (!script.GetOp(it, opcode) || opcode != OP_ELSE) return false;
    if (!script.GetOp(it, opcode, data)) return false;
    timelock = CScriptNum(data, true).getint();

    if (!script.GetOp(it, opcode) || opcode != OP_CHECKLOCKTIMEVERIFY) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_DROP) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_DUP) return false;
    if (!script.GetOp(it, opcode) || opcode != OP_HASH160) return false;
    if (!script.GetOp(it, opcode, data) || data.size() != 20) return false;
    destB = CKeyID(uint160(data));

    return true;
}

CScript CreateConditionalSpendA(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const std::vector<unsigned char>& secret,
    const CScript& redeemScript)
{
    CScript script;
    script << sig << ToByteVector(pubkey) << secret << OP_TRUE;
    script << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());
    return script;
}

CScript CreateConditionalSpendB(
    const std::vector<unsigned char>& sig,
    const CPubKey& pubkey,
    const CScript& redeemScript)
{
    CScript script;
    script << sig << ToByteVector(pubkey) << OP_FALSE;
    script << std::vector<unsigned char>(redeemScript.begin(), redeemScript.end());
    return script;
}
```

---

## 5. RPC (src/rpc/conditional.cpp)

```cpp
#include "rpc/server.h"
#include "script/conditional.h"
#include "script/standard.h"
#include "key_io.h"
#include "util/strencodings.h"

UniValue createconditionalsecret(const JSONRPCRequest& request)
{
    // Generate random 32-byte secret and its hash
    std::vector<unsigned char> secret(32);
    GetStrongRandBytes(secret.data(), 32);
    uint256 hashlock = Hash(secret.begin(), secret.end());

    UniValue result(UniValue::VOBJ);
    result.pushKV("secret", HexStr(secret));
    result.pushKV("hashlock", hashlock.GetHex());
    return result;
}

UniValue createconditional(const JSONRPCRequest& request)
{
    // createconditional <destA> <destB> <hashlock> <timelock>
    CTxDestination destA = DecodeDestination(request.params[0].get_str());
    CTxDestination destB = DecodeDestination(request.params[1].get_str());
    uint256 hashlock = uint256S(request.params[2].get_str());
    uint32_t timelock = request.params[3].get_int();

    CScript redeemScript = CreateConditionalScript(
        hashlock, timelock,
        boost::get<CKeyID>(destA),
        boost::get<CKeyID>(destB)
    );

    CScriptID scriptID(redeemScript);

    UniValue result(UniValue::VOBJ);
    result.pushKV("address", EncodeDestination(scriptID));
    result.pushKV("redeemScript", HexStr(redeemScript));
    result.pushKV("hashlock", hashlock.GetHex());
    result.pushKV("timelock", (int64_t)timelock);
    return result;
}

UniValue decodeconditional(const JSONRPCRequest& request)
{
    // decodeconditional <redeemScript>
    std::vector<unsigned char> data = ParseHex(request.params[0].get_str());
    CScript script(data.begin(), data.end());

    uint256 hashlock;
    uint32_t timelock;
    CKeyID destA, destB;

    if (!DecodeConditionalScript(script, hashlock, timelock, destA, destB)) {
        throw JSONRPCError(RPC_INVALID_PARAMETER, "Not a conditional script");
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("hashlock", hashlock.GetHex());
    result.pushKV("timelock", (int64_t)timelock);
    result.pushKV("destA", EncodeDestination(destA));
    result.pushKV("destB", EncodeDestination(destB));
    return result;
}

// Register RPCs
static const CRPCCommand commands[] = {
    {"conditional", "createconditionalsecret", &createconditionalsecret, {}},
    {"conditional", "createconditional", &createconditional, {"destA", "destB", "hashlock", "timelock"}},
    {"conditional", "decodeconditional", &decodeconditional, {"redeemScript"}},
};

void RegisterConditionalRPCCommands(CRPCTable& t)
{
    for (const auto& c : commands)
        t.appendCommand(c.name, &c);
}
```

---

## 6. USAGE

```bash
# 1. Générer secret + hashlock
pivx-cli createconditionalsecret
# {"secret": "a1b2c3...", "hashlock": "d4e5f6..."}

# 2. Créer adresse conditionnelle
pivx-cli createconditional "DAddrA..." "DAddrB..." "d4e5f6..." 150000
# {"address": "s...", "redeemScript": "6382..."}

# 3. Envoyer funds (transaction normale)
pivx-cli sendtoaddress "s..." 100

# 4. Décoder script
pivx-cli decodeconditional "6382..."
# {"hashlock": "...", "timelock": 150000, "destA": "...", "destB": "..."}
```

---

## 7. PLAN D'IMPLEMENTATION

| Étape | Fichier | Lignes | Durée |
|-------|---------|--------|-------|
| 1 | `script/conditional.h` | ~40 | 2h |
| 2 | `script/conditional.cpp` | ~80 | 4h |
| 3 | `rpc/conditional.cpp` | ~100 | 4h |
| 4 | Register RPC | ~5 | 30min |
| 5 | Test regtest | - | 2h |
| **TOTAL** | | **~220** | **~2 jours** |

---

## 8. ZERO IMPACT

```
✗ Pas de TxType
✗ Pas de consensus
✗ Pas de validation.cpp
✗ Pas de khu_validation.cpp
✗ Pas de LevelDB
✗ Pas de mempool
✗ Pas de P2P
✗ Pas de wallet changes
```

**100% helpers sur P2SH standard existant.**

---

## 9. COMPATIBILITE

| Chain | Compatible | Raison |
|-------|------------|--------|
| Bitcoin | ✅ | BIP-199 standard |
| Litecoin | ✅ | Same opcodes |
| Dash | ✅ | Same opcodes |
| Zcash (t-addr) | ✅ | Same opcodes |
| Bitcoin Cash | ✅ | Same opcodes |
| Dogecoin | ✅ | Same opcodes |

---

**FIN DU BLUEPRINT**

**Version:** 2.0
**Lignes de code:** ~220
**Durée:** ~2 jours
**Impact consensus:** ZERO
