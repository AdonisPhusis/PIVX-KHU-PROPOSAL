# 01 — PIVX V6 BLOCK REWARD BLUEPRINT

**Date:** 2025-11-27
**Version:** 2.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce blueprint définit le **nouveau système de block reward PIVX V6** :

**RÈGLE UNIQUE : À l'activation V6, le block reward passe à 0 PIV IMMÉDIATEMENT.**

Pas de transition progressive. Pas de schedule. Pas de formule. **Zéro immédiat.**

---

## 1. CHANGEMENT FONDAMENTAL : V5.6 → V6

### 1.1 Système Actuel PIVX V5.6

```
┌─────────────────────────────────────────────┐
│ PIVX V5.6 — Distribution Actuelle           │
├─────────────────────────────────────────────┤
│ Staker:      6 PIV par bloc                 │
│ Masternode:  4 PIV par bloc                 │
│ DAO:        10 PIV par bloc                 │
├─────────────────────────────────────────────┤
│ TOTAL:      20 PIV par bloc                 │
└─────────────────────────────────────────────┘
```

### 1.2 Nouveau Système PIVX V6-KHU

```
┌─────────────────────────────────────────────┐
│ PIVX V6-KHU — Block Reward = 0              │
├─────────────────────────────────────────────┤
│ Staker:      0 PIV par bloc                 │
│ Masternode:  0 PIV par bloc                 │
│ DAO:         0 PIV par bloc                 │
├─────────────────────────────────────────────┤
│ TOTAL:       0 PIV par bloc                 │
└─────────────────────────────────────────────┘

L'économie post-V6 est gouvernée par :
  • Yield KHU (R%) : ZKHU stakers reçoivent R% annuel
  • Treasury (T) : DAO reçoit U × R% / 8 / 365 par jour
```

### 1.3 Transition

```
Bloc V6 - 1 : 6 + 4 + 10 = 20 PIV/bloc (V5.6)
Bloc V6     : 0 + 0 + 0  =  0 PIV/bloc (V6) ← IMMÉDIAT
Bloc V6 + 1 : 0 + 0 + 0  =  0 PIV/bloc
...
```

**Aucune période de transition. Aucun schedule. Changement brutal.**

---

## 2. POURQUOI ZÉRO IMMÉDIAT ?

### 2.1 Philosophie

L'émission PIVX traditionnelle (block rewards) est remplacée par un système plus sophistiqué :

1. **Yield R%** : Les stakers ZKHU reçoivent un rendement annuel gouverné par DOMC
2. **Treasury T** : Le DAO reçoit un pourcentage indexé sur l'activité économique
3. **Pas d'inflation arbitraire** : Seuls les participants actifs (stakers KHU) créent de la valeur

### 2.2 Avantages

```
V5.6 (Ancien) :
  ❌ Inflation fixe 20 PIV/bloc (~10.5M PIV/an)
  ❌ Tout le monde reçoit, même inactifs
  ❌ Pas de gouvernance sur émission

V6 (Nouveau) :
  ✅ Inflation zéro (block rewards)
  ✅ Seuls les stakers ZKHU reçoivent du yield
  ✅ R% gouverné par masternodes (DOMC)
  ✅ Économie basée sur participation active
```

---

## 3. IMPLÉMENTATION

### 3.1 Code Canonique (validation.cpp)

```cpp
CAmount GetBlockValue(int nHeight)
{
    const Consensus::Params& consensus = Params().GetConsensus();

    // KHU V6.0: ZERO BLOCK REWARD IMMEDIATELY
    // At V6 activation, block reward = 0 PIV (no transition, immediate zero)
    // Economy is governed by:
    //   - R% yield for ZKHU stakers (40%→7% over 33 years via DOMC)
    //   - T (DAO Treasury) accumulates from U × R% / 8 / 365
    if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_V6_0)) {
        return 0;  // Zero emission immediately at V6 activation
    }

    // Pre-V6 legacy rewards (PIVX V5.6)
    // ... existing code for V5.6 rewards
}
```

### 3.2 Vérification

```cpp
// Dans ConnectBlock ou validation
bool VerifyBlockReward(int nHeight, CAmount actualReward)
{
    const Consensus::Params& consensus = Params().GetConsensus();

    if (NetworkUpgradeActive(nHeight, consensus, Consensus::UPGRADE_V6_0)) {
        // Post-V6: block reward MUST be 0
        return actualReward == 0;
    }

    // Pre-V6: use legacy validation
    return ValidateLegacyReward(nHeight, actualReward);
}
```

---

## 4. ÉCONOMIE POST-V6

### 4.1 Sources de Récompenses

```
┌────────────────────────────────────────────────────────────────┐
│ POST-V6 : ÉCONOMIE GOUVERNÉE PAR R%                            │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│ Block Reward = 0 PIV (aucune émission traditionnelle)          │
│                                                                 │
│ Yield ZKHU :                                                    │
│   • Formule : Z × R% / 365 par jour                            │
│   • Bénéficiaires : Stakers ZKHU uniquement                    │
│   • R% initial : 40% (4000 basis points)                       │
│   • R% gouverné par DOMC (vote masternodes)                    │
│                                                                 │
│ Treasury (T) :                                                  │
│   • Formule : U × R% / 8 / 365 par jour                        │
│   • Bénéficiaire : DAO Treasury Pool                           │
│   • ~5% de U par an (quand R%=40%)                             │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

### 4.2 R% Timeline

```
R_MAX_dynamic = max(700, 4000 - year × 100)

Année 0  : R_MAX = 40% (4000 bp)
Année 10 : R_MAX = 30% (3000 bp)
Année 20 : R_MAX = 20% (2000 bp)
Année 30 : R_MAX = 10% (1000 bp)
Année 33+: R_MAX = 7%  (700 bp) ← Plancher
```

### 4.3 Comparaison Inflation

```
V5.6 : ~10.5M PIV/an (fixe, block rewards)

V6 (scénario 50/50, R%=40%) :
  • Yield : ~20% inflation (sur ZKHU seulement)
  • Effective : ~10% si 50% du supply est staké
  • Treasury : ~5% de U par an

→ Inflation V6 est VARIABLE et GOUVERNABLE
→ Dépend du ratio staking et du vote DOMC
```

---

## 5. SÉPARATION STRICTE

### 5.1 Deux Systèmes Indépendants

```
┌─────────────────────────────────────────┐
│ BLOCK REWARD (V6)                       │
├─────────────────────────────────────────┤
│ • Valeur : 0 PIV (toujours)             │
│ • NON-GOUVERNABLE                       │
│ • Pas de schedule                       │
│ • Pas de transition                     │
│ • ❌ DOMC ne peut PAS modifier          │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ YIELD KHU (R%)                          │
├─────────────────────────────────────────┤
│ • Récompense staking ZKHU               │
│ • Variable [0, R_MAX_dynamic]           │
│ • GOUVERNABLE (DOMC vote)               │
│ • Contrôle rendement staking            │
│ • ❌ N'affecte PAS block reward         │
└─────────────────────────────────────────┘
```

**INTERDICTION ABSOLUE : Ces deux systèmes sont définitivement séparés.**

---

## 6. INTERDICTIONS ABSOLUES

### 6.1 Code Interdit

```cpp
// ❌ INTERDIT : Schedule progressif
int64_t reward = std::max(6 - year, 0) * COIN;  // ❌ JAMAIS !

// ❌ INTERDIT : Transition douce
if (nHeight < V6_ACTIVATION + TRANSITION_PERIOD) {
    reward = gradualDecrease(nHeight);  // ❌ JAMAIS !
}

// ❌ INTERDIT : Permettre block reward non-zéro post-V6
if (NetworkUpgradeActive(nHeight, consensus, UPGRADE_V6_0)) {
    return legacyReward;  // ❌ JAMAIS ! Doit retourner 0
}

// ❌ INTERDIT : DOMC sur block reward
if (domc_vote_increase_emission) {
    block_reward += bonus;  // ❌ JAMAIS !
}
```

### 6.2 Modifications Interdites

```
❌ Ajouter un schedule d'émission (6→5→4→...→0)
❌ Créer une période de transition
❌ Permettre DOMC de modifier le block reward
❌ Réintroduire des block rewards après V6
❌ Confondre Yield R% avec block reward
```

---

## 7. TESTS

### 7.1 Test Unitaire

```cpp
BOOST_AUTO_TEST_CASE(test_v6_zero_block_reward)
{
    // Avant V6 : reward V5.6 standard
    BOOST_CHECK(GetBlockValue(V6_ACTIVATION - 1) > 0);

    // À V6 : ZÉRO immédiat
    BOOST_CHECK_EQUAL(GetBlockValue(V6_ACTIVATION), 0);

    // Après V6 : toujours ZÉRO
    BOOST_CHECK_EQUAL(GetBlockValue(V6_ACTIVATION + 1), 0);
    BOOST_CHECK_EQUAL(GetBlockValue(V6_ACTIVATION + 1000000), 0);
}
```

### 7.2 Test Fonctionnel

```python
def test_v6_zero_emission():
    # Generate blocks before V6
    pre_v6_reward = node.getblock(node.getblockhash(V6_ACTIVATION - 1))['reward']
    assert pre_v6_reward > 0, "Pre-V6 should have rewards"

    # Generate blocks at/after V6
    v6_reward = node.getblock(node.getblockhash(V6_ACTIVATION))['reward']
    assert v6_reward == 0, "V6 block reward must be 0"

    post_v6_reward = node.getblock(node.getblockhash(V6_ACTIVATION + 100))['reward']
    assert post_v6_reward == 0, "Post-V6 block reward must be 0"
```

---

## 8. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règle fondamentale (NON-NÉGOCIABLE) :**

```
À l'activation V6 : Block Reward = 0 PIV
                    Immédiatement
                    Pour toujours
                    Non-gouvernable
```

L'économie post-V6 est entièrement gouvernée par R% (yield ZKHU) et T (treasury).

---

**FIN DU BLUEPRINT**

**Version:** 2.0
**Date:** 2025-11-27
**Status:** CANONIQUE
**Changelog:**
- v2.0: Réécriture complète - Block reward = 0 immédiat (suppression schedule 6→0)
- v1.0: Version originale avec schedule progressif (OBSOLÈTE)
