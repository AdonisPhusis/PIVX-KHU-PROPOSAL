# RAPPORT — MISE À JOUR BLUEPRINT MASTER + SOUS-BLUEPRINT ZKHU

**Date:** 2025-11-22
**Branche:** khu-phase1-consensus
**Statut:** CORRECTION ARCHITECTURALE MAJEURE COMPLÉTÉE
**Priorité:** 🔴 BLOQUANT PHASE 4

---

## RÉSUMÉ EXÉCUTIF

Suite à la correction critique de la séparation ZKHU/Sapling storage (RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md), j'ai gravé les règles ZKHU canoniques dans le **MASTER BLUEPRINT** et créé un **sous-blueprint ZKHU** dédié.

**Objectif:** Éliminer définitivement toute confusion sur l'architecture ZKHU/Sapling et ancrer les 4 règles fondamentales dans la documentation immuable du projet.

---

## 1. MODIFICATIONS EFFECTUÉES

### 1.1 Mise à Jour `docs/01-blueprint-master-flow.md`

**Section ajoutée:** `1.3.7 SAPLING / ZKHU — RÈGLES GLOBALES (CANONIQUES)`

**Emplacement:** Après section 1.3.6 (HTLC), avant section 1.4 (Nomenclature PIVX)

**Contenu (85 lignes):**

#### RÈGLE 1 — INTERDICTION ABSOLUE ZEROCOIN / ZPIV

```
❌ Ne JAMAIS utiliser quoi que ce soit de Zerocoin ou zPIV
❌ Ne JAMAIS mentionner, imiter, réutiliser de code Zerocoin
❌ Ne JAMAIS s'inspirer de l'architecture Zerocoin
❌ Zerocoin est MORT et ne doit JAMAIS contaminer KHU
```

#### RÈGLE 2 — ZKHU = STAKING ONLY (PAS Z→Z)

```
✅ ZKHU est pour STAKING uniquement
❌ ZKHU ne fait PAS de privacy spending
❌ Pas de transactions Z→Z (ZKHU → ZKHU)
❌ Pas de transactions ZKHU → Shield
❌ Pas de transactions Shield → ZKHU
❌ Pas de join-split
❌ Pas de pool mixing

Pipeline strict:
  PIV → KHU_T → ZKHU → KHU_T → PIV
  (T→Z pour stake, Z→T pour unstake, pas de Z→Z)
```

#### RÈGLE 3 — CRYPTOGRAPHIE SAPLING PARTAGÉE, STOCKAGE LEVELDB SÉPARÉ

```
✅ PARTAGÉ (Cryptographie):
  - Circuits zk-SNARK Sapling (pas de modification)
  - Format OutputDescription (512-byte memo)
  - Algorithmes de commitment/nullifier
  - Format de proof zk-SNARK

❌ SÉPARÉ (Stockage LevelDB):
  - zkhuTree ≠ saplingTree (merkle trees distincts)
  - zkhuNullifierSet ≠ nullifierSet (nullifier sets distincts)
  - Clés LevelDB préfixe 'K' pour ZKHU
  - Clés LevelDB préfixe 'S'/'s' pour Shield
  - Anonymity sets séparés (pas de pool mixing)
```

#### RÈGLE 4 — CLÉS LEVELDB CANONIQUES (IMMUABLES)

```cpp
// ZKHU (namespace 'K' — OBLIGATOIRE)
'K' + 'A' + anchor_khu     → ZKHU SaplingMerkleTree
'K' + 'N' + nullifier_khu  → ZKHU nullifier spent flag
'K' + 'T' + note_id        → ZKHUNoteData (amount, Ur, height)

// Shield (PIVX Sapling public — namespace 'S'/'s')
'S' + anchor      → Shield SaplingMerkleTree
's' + nullifier   → Shield nullifier spent flag

// ⚠️ CRITICAL: Aucun chevauchement de clés
// ZKHU et Shield sont COMPLÈTEMENT isolés
```

**INTERDICTIONS ABSOLUES ZKHU/SAPLING:**

```cpp
❌ JAMAIS utiliser clés Shield ('S', 's') pour ZKHU
❌ JAMAIS partager saplingTree entre ZKHU et Shield
❌ JAMAIS partager nullifierSet entre ZKHU et Shield
❌ JAMAIS permettre Z→Z transactions (ZKHU ↔ Shield)
❌ JAMAIS mélanger anonymity sets ZKHU/Shield
❌ JAMAIS modifier circuits zk-SNARK Sapling
❌ JAMAIS réutiliser quoi que ce soit de Zerocoin/zPIV
```

**CONSÉQUENCES TECHNIQUES:**

```
1. ZKHU réutilise la cryptographie Sapling éprouvée (pas de nouveau circuit)
2. ZKHU maintient ses propres structures de stockage (isolation complète)
3. Pas de conversion ZKHU ↔ Shield possible (pas de Z→Z)
4. Pas d'anonymity set partagé (ZKHU = staking, Shield = privacy)
5. Audit et compliance ZKHU séparés de Shield
```

**Référence:** `docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md`

---

### 1.2 Création `docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md`

**Nouveau fichier:** Sous-blueprint ZKHU complet (580 lignes)

**Structure:**

#### Section 1: RÈGLES FONDAMENTALES ZKHU
- 1.1 ZKHU ≠ zPIV (interdiction Zerocoin)
- 1.2 ZKHU = Staking Only (pas Z→Z)
- 1.3 Pipeline strict KHU_T ↔ ZKHU

#### Section 2: ARCHITECTURE SAPLING
- 2.1 Cryptographie Sapling PARTAGÉE
- 2.2 Stockage LevelDB SÉPARÉ
- 2.3 Structures de données (ZKHUNoteData, CKHUSaplingTree)
- 2.4 Memo format (512 bytes avec metadata)

#### Section 3: OPÉRATION STAKE (KHU_T → ZKHU)
- 3.1 Transaction STAKE
- 3.2 Validation STAKE (CheckKHUStake)
- 3.3 Application STAKE (ApplyKHUStake)
- 3.4 Impact sur état global (STAKE ne modifie PAS C/U/Cr/Ur)

#### Section 4: OPÉRATION UNSTAKE (ZKHU → KHU_T)
- 4.1 Transaction UNSTAKE
- 4.2 Validation UNSTAKE (CheckKHUUnstake)
- 4.3 Application UNSTAKE (ApplyKHUUnstake - double flux atomique)
- 4.4 Impact sur état global (Phase future: U+=B, C+=B, Cr-=B, Ur-=B)

#### Section 5: INTERDICTIONS ABSOLUES
- 5.1 Code interdit (avec exemples ❌ vs ✅)
- 5.2 Transactions interdites
- 5.3 Architecture interdite

#### Section 6: CHECKLIST IMPLÉMENTATION PHASE 4
- 6.1 Structures de données
- 6.2 API CCoinsViewCache
- 6.3 Validation
- 6.4 Tests (unitaires + fonctionnels)
- 6.5 Vérifications finales (grep commands)

#### Section 7: RÉFÉRENCES
- Documents liés
- Sous-blueprints futurs

#### Section 8: VALIDATION FINALE
- Statut CANONIQUE et IMMUABLE
- Procédure de modification

---

## 2. CONFIRMATION ARCHITECTURALE

### 2.1 ZKHU ≠ zPIV (Confirmé)

**✅ Ma compréhension CORRECTE:**
- ZKHU ne doit JAMAIS utiliser quoi que ce soit de Zerocoin ou zPIV
- ZKHU est une implémentation PROPRE basée sur Sapling (pas sur Zerocoin)
- Toute référence à Zerocoin/zPIV est INTERDITE

**❌ Erreur corrigée:**
- Documentation précédente suggérait pool Sapling partagé avec zPIV/Shield
- Correction: ZKHU et Shield partagent la CRYPTOGRAPHIE, pas le STOCKAGE

### 2.2 ZKHU Ne Partage Jamais les Mêmes Clés DB que SHIELD (Confirmé)

**✅ Clés LevelDB correctes:**

**ZKHU (namespace 'K'):**
```
'K' + 'A' + anchor_khu     → ZKHU SaplingMerkleTree
'K' + 'N' + nullifier_khu  → ZKHU nullifier spent flag
'K' + 'T' + note_id        → ZKHUNoteData
```

**Shield (namespace 'S'/'s'):**
```
'S' + anchor      → Shield SaplingMerkleTree
's' + nullifier   → Shield nullifier spent flag
```

**❌ Clés Shield INTERDITES pour ZKHU:**
- Ne JAMAIS utiliser 'S' + anchor pour ZKHU
- Ne JAMAIS utiliser 's' + nullifier pour ZKHU

### 2.3 Aucun Concept de Pool Sapling Partagé (Confirmé)

**✅ Ma compréhension CORRECTE:**

**Partagé:**
- Circuits zk-SNARK Sapling (cryptographie)
- Format OutputDescription (512-byte memo)
- Algorithmes de commitment/nullifier

**Séparé:**
- zkhuTree ≠ saplingTree
- zkhuNullifierSet ≠ nullifierSet
- Anonymity sets distincts
- Pas de pool mixing
- Pas de Z→Z transactions

**❌ Concept erroné éliminé:**
- "Pool Sapling partagé zPIV/ZKHU" n'existe PAS
- ZKHU et Shield sont COMPLÈTEMENT isolés au niveau stockage

---

## 3. IMPACT SUR PHASES FUTURES

### 3.1 Phase 1 (Foundation) — AUCUN IMPACT

Phase 1 ne touche pas Sapling. Pas de modification nécessaire.

### 3.2 Phase 2 (MINT/REDEEM) — AUCUN IMPACT

MINT/REDEEM concernent PIV ↔ KHU_T (transparent). Pas de Sapling.

### 3.3 Phase 3 (DAILY_YIELD) — IMPACT MINEUR

**Changement:**
- Yield appliqué aux notes ZKHU (namespace 'K' + 'N')
- Utiliser `view.GetKHUNoteData()` pour itérer sur notes ZKHU
- Ne PAS utiliser `view.GetSaplingNoteData()` (Shield)

### 3.4 Phase 4 (SAPLING STAKE/UNSTAKE) — IMPACT MAJEUR

**Implémentation obligatoire:**

1. **Structures de données:**
   - `ZKHUNoteData` (src/khu/khu_sapling.h)
   - `CKHUSaplingTree` class

2. **API CCoinsViewCache:**
   - `view.GetKHUAnchor()`
   - `view.PushKHUAnchor()`
   - `view.GetKHUNullifier()`
   - `view.SetKHUNullifier()`
   - `view.GetKHUNoteData()`
   - `view.WriteKHUNoteData()`

3. **Validation:**
   - `CheckKHUStake()` utilise API ZKHU (pas Shield)
   - `CheckKHUUnstake()` vérifie nullifier dans zkhuNullifierSet (pas nullifierSet)

4. **Tests:**
   - Vérifier isolation ZKHU/Shield
   - Vérifier namespace 'K' utilisé (pas 'S'/'s')

**Référence détaillée:** `docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md`

### 3.5 Phases 5-10 — AUCUN IMPACT DIRECT

DOMC, SAFU, GUI, Audit, Testnet, Mainnet ne concernent pas l'architecture Sapling.

---

## 4. VALIDATION ET VÉRIFICATION

### 4.1 Checklist Documentation

✅ **Section 1.3.7 ajoutée au MASTER BLUEPRINT:**
- [x] RÈGLE 1: Interdiction Zerocoin/zPIV
- [x] RÈGLE 2: ZKHU = staking only (pas Z→Z)
- [x] RÈGLE 3: Crypto partagée, storage séparé
- [x] RÈGLE 4: Clés LevelDB canoniques
- [x] Interdictions absolues listées
- [x] Conséquences techniques documentées

✅ **Sous-blueprint 07-ZKHU-STAKE-UNSTAKE.md créé:**
- [x] Règles fondamentales ZKHU
- [x] Architecture Sapling détaillée
- [x] Opération STAKE complète (validation + application)
- [x] Opération UNSTAKE complète (validation + application)
- [x] Interdictions absolues avec exemples code
- [x] Checklist implémentation Phase 4
- [x] Commandes de vérification (grep)

✅ **Cohérence avec corrections précédentes:**
- [x] Aligné avec RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md
- [x] Aligné avec docs/03-architecture-overview.md section 7.1
- [x] Aligné avec docs/06-protocol-reference.md section 18

### 4.2 Vérification Grep (Pour Phase 4)

**Commandes de validation:**

```bash
# Vérifier présence de structures ZKHU (Phase 4)
grep -r "zkhuTree" src/khu/          # DOIT avoir occurrences
grep -r "zkhuNullifierSet" src/khu/  # DOIT avoir occurrences
grep -r "'K' + 'A'" src/khu/         # DOIT avoir occurrences
grep -r "'K' + 'N'" src/khu/         # DOIT avoir occurrences

# Vérifier absence de contamination Shield
grep -r "'S' + anchor" src/khu/      # DOIT être vide
grep -r "'s' + nullifier" src/khu/   # DOIT être vide
grep -r "PushSaplingAnchor.*KHU" src/  # DOIT être vide
grep -r "GetNullifier.*SAPLING.*khu" src/  # DOIT être vide
```

**Résultat attendu Phase 4:** Toutes les vérifications passent.

### 4.3 Statut Documentation

**Documents IMMUABLES (ne doivent PLUS changer):**
- ✅ `01-blueprint-master-flow.md` — Section 1.3.7 ajoutée (CANONIQUE)
- ✅ `blueprints/07-ZKHU-STAKE-UNSTAKE.md` — Créé (CANONIQUE)

**Documents à jour (peuvent évoluer):**
- ✅ `02-canonical-specification.md` — Déjà corrigé
- ✅ `03-architecture-overview.md` — Déjà corrigé (section 7.1)
- ✅ `06-protocol-reference.md` — Déjà corrigé (section 18)
- ✅ `reports/RAPPORT_ANTI_DERIVE.md` — Déjà mis à jour
- ✅ `reports/RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md` — Déjà créé
- ✅ `reports/RAPPORT_PHASE1_FIX_ZKHU_SAPLING.md` — Ce rapport

---

## 5. RÉCAPITULATIF RÈGLES ZKHU (CANONICAL)

### RÈGLE 1: ZEROCOIN/ZPIV INTERDIT
❌ Ne JAMAIS utiliser quoi que ce soit de Zerocoin ou zPIV

### RÈGLE 2: ZKHU = STAKING ONLY
✅ Staking uniquement
❌ Pas de Z→Z transactions

### RÈGLE 3: CRYPTO PARTAGÉE, STORAGE SÉPARÉ
✅ Circuits Sapling partagés
❌ zkhuTree ≠ saplingTree
❌ zkhuNullifierSet ≠ nullifierSet

### RÈGLE 4: NAMESPACE 'K' POUR ZKHU
✅ 'K' + 'A' + anchor_khu
✅ 'K' + 'N' + nullifier_khu
❌ JAMAIS utiliser 'S' ou 's' pour ZKHU

---

## 6. PROCHAINES ÉTAPES

### 6.1 Phase 1 (En cours)
- Continuer implémentation Foundation (State, DB, RPC)
- Aucun changement nécessaire suite à cette mise à jour

### 6.2 Phase 2-3
- MINT/REDEEM: Aucun impact (pas de Sapling)
- DAILY_YIELD: Utiliser namespace 'K' pour itération notes

### 6.3 Phase 4 (Future)
- **Référence obligatoire:** `docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md`
- Implémenter API ZKHU (PushKHUAnchor, GetKHUNullifier, etc.)
- Tests d'isolation ZKHU/Shield
- Vérifications grep avant merge

---

## 7. FICHIERS MODIFIÉS/CRÉÉS

**Modifiés:**
```
M docs/01-blueprint-master-flow.md
  + Section 1.3.7 (85 lignes)
```

**Créés:**
```
A docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md (580 lignes)
A docs/reports/RAPPORT_PHASE1_FIX_ZKHU_SAPLING.md (ce fichier)
```

**Commit précédent:**
```
e0d0f05 docs: CRITICAL FIX — ZKHU uses separate LevelDB namespace
  M docs/03-architecture-overview.md
  M docs/06-protocol-reference.md
  M docs/reports/RAPPORT_ANTI_DERIVE.md
  A docs/reports/RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md
```

---

## 8. CONCLUSION

✅ **RÈGLES ZKHU GRAVÉES DANS LE MASTER BLUEPRINT**

La section 1.3.7 du blueprint master-flow définit maintenant de manière **IMMUABLE** les 4 règles fondamentales ZKHU/Sapling. Ces règles font partie des **LOIS ABSOLUES** du projet au même titre que les invariants C==U et Cr==Ur.

✅ **SOUS-BLUEPRINT ZKHU COMPLET**

Le fichier `docs/blueprints/07-ZKHU-STAKE-UNSTAKE.md` fournit la spécification détaillée et complète de l'architecture ZKHU, avec code C++, validation, tests, et interdictions.

✅ **AUCUNE CONFUSION POSSIBLE**

- ZKHU ≠ zPIV (Zerocoin interdit)
- ZKHU ne partage PAS les clés LevelDB Shield
- ZKHU maintient zkhuTree et zkhuNullifierSet séparés
- Pas de pool Sapling partagé
- Pipeline strict: PIV → KHU_T → ZKHU → KHU_T → PIV

✅ **PHASE 1 NON IMPACTÉE**

Phase 1 (Foundation) peut continuer sans modification. Les règles ZKHU s'appliquent à Phase 4.

✅ **DOCUMENTATION COMPLÈTE ET COHÉRENTE**

Tous les documents sont maintenant alignés sur l'architecture correcte (primitives Sapling partagées, stockage LevelDB séparé).

---

**Statut:** ✅ CORRECTION COMPLÉTÉE
**Blocage Phase 4:** ❌ LEVÉ (blueprint disponible)
**Review requis:** ✅ ARCHITECTE (validation des règles gravées)

---

**FIN DU RAPPORT**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** FINAL
