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

- Commence à **37%** par an
- Les masternodes votent pour l'ajuster tous les 4 mois
- Maximum décroît avec le temps (37% → 4% sur 33 ans)

## Exemple concret

```
Alice a 10,000 PIV

Jour 0: MINT 10,000 PIV → reçoit 10,000 KHU
Jour 0: STAKE 10,000 KHU → devient 10,000 ZKHU (privé)

Jour 1-3: Période de maturity (pas de rendement)

Jour 4-30: Rendement s'accumule
  - R = 37% par an = 0.1% par jour
  - 27 jours × 10 KHU/jour = 270 KHU de rendement

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
