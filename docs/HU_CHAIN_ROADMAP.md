# HU CHAIN - Roadmap

**Version:** 1.0
**Date:** 2025-11-29
**Status:** PLANNING

---

## Vision

**HU = Hedge Unit** - Une blockchain propre, légère, avec économie gouvernée par R%.

```
┌─────────────────────────────────────────────────────────────────┐
│                        HU CHAIN                                 │
│                                                                 │
│   HU (coin natif)                                              │
│    │                                                           │
│    ├── HU SHIELD ──── Transfers privés (Sapling)              │
│    │                                                           │
│    └── KHU (colored 1:1)                                       │
│         │                                                      │
│         └── ZKHU ──── Staking privé + Yield R%                │
│                       (PAS de transfers entre notes)           │
│                                                                 │
│   Gouvernance: DOMC (vote R%) + Proposals (Treasury T)         │
│   Finalité: LLMQ Chainlocks (pas de reorg)                     │
│   Émission: ZÉRO (tout via R%)                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## Origine

Fork de PIVX avec code KHU existant (commit `80370e9`), nettoyé et optimisé.

**Backup disponible:**
- `github.com/AdonisPhusis/PIVX-KHU-PROPOSAL` (frozen)
- `github.com/AdonisPhusis/PIVX-V6-KHU` (dev)
- `github.com/AdonisPhusis/HEDGE-HUNIT` (HU Chain)

---

## Architecture

### Ce qu'on GARDE

| Composant | Usage |
|-----------|-------|
| Sapling crypto | HU SHIELD + ZKHU |
| Masternodes DMN/BLS | Quorum LLMQ |
| LLMQ/Chainlocks | Finalité rapide |
| HTLC opcodes | Swap PIV → HU |
| P2P network | Communication |
| Wallet HD | Gestion clés |
| RPC server | Interface |
| Code KHU | MINT/REDEEM/STAKE/UNSTAKE |
| DOMC | Gouvernance R% |

### Ce qu'on SUPPRIME

| Composant | Lignes | Raison |
|-----------|--------|--------|
| `src/zerocoin/` | ~15,000 | Legacy mort |
| `src/libzerocoin/` | ~8,000 | Dépendance zerocoin |
| `src/legacy/` | ~3,000 | Code obsolète |
| PoS staking | ~5,000 | Block reward = 0 |
| Cold staking | ~2,000 | Plus utile |
| Budget system legacy | ~4,000 | Remplacé par T |
| MN payments old | ~2,000 | Plus de rewards |
| **TOTAL** | **~42,000** | **-17% code** |

---

## Genesis Block

```
PREMINE TOTAL: 28,240,000 HU

┌────────────────────────────────────────────────────────────┐
│                                                            │
│  [1] MN Collateral    120,000 HU    (12 × 10,000)         │
│      → 12 masternodes contrôlés                           │
│      → LLMQ opérationnel immédiatement                    │
│                                                            │
│  [2] Pool R%          120,000 HU                          │
│      → Pour payer les yields ZKHU                         │
│      → Alimente Cr/Ur                                     │
│                                                            │
│  [3] Swap Reserve     28,000,000 HU                       │
│      → 50% du supply PIV (~56M)                           │
│      → Verrouillé dans contrat HTLC                       │
│      → Appartient à PERSONNE jusqu'au swap               │
│                                                            │
│  [4] Treasury T       0 HU                                │
│      → Monte automatiquement via U×R%/8/365              │
│                                                            │
└────────────────────────────────────────────────────────────┘
```

---

## Mécanisme de SWAP PIV → HU

```
PIVX Chain (legacy)                HU Chain (nouvelle)
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

YIELD = ZKHU × R% / 365 par jour
        → Payé depuis Pool R% (Cr)

TREASURY = U × R% / 8 / 365 par jour
           → ~5% de l'activité économique
           → Finance les proposals

R% GOUVERNANCE:
├── Initial: 40%
├── Plancher: 7%
├── Décroissance: -1%/an
└── Atteint plancher: 33 ans
```

---

## Plan d'Implémentation

### Phase 1: Nettoyage [3-5 jours]

| Tâche | Status |
|-------|--------|
| Fork PIVX dans nouveau repo | ☐ |
| Supprimer `src/zerocoin/` | ☐ |
| Supprimer `src/libzerocoin/` | ☐ |
| Supprimer legacy staking code | ☐ |
| Supprimer cold staking | ☐ |
| Supprimer budget system legacy | ☐ |
| Fix compilation errors | ☐ |
| Build test | ☐ |

### Phase 2: Renommage [2-3 jours]

| Tâche | Status |
|-------|--------|
| PIV → HU (grep/sed global) | ☐ |
| PIVX → HU (branding) | ☐ |
| pivxd → hud | ☐ |
| pivx-cli → hu-cli | ☐ |
| Mise à jour messages/strings | ☐ |
| Mise à jour RPC help | ☐ |

### Phase 3: Genesis [3-5 jours]

| Tâche | Status |
|-------|--------|
| Nouveau `chainparams.cpp` | ☐ |
| Genesis block avec premine | ☐ |
| Configuration LLMQ 12 MN | ☐ |
| Activation KHU bloc 0 | ☐ |
| Block reward = 0 | ☐ |
| Désactiver upgrades inutiles | ☐ |
| Calculer genesis hash | ☐ |

### Phase 4: Intégration KHU [2-3 jours]

| Tâche | Status |
|-------|--------|
| Adapter code KHU existant | ☐ |
| HU SHIELD = Sapling standard | ☐ |
| ZKHU = staking uniquement | ☐ |
| Tests unitaires | ☐ |
| DOMC gouvernance | ☐ |

### Phase 5: Testnet [1-2 semaines]

| Tâche | Status |
|-------|--------|
| Déployer 12 MN testnet | ☐ |
| Tester LLMQ/Chainlocks | ☐ |
| Tester KHU pipeline complet | ☐ |
| Tester DOMC vote R% | ☐ |
| Tester HTLC swap | ☐ |
| Stress test | ☐ |
| Bug fixes | ☐ |

### Phase 6: Mainnet [1 semaine]

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
Semaine 2-3:  Phase 3 + 4 (Genesis + KHU)
Semaine 3-5:  Phase 5 (Testnet)
Semaine 5-6:  Phase 6 (Mainnet)

TOTAL: 4-6 semaines
```

---

## Fichiers Clés à Modifier

```
src/chainparams.cpp        → Genesis HU + params
src/chainparamsbase.cpp    → Network names
src/init.cpp               → Initialisation
src/validation.cpp         → Block reward = 0
src/consensus/params.h     → Consensus params
src/llmq/params.h          → LLMQ 12 MN config
src/amount.h               → HU denomination
src/util/system.cpp        → Branding
src/clientversion.h        → Version HU

# Supprimer entièrement:
src/zerocoin/*
src/libzerocoin/*
src/stakeinput.*
src/kernel.*
```

---

## Paramètres Consensus

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
```

---

## Risques et Mitigations

| Risque | Mitigation |
|--------|------------|
| 12 MN = centralisation | Ouvrir progressivement à d'autres |
| Swap reserve grosse | Contrat HTLC vérifié |
| Bugs nouveau code | Testnet extensif |
| Communauté PIV hostile | Communication claire sur migration |

---

## Contact

- **Repo:** github.com/AdonisPhusis/HEDGE-HUNIT
- **Backup KHU/PIVX:** commit `80370e9`

---

## Changelog

- **v1.0** (2025-11-29): Vision initiale HU Chain
