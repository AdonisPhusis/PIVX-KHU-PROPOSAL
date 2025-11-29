# HU CHAIN - Décisions Finales

**Version:** 1.0
**Date:** 2025-11-29
**Status:** APPROVED
**Source:** Discussion architecte/dev 2025-11-29

---

## 1. CONSENSUS (Production de blocs)

### Décision v1
```
✅ GARDER le PoS HU classique (comme PIVX)
✅ Block reward = 0 HU (GetBlockValue() = 0)
✅ Le staking HU sert UNIQUEMENT à produire les blocs
```

### Ce qu'on NE fait PAS en v1
```
❌ ZKHU-PoS (trop de travail, prévu pour v2)
❌ MN-only block production (trop centralisé)
```

### Justification
- Code testnet KHU fonctionne déjà
- Stabilité > innovation pour genesis
- ZKHU-PoS = upgrade v2 "stylée" future

---

## 2. FINALITÉ

### Décision
```
✅ LLMQ / Chainlocks avec Masternodes
✅ Finalité 12 blocs (FINALITY_DEPTH = 12)
✅ Reorg > 12 blocs = IMPOSSIBLE
```

### Séparation des rôles
```
Production blocs  = PoS HU (stakers)
Finalité          = Masternodes LLMQ
Gouvernance R%    = DOMC via MN
```

---

## 3. ÉCONOMIE KHU/ZKHU

### Décision
```
✅ AUCUNE modification du moteur KHU
✅ Pipeline complet: PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
```

### Invariants IMMUABLES
```cpp
C == U + Z      // Collateral = Transparent + Shielded
Cr == Ur        // Reward pool = Reward rights
T >= 0          // Treasury jamais négatif
```

### Paramètres R%
```
R_initial = 40% (4000 bp)
R_floor = 7% (700 bp)
R_decay = -1%/an
R_MAX après 33 ans = 7%
```

### Treasury T
```cpp
T_daily = (U × R_annual) / 10000 / T_DIVISOR / 365
// T_DIVISOR = 8 → ~5% de U par an à R=40%
```

---

## 4. GENESIS HU

### Structure Premine
```
┌─────────────────────────────────────────────────────────────┐
│  [1] Dev Reward       120,000 HU                            │
│      → Adresse HU normale (P2PKH)                          │
│      → HORS KHU (ne touche pas C/U/Z/Cr/Ur/T)              │
│      → Cadeau développeur                                   │
│                                                             │
│  [2] MN Collateral    120,000 HU (12 × 10,000)             │
│      → 12 masternodes contrôlés                            │
│      → LLMQ opérationnel dès genesis                       │
│                                                             │
│  [3] Swap Reserve     28,000,000 HU                        │
│      → Verrouillé dans script HTLC                         │
│      → HORS KHU (ne touche pas C/U/Z/Cr/Ur/T)              │
│      → Libéré 1:1 contre PIV burned                        │
│                                                             │
│  [4] KhuGlobalState initial                                │
│      C = 0, U = 0, Z = 0                                   │
│      Cr = 0, Ur = 0                                        │
│      T = 0                                                  │
│      R_annual = 4000 (40%)                                 │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Points CRITIQUES
```
❌ Les 120k dev reward ne sont PAS dans Cr/Ur/T
❌ Les 28M swap reserve ne sont PAS dans C/U/Z
✅ KhuGlobalState démarre à zéro
✅ Invariants préservés dès genesis
```

---

## 5. DAO / PROPOSALS

### Décision v1
```
✅ Proposals qui tapent DIRECTEMENT dans T
✅ Supprimer TOUT le budget system legacy PIVX
```

### Système DAO minimal
```
1. Proposal créée (RPC: dao_create)
   - id, address, amount, payout_height, votes

2. Vote MN (RPC: dao_vote)
   - yes_votes / no_votes
   - Approuvé si majorité MN

3. Exécution dans ConnectBlock
   - Si proposal approved && payout_height == nHeight
   - Si state.T >= amount
   - Alors: state.T -= amount
   - Création TX type DAO_PAYOUT → address

4. Invariants checkés après
```

### TX DAO_PAYOUT
```cpp
// Nouvelle transaction spéciale
TxType::DAO_PAYOUT = 12  // Après KHU types (6-11)

// Caractéristiques:
- Pas d'inputs (comme coinbase)
- 1 output vers address de la proposal
- Amount borné par T (T >= 0 toujours)
- Ne touche PAS C/U/Z/Cr/Ur (seulement T)
```

### Ce qu'on SUPPRIME
```
❌ Budget system PIVX (block reward based)
❌ Superblocks
❌ Cycles d'élection complexes
❌ Payouts prédéfinis
```

---

## 6. WALLET QT

### Décision
```
✅ GARDER Qt minimal
✅ Rebrand PIVX → HU
✅ KHU via RPC uniquement au début
```

### RPC KHU disponibles
```
khumint, khuredeem, khusend
khubalance, khulistunspent
khustake, khuunstake, khuliststaked
getkhustate, khudiagnostics
```

### Qt v2 (futur)
```
- Onglet KHU/ZKHU
- Panel DOMC
- Monitoring Treasury T
```

---

## 7. SUPPRESSIONS CODE

### Obligatoires (v1)
```
rm -rf src/libzerocoin/          (~2,377 lignes)
rm -rf src/zpiv/                 (~471 lignes)
rm src/legacy/validation_zerocoin_legacy.*
rm src/legacy/stakemodifier.*    (~10,147 lignes)
rm src/stakeinput.*              (GARDE le PoS!)
rm src/kernel.*                  (GARDE le PoS!)
```

### ATTENTION - NE PAS SUPPRIMER
```
⚠️ Le moteur PoS (CheckProofOfStake, etc.)
⚠️ Juste mettre GetBlockValue() = 0
⚠️ Le staking HU reste nécessaire pour produire les blocs
```

### Sporks à supprimer
```cpp
SPORK_2_SWIFTTX
SPORK_3_SWIFTTX_BLOCK_FILTERING
SPORK_5_MAX_VALUE
SPORK_16_ZEROCOIN_MAINTENANCE_MODE
SPORK_17_COLDSTAKING_ENFORCEMENT
SPORK_18_ZEROCOIN_PUBLICSPEND_V4
SPORK_19_COLDSTAKING_MAINTENANCE
```

---

## 8. ROADMAP

### HU v1 (Genesis)
```
[x] Consensus = PoS HU classique (reward 0)
[x] Finalité = Masternodes / LLMQ
[x] KHU & ZKHU = opérationnels
[x] DOMC = vote R% actif
[x] T = accumulation + DAO_PAYOUT
[x] Swap PIV → HU (HTLC)
[x] Wallet Qt basique
```

### HU v2 (Upgrade future)
```
[ ] ZKHU-PoS : production blocs avec notes Sapling
[ ] Qt amélioré (KHU panel)
[ ] Optimisations LLMQ
[ ] DAO governance avancée
```

---

## 9. PARAMÈTRES CONSENSUS HU

```cpp
// HU Chain Parameters
consensus.nMasternodeCollateral = 10000 * COIN;  // 10k HU
consensus.nLLMQMinSize = 12;                     // 12 MN minimum
consensus.nBlockReward = 0;                      // Toujours 0

// KHU Parameters (hérités)
consensus.nKHUMaturityBlocks = 4320;   // 3 jours
consensus.nDOMCCycleLength = 172800;   // 4 mois
consensus.nR_initial = 4000;           // 40%
consensus.nR_floor = 700;              // 7%
consensus.nR_decay = 100;              // -1%/an
consensus.nT_divisor = 8;              // Treasury divider

// Finality
consensus.nFinalityDepth = 12;         // Reorg > 12 interdit
```

---

## 10. SYNTHÈSE EN UNE PHRASE

> **HU Chain v1 = PIVX-V6-KHU fonctionnel, PoS HU reward=0, MN finalité,
> KHU/ZKHU/DOMC/T inchangés, DAO via T direct, dev-premine externe,
> swap PIV→HU, Qt minimal. ZKHU-PoS = v2.**

---

## RÉFÉRENCES

- Blueprint: `docs/blueprints/V2-01-HU-CHAIN-CLEANUP.md`
- Roadmap: `docs/HU_CHAIN_ROADMAP.md`
- SPEC KHU: `docs/SPEC.md`
- Architecture: `docs/ARCHITECTURE.md`

---

## CHANGELOG

- **v1.0** (2025-11-29): Décisions finales discussion architecte/dev
