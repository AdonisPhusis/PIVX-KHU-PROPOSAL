# Blueprint 10: Plan Testnet Public KHU V6

**Version:** 1.4
**Date:** 2025-11-27
**Status:** PLAN D'EXECUTION

---

## Objectif

Assurer un testnet public robuste en **deux testnets séquentiels**:

1. **TESTNET 1: "KHU Pur"** → Valider système KHU isolé (émission zéro)
2. **TESTNET 2: "KHU + V6 PIVX"** → Valider compatibilité V6 complet (~1 an)

---

## Vue d'Ensemble

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    TESTNET 1: "KHU Pur"                                 │
│                    Valider le système KHU isolé                         │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Configuration:                                                          │
│  • Émission bloc: ZERO                                                  │
│  • V6: actif bloc 1 (pas de transition à tester)                        │
│  • Premine: 1,000,000 PIV (wallet fixe pour calculs)                    │
│                                                                          │
│  Valider:                                                               │
│  ✓ Pipeline complet: MINT → STAKE → UNSTAKE → REDEEM                   │
│  ✓ R% yield distribution (40% initial)                                 │
│  ✓ Invariants: C == U + Z, Cr == Ur                                    │
│  ✓ Persistence state après restart                                      │
│  ✓ Multi-wallet sync                                                    │
│                                                                          │
│  Durée: Jusqu'à 100% validé                                             │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Système KHU validé ✓
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    TESTNET 2: "KHU + V6 PIVX"                           │
│                    Valider compatibilité V6 complet                     │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                          │
│  Configuration:                                                          │
│  • V6 activation: bloc X                                                │
│  • Émission pré-V6: 4/6/10 PIV/bloc (Staker/MN/DAO)                    │
│  • Émission post-V6: 6/6/6 PIV/bloc puis -1/an                         │
│  • Finalité LLMQ: opérationnelle                                        │
│  • DOMC: cycles actifs                                                  │
│  • DAO Treasury: accumulation T                                         │
│                                                                          │
│  Valider:                                                               │
│  ✓ Transition émission: 4/6/10 → 6/6/6                                 │
│  ✓ Décroissance -1/an: 6/6/6 → 5/5/5 → ... → 0/0/0                    │
│  ✓ KHU fonctionne AVEC émission en parallèle                           │
│  ✓ Finalité LLMQ bloque reorgs > 12 blocs                              │
│  ✓ DOMC cycle complet (vote R%)                                        │
│  ✓ DAO Treasury accumule 2%/an                                         │
│                                                                          │
│  Durée: ~1 an simulé (ou accéléré)                                      │
│                                                                          │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## TESTNET 1: "KHU Pur"

### 1.1 Pourquoi Émission Zéro?

Avec émission, la balance wallet change à chaque bloc:
```
Bloc 200: wallet = 3600 PIV (mining)
Bloc 201: wallet = 3618 PIV (+18 reward)
          MINT 100 KHU → wallet = ?
          Impossible de vérifier précisément!
```

Avec émission zéro et premine fixe:
```
Bloc 1:   wallet = 1,000,000 PIV (CONSTANT)
          MINT 100 KHU
Bloc 2:   wallet = 999,899.9999 PIV
          Différence = 100.0001 PIV = 100 lockés + fee
          VÉRIFICATION EXACTE!
```

### 1.2 Configuration Testnet 1

```cpp
// chainparams.cpp - Testnet 1 "KHU Pur"
class CTestNetKHU_Pure : public CChainParams {
public:
    CTestNetKHU_Pure() {
        strNetworkID = "khu-testnet-1";

        // V6 actif immédiatement
        consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 1;

        // ÉMISSION ZÉRO
        consensus.nBlockReward = 0;

        // PREMINE 1M PIV
        genesis = CreateGenesisWithPremine(1000000 * COIN);

        // KHU config simplifiée
        consensus.nDOMCEnabled = false;      // Pas de vote
        consensus.nR_annual = 4000;          // 40% fixe
        consensus.nDAOTreasuryEnabled = false;

        // Maturity optionnellement réduite pour tests rapides
        // consensus.nKHUMaturityBlocks = 100;
    }
};
```

### 1.3 Tests Testnet 1

| Test | Description | Vérification |
|------|-------------|--------------|
| T1-MINT | MINT 100 PIV → 100 KHU | `1M - wallet = 100 + fee` |
| T1-REDEEM | REDEEM 50 KHU → 50 PIV | `wallet += 50 - fee` |
| T1-STAKE | STAKE 200 KHU → 200 ZKHU | `C constant, U-=200, Z+=200` |
| T1-UNSTAKE | UNSTAKE après maturity | `C+=Y, U+=P+Y, Z-=P` |
| T1-YIELD | R% distribution | `yield = P × 40% / 365` |
| T1-PERSIST | Restart daemon | State identique |
| T1-INVARIANT | Après chaque op | `C == U + Z` |

### 1.4 Formule de Conservation

```
À tout moment sur Testnet 1:

PREMINE = wallet_PIV + C + total_fees

Cette équation DOIT être vraie après chaque opération!
```

### 1.5 Critères GO vers Testnet 2

| Critère | Requis |
|---------|--------|
| Pipeline MINT/REDEEM | 100% exact |
| Pipeline STAKE/UNSTAKE | 100% exact |
| R% yield distribution | Calculs exacts |
| Invariants C==U+Z | 0 violation |
| Persistence | OK après restart |
| Stabilité | 7 jours sans crash |

**GO Testnet 2 uniquement si TOUS = OK**

---

## TESTNET 2: "KHU + V6 PIVX"

### 2.1 Objectif

Valider que le système KHU (déjà prouvé sur Testnet 1) est **compatible** avec l'écosystème V6 PIVX complet:
- Transition émission 4/6/10 → 6/6/6
- Décroissance annuelle -1
- Finalité LLMQ
- DOMC gouvernance
- DAO Treasury

### 2.2 Configuration Testnet 2

```cpp
// chainparams.cpp - Testnet 2 "KHU + V6"
class CTestNetKHU_V6 : public CChainParams {
public:
    CTestNetKHU_V6() {
        strNetworkID = "khu-testnet-2";

        // V6 activation à un bloc spécifique
        consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 1000;

        // ÉMISSION PRÉ-V6: 4/6/10 (Staker/MN/DAO)
        consensus.nPreV6BlockReward = {4, 6, 10};  // Total 20 PIV/bloc

        // ÉMISSION POST-V6: 6/6/6 puis -1/an
        consensus.nPostV6BlockReward = {6, 6, 6};  // Total 18 PIV/bloc
        consensus.nEmissionDecay = 1;              // -1 par rôle par an

        // ACCÉLÉRÉ: 1 an = 1000 blocs (au lieu de 525600)
        consensus.nBlocksPerYear = 1000;  // Pour tests rapides

        // Finalité LLMQ
        consensus.nLLMQEnabled = true;
        consensus.nFinalityDepth = 12;

        // DOMC (cycles courts)
        consensus.nDOMCEnabled = true;
        consensus.nDOMCCycleLength = 1000;  // Au lieu de 172800

        // DAO Treasury
        consensus.nDAOTreasuryEnabled = true;
    }
};
```

### 2.3 Timeline Émission (Accélérée)

```
Blocs 0-999:     Pré-V6
                 Émission: 4/6/10 = 20 PIV/bloc
                 KHU: désactivé

Bloc 1000:       V6 ACTIVATION
                 Émission: 6/6/6 = 18 PIV/bloc (année 0)
                 KHU: activé
                 R%: 40% initial

Blocs 1000-1999: Année 0
                 Émission: 6/6/6 = 18 PIV/bloc

Blocs 2000-2999: Année 1
                 Émission: 5/5/5 = 15 PIV/bloc
                 Premier DOMC cycle complet

Blocs 3000-3999: Année 2
                 Émission: 4/4/4 = 12 PIV/bloc

Blocs 4000-4999: Année 3
                 Émission: 3/3/3 = 9 PIV/bloc

Blocs 5000-5999: Année 4
                 Émission: 2/2/2 = 6 PIV/bloc

Blocs 6000-6999: Année 5
                 Émission: 1/1/1 = 3 PIV/bloc

Blocs 7000+:     Année 6+
                 Émission: 0/0/0 = 0 PIV/bloc
                 DAO financé uniquement par T
```

### 2.4 Tests Testnet 2

| Test | Description | Bloc(s) |
|------|-------------|---------|
| T2-PRE-V6 | Émission 4/6/10 fonctionne | 1-999 |
| T2-TRANSITION | KHU state initialisé à V6 | 1000 |
| T2-POST-V6 | Émission 6/6/6 + KHU cohabitent | 1001+ |
| T2-DECAY | Émission décroît correctement | 2000, 3000... |
| T2-YEAR6 | Émission = 0 après année 5 | 7000+ |
| T2-FINALITY | Reorg > 12 blocs bloqué | Continu |
| T2-DOMC | Vote R% fonctionne | Cycle 1+ |
| T2-TREASURY | T accumule 2%/an | Continu |

### 2.5 Critères GO vers Mainnet

| Critère | Requis |
|---------|--------|
| Transition 4/6/10 → 6/6/6 | OK |
| 6 années d'émission simulées | OK |
| KHU + émission en parallèle | Pas d'interférence |
| Finalité LLMQ | Bloque reorgs |
| DOMC cycle complet | R% change |
| DAO Treasury | T accumule |
| Stabilité | 30 jours (~1 an simulé) |

---

## Infrastructure

### Testnet 1 (Simple)

```
┌─────────────────────┐
│  1 Node + 3 Wallets │
│  (regtest-like)     │
├─────────────────────┤
│  wallet_genesis.dat │  ← 1M PIV premine
│  wallet_test1.dat   │
│  wallet_test2.dat   │
└─────────────────────┘
```

### Testnet 2 (Multi-Node)

```
┌─────────────────────────────────────────────────────────────────────┐
│                         VPS TESTNET 2                               │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐             │
│  │   NODE 1    │◄──►│   NODE 2    │◄──►│   NODE 3    │             │
│  │   Staker    │    │ Masternode  │    │  Explorer   │             │
│  │ (génère)    │    │ (vote DOMC) │    │ (txindex)   │             │
│  └─────────────┘    └─────────────┘    └─────────────┘             │
│                                                                      │
│  Finalité LLMQ entre les 3 nodes                                    │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Timeline Globale

```
Semaine 1-4:   TESTNET 1
               • Setup et tests pipeline
               • Validation R% yield
               • 7 jours stabilité
               → GO/NO-GO

Semaine 5-8:   TESTNET 2 Setup
               • Multi-node avec finalité
               • Tests transition V6

Semaine 9-12:  TESTNET 2 Année 0-2 simulées
               • Émission 6/6/6 → 5/5/5 → 4/4/4
               • Premier DOMC cycle

Semaine 13-16: TESTNET 2 Année 3-6 simulées
               • Émission décroît vers 0
               • DAO Treasury devient source principale

Semaine 17+:   Testnet public ouvert
               • Utilisateurs externes
               • Bug bounty
```

---

## Résumé

| Testnet | Focus | Émission | Durée |
|---------|-------|----------|-------|
| **1** | Système KHU pur | ZERO | 4 semaines |
| **2** | Compatibilité V6 | 4/6/10 → 6/6/6 -1 | ~1 an simulé |

**Principe:**
1. D'abord prouver que KHU marche (Testnet 1)
2. Ensuite prouver qu'il est compatible V6 (Testnet 2)

---

**Document créé:** 2025-11-27
**Auteur:** Architecture KHU
**Prochaine révision:** Après validation Testnet 1
