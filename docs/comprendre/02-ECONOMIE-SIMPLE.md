# Économie KHU (Version Simple)

## D'où vient le rendement?

**Le rendement (yield) est de l'inflation contrôlée.**

Quand tu UNSTAKE avec du rendement, de nouveaux PIV sont créés pour te payer.

```
Tu stakes 1000 KHU pendant 1 an à 40%
Tu UNSTAKE → Tu reçois 1400 KHU
Tu REDEEM → Tu reçois 1400 PIV

Les 400 PIV de rendement sont CRÉÉS à ce moment (inflation)
```

## Pourquoi c'est pas un problème?

1. **R% décroît** — De 40% à 7% sur 33 ans
2. **Émission PIV décroît** — De 18 PIV/bloc à 0 en 6 ans
3. **Les deux se compensent** — Inflation totale reste raisonnable

## Le calcul du rendement

```
Rendement quotidien = (Principal × R% / 100) / 365

Exemple avec 10,000 KHU à 40%:
= (10,000 × 40 / 100) / 365
= 4,000 / 365
= 10.96 KHU par jour
```

## Timeline d'un stake

```
Jour 0:    STAKE 10,000 KHU
           ↓
Jour 1:    Maturity (pas de yield)
Jour 2:    Maturity (pas de yield)
Jour 3:    Maturity (pas de yield)
           ↓
Jour 4:    +10.96 KHU (yield commence!)
Jour 5:    +10.96 KHU
Jour 6:    +10.96 KHU
...
Jour 30:   +10.96 KHU
           ↓
UNSTAKE:   10,000 + (27 × 10.96) = 10,296 KHU
```

## Les 3 jours de maturity

**Pourquoi attendre 3 jours?**

- Empêche le "flash staking" (stake → unstake immédiat)
- Force un engagement minimum
- Protège le réseau contre les abus

## Qui décide du taux R%?

**Les masternodes votent tous les 4 mois (cycle DOMC).**

```
CYCLE DOMC (4 mois = 172800 blocs)
══════════════════════════════════════════════════════════════

0────────────132480────────152640────────172800
│              │              │              │
│              │    VOTE      │  ADAPTATION  │
│              │  (2 sem)     │   (2 sem)    │
│              │   commits    │              │
│              │   secrets    │  REVEAL      │
│              │              │  instantané  │
│              │              │  ↓           │
│              │              │  Futur R%    │
│              │              │  visible     │
│                                            │
├────────────────────────────────────────────┤
│     R% ACTIF PENDANT TOUT LE CYCLE         │
│              (4 mois)                      │
└────────────────────────────────────────────┴─────►
                                             │
                                   Nouveau R% activé
```

**Le processus:**
1. **R% actif 4 mois complets** (jamais interrompu)
2. **Mois 1-3**: R% actif, pas de vote
3. **Semaines 13-14**: VOTE (commits secrets), R% toujours actif
4. **Bloc 152640**: REVEAL instantané → futur R% visible
5. **Semaines 15-16**: ADAPTATION (tout le monde s'adapte)
6. **Fin du cycle**: Nouveau R% activé automatiquement

## R_MAX — Le Plafond du Vote

**R_MAX décroît automatiquement de 1% par an jusqu'au plancher de 7%.**

```
R_MAX = max(7%, 40% - année×1%)

Année 0:  R_MAX = 40%  → MN peuvent voter entre 0% et 40%
Année 10: R_MAX = 30%  → MN peuvent voter entre 0% et 30%
Année 20: R_MAX = 20%  → MN peuvent voter entre 0% et 20%
Année 33+: R_MAX = 7%  → MN peuvent voter entre 0% et 7% (plancher définitif)
```

**Important:**
- Les MN **décident** du R% réel (ce n'est pas automatique)
- Le protocole **limite** le vote maximum via R_MAX (ça c'est automatique)
- Après le vote → R% **appliqué automatiquement**

## Résumé

| Question | Réponse |
|----------|---------|
| D'où vient le yield? | Inflation (nouveaux PIV créés) |
| Combien? | R% par an (commence à 40%) |
| Qui décide R%? | Les masternodes votent |
| Quand le yield commence? | Après 3 jours de maturity |
| Le yield est-il garanti? | Oui, tant que tu stakes assez longtemps |
