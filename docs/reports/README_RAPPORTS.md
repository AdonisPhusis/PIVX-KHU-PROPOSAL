# INDEX DES RAPPORTS — PIVX-V6-KHU

**Date dernière mise à jour:** 2025-11-22

---

## RAPPORTS CANONIQUES (À JOUR)

Ces rapports sont **VALIDES** et doivent être consultés:

### 1. RAPPORT_TECHNIQUE_CONTRADICTEUR.md ✅
**Date:** 2025-11-22
**Statut:** CANONIQUE
**Contenu:** Analyse technique complète post-recadrage
**Résumé:**
- Cohérence inter-documents: ✅ VÉRIFIÉE
- Faisabilité technique: ✅ 95% confiance
- Roadmap: ✅ CORRECTE (pas de modification)
- Décisions architecturales: ✅ VALIDÉES
- 3 clarifications techniques nécessaires (burn PIV, pool separation, HTLC timelock)

### 2. RAPPORT_PHASE1_RECADRAGE.md ✅
**Date:** 2025-11-22
**Statut:** CANONIQUE
**Contenu:** Correction des 4 dérives majeures
**Résumé:**
- Dérive #1: Mentions Zerocoin SUPPRIMÉES
- Dérive #2: Modification roadmap ANNULÉE
- Dérive #3: Propositions non demandées RETIRÉES
- Dérive #4: Suggestions économiques ANNULÉES
- Confirmations obligatoires fournies

### 3. RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md ✅
**Date:** 2025-11-22
**Statut:** CANONIQUE
**Contenu:** Décisions architecturales définitives Phase 1
**Résumé:**
- Reorg Strategy: STATE-BASED
- Per-note Ur tracking: STREAMING cursor
- Rolling Frontier Tree: MARQUAGE (pas suppression)
- 7 rappels critiques obligatoires

### 4. RAPPORT_ANTI_DERIVE.md ✅
**Statut:** CANONIQUE
**Contenu:** Garde-fous ANTI-DÉRIVE avant Phase 1

---

## RAPPORTS OBSOLÈTES (CONTIENNENT DÉRIVES)

Ces rapports contiennent des **ERREURS CORRIGÉES** et ne doivent PAS être suivis:

### ⚠️ RAPPORT_INGENIERIE_SENIOR_PHASE1.md (OBSOLÈTE)
**Date:** 2025-11-22
**Statut:** ❌ OBSOLÈTE - CONTIENT 4 DÉRIVES MAJEURES
**Remplacé par:** RAPPORT_TECHNIQUE_CONTRADICTEUR.md

**DÉRIVES CONTENUES (NE PAS SUIVRE):**
1. ❌ Mentions Zerocoin (sections 2.2, 3.1)
2. ❌ Proposition réorganisation roadmap (section 6.2)
3. ❌ Propositions non sollicitées (section 5: Phase 0, pre-commit hooks, Prometheus, fuzzing obligatoire)
4. ❌ Suggestions DOMC simplifié (section 7.2)

**Pourquoi conservé:** Historique des erreurs pour apprentissage.

**IMPORTANT:** Ne PAS implémenter les propositions de ce rapport.

---

## RAPPORTS TECHNIQUES (À JOUR)

### AUDIT_CANONICITE_CPP.md ✅
**Statut:** VALIDE
**Contenu:** Vérification canonic

ité KhuGlobalState

### RAPPORT_DOC_COHERENCE.md ✅
**Statut:** VALIDE
**Contenu:** Cohérence documents avant corrections

### RAPPORT_PHASE1_DOC_PATCH.md ✅
**Statut:** VALIDE
**Contenu:** Patch documentation Phase 1

---

## HIÉRARCHIE DES RAPPORTS

```
RAPPORTS CANONIQUES (suivre strictement)
├── RAPPORT_TECHNIQUE_CONTRADICTEUR.md      ← PRINCIPAL (analyse complète)
├── RAPPORT_PHASE1_RECADRAGE.md             ← CORRECTIONS dérives
└── RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md  ← DÉCISIONS architecte

RAPPORTS OBSOLÈTES (ne PAS suivre)
└── RAPPORT_INGENIERIE_SENIOR_PHASE1.md     ← ❌ CONTIENT DÉRIVES
```

---

## RÉFÉRENCES CROISÉES

### Dérive #1: Zerocoin
- **Source erreur:** RAPPORT_INGENIERIE_SENIOR_PHASE1.md section 2.2, 3.1
- **Correction:** RAPPORT_PHASE1_RECADRAGE.md section "DÉRIVE #1"
- **Nouveau:** RAPPORT_TECHNIQUE_CONTRADICTEUR.md (aucune mention Zerocoin)

### Dérive #2: Roadmap
- **Source erreur:** RAPPORT_INGENIERIE_SENIOR_PHASE1.md section 6.2
- **Correction:** RAPPORT_PHASE1_RECADRAGE.md section "DÉRIVE #2"
- **Nouveau:** RAPPORT_TECHNIQUE_CONTRADICTEUR.md section 2.4 (roadmap validée)

### Dérive #3: Propositions non sollicitées
- **Source erreur:** RAPPORT_INGENIERIE_SENIOR_PHASE1.md section 5
- **Correction:** RAPPORT_PHASE1_RECADRAGE.md section "DÉRIVE #3"
- **Nouveau:** RAPPORT_TECHNIQUE_CONTRADICTEUR.md section 5 (propositions OPTIONNELLES uniquement)

### Dérive #4: DOMC simplifié
- **Source erreur:** RAPPORT_INGENIERIE_SENIOR_PHASE1.md section 7.2
- **Correction:** RAPPORT_PHASE1_RECADRAGE.md section "DÉRIVE #4"
- **Nouveau:** RAPPORT_TECHNIQUE_CONTRADICTEUR.md section 3.6 (DOMC canonique respecté)

---

## INSTRUCTIONS POUR DÉVELOPPEURS

### Quel rapport consulter?

**Pour démarrer Phase 1:**
→ Lire **RAPPORT_TECHNIQUE_CONTRADICTEUR.md** section 3.1

**Pour comprendre décisions architecturales:**
→ Lire **RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md**

**Pour comprendre les erreurs à éviter:**
→ Lire **RAPPORT_PHASE1_RECADRAGE.md**

**Pour historique (NE PAS IMPLÉMENTER):**
→ Consulter RAPPORT_INGENIERIE_SENIOR_PHASE1.md (obsolète)

### Ordre de lecture recommandé

1. RAPPORT_PHASE1_RECADRAGE.md (comprendre dérives)
2. RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md (décisions)
3. RAPPORT_TECHNIQUE_CONTRADICTEUR.md (analyse complète)
4. Documents canoniques (docs/02-*, docs/03-*, etc.)

---

## CHANGELOG

**2025-11-22:**
- Création RAPPORT_TECHNIQUE_CONTRADICTEUR.md (rapport canonique)
- Création RAPPORT_PHASE1_RECADRAGE.md (corrections dérives)
- Marquage RAPPORT_INGENIERIE_SENIOR_PHASE1.md comme OBSOLÈTE
- Création ce README

---

## CONTACT

**Questions sur rapports:**
- Dérive Zerocoin? → Voir RAPPORT_PHASE1_RECADRAGE.md DÉRIVE #1
- Roadmap ordre? → Voir RAPPORT_TECHNIQUE_CONTRADICTEUR.md section 2.4
- Décisions architecte? → Voir RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md

**En cas de conflit:**
- Documents canoniques (docs/02-*.md, docs/03-*.md) > Rapports
- Architecte > Rapports
- RAPPORT_TECHNIQUE_CONTRADICTEUR.md > RAPPORT_INGENIERIE_SENIOR_PHASE1.md

---

**FIN DU README**
