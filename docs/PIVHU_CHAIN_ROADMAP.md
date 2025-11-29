# HU CHAIN - Roadmap

**Version:** 2.0
**Date:** 2025-11-29
**Status:** APPROVED
**Ref:** HU_CHAIN_DECISIONS.md

---

## Vision

**HU = Hedge Unit** - Une blockchain propre, légère, avec économie gouvernée par R%.

```
┌─────────────────────────────────────────────────────────────────┐
│                        HU CHAIN v1                              │
│                                                                 │
│   HU (coin natif)                                              │
│    │                                                           │
│    ├── PIVHU SHIELD ──── Transfers privés (Sapling)              │
│    │                                                           │
│    └── KHU (colored 1:1)                                       │
│         │                                                      │
│         └── ZKHU ──── Staking privé + Yield R%                │
│                       (PAS de transfers entre notes)           │
│                                                                 │
│   Consensus: PoS PIVHU (reward=0) + Masternodes finalité         │
│   Gouvernance: DOMC (vote R%) + Proposals (Treasury T)         │
│   Finalité: LLMQ Chainlocks (reorg > 12 impossible)           │
│   Émission: ZÉRO bloc (tout via yield R%)                     │
│   DAO: Proposals tapent directement dans T                     │
└─────────────────────────────────────────────────────────────────┘
```

---

## Origine

Fork de PIVX avec code KHU existant, nettoyé et optimisé.

**Repositories:**
- `github.com/AdonisPhusis/PIVX-KHU-PROPOSAL` (frozen backup)
- `github.com/AdonisPhusis/PIVX-V6-KHU` (dev backup)
- `github.com/AdonisPhusis/HEDGE-HUNIT` (PIVHU Chain active)

---

## Consensus v1

### Décision Finale
```
┌─────────────────────────────────────────────────────────────────┐
│  PRODUCTION BLOCS                                               │
│  ✅ PoS PIVHU classique (comme PIVX)                              │
│  ✅ Block reward = 0 (GetBlockValue() = 0)                     │
│  ✅ Staking PIVHU = sécurité réseau uniquement                    │
│                                                                 │
│  FINALITÉ                                                       │
│  ✅ Masternodes LLMQ/BLS                                       │
│  ✅ Chainlocks                                                  │
│  ✅ Reorg > 12 blocs = IMPOSSIBLE                              │
│                                                                 │
│  v2 FUTURE                                                      │
│  ☐ ZKHU-PoS (production blocs avec notes Sapling)              │
└─────────────────────────────────────────────────────────────────┘
```

### Pourquoi ce choix?
- Code testnet KHU déjà fonctionnel
- Stabilité > innovation pour genesis
- ZKHU-PoS = upgrade v2 future

---

## Architecture

### Ce qu'on GARDE

| Composant | Usage | Status |
|-----------|-------|--------|
| **PoS moteur** | Production blocs (reward=0) | ✅ GARDER |
| Sapling crypto | PIVHU SHIELD + ZKHU | ✅ |
| Masternodes DMN/BLS | Quorum LLMQ + finalité | ✅ |
| LLMQ/Chainlocks | Finalité 12 blocs | ✅ |
| HTLC opcodes | Swap PIV → HU | ✅ |
| P2P network | Communication | ✅ |
| Wallet HD | Gestion clés | ✅ |
| Qt GUI | Interface minimale | ✅ |
| RPC server | Interface | ✅ |
| Code KHU | MINT/REDEEM/STAKE/UNSTAKE | ✅ |
| DOMC | Gouvernance R% | ✅ |

### Ce qu'on SUPPRIME

| Composant | Lignes | Raison |
|-----------|--------|--------|
| `src/libzerocoin/` | ~2,400 | Legacy mort |
| `src/zpiv/` | ~500 | zPIV PoS mort |
| `src/legacy/zerocoin*` | ~3,100 | Legacy mort |
| Cold staking refs | ~300 | Plus utile |
| Budget system legacy | ~3,700 | Remplacé par T direct |
| 7 Sporks deprecated | ~50 | Obsolètes |
| **TOTAL** | **~10,000** | |

### ⚠️ CE QU'ON NE SUPPRIME PAS

```
❌ NE PAS supprimer le moteur PoS (CheckProofOfStake, etc.)
❌ NE PAS supprimer stakeinput.*, kernel.*
✅ JUSTE mettre GetBlockValue() = 0
```

---

## Genesis Block

```
PREMINE TOTAL: 28,240,000 HU

┌────────────────────────────────────────────────────────────┐
│                                                            │
│  [1] Dev Reward       120,000 PIVHU                          │
│      → Adresse HU normale (P2PKH)                         │
│      → HORS système KHU                                   │
│      → Ne touche PAS C/U/Z/Cr/Ur/T                        │
│                                                            │
│  [2] MN Collateral    120,000 PIVHU    (12 × 10,000)         │
│      → 12 masternodes contrôlés                           │
│      → LLMQ opérationnel dès genesis                      │
│                                                            │
│  [3] Swap Reserve     28,000,000 PIVHU                       │
│      → 50% du supply PIV (~56M)                           │
│      → Verrouillé dans script HTLC                        │
│      → HORS système KHU                                   │
│      → Libéré 1:1 contre PIV burned                       │
│                                                            │
│  [4] KhuGlobalState initial                               │
│      C = 0, U = 0, Z = 0                                  │
│      Cr = 0, Ur = 0                                       │
│      T = 0                                                 │
│      R_annual = 4000 (40%)                                │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

### Points CRITIQUES
```
✅ Dev reward = HU normal, HORS invariants KHU
✅ Swap reserve = HU verrouillé HTLC, HORS KHU
✅ KhuGlobalState démarre à ZÉRO
✅ Invariants préservés dès genesis
```

---

## DAO / Proposals (T direct)

### Système DAO v1

```
┌─────────────────────────────────────────────────────────────────┐
│  DAO MINIMALISTE - Proposals tapent dans T                      │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. T accumule automatiquement                                 │
│     T_daily = (U × R_annual) / 10000 / 8 / 365                 │
│                                                                 │
│  2. Proposal créée (RPC: dao_create)                           │
│     {id, address, amount, payout_height, votes}                │
│                                                                 │
│  3. Vote MN (RPC: dao_vote)                                    │
│     Approuvé si majorité MN                                    │
│                                                                 │
│  4. Exécution (ConnectBlock)                                   │
│     Si approved && height == payout_height && T >= amount:     │
│     - state.T -= amount                                        │
│     - TX DAO_PAYOUT créée → address                            │
│                                                                 │
│  5. Invariants checkés                                         │
│     T >= 0 toujours                                            │
│     C/U/Z/Cr/Ur NON touchés                                    │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Ce qu'on SUPPRIME
```
❌ Budget PIVX (block reward based)
❌ Superblocks
❌ Cycles d'élection complexes
❌ Payouts prédéfinis coinbase
```

---

## Mécanisme de SWAP PIV → HU

```
PIVX Chain (legacy)                PIVHU Chain (nouvelle)
      │                                   │
      │  1. User lock PIV                │
      │     HTLC(hash, timelock)         │
      │                                   │
      │        ──── secret ────►         │
      │                                   │
      │                                   │  2. User claim HU
      │                                   │     avec le secret
      │                                   │
      ▼                                   ▼
PIV locked forever                   HU libéré de reserve
(= burned)                           (1:1 garanti)
```

**Avantages:**
- Atomic swap - pas de trust
- PIV "burned" automatiquement
- 1:1 garanti par le code
- Bootstrap sur communauté existante

---

## Économie

```
ÉMISSION = 0 HU/bloc (toujours)

YIELD = note.amount × R% / 365 par jour
        → Stakers ZKHU reçoivent le yield
        → Mécanisme Cr/Ur intact

TREASURY = U × R% / 8 / 365 par jour
           → ~5% de l'activité économique
           → Finance les proposals DAO
           → Dépensé via TX DAO_PAYOUT

R% GOUVERNANCE:
├── Initial: 40%
├── Plancher: 7%
├── Décroissance: -1%/an
└── Atteint plancher: 33 ans

INVARIANTS:
├── C == U + Z (toujours)
├── Cr == Ur (toujours)
└── T >= 0 (toujours)
```

---

## Plan d'Implémentation

### Phase 1: Nettoyage [3-5 jours]

| Tâche | Status |
|-------|--------|
| Fork dans HEDGE-HUNIT repo | ☐ |
| Supprimer `src/libzerocoin/` | ☐ |
| Supprimer `src/zpiv/` | ☐ |
| Supprimer `legacy/zerocoin*` | ☐ |
| Nettoyer cold staking refs | ☐ |
| Supprimer budget legacy | ☐ |
| Supprimer 7 sporks deprecated | ☐ |
| ⚠️ GARDER moteur PoS | ☐ |
| Fix compilation errors | ☐ |
| Build test | ☐ |

### Phase 2: Renommage [2-3 jours]

| Tâche | Status |
|-------|--------|
| PIV → HU (grep/sed global) | ☐ |
| PIVX → HU (branding) | ☐ |
| pivxd → pivhud | ☐ |
| pivx-cli → pivhu-cli | ☐ |
| Mise à jour messages/strings | ☐ |
| Mise à jour RPC help | ☐ |

### Phase 3: Genesis [3-5 jours]

| Tâche | Status |
|-------|--------|
| Nouveau `chainparams.cpp` | ☐ |
| Genesis block avec premine | ☐ |
| Dev reward 120k PIVHU (adresse normale) | ☐ |
| MN collateral 120k PIVHU | ☐ |
| Swap reserve 28M PIVHU (HTLC script) | ☐ |
| Configuration LLMQ 12 MN | ☐ |
| Block reward = 0 | ☐ |
| KhuGlobalState = zéro | ☐ |
| Calculer genesis hash | ☐ |

### Phase 4: DAO T-direct [2-3 jours]

| Tâche | Status |
|-------|--------|
| Implémenter dao_create RPC | ☐ |
| Implémenter dao_vote RPC | ☐ |
| TX type DAO_PAYOUT (12) | ☐ |
| Logic ConnectBlock T -= amount | ☐ |
| Tests proposals | ☐ |

### Phase 5: Intégration KHU [2-3 jours]

| Tâche | Status |
|-------|--------|
| Adapter code KHU existant | ☐ |
| PIVHU SHIELD = Sapling standard | ☐ |
| ZKHU = staking uniquement | ☐ |
| Tests unitaires KHU | ☐ |
| DOMC gouvernance | ☐ |

### Phase 6: Testnet [1-2 semaines]

| Tâche | Status |
|-------|--------|
| Déployer 12 MN testnet | ☐ |
| Tester LLMQ/Chainlocks | ☐ |
| Tester KHU pipeline complet | ☐ |
| Tester DOMC vote R% | ☐ |
| Tester DAO proposals | ☐ |
| Tester HTLC swap | ☐ |
| Stress test | ☐ |
| Bug fixes | ☐ |

### Phase 7: Mainnet [1 semaine]

| Tâche | Status |
|-------|--------|
| Genesis mainnet final | ☐ |
| Déployer 12 MN production | ☐ |
| Activer chainlocks | ☐ |
| Ouvrir swap PIV → HU | ☐ |
| Documentation publique | ☐ |
| Annonce communauté | ☐ |

---

## Timeline Estimée

```
Semaine 1-2:  Phase 1 + 2 (Nettoyage + Renommage)
Semaine 2-3:  Phase 3 + 4 (Genesis + DAO)
Semaine 3-4:  Phase 5 (Intégration KHU)
Semaine 4-6:  Phase 6 (Testnet)
Semaine 6-7:  Phase 7 (Mainnet)

TOTAL: 5-7 semaines
```

---

## Fichiers Clés à Modifier

```
# Genesis & Params
src/chainparams.cpp        → Genesis PIVHU + params
src/chainparamsbase.cpp    → Network names
src/consensus/params.h     → Consensus params
src/llmq/params.h          → LLMQ 12 MN config
src/amount.h               → HU denomination

# Consensus
src/validation.cpp         → Block reward = 0 + DAO_PAYOUT
src/init.cpp               → Initialisation

# Branding
src/util/system.cpp        → Branding HU
src/clientversion.h        → Version HU

# DAO
src/budget/                → Simplifier pour T-direct

# À SUPPRIMER
src/libzerocoin/*
src/zpiv/*
src/legacy/zerocoin*
src/sporkid.h              → Retirer 7 sporks deprecated

# ⚠️ NE PAS SUPPRIMER
src/stakeinput.*           → GARDER (PoS)
src/kernel.*               → GARDER (PoS)
```

---

## Paramètres Consensus

```cpp
// PIVHU Chain Parameters
consensus.nMasternodeCollateral = 10000 * COIN;  // 10k HU
consensus.nLLMQMinSize = 12;                     // 12 MN minimum
consensus.nBlockReward = 0;                      // Toujours 0
consensus.nFinalityDepth = 12;                   // Reorg > 12 interdit

// KHU Parameters (hérités)
consensus.nKHUMaturityBlocks = 4320;   // 3 jours
consensus.nDOMCCycleLength = 172800;   // 4 mois
consensus.nR_initial = 4000;           // 40%
consensus.nR_floor = 700;              // 7%
consensus.nR_decay = 100;              // -1%/an
consensus.nT_divisor = 8;              // Treasury divider
```

---

## Risques et Mitigations

| Risque | Mitigation |
|--------|------------|
| 12 MN = centralisation | Ouvrir progressivement à d'autres |
| Personne ne stake HU | Dev stake partie premine pour liveness |
| Swap reserve grosse | Script HTLC vérifié |
| Bugs nouveau code | Testnet extensif |
| Communauté PIV hostile | Communication claire sur migration |

---

## Évolutions Futures (v2)

| Feature | Description |
|---------|-------------|
| ZKHU-PoS | Production blocs avec notes Sapling |
| Qt avancé | Panel KHU/ZKHU intégré |
| DAO v2 | Gouvernance avancée |
| Bridges | Cross-chain swaps |

---

## Contact

- **Repo:** github.com/AdonisPhusis/HEDGE-HUNIT
- **Backup:** github.com/AdonisPhusis/PIVX-V6-KHU

---

## Documents Associés

- `docs/HU_CHAIN_DECISIONS.md` - Décisions finales
- `docs/blueprints/V2-01-HU-CHAIN-CLEANUP.md` - Plan technique suppression
- `docs/SPEC.md` - Spécification KHU
- `docs/ARCHITECTURE.md` - Architecture technique

---

## Changelog

- **v2.0** (2025-11-29): Mise à jour avec décisions finales (consensus PoS, DAO T-direct)
- **v1.0** (2025-11-29): Vision initiale PIVHU Chain
