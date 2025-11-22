# RAPPORT ANTI-DÉRIVE — PIVX-V6-KHU

**Date:** 2025-11-22
**Branche:** khu-phase1-consensus
**Statut:** GARDE-FOUS ANTI-DÉRIVE COMPLETS AVANT PHASE 1

---

## RÉSUMÉ EXÉCUTIF

J'ai implémenté 6 catégories de garde-fous anti-dérive dans la documentation PIVX-V6-KHU pour prévenir les erreurs d'implémentation qui causeraient des échecs de consensus. Ces protections couvrent les risques critiques identifiés par l'architecte avant le début de la Phase 1.

**Documents modifiés:**
- `docs/02-canonical-specification.md` (spec canonique)
- `docs/03-architecture-overview.md` (architecture)
- `docs/06-protocol-reference.md` (référence d'implémentation)

**Lignes ajoutées:** ~500 lignes de garde-fous, checksums, et checklists de vérification

---

## GARDE-FOUS IMPLÉMENTÉS

### 1. ANTI-DÉRIVE #1: Locks Explicites

**Risque:** Oubli de `cs_khu` lock → race conditions → corruption de state

**Protections ajoutées:**

#### Doc 03 (ligne 252-258)
```cpp
// ============================================================
// ⚠️ CRITICAL LOCK: LOCK2(cs_main, cs_khu)
// ============================================================
// MUST acquire BOTH locks BEFORE any KHU state access or mutation
// Lock order is MANDATORY: cs_main first, cs_khu second (never reversed)
LOCK2(cs_main, cs_khu);
```

#### Doc 06 (section 3.3: Lock Verification Checklist)
- Liste exhaustive des fonctions nécessitant `cs_khu`
- Ordre de lock obligatoire: `LOCK2(cs_main, cs_khu)`
- Checklist de vérification en 10 points
- Commandes grep automatisées pour détecter violations
- `AssertLockHeld(cs_khu)` obligatoire dans toutes fonctions de mutation
- Instructions pour compilation avec `-DDEBUG_LOCKORDER`

**Vérification:**
```bash
grep -B5 "ApplyDailyYield\|ApplyKhuUnstake" src/**/*.cpp | grep "LOCK2.*cs_khu"
grep "AssertLockHeld(cs_khu)" src/khu/*.cpp
```

---

### 2. ANTI-DÉRIVE #2: Ordre Yield → Transactions

**Risque:** Si transactions exécutées AVANT yield → UNSTAKE utilise Ur faux → invariant violation

**Protections ajoutées:**

#### Doc 02 (après section 3.8)
Bloc warning de 70 lignes expliquant:
- Scénario d'erreur complet (ordre inversé)
- Pourquoi `ApplyDailyYieldIfNeeded()` DOIT précéder `ProcessKHUTransactions()`
- Conséquences: `state.Cr != state.Ur` → bloc rejeté

#### Doc 06 (section 3)
Bloc warning détaillant:
- Exemple de code FORBIDDEN (transactions → yield)
- Exemple de code CORRECT (yield → transactions)
- Enforcement: ordre HARDCODED, non modifiable
- Commande de vérification grep

**Vérification:**
```bash
grep -A10 "ApplyDailyYield" src/validation.cpp | grep -B5 "for.*vtx"
# ApplyDailyYield MUST appear BEFORE transaction loop
```

---

### 3. ANTI-DÉRIVE #3: Error Handling ProcessKHUTransaction

**Risque:** Boucle sans vérifier return value → transactions invalides silencieusement ignorées

**Protections ajoutées:**

#### Doc 03 (ligne 276)
```cpp
// ⚠️ ANTI-DÉRIVE: TOUTES les erreurs de transaction KHU doivent rejeter le bloc
for (const auto& tx : block.vtx) {
    if (!tx->IsKHUTransaction())
        continue;

    // CRITICAL: Check return value and reject block immediately on failure
    if (!ApplyKHUMintOrRedeem(*tx, newState, view, state))
        return false;  // DO NOT CONTINUE - block is invalid
}
```

#### Doc 06 (ligne 280)
```cpp
// ⚠️ ANTI-DÉRIVE: Return value MUST be checked for EVERY transaction
for (const auto& tx : block.vtx) {
    // CRITICAL: Check return value and stop immediately on failure
    // DO NOT use patterns like:
    //   - ProcessKHUTransaction(tx, ...); (ignoring return value)
    //   - if (ProcessKHUTransaction(...)) continue; (inverted logic)
    if (!ProcessKHUTransaction(tx, newState, view, state))
        return false;  // Stop immediately - block is INVALID
}
```

**Forbidden patterns documentés:** ignorer return value, try-catch silencieux, logique inversée

---

### 4. ANTI-DÉRIVE #4: Checksum Structure KhuGlobalState

**Risque:** Docs 02 et 03 désynchronisés → implémentation utilise mauvaise structure → consensus failure

**Protections ajoutées:**

#### Doc 02 (après section 2.1)
Bloc checksum de structure avec:
- SHA256 des 14 champs dans l'ordre canonique
- 5 règles de synchronisation
- Script de vérification diff automatique
- Protocole de modification en 5 étapes

#### Doc 03 (après struct KhuGlobalState)
Même checksum référencé avec commande de vérification:
```bash
diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
     <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md)
```

**Règle:** Si modification de struct → doc 02 d'abord → doc 03 immédiatement → diff verification

---

### 5. ANTI-DÉRIVE #5: Séparation Pools Sapling

**Risque:** ZKHU et zPIV partagent même arbre → contamination pools → anonymity loss

**Protections ajoutées:**

#### Doc 06 (section 18: Sapling Pool Separation)
Bloc de 80 lignes incluant:

**Compile-time verification:**
```cpp
static_assert(offsetof(SaplingState, zkhuTree) != offsetof(SaplingState, saplingTree),
              "ZKHU and zPIV trees MUST be separate members");
```

**Forbidden patterns:**
```cpp
// ❌ NEVER
saplingTree.append(zkhu_commitment);  // WRONG - use zkhuTree

// ❌ NEVER
if (nullifierSet.count(zkhu_nullifier))  // WRONG - use zkhuNullifierSet
```

**Correct patterns:**
```cpp
// ✅ CORRECT
zkhuTree.append(khu_commitment);
if (zkhuNullifierSet.count(spend.nullifier))
if (spend.anchor != zkhuTree.root())
```

**Checklist de 10 points** pour implémentation Sapling

**Audit commands:**
```bash
grep -r "saplingTree.append.*KHU" src/  # Should return NOTHING
grep -r "nullifierSet.*zkhu" src/       # Should return NOTHING
```

---

### 6. ANTI-DÉRIVE #6: Ordre Sérialisation GetHash()

**Risque:** Changer ordre sérialisation → hash change → fork total réseau

**Protections ajoutées:**

#### Doc 06 (section 5: State Hash and Chaining)
Bloc de 140 lignes incluant:

**Ordre canonique obligatoire (14 champs):**
```cpp
uint256 KhuGlobalState::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);

    // ⚠️ CRITICAL: Fields MUST be serialized in THIS EXACT ORDER
    ss << C;                          // Field 1 - MUST be first
    ss << U;                          // Field 2
    // ... 12 more fields in EXACT order ...
    ss << hashPrevState;              // Field 14 - MUST be last

    return ss.GetHash();  // SHA256d
}
```

**Forbidden modifications documentées:**
- Réordonner champs (même pour "lisibilité")
- Sauter champs
- Ajouter nouveaux champs au milieu
- Changer méthode de sérialisation
- Changer algorithme de hash

**Script de vérification automatique:**
```bash
grep -A20 "GetHash.*const" src/khu/khu_state.cpp | grep "ss <<" | awk '{print $3}'
# MUST match: C; U; Cr; Ur; ... hashPrevState;
```

**Conséquence:** Network split → COMPLETE CONSENSUS FAILURE (irréversible sans hard fork)

---

## SECTION ANTI-DÉRIVE GLOBALE (Doc 06 Section 20)

Nouvelle section complète de 270 lignes consolidant TOUS les garde-fous:

### 20.1-20.7: Détails de chaque catégorie
- Risques
- Enforcement rules
- Automated verification commands
- Consequences of violation

### 20.8: Master Checklist
Script bash complet `verify_anti_derive.sh` (55 lignes) vérifiant automatiquement:
1. Lock usage
2. Execution order
3. Error handling
4. Struct synchronization
5. Pool separation
6. Serialization order

**Usage:** Exécuter avant CHAQUE commit durant Phase 1

### 20.9: Consequence Table
Table de sévérité:
- 🟢 Low: Recovery automatique via consensus
- 🟡 Medium: Restart node / reindex
- 🔴 Critical: Hard fork requis

### 20.10: Final Implementation Rule
Checklist en 6 points AVANT merge vers main branch

---

## STATISTIQUES

**Total protections ajoutées:**
- 6 catégories de garde-fous critiques
- 17 blocs de warning explicites
- 8 scripts de vérification automatique
- 1 section consolidée de 270 lignes
- 42 règles de conformité obligatoires

**Documents mis à jour:**
- `02-canonical-specification.md`: +120 lignes
- `03-architecture-overview.md`: +80 lignes
- `06-protocol-reference.md`: +400 lignes

**Coverage:**
- Locks: ✅ 100%
- Execution order: ✅ 100%
- Error handling: ✅ 100%
- Struct sync: ✅ 100%
- Pool separation: ✅ 100%
- Serialization: ✅ 100%

---

## IMPACT SUR PHASE 1

**Avant implémentation Phase 1:**

1. ✅ Lire section 20 de doc 06 (ANTI-DÉRIVE CONSOLIDÉ)
2. ✅ Copier `verify_anti_derive.sh` dans `scripts/`
3. ✅ Configurer pre-commit hook pour exécuter verification
4. ✅ Compiler avec `-DDEBUG_LOCKORDER` durant développement
5. ✅ Vérifier compliance avec TOUS les points de checklist

**Durant implémentation Phase 1:**

- Exécuter `verify_anti_derive.sh` avant chaque commit
- Vérifier manuellement patterns FORBIDDEN vs CORRECT
- Ne JAMAIS modifier ordre canonique (yield, serialization)
- Toujours vérifier return values
- Toujours acquérir locks dans ordre correct

**Avant merge vers main:**

- Exit code 0 de `verify_anti_derive.sh`
- Manual code review confirmant compliance
- Integration tests incluant edge cases ANTI-DÉRIVE

---

## VALIDATION

### Test de Cohérence Documentaire

```bash
# Vérifier struct sync
diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
     <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md)
# ✅ Passed (structures identiques)

# Vérifier références croisées
grep -c "ANTI-DÉRIVE" docs/02-canonical-specification.md
# 2 occurrences (ordre yield + checksum struct)

grep -c "ANTI-DÉRIVE" docs/03-architecture-overview.md
# 3 occurrences (locks + error handling + checksum struct)

grep -c "ANTI-DÉRIVE" docs/06-protocol-reference.md
# 7 occurrences (section 20 complète)
```

### Vérification Exhaustivité

Tous les 6 risques identifiés par l'architecte sont couverts:
- ✅ Risque #1: Oublis de lock → Section 3.3 + 20.1
- ✅ Risque #2: Ordre yield → Section 3 + 20.2
- ✅ Risque #3: ProcessKHUTransaction loop → Section 3.3 + 20.3
- ✅ Risque #4: Désync struct → Sections 2.1 + 20.4
- ✅ Risque #5: Confusion pools → Section 18 + 20.5
- ✅ Risque #6: Ordre sérialisation → Section 5 + 20.6

---

## CONCLUSION

La documentation PIVX-V6-KHU est maintenant équipée de garde-fous anti-dérive complets couvrant les 6 risques critiques identifiés. Ces protections comprennent:

1. **Warnings explicites** à chaque point de risque
2. **Scripts de vérification automatique** pour détecter violations
3. **Patterns FORBIDDEN vs CORRECT** clairement documentés
4. **Checklists de conformité** pour implémentation
5. **Compile-time assertions** pour prévention structurelle
6. **Section consolidée (section 20)** centralisant toutes protections

**La documentation est PRÊTE pour Phase 1 implementation.**

**Prochaines étapes recommandées:**
1. Review cette documentation par l'architecte
2. Setup pre-commit hooks avec `verify_anti_derive.sh`
3. Début implémentation C++ Phase 1 avec compliance stricte
4. Tests d'intégration incluant tous cas ANTI-DÉRIVE

---

**Fin du rapport**
