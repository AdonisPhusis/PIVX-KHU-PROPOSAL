# 07 — MODÈLE ÉCONOMIQUE SIMPLIFIÉ

**Version:** 1.0
**Status:** PÉDAGOGIQUE

---

## 1. CE QUI CHANGE (4/6/10 → 6/6/6 → 0/0/0)

### Ancien Système (PIVX v5)
```
Par bloc (perpétuel, jamais de fin):
  Staker:      4 PIV   (défavorisé)
  Masternode:  6 PIV
  DAO:        10 PIV   (favorisé)
  ─────────────────────
  TOTAL:      20 PIV/bloc (inflation constante)
```

### Nouveau Système (v6-KHU)
```
Par bloc (6 ans puis FIN):
  Staker:      6 PIV → 5 → 4 → 3 → 2 → 1 → 0
  Masternode:  6 PIV → 5 → 4 → 3 → 2 → 1 → 0
  DAO:         6 PIV → 5 → 4 → 3 → 2 → 1 → 0
  ─────────────────────────────────────────────
  TOTAL:      18 PIV/bloc → 15 → 12 → 9 → 6 → 3 → 0

  Après 6 ans: ÉMISSION = 0 PIV/bloc (fin définitive)
```

**Résultat:** Égalité parfaite + fin programmée de l'inflation PIVX.

---

## 2. CE QUE CHAQUE ACTEUR GAGNE

### 2.1 Masternodes — Pouvoir + Finalité

```
AVANT (v5):
  • 6 PIV/bloc (perpétuel)
  • Vote budget DAO

APRÈS (v6-KHU):
  • 6→0 PIV/bloc (6 ans)
  • Vote budget DAO
  • ✅ NOUVEAU: Vote R% (DOMC) — contrôle le yield des stakers
  • ✅ NOUVEAU: Finalité LLMQ — sécurisent la chaîne (12 blocs)
```

**Message:** Les MN perdent les rewards fixes mais gagnent du POUVOIR (gouvernance R%).

### 2.2 Stakers — R% + Privacy

```
AVANT (v5):
  • 4 PIV/bloc (perpétuel, transparent)

APRÈS (v6-KHU):
  • 6→0 PIV/bloc (6 ans) — MIEUX que v5 pendant 6 ans!
  • ✅ NOUVEAU: Yield R% PERPÉTUEL sur stake KHU
    - Année 0-1: jusqu'à 30%
    - Année 26+: minimum 4%
  • ✅ NOUVEAU: Privacy (ZKHU Sapling) — staking anonyme
```

**Message:** Les stakers perdent l'émission fixe mais gagnent R% PERPÉTUEL + PRIVACY.

### 2.3 DAO — % de l'Activité On-Chain

```
AVANT (v5):
  • 10 PIV/bloc (perpétuel, fixe)

APRÈS (v6-KHU):
  • 6→0 PIV/bloc (6 ans)
  • ✅ NOUVEAU: Pool T = 0.5% × (U + Ur) tous les 4 mois
    - Si activité KHU = 0 → DAO reçoit 0
    - Si activité KHU = 100M → DAO reçoit 500K / 4 mois
```

**Message:** La DAO perd le revenu fixe mais gagne un % de l'ACTIVITÉ RÉELLE.

---

## 3. L'INFLATION RÉELLE (DÉMYSTIFICATION)

### 3.1 Confusion Commune

```
❌ FAUX: "R% = 30% d'inflation sur la supply totale PIV"

✅ VRAI: R% s'applique UNIQUEMENT aux KHU stakés (ZKHU)
```

### 3.2 Calcul Réel

**Exemple avec supply hypothétique:**
```
Supply totale PIV:     100,000,000 PIV
KHU mintés (U):         10,000,000 KHU  (10% de la supply)
KHU stakés (ZKHU):       5,000,000 KHU  (50% des KHU mintés)

Avec R% = 30%:
  Yield annuel = 5,000,000 × 30% = 1,500,000 KHU

Inflation RÉELLE sur supply totale:
  1,500,000 / 100,000,000 = 1.5% (pas 30%!)
```

### 3.3 Formule Simplifiée

```
Inflation_réelle = R% × (ZKHU_staké / Supply_totale_PIV)

Si seulement 5% de la supply est stakée en ZKHU:
  Inflation_réelle = 30% × 5% = 1.5%

Si seulement 1% de la supply est stakée en ZKHU:
  Inflation_réelle = 30% × 1% = 0.3%
```

**L'inflation dépend de l'ADOPTION, pas du taux R%.**

---

## 3.4 FLUX SIMPLIFIÉS (PAS DE SÉQUENCE OBLIGATOIRE)

> **IMPORTANT:** Le pipeline n'est PAS une séquence obligatoire.
> Chaque opération est INDÉPENDANTE.

```
FLUX SIMPLE (trading rapide):
┌─────────────────────────────────────────────┐
│ PIV → MINT → KHU_T → [trade] → REDEEM → PIV │
└─────────────────────────────────────────────┘
  • Pas de stake requis
  • Pas d'attente
  • KHU_T = transparent (comme PIV)


FLUX AVEC YIELD (pour R%):
┌─────────────────────────────────────────────────────────┐
│ PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → REDEEM   │
└─────────────────────────────────────────────────────────┘
  • Stake = optionnel (pour yield R%)
  • Maturity: 4320 blocs (3 jours) minimum
  • ZKHU = privacy (Sapling)


FLUX MIXTE (le plus courant):
┌─────────────────────────────────────────────┐
│ MINT → KHU_T                                │
│         ├── [trade] ← liquidité immédiate   │
│         └── STAKE → ZKHU ← yield R%         │
│                      ↓                      │
│              [UNSTAKE quand besoin]         │
│                      ↓                      │
│              REDEEM → PIV                   │
└─────────────────────────────────────────────┘
```

**Pour un arbitrageur:**
- MINT/REDEEM = instantané (quelques blocs)
- STAKE/UNSTAKE = optionnel (seulement si veut R%)
- Pas de "lock-in" obligatoire

---

## 4. DAO TREASURY — BASÉ SUR L'ACTIVITÉ

### 4.1 Formule

```
Tous les 172,800 blocs (4 mois):
  T += 0.5% × (U + Ur)

Où:
  U  = KHU_T en circulation (activité MINT)
  Ur = Rewards accumulés (activité STAKE)
```

### 4.2 Exemples Concrets

```
Scénario A: Faible activité
  U = 1,000,000 KHU
  Ur = 500,000 KHU
  ─────────────────
  T += 0.5% × 1,500,000 = 7,500 KHU / 4 mois
  T += ~22,500 KHU / an (~1.5% de l'activité)

Scénario B: Forte activité
  U = 50,000,000 KHU
  Ur = 25,000,000 KHU
  ─────────────────
  T += 0.5% × 75,000,000 = 375,000 KHU / 4 mois
  T += ~1,125,000 KHU / an (~1.5% de l'activité)
```

**Si personne n'utilise KHU → DAO reçoit 0.**
**Si beaucoup utilisent KHU → DAO reçoit beaucoup.**

### 4.3 Comparaison Ancien/Nouveau

```
Ancien (v5):
  DAO = 10 PIV/bloc × 525,600 blocs/an = 5,256,000 PIV/an (FIXE)

Nouveau (v6-KHU après année 6):
  DAO = 1.5% × (U + Ur) par an

  Si U + Ur = 100M KHU → DAO = 1.5M KHU/an
  Si U + Ur = 10M KHU  → DAO = 150K KHU/an
  Si U + Ur = 0        → DAO = 0
```

**La DAO est maintenant ALIGNÉE avec l'activité du réseau.**

---

## 5. TIMELINE COMPLÈTE

```
┌────────────────────────────────────────────────────────────────┐
│ ANNÉES 0-6: TRANSITION                                          │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ÉMISSION PIVX:     18 → 15 → 12 → 9 → 6 → 3 → 0 PIV/bloc       │
│                    (égalité 6/6/6 pour MN/Staker/DAO)           │
│                                                                 │
│ + YIELD R%:        Actif dès le début (jusqu'à 30%)             │
│                    (pour stakers ZKHU uniquement)               │
│                                                                 │
│ + DAO POOL T:      +0.5% × (U+Ur) tous les 4 mois               │
│                    (accumulation automatique)                   │
│                                                                 │
│ → Stakers reçoivent: Émission PIVX + Yield R%                   │
│ → MN reçoivent:      Émission PIVX + Pouvoir DOMC               │
│ → DAO reçoit:        Émission PIVX + Pool T                     │
│                                                                 │
└────────────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────────────┐
│ ANNÉES 6+: NOUVEAU RÉGIME PERMANENT                             │
├────────────────────────────────────────────────────────────────┤
│                                                                 │
│ ÉMISSION PIVX:     0 PIV/bloc (terminée définitivement)         │
│                                                                 │
│ YIELD R%:          Seule source de rewards stakers              │
│                    (4% minimum garanti via R_MAX)               │
│                                                                 │
│ DAO POOL T:        Seule source de financement DAO              │
│                    (~1.5%/an de l'activité KHU)                 │
│                                                                 │
│ → Stakers reçoivent: UNIQUEMENT Yield R%                        │
│ → MN reçoivent:      Pouvoir DOMC (pas de rewards directs)      │
│ → DAO reçoit:        UNIQUEMENT Pool T                          │
│                                                                 │
└────────────────────────────────────────────────────────────────┘
```

---

## 6. INFLATION MAXIMALE THÉORIQUE (ANNÉE 30+)

### 6.1 Calcul Pessimiste (Pire Cas)

```
Hypothèse pessimiste:
  - 100% de la supply PIV est convertie en KHU
  - 100% des KHU sont stakés en ZKHU
  - R% = 4% (minimum après 26 ans)

Inflation staking:
  4% × 100% = 4% par an sur la supply totale

Inflation DAO:
  0.5% × 3 cycles/an = 1.5% par an sur U+Ur
  (mais U+Ur ≤ supply, donc ≤ 1.5%)

TOTAL PIRE CAS: ~5.5% par an
```

### 6.2 Calcul Réaliste

```
Hypothèse réaliste:
  - 20% de la supply PIV est convertie en KHU
  - 50% des KHU sont stakés en ZKHU
  - R% = 4% (minimum)

Inflation staking réelle:
  4% × 20% × 50% = 0.4% par an sur la supply totale

Inflation DAO réelle:
  1.5% × 20% = 0.3% par an sur la supply totale

TOTAL RÉALISTE: ~0.7% par an
```

### 6.3 Comparaison

```
                    Ancien (v5)    Nouveau (v6, année 30)
                    ───────────    ──────────────────────
Émission PIVX:      20 PIV/bloc    0 PIV/bloc
Inflation directe:  ~5-10%/an      0%
Inflation staking:  N/A            0.4-4% (selon adoption)
Inflation DAO:      N/A            0.3-1.5% (selon activité)
                    ───────────    ──────────────────────
TOTAL:              ~5-10%/an      ~0.7-5.5%/an
```

**Le nouveau système est MOINS inflationniste et ALIGNÉ avec l'activité réelle.**

---

## 7. RÉSUMÉ POUR CHAQUE ACTEUR

### Pour les Masternodes

```
"Vous perdez des rewards fixes (6→0 PIV/bloc)
 mais vous GAGNEZ le contrôle du système:

 ✅ Vote R% (DOMC) — vous décidez du yield des stakers
 ✅ Finalité LLMQ — vous sécurisez la chaîne
 ✅ Vote DAO — vous décidez des dépenses

 Votre POUVOIR augmente, pas vos rewards directs."
```

### Pour les Stakers

```
"Vous perdez l'émission fixe (6→0 PIV/bloc)
 mais vous GAGNEZ un yield PERPÉTUEL:

 ✅ R% jusqu'à 37% (début) → 4% (minimum garanti)
 ✅ Privacy (ZKHU Sapling) — staking anonyme
 ✅ Yield PERPÉTUEL — ne s'arrête jamais

 L'émission s'arrête, mais votre yield continue."
```

### Pour la DAO

```
"Vous perdez le revenu fixe (6→0 PIV/bloc)
 mais vous GAGNEZ un % de l'ACTIVITÉ:

 ✅ Si activité = 0 → DAO reçoit 0 (aligné)
 ✅ Si activité forte → DAO reçoit beaucoup
 ✅ ~1.5%/an de (U + Ur)

 La DAO est maintenant ALIGNÉE avec le succès du réseau."
```

---

## 8. FAQ RAPIDE

**Q: L'inflation va exploser avec R% = 37%?**
A: Non. R% s'applique UNIQUEMENT aux ZKHU stakés, pas à la supply totale.
   Si 5% de la supply est stakée → inflation réelle = 1.85%.

**Q: La DAO va mourir après 6 ans?**
A: Non. Le Pool T accumule 2% × (U+Ur) par an dès l'Année 0.
   Si le réseau est actif, la DAO est financée.

**Q: Les MN perdent tout?**
A: Non. Ils perdent les rewards fixes mais gagnent le POUVOIR.
   Ils contrôlent R% (DOMC) et la finalité (LLMQ).

**Q: Pourquoi 4% minimum garanti?**
A: R_MAX_dynamic = max(400, 3700 - year×100) en basis points.
   Après 33 ans, R_MAX = 4%. Les MN peuvent voter R% ≤ 4%.

**Q: C'est mieux ou pire que PIVX v5?**
A: Différent. Moins d'inflation directe, plus de gouvernance.
   Le succès dépend de l'ADOPTION de KHU.

---

**FIN DU DOCUMENT PÉDAGOGIQUE**

**Version:** 1.0
**Date:** 2025-11-24
**Status:** PÉDAGOGIQUE
