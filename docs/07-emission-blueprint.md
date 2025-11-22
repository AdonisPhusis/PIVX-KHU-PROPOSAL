# 🔥 SOUS-BLUEPRINT ÉMISSION PIVX — RÈGLE IMMUABLE

**VERSION FINALE — INVIOLABLE**

---

## 1. RÈGLE CANONIQUE

L'émission PIVX suit une règle mathématique simple et **IMMUABLE** :

```cpp
BLOCKS_PER_YEAR = 525600  // 1 bloc/minute × 60 × 24 × 365
year = (height - activation_height) / BLOCKS_PER_YEAR
reward_year = max(6 - year, 0)  // PIV par bloc et par compartiment
```

---

## 2. RÉPARTITION ÉGALE (3 COMPARTIMENTS)

Chaque bloc génère **3 récompenses identiques** :

```cpp
staker_reward = reward_year  // PIV
mn_reward     = reward_year  // PIV
dao_reward    = reward_year  // PIV
```

**Pas de priorité. Pas de bonus. Pas de modulation.**

---

## 3. EXTINCTION TOTALE APRÈS 6 ANS

| Année | reward_year | Émission/bloc (total) |
|-------|-------------|----------------------|
| 0     | 6 PIV       | 18 PIV               |
| 1     | 5 PIV       | 15 PIV               |
| 2     | 4 PIV       | 12 PIV               |
| 3     | 3 PIV       | 9 PIV                |
| 4     | 2 PIV       | 6 PIV                |
| 5     | 1 PIV       | 3 PIV                |
| **≥6**| **0 PIV**   | **0 PIV**            |

**Après year >= 6 : émission = 0. Définitif.**

---

## 4. INVIOLABILITÉ

Cette règle **NE PEUT PAS** être modifiée par :

- ❌ Gouvernance DOMC
- ❌ Vote masternode
- ❌ Paramètre runtime
- ❌ Soft fork
- ❌ Proposition DAO

**Seul un hard fork avec consensus communautaire peut changer cette règle.**

---

## 5. ANTI-DÉRIVE — INTERDICTIONS ABSOLUES

### 5.1 Interdictions de conception

- ❌ **Pas de R% dans l'émission** : R% contrôle UNIQUEMENT le yield Cr/Ur, jamais l'émission PIVX
- ❌ **Pas de brûlage KHU** : KHU ne se brûle jamais (seul REDEEM détruit du KHU)
- ❌ **Pas de bonus masternode** : staker == mn == dao, toujours
- ❌ **Pas de seuil de sécurité** : pas de "si Cr trop bas alors..."
- ❌ **Pas d'émission fractionnée** : reward_year est un entier PIV
- ❌ **Pas de table non linéaire** : max(6 - year, 0) est la seule formule
- ❌ **Pas d'émission par bloc dynamique** : pas de modulation selon hashrate/staking/etc

### 5.2 Interdictions d'implémentation

- ❌ **Pas d'interpolation** : pas de transition douce entre années
- ❌ **Pas de year_fraction** : année entière uniquement (division floor)
- ❌ **Pas d'optimisation** : ne jamais "améliorer" la formule
- ❌ **Pas de table alternative** : aucune lookup table, aucun cache pré-calculé
- ❌ **Pas de stratégie DOMC sur émission** : DOMC ne touche QUE R%

### 5.3 Interdictions de test

- ❌ **Pas de fuzzing de reward_year** : la formule est sacrée, on ne teste pas des variantes
- ❌ **Pas de "que se passe-t-il si reward_year > 6"** : impossible par construction

---

## 6. IMPLÉMENTATION C++ (RÉFÉRENCE)

```cpp
// validation.cpp ou miner.cpp
static const int BLOCKS_PER_YEAR = 525600;

CAmount GetBlockSubsidy(int nHeight, int activation_height)
{
    if (nHeight < activation_height) {
        return 0;  // Avant activation KHU
    }

    int year = (nHeight - activation_height) / BLOCKS_PER_YEAR;
    int reward_year = std::max(6 - year, 0);

    return reward_year * COIN;  // Retourne PIV en satoshis
}

// Répartition dans ConnectBlock:
CAmount staker_reward = GetBlockSubsidy(nHeight, activation_height);
CAmount mn_reward = staker_reward;
CAmount dao_reward = staker_reward;
```

**Aucune autre logique. Aucune exception. Aucun cas particulier.**

**TIMING CRITIQUE — Ordre d'application dans ConnectBlock:**

L'application des récompenses d'émission DOIT se faire **APRÈS** les opérations KHU et le yield scheduler:

```
1. ApplyDailyYieldIfNeeded()   // Phase 3
2. ProcessKHUTransactions()     // Phase 2+
3. ApplyBlockReward()           // ← ÉMISSION ICI (Phase 1)
4. CheckInvariants()
5. PersistState()
```

**Raison:** Les récompenses d'émission ne modifient PAS l'état KHU (C, U, Cr, Ur).
Elles sont distribuées en PIV pur, indépendamment du système KHU.

**Référence:** Voir `06-PROTOCOL-REFERENCE.md` section "Order of Operations in ConnectBlock".

---

## 7. TESTS OBLIGATOIRES

### 7.1 Test unitaire : Progression annuelle

```cpp
BOOST_AUTO_TEST_CASE(emission_pivx_annual_decay)
{
    int activation = 1000;

    // Year 0: 6 PIV
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation, activation), 6 * COIN);
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525599, activation), 6 * COIN);

    // Year 1: 5 PIV
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525600, activation), 5 * COIN);

    // Year 2: 4 PIV
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525600*2, activation), 4 * COIN);

    // Year 5: 1 PIV
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525600*5, activation), 1 * COIN);

    // Year 6: 0 PIV
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525600*6, activation), 0);

    // Year 100: 0 PIV (extinction permanente)
    BOOST_CHECK_EQUAL(GetBlockSubsidy(activation + 525600*100, activation), 0);
}
```

### 7.2 Test unitaire : Répartition égale

```cpp
BOOST_AUTO_TEST_CASE(emission_pivx_equal_distribution)
{
    int activation = 1000;
    int height = activation + 100000;  // Year 0

    CAmount subsidy = GetBlockSubsidy(height, activation);
    CAmount staker = subsidy;
    CAmount mn = subsidy;
    CAmount dao = subsidy;

    BOOST_CHECK_EQUAL(staker, mn);
    BOOST_CHECK_EQUAL(mn, dao);
    BOOST_CHECK_EQUAL(staker, 6 * COIN);
}
```

### 7.3 Test fonctionnel : Vérification sur 7 ans

Script Python vérifiant que sur 7 années complètes :
- Émission totale = (6+5+4+3+2+1+0) × 525600 × 3 compartiments
- Pas d'émission après year 6
- Chaque compartiment reçoit exactement 1/3

---

## 8. COHÉRENCE AVEC KHU

**L'émission PIVX et le système KHU sont INDÉPENDANTS :**

| Système       | Variable contrôlée | Gouvernance |
|---------------|--------------------|-------------|
| **Émission PIVX** | reward_year    | AUCUNE (hard-codée) |
| **Yield KHU**     | R%             | DOMC annuel |

**Séparation stricte :**
- PIVX emission → offre totale PIV (déflationnaire)
- KHU yield → rewards staking (Cr/Ur, neutre sur supply)

**UNSTAKE donne du KHU, jamais du PIV.**

---

## 9. VALIDATION FORMELLE

**Propriété P1 (Monotonicité décroissante) :**
```
∀ h1, h2 : h1 < h2 ⇒ reward(h1) ≥ reward(h2)
```

**Propriété P2 (Extinction certaine) :**
```
∀ h ≥ activation + 6×525600 : reward(h) = 0
```

**Propriété P3 (Égalité compartimentaire) :**
```
∀ h : staker_reward(h) = mn_reward(h) = dao_reward(h)
```

---

## 10. CLAUSE DE NON-MODIFICATION

**Ce document est VERROUILLÉ.**

Toute modification de la règle d'émission PIVX dans le code, la documentation ou les tests sans mise à jour correspondante de ce document est une **violation de consensus**.

**Toute "optimisation", "amélioration" ou "ajustement" de cette formule est INTERDIT.**

---

**FIN DU SOUS-BLUEPRINT ÉMISSION**

🔒 **IMMUABLE. INVIOLABLE. VÉRIFIÉ À CHAQUE PHASE.**
