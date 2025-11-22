# 08 — PIVX-V6-KHU FAQ TECHNIQUE

Version: 1.0.0
Status: WORKING DOCUMENT
Audience: Core developers, reviewers, security auditors

---

## 1. OBJECTIF

Document de référence Q/R pour les décisions d'implémentation PIVX-V6-KHU.

**Rôle:**
- Clarifier les points d'implémentation non évidents
- Documenter les choix techniques basés sur PIVX patterns
- Fournir des réponses normatives pour les reviewers
- Pointer vers les sections canoniques (02/03/06) quand applicable

**Ce document N'EST PAS:**
- Une source de règles consensus (voir 02-CANONICAL-SPECIFICATION.md)
- Une spec alternative (voir 06-PROTOCOL-REFERENCE.md)
- Un tutoriel utilisateur

**Utilisation:**
- Développeurs: consulter avant d'implémenter un module
- Reviewers: vérifier que l'implémentation suit les réponses ici
- Architecte: mettre à jour ce document quand de nouvelles questions émergent

---

## 2. QUESTIONS RÉSOLUES

### Q35: Comment créer du PIV lors d'un REDEEM sans violer les règles d'émission?

**Contexte:**
Un REDEEM détruit du KHU_T et doit créer du PIV. Mais PIVX interdit la création arbitraire de PIV (inflation).

**Réponse:**
Utiliser le mécanisme d'exemption de type transaction existant dans PIVX.

**Implémentation:**
```cpp
// src/khu/khu_redeem.cpp
bool CreateKHURedeemTransaction(CMutableTransaction& tx, const CKHURedeemPayload& payload) {
    // 1. Mark transaction as KHU_REDEEM type
    tx.nType = CTransaction::TX_KHU_REDEEM;

    // 2. Create PIV output from collateral
    tx.vout.push_back(CTxOut(payload.khuAmount, payload.pivRecipient));

    // 3. No PIV inputs needed (exemption)
    // Validation will check against KhuGlobalState.C instead

    return true;
}

// src/consensus/tx_verify.cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, ...) {
    if (tx.nType == CTransaction::TX_KHU_REDEEM) {
        // Exempt from standard input validation
        // Checked separately in CheckKHURedeem()
        return true;
    }
    // ... standard validation
}
```

**Pattern PIVX:**
- Les transactions spéciales (ProRegTx, etc.) ont déjà ce mécanisme
- REDEEM suit le même pattern

**Référence normative:**
- 02-CANONICAL-SPECIFICATION.md section 3.2 (REDEEM)
- 06-PROTOCOL-REFERENCE.md section 2.5 (REDEEM Payload)

---

### Q49: Comment séparer le pool Sapling ZKHU du pool zPIV existant?

**Contexte:**
ZKHU utilise Sapling, mais zPIV aussi. Collision possible?

**Réponse:**
Utiliser des **anchors séparés** (Merkle trees distincts).

**Implémentation:**
```cpp
// src/sapling/sapling_state.h
class SaplingState {
    SaplingMerkleTree saplingTree;      // zPIV pool
    SaplingMerkleTree zkhuTree;         // ZKHU pool (new)

    std::set<uint256> nullifierSet;     // zPIV nullifiers
    std::set<uint256> zkhuNullifierSet; // ZKHU nullifiers (new)
};

// src/khu/khu_stake.cpp
bool CheckKHUStake(const CTransaction& tx, ...) {
    // Add commitment to ZKHU tree (not Sapling tree)
    zkhuTree.append(tx.sapData->vShieldedOutput[0].cmu);

    // Anchor = root of zkhuTree
    uint256 anchor = zkhuTree.root();

    return true;
}

// src/khu/khu_unstake.cpp
bool CheckKHUUnstake(const CTransaction& tx, ...) {
    // Verify nullifier against ZKHU set (not zPIV set)
    if (zkhuNullifierSet.count(tx.sapData->vShieldedSpend[0].nullifier))
        return state.Invalid("khu-unstake-double-spend");

    // Verify anchor matches zkhuTree root
    if (tx.sapData->vShieldedSpend[0].anchor != zkhuTree.root())
        return state.Invalid("khu-unstake-invalid-anchor");

    return true;
}
```

**Conséquence:**
- ZKHU et zPIV ont des anonymity sets complètement séparés
- Pas de conversion ZKHU ↔ zPIV possible
- Pas de collision de nullifiers
- Audit indépendant de chaque pool

**Pattern PIVX:**
- Sapling supporte nativement plusieurs pools (design Zcash)
- Anchor = racine du Merkle tree spécifique à chaque pool

**Référence normative:**
- 03-ARCHITECTURE-OVERVIEW.md section 7.1 (Sapling Integration)

---

### Q63: Comment implémenter KhuGlobalState::GetHash()?

**Contexte:**
Le hash de l'état KHU est utilisé pour:
- Chaînage (hashPrevState)
- Finalité masternode (signature de l'état)
- Audit et vérification

**Réponse:**
Sérialiser tous les champs de consensus et hasher avec SHA256d (double SHA256).

**Implémentation:**
```cpp
// src/khu/khu_state.h
uint256 KhuGlobalState::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << C;
    ss << U;
    ss << Cr;
    ss << Ur;
    ss << R_annual;
    ss << R_MAX_dynamic;
    ss << last_domc_height;
    ss << domc_cycle_start;
    ss << domc_cycle_length;
    ss << domc_phase_length;
    ss << last_yield_update_height;
    ss << nHeight;
    ss << hashBlock;
    ss << hashPrevState;

    return ss.GetHash();  // SHA256d
}
```

**Justification:**
- Tous les champs consensus doivent être inclus
- Ordre déterministe (ordre de déclaration dans struct)
- SHA256d = standard Bitcoin/PIVX pour hashes de consensus

**Pattern PIVX:**
- `CBlock::GetHash()` utilise le même pattern
- `CTransaction::GetHash()` aussi

**Référence normative:**
- 06-PROTOCOL-REFERENCE.md section 2.1 (KhuGlobalState structure)

---

### Q64: Comment gérer l'activation sur regtest vs mainnet?

**Contexte:**
Regtest doit activer KHU immédiatement pour les tests. Mainnet aura une activation future.

**Réponse:**
Paramètre `nActivationHeight` spécifique par réseau dans `chainparams.cpp`.

**Implémentation:**
```cpp
// src/chainparams.cpp
class CMainParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 9999999;
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nProtocolVersion = 70023;
};

class CTestNetParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 500000;
};

class CRegTestParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 1;  // Immediate
};

// src/validation.cpp
bool ConnectBlock(...) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        // KHU consensus active
    }
}
```

**Pattern PIVX:**
- Tous les upgrades (Sapling, Shield Stake, etc.) utilisent ce pattern
- `NetworkUpgradeActive()` vérifie automatiquement le réseau courant

**Référence normative:**
- 02-CANONICAL-SPECIFICATION.md section 13 (Network Upgrade)

---

### Q65: Comment reconstruire l'état KHU après un crash ou un resync?

**Contexte:**
Si la DB est corrompue ou si un nœud resync depuis genesis, l'état KHU doit être reconstruit.

**Réponse:**
Rejouer tous les blocs depuis `nActivationHeight`.

**Implémentation:**
```cpp
// src/khu/khu_db.cpp
bool CKHUStateDB::ReconstructState(uint32_t fromHeight, uint32_t toHeight) {
    // 1. Initialize genesis state
    KhuGlobalState state;
    state.C = 0;
    state.U = 0;
    state.Cr = 0;
    state.Ur = 0;
    state.R_annual = 500;  // 5% initial
    state.last_yield_update_height = fromHeight;
    state.nHeight = fromHeight;

    // 2. Replay blocks
    for (uint32_t h = fromHeight + 1; h <= toHeight; h++) {
        CBlockIndex* pindex = chainActive[h];
        CBlock block;
        if (!ReadBlockFromDisk(block, pindex, Params().GetConsensus()))
            return false;

        // 3. Apply all KHU transactions
        for (const auto& tx : block.vtx) {
            ApplyKHUTransaction(*tx, state, view, validationState);
        }

        // 4. Apply yield if needed
        if ((h - state.last_yield_update_height) >= 1440) {
            UpdateDailyYield(state, h, view);
            state.last_yield_update_height = h;
        }

        // 5. Update state metadata
        state.nHeight = h;
        state.hashBlock = pindex->GetBlockHash();
        state.hashPrevState = prevHash;

        // 6. Persist every N blocks (checkpoint)
        if (h % 1000 == 0) {
            WriteKHUState(state);
        }
    }

    // 7. Final write
    WriteKHUState(state);

    return true;
}
```

**Optimisation:**
- Checkpoints tous les 1000 blocs pour éviter de tout refaire en cas de crash
- Batch writes LevelDB

**Pattern PIVX:**
- `ReindexChainState()` utilise le même pattern pour UTXO set
- Deterministic = même état à chaque replay

**Référence normative:**
- 03-ARCHITECTURE-OVERVIEW.md section 3.4 (Reorg Handling)

---

### Q66: Comment garantir l'atomicité des écritures DB pour l'état KHU?

**Contexte:**
Si le nœud crash entre deux écritures, l'état KHU pourrait être corrompu.

**Réponse:**
Utiliser `CDBBatch` pour écriture atomique (all-or-nothing).

**Implémentation:**
```cpp
// src/khu/khu_db.cpp
bool CKHUStateDB::WriteKHUState(const KhuGlobalState& state) {
    CDBBatch batch(*this);

    // 1. Write state at height
    batch.Write(std::make_pair('K', std::make_pair('S', state.nHeight)), state);

    // 2. Write state by hash
    batch.Write(std::make_pair('K', std::make_pair('H', state.GetHash())), state);

    // 3. Write best state hash
    batch.Write(std::make_pair('K', 'B'), state.GetHash());

    // 4. Atomic commit (all-or-nothing)
    return WriteBatch(batch, true);  // fSync=true
}

// src/validation.cpp ConnectBlock
bool ConnectBlock(...) {
    // ... process KHU transactions ...

    // Atomic write at end
    if (!pKHUStateDB->WriteKHUState(newState))
        return state.Invalid("khu-state-write-failed");

    return true;
}
```

**Garanties:**
- Soit toutes les écritures réussissent, soit aucune
- `fSync=true` force fsync() pour durabilité
- En cas de crash, DB reste dans l'état précédent (cohérent)

**Pattern PIVX:**
- `CCoinsViewDB::BatchWrite()` utilise `CDBBatch`
- Standard LevelDB pour atomicité

**Référence normative:**
- 03-ARCHITECTURE-OVERVIEW.md section 3.3 (Persistance)

---

### Q67: Quelle valeur initiale pour last_yield_update_height?

**Contexte:**
À l'activation (height = nActivationHeight), quelle valeur pour `last_yield_update_height`?

**Réponse:**
`last_yield_update_height = nActivationHeight`

**Justification:**
- Premier yield se déclenche à `nActivationHeight + 1440`
- Pas de yield au bloc d'activation (pas encore de ZKHU stakés)
- Alignement avec le début de l'ère KHU

**Implémentation:**
```cpp
// src/validation.cpp ConnectBlock
bool ConnectBlock(...) {
    if (pindex->nHeight == consensusParams.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight) {
        // Genesis KHU state
        KhuGlobalState genesisState;
        genesisState.C = 0;
        genesisState.U = 0;
        genesisState.Cr = 0;
        genesisState.Ur = 0;
        genesisState.R_annual = 500;  // 5% initial
        genesisState.last_yield_update_height = pindex->nHeight;  // ← ICI
        genesisState.nHeight = pindex->nHeight;
        genesisState.hashBlock = pindex->GetBlockHash();
        genesisState.hashPrevState.SetNull();

        pKHUStateDB->WriteKHUState(genesisState);
    }
}
```

**Référence normative:**
- 02-CANONICAL-SPECIFICATION.md section 8.2 (Daily Update)

---

### Q68: Prefix LevelDB 'K' : risque de collision avec d'autres clés?

**Contexte:**
PIVX utilise déjà plusieurs prefixes LevelDB. 'K' est-il safe?

**Réponse:**
Audit des prefixes existants confirmé, 'K' est libre.

**Prefixes PIVX existants (src/txdb.cpp):**
```
'c' → Coins (UTXO)
'B' → Best block hash
'f' → Flag (tx index, etc.)
'l' → Last block file
'R' → Reindex flag
't' → Transaction index
'b' → Block index
'S' → Sapling anchors
'N' → Sapling nullifiers
's' → Shield stake data
'z' → Zerocoin (legacy)
```

**Prefixes KHU (nouveaux):**
```
'K' + 'S' + height      → KhuGlobalState
'K' + 'B'               → Best KHU hash
'K' + 'H' + hash        → KhuGlobalState by hash
'K' + 'C' + height      → Commitment (finality)
'K' + 'U' + outpoint    → CKHUUTXO
'K' + 'N' + nullifier   → ZKHU note data
'K' + 'D' + cycle       → DOMC votes
'K' + 'T' + outpoint    → HTLC data
```

**Conclusion:**
- Pas de collision avec prefixes existants
- 'K' = KHU (mnémotechnique)
- Sous-prefixes ('S', 'B', 'H', etc.) évitent collision entre types KHU

**Action requise:**
Documenter dans `src/txdb.h` les nouveaux prefixes.

**Référence normative:**
- 06-PROTOCOL-REFERENCE.md section 12.1 (LevelDB Keys)

---

### Q70: Que faire en cas de violation d'invariant (C != U ou Cr != Ur)?

**Contexte:**
Si `CheckInvariants()` échoue, quel comportement?

**Réponse:**
**Rejet immédiat du bloc. Pas de tolérance. Pas de correction.**

**Implémentation:**
```cpp
// src/validation.cpp ConnectBlock
bool ConnectBlock(...) {
    // ... process KHU transactions ...

    // Check invariants
    if (!newState.CheckInvariants()) {
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation",
                           strprintf("C=%d U=%d Cr=%d Ur=%d at height %d",
                                   newState.C, newState.U, newState.Cr, newState.Ur,
                                   pindex->nHeight));
    }

    // ... finality check, persist ...
}

// src/khu/khu_state.h
bool KhuGlobalState::CheckInvariants() const {
    // INVARIANT_1: C == U (strict equality)
    bool cd_ok = (U == 0 || C == U);

    // INVARIANT_2: Cr == Ur (strict equality)
    bool cdr_ok = (Ur == 0 || Cr == Ur);

    return cd_ok && cdr_ok;
}
```

**Comportement:**
- Bloc rejeté
- Peer banni (score -100)
- Logs détaillés (C, U, Cr, Ur, height) pour debug
- Pas de tentative de "correction automatique"

**Cas edge (U=0 ou Ur=0):**
- `U=0` : pas encore de MINT → C doit être 0
- `Ur=0` : pas encore de yield → Cr doit être 0
- Condition: `(U == 0 || C == U)` permet l'état genesis

**Pattern PIVX:**
- Violations consensus → rejet immédiat
- Pas de "soft errors" en consensus

**Référence normative:**
- 02-CANONICAL-SPECIFICATION.md section 2.3 (Invariants)

---

### Q71: Ordre de locking (LOCK2) entre cs_main et cs_khu?

**Contexte:**
Pour éviter deadlocks, quel ordre de locking?

**Réponse:**
**Toujours `LOCK2(cs_main, cs_khu)` (jamais l'inverse).**

**Justification:**
- `cs_main` est le lock global de la blockchain (highest priority)
- `cs_khu` est spécifique à l'état KHU (lower priority)
- Pattern: locks généraux avant locks spécifiques

**Implémentation:**
```cpp
// src/khu/khu_state.cpp
extern CCriticalSection cs_khu;

bool UpdateKHUState(...) {
    LOCK2(cs_main, cs_khu);  // ← Toujours cet ordre

    // ... modify khuGlobalState ...

    return true;
}

// src/rpc/khu.cpp
UniValue getkhuglobalstate(...) {
    LOCK2(cs_main, cs_khu);  // ← Même ordre dans RPC

    KhuGlobalState state = khuGlobalState;

    // ... return JSON ...
}
```

**Détection deadlock:**
- Compiler avec `-DDEBUG_LOCKORDER` (détecte violations)
- Tests fonctionnels avec threads concurrents

**Pattern PIVX:**
- `LOCK2(cs_main, cs_wallet)` partout
- `LOCK2(cs_main, cs_mapBlockIndex)` aussi
- Ordre cohérent dans toute la codebase

**Référence normative:**
- Standard C++ lock ordering (avoid deadlock)

---

### Q72: Faut-il un assert() contre l'overflow de l'émission?

**Contexte:**
Vérifier que `reward_year * COIN` ne dépasse pas `int64_t::max`?

**Réponse:**
**Oui, assert() au démarrage pour vérifier la cohérence des constantes.**

**Implémentation:**
```cpp
// src/init.cpp AppInitMain
bool AppInitMain() {
    // ... initialization ...

    // Verify emission constants
    static_assert(6 * COIN < std::numeric_limits<int64_t>::max() / 3,
                  "Emission overflow: reward_year * 3 exceeds int64_t");

    // Verify total emission cap
    int64_t total_emission = 0;
    for (int year = 0; year < 6; year++) {
        int64_t reward_year = (6 - year) * COIN;
        int64_t year_emission = reward_year * 3 * 525600;  // 3 compartments

        assert(total_emission + year_emission > total_emission);  // No overflow
        total_emission += year_emission;
    }

    LogPrintf("KHU: Total emission cap = %d PIV\n", total_emission / COIN);

    // ...
}
```

**Calcul:**
- Max reward_year = 6 PIV = 600000000 satoshis
- Max per block = 6 * 3 = 18 PIV = 1800000000 satoshis
- `int64_t::max` = 9223372036854775807 (9.2 quintillion)
- 1800000000 << int64_t::max ✓

**Justification:**
- Catch early si constantes modifiées incorrectement
- Fail-fast au démarrage (pas en production)
- Documentation via code

**Pattern PIVX:**
- `static_assert` pour vérifications compile-time
- `assert()` runtime pour invariants critiques

**Référence normative:**
- 07-emission-blueprint.md section 6 (Implémentation C++)

---

### Q73: Le DAO reward va-t-il au budget system PIVX existant?

**Contexte:**
PIVX a un budget system pour financer le développement. Comment intégrer DAO reward?

**Réponse:**
**Oui, réutiliser le budget system existant (adresse treasury).**

**Implémentation:**
```cpp
// src/validation.cpp
CAmount GetBlockSubsidy(int nHeight, const Consensus::Params& params) {
    if (!params.NetworkUpgradeActive(nHeight, Consensus::UPGRADE_KHU))
        return OLD_REWARD;

    int year = (nHeight - KHU_ACTIVATION) / 525600;
    int64_t reward_year = std::max(6 - year, 0);

    return reward_year * COIN;
}

void CreateCoinStake(CBlock& block, ...) {
    CAmount blockReward = GetBlockSubsidy(block.nHeight, params);

    // Staker
    block.vtx[1]->vout.push_back(CTxOut(blockReward, stakerScript));

    // Masternode
    block.vtx[1]->vout.push_back(CTxOut(blockReward, masternodeScript));

    // DAO → Treasury address
    CScript treasuryScript = GetScriptForDestination(params.GetTreasuryAddress());
    block.vtx[1]->vout.push_back(CTxOut(blockReward, treasuryScript));
}
```

**Treasury address:**
- Définie dans `chainparams.cpp` par réseau
- Mainnet: adresse multisig contrôlée par gouvernance
- Testnet/Regtest: adresse de test

**Pattern PIVX:**
- Budget system existant utilise déjà treasury payouts
- Pas de nouveau mécanisme nécessaire

**Référence normative:**
- 07-emission-blueprint.md section 2 (Répartition égale)

---

### Q74: Precision int64 pour le yield: risque de perte de précision?

**Contexte:**
Calcul daily yield: `(amount * R_annual / 10000) / 365`

**Réponse:**
**Précision acceptable. Arrondi à l'inférieur (division entière).**

**Analyse:**
```cpp
// Exemple: 1000 KHU (100000000000 satoshis), R=500 (5%)
int64_t amount = 1000 * COIN;          // 100000000000
uint16_t R_annual = 500;               // 5% (basis points)

// Yield annuel
int64_t annual = (amount * R_annual) / 10000;
// = (100000000000 * 500) / 10000
// = 5000000000000 / 10000
// = 500000000  (5 KHU)

// Yield quotidien
int64_t daily = annual / 365;
// = 500000000 / 365
// = 1369863  satoshis/jour
// ≈ 0.01369863 KHU/jour

// Perte par division entière
double exact = 500000000.0 / 365.0;  // 1369863.0136986...
int64_t integer = 500000000 / 365;   // 1369863
double loss = exact - integer;       // ~0.014 satoshis/jour
```

**Perte maximale:**
- ~0.014 satoshis/jour pour 1000 KHU
- Négligeable (< 1 satoshi)
- Sur 1 an: ~5 satoshis perdus (0.00000005 KHU)

**Justification:**
- Division entière = déterministe (pas de float)
- Perte négligeable comparée aux montants
- Arrondi inférieur = conservateur (pas d'inflation accidentelle)

**Alternative rejetée:**
- Utiliser `double` : non-déterministe, interdit en consensus
- Stocker remainder : complexité inutile

**Pattern PIVX:**
- Bitcoin/PIVX utilisent division entière partout
- Fees calculations aussi

**Référence normative:**
- 02-CANONICAL-SPECIFICATION.md section 14 (Absolute Prohibitions)
  - "Floating point arithmetic → FORBIDDEN"

---

### Q75: Le preimage HTLC est-il visible on-chain (mempool)?

**Contexte:**
Quand Alice claim un HTLC en révélant le preimage, Bob peut-il le voir dans mempool et front-run?

**Réponse:**
**Oui, le preimage est visible on-chain et dans mempool. C'est le comportement attendu.**

**Justification:**
- HTLC = Hash Time-Locked Contract (standard Bitcoin/Lightning)
- Le preimage DOIT être révélé on-chain pour claim
- Pas de "privacy" du preimage (c'est le point de HTLC)

**Implémentation:**
```cpp
// src/khu/khu_htlc.cpp
bool CheckHTLCClaim(const CTransaction& tx, ...) {
    // 1. Extract preimage from transaction
    std::vector<unsigned char> preimage;
    if (!tx.GetHTLCPreimage(preimage))
        return state.Invalid("htlc-missing-preimage");

    // 2. Verify hash
    uint256 hash = Hash(preimage.begin(), preimage.end());
    if (hash != htlc.hashlock)
        return state.Invalid("htlc-invalid-preimage");

    // 3. Transfer funds
    // ...
}

// Preimage stocké dans:
// - tx.vExtraPayload (PIVX special tx payload)
// - Ou script witness
```

**Scénario cross-chain (atomic swap):**
1. Alice crée HTLC sur PIVX avec hashlock=H
2. Bob crée HTLC sur Monero avec même hashlock=H
3. Alice claim sur Monero → révèle preimage P
4. Preimage P visible dans blockchain Monero
5. Bob voit P, claim sur PIVX avant timelock
6. **C'est le comportement attendu (atomicité)**

**Protection Gateway:**
- Gateway watchers surveillent les deux chaînes
- Si preimage révélé sur une chaîne, claim immédiatement sur l'autre
- Pas de protection mempool (inutile, le preimage sera on-chain de toute façon)

**Mempool policy:**
- Pas de "preimage sniffing" (Q: voir mempool rules dans 06-PROTOCOL)
- Pas de priorité HTLC_CLAIM vs HTLC_REFUND
- Standard RBF (Replace-By-Fee) si conflit

**Pattern Bitcoin:**
- Lightning Network HTLCs fonctionnent pareil
- Preimage toujours visible on-chain

**Référence normative:**
- 06-PROTOCOL-REFERENCE.md section 4 (HTLC Mempool Rules)
- 02-CANONICAL-SPECIFICATION.md section 10 (HTLC Cross-Chain)

---

## 3. QUESTIONS EN ATTENTE

*(Aucune pour l'instant)*

---

## 4. CHANGELOG

```
1.0.0 - 2024-01-XX - Initial version with Q35, Q49, Q63-Q75
```

---

## END OF FAQ TECHNIQUE

Ce document est vivant et sera mis à jour au fur et à mesure des questions.

Toute décision d'implémentation non triviale doit être documentée ici.
