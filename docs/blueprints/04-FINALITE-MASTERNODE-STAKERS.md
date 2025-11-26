# 04 — FINALITÉ MASTERNODE + STAKERS BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit le système de **finalité** assuré par les **masternodes** et **stakers** dans PIVX v6-KHU.

**La finalité garantit qu'un bloc validé par le réseau ne peut JAMAIS être reorganisé.**

**RÈGLE ABSOLUE : Finalité = Masternodes (BLS quorum) + Stakers (PoS) = Sécurité maximale.**

---

## 1. FINALITÉ : CONCEPT ET NÉCESSITÉ

### 1.1 Qu'est-ce que la Finalité ?

**Finalité** = Garantie qu'une transaction confirmée ne peut JAMAIS être annulée.

```
Sans finalité (Bitcoin) :
┌─────────────────────────────────────────────┐
│ Bloc 1000 confirmé                          │
│ ↓                                           │
│ Bloc 1001, 1002, 1003... ajoutés           │
│ ↓                                           │
│ ⚠️ REORG possible !                         │
│ Un mineur avec plus de hashpower peut      │
│ créer une chaîne alternative et invalider   │
│ blocs 1000-1003                             │
└─────────────────────────────────────────────┘

Avec finalité (PIVX v6-KHU) :
┌─────────────────────────────────────────────┐
│ Bloc 1000 confirmé                          │
│ ↓                                           │
│ Masternodes signent bloc 1000 (BLS)        │
│ ↓                                           │
│ Bloc 1000 FINALISÉ ✅                       │
│ ↓                                           │
│ ❌ REORG IMPOSSIBLE au-delà bloc 1000       │
│ Bloc 1000 est IMMUABLE                      │
└─────────────────────────────────────────────┘
```

### 1.2 Pourquoi la Finalité est Critique pour KHU ?

**KHU State = État global économique (C, U, Cr, Ur)**

Sans finalité, un reorg pourrait :
```
❌ Annuler des MINT/REDEEM (double-spend)
❌ Modifier l'état global KHU (C, U, Cr, Ur)
❌ Invalider des UNSTAKE (perte de rewards)
❌ Casser les invariants (C==U+Z, Cr==Ur)
❌ Détruire la confiance dans le système
```

Avec finalité :
```
✅ État KHU IMMUABLE après finalisation
✅ Transactions KHU définitives (12 blocs)
✅ Invariants garantis perpétuellement
✅ Sécurité économique maximale
✅ Confiance totale dans le système
```

### 1.3 Finalité PIVX v6-KHU : Masternodes + Stakers

```
┌─────────────────────────────────────────────┐
│ MASTERNODES (Quorum BLS)                    │
├─────────────────────────────────────────────┤
│ • Signent les blocs (BLS signatures)       │
│ • Forment quorums (LLMQ_400_60)            │
│ • Valident KHU state                        │
│ • Fournissent finalité (12 blocs)          │
│ • Récompense : reward_year PIV              │
└─────────────────────────────────────────────┘

┌─────────────────────────────────────────────┐
│ STAKERS (Proof-of-Stake)                    │
├─────────────────────────────────────────────┤
│ • Créent les blocs (PoS mining)            │
│ • Valident transactions                     │
│ • Maintiennent la blockchain                │
│ • Appliquent KHU state transitions          │
│ • Récompense : reward_year PIV + R% (KHU)  │
└─────────────────────────────────────────────┘

ENSEMBLE → Sécurité hybride (PoS + Quorum BLS)
```

---

## 2. MASTERNODES : QUORUMS BLS

### 2.1 Qu'est-ce qu'un Masternode ?

**Masternode** = Nœud PIVX avec collateral verrouillé (10,000 PIV) qui fournit des services au réseau.

**Services masternodes PIVX v6-KHU :**
- Signatures BLS pour finalité
- Votes DOMC (gouvernance R%)
- Validation KHU state
- Quorums LLMQ (Long-Living Masternode Quorums)
- Services réseau (mixing, instant send, etc.)

**Récompense :** reward_year PIV par bloc (égal à staker et DAO)

### 2.2 BLS Signatures (Boneh-Lynn-Shacham)

**BLS** = Schéma de signature permettant l'agrégation.

```
┌─────────────────────────────────────────────┐
│ SIGNATURES BLS — Propriétés                 │
├─────────────────────────────────────────────┤
│ ✅ Agrégation : N signatures → 1 signature  │
│ ✅ Taille fixe : 48 bytes (toujours)        │
│ ✅ Vérification : O(1) (rapide)             │
│ ✅ Déterministe : Même message = même sig   │
│ ✅ Sécurité : 128-bit (cryptographiquement) │
└─────────────────────────────────────────────┘

Exemple agrégation :
  MN1 signe bloc 1000 : sig1 (48 bytes)
  MN2 signe bloc 1000 : sig2 (48 bytes)
  ...
  MN400 signe bloc 1000 : sig400 (48 bytes)

  Agrégation : sig_total = sig1 + sig2 + ... + sig400
  Taille : 48 bytes (pas 400 × 48 !)

  Vérification : Verify(sig_total, pubkey_aggregated, bloc_1000)
  → ✅ ou ❌ en O(1)
```

**Avantage massif :** 400 signatures → 1 signature de 48 bytes.

### 2.3 Quorums LLMQ (Long-Living Masternode Quorums)

**LLMQ** = Groupe de masternodes sélectionnés de manière déterministe pour signer les blocs.

```cpp
// Configuration quorum PIVX v6-KHU
const Consensus::LLMQType LLMQ_400_60 = {
    .type = 1,
    .name = "llmq_400_60",
    .size = 400,              // 400 masternodes dans le quorum
    .minSize = 300,           // Minimum 300 MN actifs
    .threshold = 240,         // 60% doivent signer (240/400)
    .dkgInterval = 24,        // Nouveau quorum tous les 24 blocs
    .dkgPhaseBlocks = 2,      // DKG en 2 blocs
    .signingActiveQuorumCount = 24,  // 24 quorums actifs
};
```

**Propriétés quorum :**
- **Taille** : 400 masternodes
- **Threshold** : 60% (240/400) doivent signer
- **Rotation** : Nouveau quorum tous les 24 blocs
- **Sélection** : Déterministe (basé sur hash de bloc)

**Exemple sélection :**
```cpp
// Sélection déterministe quorum
uint256 quorum_hash = GetQuorumHash(nHeight);
std::vector<COutPoint> selected_mns = SelectQuorumMembers(quorum_hash, 400);

// Tous les nœuds arrivent au MÊME quorum
// Pas de coordination nécessaire !
```

### 2.4 Processus de Finalisation

```
┌──────────────────────────────────────────────────────┐
│ ÉTAPE 1 : Staker crée bloc N                        │
├──────────────────────────────────────────────────────┤
│ • Staker trouve PoS solution                         │
│ • Crée bloc N avec transactions                      │
│ • Calcule KHU state transition                       │
│ • Broadcast bloc N au réseau                         │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ ÉTAPE 2 : Masternodes du quorum signent bloc N      │
├──────────────────────────────────────────────────────┤
│ • Quorum actif : 400 MN sélectionnés                 │
│ • Chaque MN valide bloc N :                          │
│   - Transactions valides ?                           │
│   - KHU state correct ?                              │
│   - Invariants respectés ?                           │
│ • Si valide : MN signe bloc N (BLS signature)        │
│ • Broadcast signature au réseau                      │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ ÉTAPE 3 : Agrégation signatures                     │
├──────────────────────────────────────────────────────┤
│ • Collecte signatures BLS (≥ 240/400 threshold)      │
│ • Agrégation : sig_total = sig1 + sig2 + ... + sigN │
│ • Création KHUStateCommitment :                      │
│   - nHeight = N                                      │
│   - hashKHUState = Hash(KhuGlobalState)              │
│   - hashBlock = Hash(bloc N)                         │
│   - sig = sig_total (BLS aggregated)                 │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ ÉTAPE 4 : Finalisation (12 blocs plus tard)         │
├──────────────────────────────────────────────────────┤
│ • Bloc N+12 créé                                     │
│ • finalized_height = (N+12) - 12 = N                 │
│ • Bloc N est FINALISÉ ✅                             │
│ • Reorg au-delà de N = IMPOSSIBLE                    │
└──────────────────────────────────────────────────────┘
```

---

## 3. STAKERS : PROOF-OF-STAKE

### 3.1 Qu'est-ce qu'un Staker ?

**Staker** = Utilisateur qui verrouille des PIV (ou KHU) pour participer au minage PoS.

**Staking PIVX v6-KHU :**
- **PIV Staking** : Stake PIV pour miner blocs PIVX (standard)
- **KHU Staking** : Stake KHU_T → ZKHU pour yield R% (nouveau)

**Récompense staker :**
- reward_year PIV par bloc miné (égal à MN et DAO)
- R% yield sur KHU staké (si ZKHU staking)

### 3.2 Proof-of-Stake (PoS) PIVX

**PoS PIVX** = Minage basé sur "coin age" (montant × temps).

```cpp
/**
 * Calcul kernel hash (PoS mining)
 */
bool CheckStakeKernelHash(
    const CBlockHeader& blockHeader,
    const CTransaction& stakeTx,
    uint32_t nTimeTx,
    uint256& hashProofOfStake
) {
    // Hash kernel = Hash(stakeModifier + nTimeBlockFrom + txPrev + voutPrev + nTimeTx)
    CDataStream ss(SER_GETHASH, 0);
    ss << blockHeader.nStakeModifier;     // Modifier (randomness)
    ss << nTimeBlockFrom;                 // Time of previous block
    ss << stakeTx.GetHash();              // Stake transaction
    ss << voutPrev;                       // UTXO being staked
    ss << nTimeTx;                        // Current time

    hashProofOfStake = Hash(ss.begin(), ss.end());

    // Vérifier target
    arith_uint256 bnTarget;
    bnTarget.SetCompact(blockHeader.nBits);

    // Ajuster target par coin age
    bnTarget *= (nValueIn * nStakeAge);

    // Vérifier hash < target
    if (UintToArith256(hashProofOfStake) > bnTarget)
        return false;  // Pas de solution PoS

    return true;  // Solution PoS trouvée !
}
```

**Propriétés PoS PIVX :**
- **Coin age** : Plus de coins + plus longtemps staké = plus de chances
- **Équitable** : Proportionnel au montant staké
- **Économe** : Pas de GPU/ASIC (juste CPU)
- **Sécurisé** : Attaque 51% coûte cher (acheter 51% des coins)

### 3.3 Rôle des Stakers dans KHU

**Les stakers appliquent les transitions d'état KHU.**

```cpp
/**
 * ConnectBlock : Staker applique KHU state transition
 */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex) {
    // 1. Charger état KHU précédent
    KhuGlobalState khuState;
    if (!LoadKhuState(pindex->pprev->nHeight, khuState))
        return state.Invalid("khu-load-state-failed");

    // 2. Appliquer yield quotidien (si nécessaire)
    if ((pindex->nHeight - khuState.last_yield_update_height) >= 1440) {
        if (!ApplyDailyYield(khuState, pindex->nHeight))
            return state.Invalid("khu-yield-failed");
    }

    // 3. Appliquer transactions KHU
    for (const auto& tx : block.vtx) {
        if (!ApplyKHUTransaction(tx, khuState, state))
            return state.Invalid("khu-tx-apply-failed");
    }

    // 4. Vérifier invariants
    if (!khuState.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // 5. Sauvegarder nouvel état
    if (!SaveKhuState(pindex->nHeight, khuState))
        return state.Invalid("khu-save-state-failed");

    // ✅ Bloc validé par staker
    return true;
}
```

**Stakers sont responsables de :**
- Créer blocs valides
- Appliquer transitions KHU correctes
- Vérifier invariants
- Persister état KHU

**Masternodes vérifient le travail des stakers via signatures BLS.**

---

## 4. COORDINATION MASTERNODES + STAKERS

### 4.1 Flux Complet Bloc Validé

```
┌────────────────────────────────────────────────────────┐
│ 1. STAKER CRÉE BLOC                                    │
├────────────────────────────────────────────────────────┤
│ • Trouve solution PoS                                  │
│ • Applique transactions (incluant KHU)                 │
│ • Calcule nouvel état KHU                              │
│ • Vérifie invariants (C==U+Z, Cr==Ur)                  │
│ • Broadcast bloc au réseau                             │
└────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────┐
│ 2. MASTERNODES VALIDENT                                │
├────────────────────────────────────────────────────────┤
│ Chaque MN du quorum actif :                            │
│ • Reçoit bloc                                          │
│ • Valide transactions                                  │
│ • Recalcule état KHU indépendamment                    │
│ • Vérifie hashKHUState match                           │
│ • Vérifie invariants                                   │
│                                                        │
│ Si valide : MN signe (BLS signature)                   │
│ Si invalide : MN rejette (pas de signature)           │
└────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────┐
│ 3. AGRÉGATION SIGNATURES                               │
├────────────────────────────────────────────────────────┤
│ • Collecte ≥ 240/400 signatures (threshold 60%)        │
│ • Agrégation BLS : sig_total                           │
│ • Création KHUStateCommitment                          │
│ • Stockage commitment dans blockchain                  │
└────────────────────────────────────────────────────────┘
                          ↓
┌────────────────────────────────────────────────────────┐
│ 4. FINALISATION (12 BLOCS)                             │
├────────────────────────────────────────────────────────┤
│ • Attendre 12 blocs supplémentaires                    │
│ • Bloc devient FINALISÉ                                │
│ • État KHU IMMUABLE                                    │
│ • Reorg IMPOSSIBLE                                     │
└────────────────────────────────────────────────────────┘
```

### 4.2 KHUStateCommitment Structure

```cpp
/**
 * Commitment d'état KHU signé par quorum
 */
struct KHUStateCommitment {
    uint32_t nHeight;              // Hauteur du bloc
    uint256 hashKHUState;          // Hash(KhuGlobalState)
    uint256 hashBlock;             // Hash du bloc
    CBLSSignature sig;             // Signature BLS agrégée

    // Vérifier signature
    bool Verify(const CBLSPublicKey& quorumPubKey) const {
        // Message signé = Hash(nHeight || hashKHUState || hashBlock)
        uint256 message = GetCommitmentHash();

        // Vérifier signature BLS
        return sig.VerifyInsecure(quorumPubKey, message);
    }

    // Hash du commitment (pour signature)
    uint256 GetCommitmentHash() const {
        CHashWriter ss(SER_GETHASH, 0);
        ss << nHeight;
        ss << hashKHUState;
        ss << hashBlock;
        return ss.GetHash();
    }

    SERIALIZE_METHODS(KHUStateCommitment, obj) {
        READWRITE(obj.nHeight);
        READWRITE(obj.hashKHUState);
        READWRITE(obj.hashBlock);
        READWRITE(obj.sig);
    }
};
```

### 4.3 Stockage Commitments

```cpp
// LevelDB key pour commitments
'K' + 'C' + height  →  KHUStateCommitment

/**
 * Sauvegarder commitment
 */
bool SaveKHUCommitment(uint32_t nHeight, const KHUStateCommitment& commitment) {
    CDBBatch batch(*pdbKHU);

    std::vector<unsigned char> key;
    key.push_back('K');
    key.push_back('C');
    WriteLE32(key, nHeight);

    batch.Write(key, commitment);

    return pdbKHU->WriteBatch(batch);
}

/**
 * Charger commitment
 */
bool LoadKHUCommitment(uint32_t nHeight, KHUStateCommitment& commitment) {
    std::vector<unsigned char> key;
    key.push_back('K');
    key.push_back('C');
    WriteLE32(key, nHeight);

    return pdbKHU->Read(key, commitment);
}
```

---

## 5. RÈGLE DE FINALITÉ

### 5.1 Finality Depth = 12 Blocs

```cpp
const uint32_t FINALITY_DEPTH = 12;

/**
 * Calculer hauteur finalisée
 */
uint32_t GetFinalizedHeight(uint32_t tipHeight) {
    if (tipHeight < FINALITY_DEPTH)
        return 0;

    return tipHeight - FINALITY_DEPTH;
}
```

**Exemple :**
```
Tip = bloc 1000
Finalized height = 1000 - 12 = 988

Blocs 0-988 : FINALISÉS ✅ (immuables)
Blocs 989-1000 : Pas encore finalisés (peuvent être reorg)
```

### 5.2 Protection Reorg

```cpp
/**
 * Vérifier qu'un reorg ne viole pas la finalité
 */
bool CheckReorg(const CBlockIndex* pindexFork, const CBlockIndex* pindexNew) {
    uint32_t finalizedHeight = GetFinalizedHeight(chainActive.Height());

    // Vérifier que fork n'est pas avant finalité
    if (pindexFork->nHeight < finalizedHeight) {
        LogPrintf("ERROR: Reorg violates finality! fork=%d finalized=%d\n",
                  pindexFork->nHeight, finalizedHeight);
        return false;  // REJET REORG
    }

    // Fork après finalité : OK
    return true;
}
```

**Conséquence :**
```
❌ Reorg de 20 blocs : REJETÉ (viole finalité 12 blocs)
✅ Reorg de 5 blocs : ACCEPTÉ (dans fenêtre non-finalisée)
```

### 5.3 Consensus Rule : Finality Enforcement

```cpp
bool ConnectBlock(...) {
    // ... validation normale ...

    // Si bloc crée un reorg qui viole finalité : REJET
    if (pindex->pprev != chainActive.Tip()) {
        // C'est un reorg
        CBlockIndex* pindexFork = chainActive.FindFork(pindex);
        if (!CheckReorg(pindexFork, pindex))
            return state.Invalid(false, REJECT_INVALID, "finality-violation");
    }

    return true;
}
```

---

## 6. AVANTAGES SYSTÈME HYBRIDE

### 6.1 PoS (Stakers) Seul

```
✅ Économe en énergie
✅ Décentralisé (n'importe qui peut staker)
✅ Récompenses proportionnelles
❌ Finalité faible (reorg possibles)
❌ Attaque longue portée (long-range attack)
```

### 6.2 Quorums BLS (Masternodes) Seuls

```
✅ Finalité forte
✅ Signatures compactes
✅ Validation rapide
❌ Centralisation (besoin 10k PIV collateral)
❌ Pas de création de blocs
```

### 6.3 HYBRIDE PoS + Quorums BLS ✅

```
✅ Stakers créent blocs (décentralisé, économe)
✅ Masternodes finalisent blocs (sécurité, finalité)
✅ Double protection :
   - Attaque sur stakers : bloquée par MN
   - Attaque sur MN : bloquée par PoS
✅ Finalité 12 blocs (rapide)
✅ État KHU immuable (sécurité économique)
```

**PIVX v6-KHU = Meilleur des deux mondes.**

---

## 7. IMPLÉMENTATION CANONIQUE

### 7.1 Fichier : src/consensus/finality.h

```cpp
#ifndef PIVX_CONSENSUS_FINALITY_H
#define PIVX_CONSENSUS_FINALITY_H

#include "uint256.h"
#include "bls/bls.h"

namespace Consensus {

// Constantes finalité
const uint32_t FINALITY_DEPTH = 12;
const uint32_t QUORUM_SIZE = 400;
const uint32_t QUORUM_THRESHOLD = 240;  // 60%
const uint32_t QUORUM_ROTATION = 24;    // Blocs

/**
 * Commitment d'état KHU signé par quorum
 */
struct KHUStateCommitment {
    uint32_t nHeight;
    uint256 hashKHUState;
    uint256 hashBlock;
    CBLSSignature sig;

    bool Verify(const CBLSPublicKey& quorumPubKey) const;
    uint256 GetCommitmentHash() const;

    SERIALIZE_METHODS(KHUStateCommitment, obj) {
        READWRITE(obj.nHeight, obj.hashKHUState, obj.hashBlock, obj.sig);
    }
};

/**
 * Calculer hauteur finalisée
 */
inline uint32_t GetFinalizedHeight(uint32_t tipHeight) {
    return (tipHeight >= FINALITY_DEPTH) ? (tipHeight - FINALITY_DEPTH) : 0;
}

/**
 * Vérifier qu'un reorg ne viole pas finalité
 */
bool CheckReorgFinality(const CBlockIndex* pindexFork, const CBlockIndex* pindexNew);

/**
 * Vérifier signature quorum pour bloc
 */
bool VerifyQuorumSignature(const CBlockIndex* pindex, const KHUStateCommitment& commitment);

} // namespace Consensus

#endif // PIVX_CONSENSUS_FINALITY_H
```

### 7.2 Fichier : src/consensus/finality.cpp

```cpp
#include "consensus/finality.h"
#include "chain.h"
#include "validation.h"
#include "llmq/quorums.h"

namespace Consensus {

bool KHUStateCommitment::Verify(const CBLSPublicKey& quorumPubKey) const {
    uint256 message = GetCommitmentHash();
    return sig.VerifyInsecure(quorumPubKey, message);
}

uint256 KHUStateCommitment::GetCommitmentHash() const {
    CHashWriter ss(SER_GETHASH, 0);
    ss << nHeight << hashKHUState << hashBlock;
    return ss.GetHash();
}

bool CheckReorgFinality(const CBlockIndex* pindexFork, const CBlockIndex* pindexNew) {
    uint32_t finalizedHeight = GetFinalizedHeight(chainActive.Height());

    if (pindexFork->nHeight < finalizedHeight) {
        LogPrintf("ERROR: Reorg violates finality! fork=%d finalized=%d\n",
                  pindexFork->nHeight, finalizedHeight);
        return false;
    }

    return true;
}

bool VerifyQuorumSignature(const CBlockIndex* pindex, const KHUStateCommitment& commitment) {
    // Obtenir quorum actif pour cette hauteur
    auto quorum = llmq::quorumManager->GetQuorum(LLMQ_400_60, pindex->GetBlockHash());
    if (!quorum) {
        LogPrintf("ERROR: No quorum found for block %d\n", pindex->nHeight);
        return false;
    }

    // Vérifier signature
    if (!commitment.Verify(quorum->qc.quorumPublicKey)) {
        LogPrintf("ERROR: Invalid quorum signature for block %d\n", pindex->nHeight);
        return false;
    }

    return true;
}

} // namespace Consensus
```

---

## 8. TESTS

### 8.1 Tests Unitaires : src/test/finality_tests.cpp

```cpp
#include <boost/test/unit_test.hpp>
#include "consensus/finality.h"

using namespace Consensus;

BOOST_AUTO_TEST_SUITE(finality_tests)

BOOST_AUTO_TEST_CASE(test_finalized_height)
{
    // Tip = 100, finalized = 88
    BOOST_CHECK_EQUAL(GetFinalizedHeight(100), 88);

    // Tip = 12, finalized = 0
    BOOST_CHECK_EQUAL(GetFinalizedHeight(12), 0);

    // Tip = 11, finalized = 0
    BOOST_CHECK_EQUAL(GetFinalizedHeight(11), 0);

    // Tip = 1000, finalized = 988
    BOOST_CHECK_EQUAL(GetFinalizedHeight(1000), 988);
}

BOOST_AUTO_TEST_CASE(test_reorg_protection)
{
    // Setup chain
    CBlockIndex index1000;
    index1000.nHeight = 1000;

    CBlockIndex fork980;
    fork980.nHeight = 980;

    // Finalized = 988
    // Fork at 980 < 988 : REJETÉ
    BOOST_CHECK(!CheckReorgFinality(&fork980, &index1000));

    // Fork at 990 > 988 : ACCEPTÉ
    CBlockIndex fork990;
    fork990.nHeight = 990;
    BOOST_CHECK(CheckReorgFinality(&fork990, &index1000));
}

BOOST_AUTO_TEST_SUITE_END()
```

---

## 9. INTERDICTIONS ABSOLUES

```
❌ Finalité < 12 blocs (sécurité insuffisante)
❌ Finalité > 100 blocs (UX dégradée)
❌ Reorg au-delà de finalité
❌ Modifier threshold quorum < 51%
❌ Skip vérification signature BLS
❌ Accepter bloc sans commitment
❌ Modifier QUORUM_SIZE sans consensus
❌ Signer bloc invalide (MN malveillant)
```

---

## 10. RÉFÉRENCES

**Documents liés :**
- `01-blueprint-master-flow.md` — Flow général
- `SPEC.md` — Section 6 (Masternode Finality)
- `ARCHITECTURE.md` — Section 8 (Consensus)

**Standards BLS :**
- BLS Signatures (RFC draft-irtf-cfrg-bls-signature)
- LLMQ PIVX (Dash LLMQ adaptation)

---

## 11. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **Finalité = 12 blocs** (immuable)
2. **Quorum 400/240** (60% threshold)
3. **BLS signatures** (agrégation obligatoire)
4. **Reorg protection** (au-delà finalité = rejet)
5. **Hybride PoS + BLS** (double sécurité)

**Statut :** ACTIF pour implémentation Phase 1-10

---

**FIN DU BLUEPRINT FINALITÉ MASTERNODE + STAKERS**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
