# Économie KHU (Version Simple)

## D'où vient le rendement?

**Le rendement (yield) est de l'inflation contrôlée.**

Quand tu UNSTAKE avec du rendement, de nouveaux PIV sont créés pour te payer.

```
Tu stakes 1000 KHU pendant 1 an à 37%
Tu UNSTAKE → Tu reçois 1370 KHU
Tu REDEEM → Tu reçois 1370 PIV

Les 370 PIV de rendement sont CRÉÉS à ce moment (inflation)
```

## Pourquoi c'est pas un problème?

1. **R% décroît** — De 37% à 4% sur 33 ans
2. **Émission PIV décroît** — De 18 PIV/bloc à 0 en 6 ans
3. **Les deux se compensent** — Inflation totale reste raisonnable

## Le calcul du rendement

```
Rendement quotidien = (Principal × R% / 100) / 365

Exemple avec 10,000 KHU à 37%:
= (10,000 × 37 / 100) / 365
= 3,700 / 365
= 10.14 KHU par jour
```

## Timeline d'un stake

```
Jour 0:    STAKE 10,000 KHU
           ↓
Jour 1:    Maturity (pas de yield)
Jour 2:    Maturity (pas de yield)
Jour 3:    Maturity (pas de yield)
           ↓
Jour 4:    +10.14 KHU (yield commence!)
Jour 5:    +10.14 KHU
Jour 6:    +10.14 KHU
...
Jour 30:   +10.14 KHU
           ↓
UNSTAKE:   10,000 + (27 × 10.14) = 10,274 KHU
```

## Les 3 jours de maturity

**Pourquoi attendre 3 jours?**

- Empêche le "flash staking" (stake → unstake immédiat)
- Force un engagement minimum
- Protège le réseau contre les abus

## Qui décide du taux R%?

**Les masternodes votent tous les 4 mois.**

1. Phase COMMIT (2 semaines): Les MN soumettent leur vote en secret
2. Phase REVEAL (2 semaines): Les votes sont révélés
3. Nouveau R% = Médiane des votes

## Limites du taux R%

```
R_MAX = max(4%, 37% - année×1%)

Année 0:  R_MAX = 37%
Année 10: R_MAX = 27%
Année 20: R_MAX = 17%
Année 33: R_MAX = 4% (minimum permanent)
```

## Résumé

| Question | Réponse |
|----------|---------|
| D'où vient le yield? | Inflation (nouveaux PIV créés) |
| Combien? | R% par an (commence à 37%) |
| Qui décide R%? | Les masternodes votent |
| Quand le yield commence? | Après 3 jours de maturity |
| Le yield est-il garanti? | Oui, tant que tu stakes assez longtemps |
