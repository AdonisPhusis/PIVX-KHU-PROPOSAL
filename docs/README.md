# PIVX-V6-KHU — GUIDE DE LA DOCUMENTATION

Ce guide organise toute la documentation du projet PIVX-V6-KHU pour faciliter la navigation et la compréhension du système.

---

## 📘 DOCUMENTS FONDAMENTAUX (ORDRE DE LECTURE)

Ces documents forment la base conceptuelle et technique du projet. **Lecture recommandée dans cet ordre :**

### 1. Vision et Philosophie
- **[00-declaration-independance-cryptographique.md](00-declaration-independance-cryptographique.md)**
  - Vision : Système monétaire mathématique sans confiance
  - Principe sacré : C==U, Cr==Ur (toujours, exactement, mathématiquement)
  - Invitation ouverte aux builders, forkers, sceptiques

### 2. Stratégie d'Exécution
- **[01-blueprint-master-flow.md](01-blueprint-master-flow.md)**
  - Roadmap séquentielle en 10 phases
  - 15 interdictions anti-dérive
  - Règles sacrées : formule d'émission immuable, yield indépendant de l'émission
  - Clarifications HTLC et architecture

### 3. Spécification Canonique
- **[02-canonical-specification.md](02-canonical-specification.md)**
  - Structure KhuGlobalState (14 champs)
  - Invariants sacrés : CD=1 (C==U), CDr=1 (Cr==Ur)
  - Opérations atomiques : MINT, REDEEM, DAILY_YIELD, UNSTAKE
  - Pipeline complet : PIV→MINT→KHU_T→STAKE→ZKHU→UNSTAKE→KHU_T→REDEEM→PIV

### 4. Architecture Technique
- **[03-architecture-overview.md](03-architecture-overview.md)**
  - Structure modulaire (src/khu/)
  - Clés LevelDB : Namespace 'K' pour KHU, 'S' pour Shield
  - Stratégie de reorg : STATE-BASED (état complet à chaque hauteur)
  - Performance : O(1) reorg par bloc

### 5. Modèle Économique
- **[04-economics.md](04-economics.md)**
  - Émission : 6→0 PIV/an (déflationnaire, terminal à l'an 6)
  - Yield : R% gouverné par DOMC (indépendant de l'émission)
  - Invariants : Application mathématique, jamais violés

### 6. Roadmap et Phases
- **[05-roadmap.md](05-roadmap.md)**
  - **Phase 1 : ✅ COMPLETED** - Consensus de base
  - **Phase 2 : ✅ COMPLETED** - Pipeline KHU transparent
  - **Phase 3 : ✅ COMPLETED** - Finalité masternode (52/52 tests PASS)
  - **Phase 4 : ⏳ PLANNED** - Sapling staking (~8 jours)
  - **Phase 5 : ⏳ PLANNED** - Yield Cr/Ur avec DOMC (~7 jours)
  - **Phases 6-10 : ⏳ PLANNED** - DOMC, HTLC, Wallet, Testnet, Mainnet

### 7. Référence Protocolaire
- **[06-protocol-reference.md](06-protocol-reference.md)**
  - Règles de consensus détaillées
  - Logique de validation pour chaque type de transaction
  - Spécifications des transitions d'état

---

## 🔷 BLUEPRINTS TECHNIQUES

Spécifications détaillées de chaque composant majeur :

### Implémentés (Phases 1-3)
1. **[01-PIVX-INFLATION-DIMINUTION.md](blueprints/01-PIVX-INFLATION-DIMINUTION.md)** ✅
   - Émission 6→0 PIV/an par compartiment
   - Formule immuable, codée en dur dans le consensus

2. **[02-KHU-COLORED-COIN.md](blueprints/02-KHU-COLORED-COIN.md)** ✅
   - KHU_T : Suivi UTXO opérationnel
   - Fondation Phase 1, opérations Phase 2

3. **[03-MINT-REDEEM.md](blueprints/03-MINT-REDEEM.md)** ✅
   - 12/12 tests réussis
   - Invariants C==U préservés dans tous les cas

4. **[04-FINALITE-MASTERNODE-STAKERS.md](blueprints/04-FINALITE-MASTERNODE-STAKERS.md)** ✅
   - Infrastructure LLMQ finality opérationnelle
   - Limite de 12 blocs appliquée
   - Base de données de commitments opérationnelle

### Planifiés (Phases 4+)
5. **[05-ZKHU-SAPLING-STAKE.md](blueprints/05-ZKHU-SAPLING-STAKE.md)** ⏳
   - Complexité : LOW-MEDIUM (~8 jours)
   - Approche : Wrapper autour du Sapling PIVX existant
   - Namespace : 'K' (séparé du Shield 'S')

6. **[06-YIELD-R-PERCENT.md](blueprints/06-YIELD-R-PERCENT.md)** ⏳
   - Yield quotidien : (montant × R%) / 365 / 10000
   - Gouvernance R% : Vote DOMC par masternodes
   - Pool : Cr/Ur (complètement séparé de l'émission)

7. **[07-KHU-HTLC-GATEWAY.md](blueprints/07-KHU-HTLC-GATEWAY.md)** ⏳
   - **Note clé** : Utilise les scripts HTLC Bitcoin standards
   - Pas d'implémentation HTLC KHU spéciale nécessaire
   - Gateway : Matching off-chain, KHU comme unité de compte

8. **[08-WALLET-RPC.md](blueprints/08-WALLET-RPC.md)** ⏳
   - Commandes RPC : Toutes opérations (MINT, REDEEM, STAKE, UNSTAKE)
   - GUI : Onglet KHU avec affichage des balances
   - Intégration au wallet PIVX existant

---

## 🔒 AUDITS ET SÉCURITÉ

Documentation complète des audits de sécurité et analyses d'attaques :

### Audits Principaux
- **[SECURITY_AUDIT_KHU_V6.md](audit/SECURITY_AUDIT_KHU_V6.md)**
  - Audit initial couvrant Phases 1, 2, 3
  - Focus : Protection des invariants, règles de consensus

- **[RAPPORT_RED_TEAM_FINAL.md](audit/RAPPORT_RED_TEAM_FINAL.md)**
  - Tests de sécurité offensifs
  - 20/20 vecteurs d'attaque bloqués (100%)
  - 2 vulnérabilités critiques identifiées et corrigées

- **[AUDIT_TECHNIQUE_FINAL.md](audit/AUDIT_TECHNIQUE_FINAL.md)**
  - Audit technique final
  - Statut : Prêt pour déploiement testnet

### Analyses d'Attaques Spécifiques
- **[ATTAQUE_MALFORMED.md](audit/ATTAQUE_MALFORMED.md)**
  - 8/8 vecteurs bloqués
  - Validation complète des payloads

- **[ATTAQUE_OVERFLOW.md](audit/ATTAQUE_OVERFLOW.md)**
  - 5/5 vecteurs bloqués (après hardening)
  - Utilisation de SafeAdd confirmée

- **[ATTAQUE_REORG.md](audit/ATTAQUE_REORG.md)**
  - 4/4 vecteurs bloqués (après hardening)
  - Validation d'intégrité DB ajoutée

### Audits de Conformité
- **[AUDIT_INTEGRATION_PHASES_1_2_3.md](audit/AUDIT_INTEGRATION_PHASES_1_2_3.md)**
  - Intégration complète vérifiée
  - Séquence ConnectBlock correcte
  - Aucune dépendance circulaire

- **[AUDIT_CANONICITE_CPP.md](audit/AUDIT_CANONICITE_CPP.md)**
  - Alignement code-spécification
  - Conformité C++ validée
  - Aucune divergence trouvée

### Posture de Sécurité Globale
**STATUT : PRODUCTION-READY**
- ✅ Toutes vulnérabilités critiques corrigées
- ✅ 100% couverture des vecteurs d'attaque (20/20)
- ✅ Invariants mathématiquement garantis
- ✅ Protection reorg : Double couche (12 blocs + finalité)
- ✅ Code audité : Aucun bug de consensus trouvé

---

## 📊 RAPPORTS D'IMPLÉMENTATION

### Rapports Globaux
Situés dans **[reports/global/](reports/global/)** :

- **[VERDICT-FINAL-PROJET-V2.md](reports/global/VERDICT-FINAL-PROJET-V2.md)**
  - Score : 98/100
  - Projet prêt pour la phase suivante

- **[EVALUATION-TECHNIQUE-SENIOR-CPP-V2.md](reports/global/EVALUATION-TECHNIQUE-SENIOR-CPP-V2.md)**
  - Architecture validée par senior C++
  - Décisions techniques approuvées

- **[ANALYSE-FAILLES-INVARIANTS-V1.md](reports/global/ANALYSE-FAILLES-INVARIANTS-V1.md)**
  - Analyse de sécurité complète
  - Tous les bloquants V1 résolus en V2

- **[RAPPORT_TECHNIQUE_CONTRADICTEUR.md](reports/global/RAPPORT_TECHNIQUE_CONTRADICTEUR.md)**
  - Analyse contradictoire pour validation rigoureuse

- **[RAPPORT_ANTI_DERIVE.md](reports/global/RAPPORT_ANTI_DERIVE.md)**
  - Respect des interdictions anti-dérive

- **[RAPPORT_DOC_COHERENCE.md](reports/global/RAPPORT_DOC_COHERENCE.md)**
  - Vérification de cohérence documentaire

### Phase 1 — Foundation (17 rapports)
Situés dans **[reports/phase1/](reports/phase1/)** :

**Rapports Clés :**
- **[RAPPORT_PHASE1_EMISSION_CPP.md](reports/phase1/RAPPORT_PHASE1_EMISSION_CPP.md)**
  - Implémentation émission confirmée

- **[RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md](reports/phase1/RAPPORT_DECISIONS_ARCHITECTURALES_PHASE1.md)**
  - Stratégie de reorg state-based définie

- **[RAPPORT_INGENIERIE_SENIOR_PHASE1.md](reports/phase1/RAPPORT_INGENIERIE_SENIOR_PHASE1.md)**
  - Décisions d'ingénierie validées

**Rapports de Corrections :**
- RAPPORT_PHASE1_FIX_ZKHU_SAPLING.md
- RAPPORT_PHASE1_FIX_HTLC_GATEWAY.md
- RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md
- RAPPORT_PHASE1_PURGE_ZEROCONCEPTS.md
- Et autres (voir dossier complet)

### Phase 2 — MINT/REDEEM (2 rapports)
Situés dans **[reports/phase2/](reports/phase2/)** :

- **[RAPPORT_PHASE2_IMPL_CPP.md](reports/phase2/RAPPORT_PHASE2_IMPL_CPP.md)**
  - Implémentation MINT/REDEEM complète

- **[RAPPORT_PHASE2_TESTS_CPP.md](reports/phase2/RAPPORT_PHASE2_TESTS_CPP.md)**
  - 12/12 tests réussis
  - Invariants préservés dans tous les cas

### Phase 3 — Finality (3 rapports)
Situés dans **[reports/phase3/](reports/phase3/)** :

- **[RAPPORT_FINAL_PHASE3_V6_ACTIVATION.md](reports/phase3/RAPPORT_FINAL_PHASE3_V6_ACTIVATION.md)** ⭐
  - **RAPPORT CENTRAL** pour la Phase 3
  - 52/52 tests PASS (100%)
  - Activation V6 validée
  - Système complet et opérationnel

- **[RAPPORT_PHASE3_FINALITY_CPP.md](reports/phase3/RAPPORT_PHASE3_FINALITY_CPP.md)**
  - Infrastructure de finalité complète
  - Structure KhuStateCommitment opérationnelle

- **[RAPPORT_PHASE3_SECURITY_HARDENING.md](reports/phase3/RAPPORT_PHASE3_SECURITY_HARDENING.md)**
  - CVE-KHU-2025-002 CORRIGÉ
  - VULN-KHU-2025-001 CORRIGÉ
  - 20/20 vecteurs d'attaque bloqués

---

## ❓ FAQ

### Questions Techniques
- **[08-FAQ-TECHNIQUE.md](faq/08-FAQ-TECHNIQUE.md)**
  - Réponses aux questions fréquentes
  - Clarifications techniques
  - Cas d'usage et scénarios

---

## 🧭 GUIDE DE NAVIGATION PAR PROFIL

### Pour les Nouveaux Arrivants
1. Commencer par **[00-declaration-independance-cryptographique.md](00-declaration-independance-cryptographique.md)** (vision)
2. Lire **[05-roadmap.md](05-roadmap.md)** (où en est le projet)
3. Consulter **[reports/phase3/RAPPORT_FINAL_PHASE3_V6_ACTIVATION.md](reports/phase3/RAPPORT_FINAL_PHASE3_V6_ACTIVATION.md)** (état actuel)

### Pour les Développeurs
1. **[02-canonical-specification.md](02-canonical-specification.md)** (règles de consensus)
2. **[03-architecture-overview.md](03-architecture-overview.md)** (structure du code)
3. **[blueprints/](blueprints/)** (détails d'implémentation)
4. **[reports/phase3/](reports/phase3/)** (implémentation actuelle)

### Pour les Auditeurs Sécurité
1. **[02-canonical-specification.md](02-canonical-specification.md)** (invariants)
2. **[audit/](audit/)** (tous les audits)
3. **[reports/phase3/RAPPORT_PHASE3_SECURITY_HARDENING.md](reports/phase3/RAPPORT_PHASE3_SECURITY_HARDENING.md)** (corrections CVE)
4. **[audit/RAPPORT_RED_TEAM_FINAL.md](audit/RAPPORT_RED_TEAM_FINAL.md)** (tests d'attaque)

### Pour les Économistes
1. **[04-economics.md](04-economics.md)** (modèle économique)
2. **[blueprints/01-PIVX-INFLATION-DIMINUTION.md](blueprints/01-PIVX-INFLATION-DIMINUTION.md)** (émission)
3. **[blueprints/06-YIELD-R-PERCENT.md](blueprints/06-YIELD-R-PERCENT.md)** (yield DOMC)

### Pour les Investisseurs / DAO
1. **[00-declaration-independance-cryptographique.md](00-declaration-independance-cryptographique.md)** (vision)
2. **[05-roadmap.md](05-roadmap.md)** (progression)
3. **[reports/global/VERDICT-FINAL-PROJET-V2.md](reports/global/VERDICT-FINAL-PROJET-V2.md)** (évaluation globale)
4. **[audit/RAPPORT_RED_TEAM_FINAL.md](audit/RAPPORT_RED_TEAM_FINAL.md)** (sécurité)

---

## 📐 HIÉRARCHIE ET IMMUTABILITÉ

### Documents Immutables (Spécifications Canoniques)
Ces documents définissent les règles fondamentales et **ne doivent PAS être modifiés** sans consensus communautaire :

- **[02-canonical-specification.md](02-canonical-specification.md)** - Règles de consensus
- **[04-economics.md](04-economics.md)** - Formule d'émission 6→0

### Documents Évolutifs (Guides et Statuts)
Ces documents sont mis à jour au fil du développement :

- **[05-roadmap.md](05-roadmap.md)** - Statuts des phases
- **[reports/](reports/)** - Rapports d'implémentation
- **[audit/](audit/)** - Audits de sécurité

### Documents de Référence (Contexte Historique)
Ces documents capturent les décisions et le raisonnement :

- **[00-declaration-independance-cryptographique.md](00-declaration-independance-cryptographique.md)** - Vision philosophique
- **[01-blueprint-master-flow.md](01-blueprint-master-flow.md)** - Stratégie d'exécution
- **[reports/phase1/](reports/phase1/)** - Historique Phase 1

---

## 🎯 STATUT ACTUEL DU PROJET

**Date de référence** : Phase 3 complétée

### Phases Complétées ✅
- **Phase 1** : Consensus de base, émission, état global KHU
- **Phase 2** : Pipeline MINT/REDEEM opérationnel (12/12 tests)
- **Phase 3** : Finalité masternode, activation V6 (52/52 tests, 100%)

### Sécurité ✅
- 20/20 vecteurs d'attaque bloqués (100%)
- Tous CVE critiques corrigés
- Invariants mathématiquement garantis

### Prochaines Étapes ⏳
- **Phase 4** : SAPLING staking (~8 jours)
- **Phase 5** : Yield Cr/Ur avec DOMC (~7 jours)
- **Phases 6-10** : HTLC, Wallet, Testnet, Mainnet

---

## 📞 CONTACT ET CONTRIBUTION

Pour toute question ou contribution :
- **Code Source** : Voir `/src/khu/` pour l'implémentation
- **Tests** : Voir `/src/test/khu_*` pour la suite de tests
- **Issues** : Référencer cette documentation dans vos rapports de bugs

---

**Ce README est maintenu à jour avec chaque phase complétée.**
**Dernière mise à jour** : Phase 3 completion (novembre 2025)
