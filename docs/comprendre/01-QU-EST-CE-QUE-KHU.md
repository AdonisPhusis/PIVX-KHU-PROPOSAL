# Qu'est-ce que KHU?

**KHU = Knowledge Hedge Unit**

## En une phrase

KHU est un **jeton collatéralisé 1:1 par PIV** qui permet de gagner du rendement via le staking privé.

## Comment ça marche (simplifié)

```
1. Tu as 1000 PIV
2. Tu MINT → Tu reçois 1000 KHU (ton PIV est verrouillé)
3. Tu STAKE tes KHU → Ils deviennent privés (ZKHU)
4. Tu attends 3+ jours → Tu accumules du rendement (R% par an)
5. Tu UNSTAKE → Tu récupères tes KHU + le rendement gagné
6. Tu REDEEM → Tu récupères tes PIV (+ le rendement en PIV)
```

## Les 4 opérations

| Opération | Ce que tu fais | Ce que tu reçois |
|-----------|----------------|------------------|
| **MINT** | Tu donnes PIV | Tu reçois KHU |
| **STAKE** | Tu donnes KHU | Tu reçois ZKHU (privé) |
| **UNSTAKE** | Tu révèles ZKHU | Tu reçois KHU + rendement |
| **REDEEM** | Tu donnes KHU | Tu récupères PIV |

## Pourquoi staker?

- **Rendement**: Tu gagnes R% par an (voté par les masternodes)
- **Privacité**: Personne ne sait combien tu as staké (ZKHU = privé)
- **Sécurité**: Ton PIV reste collatéralisé, tu peux toujours le récupérer

## La règle des 3 jours

**Tu dois attendre 3 jours (4320 blocs) après avoir staké avant de gagner du rendement.**

Pourquoi? Pour éviter les abus (stake → unstake immédiat).

## Le taux R%

- **R% initial = 40%** par an (à l'activation V6)
- **R% actif pendant 4 mois complets** (cycle DOMC)
- **Les MN votent R% entre 0% et R_MAX**
- **À la fin du cycle**: Nouveau R% activé automatiquement
- **R_MAX décroît automatiquement**: 40% → 7% sur 33 ans (plancher)

```
CYCLE DOMC (4 mois = 172800 blocs)
══════════════════════════════════════════════════════════════

0────────────132480────────152640────────172800
│              │              │              │
│              │    VOTE      │  ADAPTATION  │
│              │  (2 sem)     │   (2 sem)    │
│              │              │              │
│              │              │ REVEAL       │
│              │              │ instantané   │
│              │              │ ↓            │
│              │              │ Futur R%     │
│              │              │ visible      │
│                                            │
├────────────────────────────────────────────┤
│     R% ACTIF PENDANT TOUT LE CYCLE         │
│              (4 mois)                      │
└────────────────────────────────────────────┴─────►
                                             │
                                   Nouveau R% activé


R_MAX (plafond du vote) décroît avec le temps:
  Année 0:   R_MAX = 40%  → MN votent entre 0% et 40%
  Année 10:  R_MAX = 30%  → MN votent entre 0% et 30%
  Année 20:  R_MAX = 20%  → MN votent entre 0% et 20%
  Année 33+: R_MAX = 7%   → MN votent entre 0% et 7%
```

## Exemple concret

```
Alice a 10,000 PIV

Jour 0: MINT 10,000 PIV → reçoit 10,000 KHU
Jour 0: STAKE 10,000 KHU → devient 10,000 ZKHU (privé)

Jour 1-3: Période de maturity (pas de rendement)

Jour 4-30: Rendement s'accumule
  - R = 40% par an = ~0.11% par jour
  - 27 jours × 10.96 KHU/jour = ~296 KHU de rendement

Jour 30: UNSTAKE → reçoit 10,270 KHU
Jour 30: REDEEM → reçoit 10,270 PIV

Gain net: +270 PIV (2.7% en 1 mois)
```

## Ce que KHU n'est PAS

- ❌ **Pas un stablecoin** (pas de peg USD/EUR)
- ❌ **Pas de l'argent magique** (le rendement vient de l'inflation contrôlée)
- ❌ **Pas anonyme à 100%** (MINT/REDEEM sont publics, seul STAKE est privé)

## Résumé

**KHU = ton PIV qui travaille pour toi, en privé.**

Tu lock ton PIV, tu stakes, tu attends, tu gagnes du rendement, tu récupères tout.
