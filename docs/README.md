# PIVX-V6-KHU Documentation

## Structure

```
docs/
├── README.md           ← Index (tu es ici)
│
├── SPEC.md             ← Spécification canonique
├── ARCHITECTURE.md     ← Architecture technique
├── ROADMAP.md          ← Phases et statut
├── IMPLEMENTATION.md   ← Guide implémentation
│
├── comprendre/         ← Pour les normies
│   ├── 01-QU-EST-CE-QUE-KHU.md
│   └── 02-ECONOMIE-SIMPLE.md
│
├── blueprints/         ← Détails par feature
│
└── archive/            ← Documents historiques
```

---

## Documents Essentiels

| Document | Rôle UNIQUE |
|----------|-------------|
| **[SPEC.md](SPEC.md)** | Règles MATHÉMATIQUES (formules, invariants, constantes) |
| **[ARCHITECTURE.md](ARCHITECTURE.md)** | Structure CODE (fichiers, patterns, LevelDB) |
| **[ROADMAP.md](ROADMAP.md)** | Statut des phases et progression |
| **[IMPLEMENTATION.md](IMPLEMENTATION.md)** | Guide détaillé développeurs |

> **Pas de redondance:** SPEC.md = QUOI (règles), ARCHITECTURE.md = OÙ (code)

---

## Comprendre KHU (Normies)

- [Qu'est-ce que KHU?](comprendre/01-QU-EST-CE-QUE-KHU.md)
- [Économie Simple](comprendre/02-ECONOMIE-SIMPLE.md)

---

## Quick Reference

### Invariants

```
C == U + Z      // Collateral = Transparent + Shielded
Cr == Ur        // Reward pool = Reward rights
```

### Opérations

| Op | Effet |
|----|-------|
| MINT | C+, U+ |
| REDEEM | C-, U- |
| STAKE | U-, Z+ |
| UNSTAKE | Z-, U+(P+Y), C+Y, Cr-Y, Ur-Y |

### Constantes

```
Maturity:       4320 blocs (3 jours)
R% initial:     37%
DOMC cycle:     172800 blocs (4 mois)
Finality:       12 blocs
```

---

## Fichier Principal

**`/CLAUDE.md`** à la racine contient toutes les règles de développement.

---

*Dernière mise à jour: Novembre 2025*
