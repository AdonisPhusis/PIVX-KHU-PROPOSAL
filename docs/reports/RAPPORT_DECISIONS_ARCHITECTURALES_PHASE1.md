# RAPPORT — DÉCISIONS ARCHITECTURALES PHASE 1

**Date:** 2025-11-22
**Branche:** khu-phase1-consensus
**Statut:** DÉCISIONS ARCHITECTURALES INTÉGRÉES DANS DOCUMENTATION

---

## RÉSUMÉ EXÉCUTIF

Suite à l'audit complet de la documentation PIVX-V6-KHU, les **décisions architecturales définitives** pour la Phase 1 ont été formalisées et intégrées dans le document 03 (Architecture Overview). Ces décisions résolvent les ambiguïtés identifiées et fournissent des directives d'implémentation claires et non-ambiguës.

**Document modifié:**
- `docs/03-architecture-overview.md` : Nouvelle section 3.5 "DÉCISIONS ARCHITECTURALES PHASE 1 (CANONIQUES)"

**Lignes ajoutées:** +354 lignes de spécifications détaillées

**Statut:** Prêt pour implémentation Phase 1

---

## DÉCISIONS ARCHITECTURALES FINALISÉES

### DÉCISION #1: Reorg Strategy = STATE-BASED

**Choix final:** Stocker l'état complet `KhuGlobalState` à chaque bloc

**Justification:**
- Finalité LLMQ limite les reorgs à max 12 blocs
- Overhead négligeable: 12 × 150 bytes = 1.8 KB
- Simplicité maximale (pas de deltas, pas de journaling)
- DisconnectBlock() = simple lecture de l'état précédent

**Alternative rejetée:** Delta-based (stocker uniquement les changements)
- Complexité accrue sans bénéfice (max 12 blocs)
- Risque d'erreur dans le calcul inverse des deltas
- Pas de gain de performance mesurable

**Implémentation:**
```cpp
bool DisconnectBlock(...) {
    LOCK2(cs_main, cs_khu);
    KhuGlobalState prev;
    pKHUStateDB->ReadKHUState(height - 1, prev);
    khuGlobalState = prev;  // Reorg en O(1)
}
```

**Code interdit:**
- ❌ Calcul de deltas inverses
- ❌ Journaling
- ❌ Snapshot tous les N blocs

---

### DÉCISION #2: Per-Note Ur Tracking = STREAMING VIA LEVELDB CURSOR

**Choix final:** Itération note par note via curseur LevelDB, batch updates via CDBBatch

**Justification:**
- Yield appliqué 1× par 1440 blocs → performance pas critique
- Évite OOM (Out Of Memory) avec >100k notes actives
- Empreinte mémoire O(1) (constante, indépendante du nombre de notes)
- CDBBatch permet updates efficaces (single atomic write)

**Alternative rejetée:** Charger toutes les notes en RAM
- Risque OOM avec large note set
- Empreinte mémoire proportionnelle au nombre de stakers
- Pas de bénéfice performance (yield 1×/jour)

**Implémentation:**
```cpp
bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight) {
    int64_t total_daily_yield = 0;
    CDBBatch batch(CLIENT_VERSION);

    std::unique_ptr<CKHUNoteCursor> cursor = pKHUNoteDB->GetCursor();

    for (cursor->SeekToFirst(); cursor->Valid(); cursor->Next()) {
        CKHUNoteData note = cursor->GetNote();
        if (note.fSpent) continue;

        int64_t daily = (note.amount / 10000) * state.R_annual / 365;
        note.Ur_accumulated += daily;
        batch.Write(std::make_pair('N', note.nullifier), note);
        total_daily_yield += daily;
    }

    state.Cr += total_daily_yield;
    state.Ur += total_daily_yield;
    pKHUNoteDB->WriteBatch(batch);

    return state.CheckInvariants();
}
```

**Code interdit:**
- ❌ `std::vector<CKHUNoteData> allNotes = LoadAllNotes();`
- ❌ Flush DB après chaque note
- ❌ Cache global de notes

---

### DÉCISION #3: Rolling Frontier Tree = MARQUAGE, PAS SUPPRESSION

**Choix final:** Arbre Sapling standard append-only, gestion des notes actives via flag `fSpent` en DB

**Justification:**
- Sapling commitment tree est append-only par design (merkle tree)
- Suppression de commitment = fork consensus (invalide merkle proofs)
- Flag `fSpent` en DB suffit pour tracking actif/inactif
- Simplicité maximale (réutilisation code Sapling standard)

**Alternative rejetée:** Pruning des commitments anciens
- Invalide les merkle proofs existants
- Nécessite nouveau type d'arbre (complexité)
- Gain de stockage négligeable (commitments = 32 bytes)

**Implémentation:**
```cpp
struct CKHUNoteData {
    uint256 commitment;        // Never deleted from tree
    uint256 nullifier;
    int64_t amount;
    int64_t Ur_accumulated;
    uint32_t nStakeStartHeight;
    bool fSpent;               // ⚠️ MARQUAGE: true if spent
};

bool ApplyKHUUnstake(...) {
    CKHUNoteData note;
    pKHUNoteDB->ReadNote(nullifier, note);

    // Mark as spent (DO NOT delete commitment)
    note.fSpent = true;
    pKHUNoteDB->WriteNote(nullifier, note);

    // Commitment remains in Sapling tree forever
}
```

**Code interdit:**
- ❌ `saplingTree.remove(commitment);`
- ❌ `pruneOldCommitments();`
- ❌ Supprimer note de DB après UNSTAKE

---

## RAPPELS CRITIQUES (OBLIGATOIRES)

Ces règles sont des **invariants d'implémentation** qui ne peuvent JAMAIS être violés.

### 1. Ordre d'exécution: Yield → ProcessKHUTransaction

**Obligatoire:**
```cpp
ApplyDailyYieldIfNeeded();      // Étape 2
ProcessKHUTransactions();        // Étape 3
```

**Raison:** UNSTAKE consomme Ur_accumulated. Si yield pas appliqué, bonus calculé faux → invariant violation.

**Forbidden:**
```cpp
ProcessKHUTransactions();        // ❌ WRONG ORDER
ApplyDailyYieldIfNeeded();
```

---

### 2. Double flux UNSTAKE = atomique

**Obligatoire:**
```cpp
state.U  += B;   // Les 4 updates dans la même fonction
state.C  += B;   // sous le même lock cs_khu
state.Cr -= B;   // pas de return/break entre
state.Ur -= B;   // vérification invariants immédiate
```

**Forbidden:**
```cpp
state.U += B;
if (someCondition) return false;  // ❌ Rollback partiel interdit
state.C += B;
```

---

### 3. C/U et Cr/Ur = jamais séparés

**Obligatoire:**
```cpp
state.C += amount;
state.U += amount;  // Toujours ensemble
```

**Forbidden:**
```cpp
state.C += amount;
DoSomethingElse();  // ❌ Code entre mutations
state.U += amount;
```

---

### 4. Lock order: LOCK2(cs_main, cs_khu)

**Obligatoire:**
```cpp
LOCK2(cs_main, cs_khu);  // Ordre MANDATORY
```

**Forbidden:**
```cpp
LOCK(cs_khu);
LOCK(cs_main);  // ❌ Deadlock risk
```

---

### 5. Remplacer assert() → Invalid()

**Obligatoire:**
```cpp
if (!state.CheckInvariants())
    return state.Invalid("khu-invariant-violation");
```

**Forbidden:**
```cpp
assert(state.CheckInvariants());  // ❌ Crash node
```

---

### 6. Overflow yield: division avant multiplication

**Obligatoire:**
```cpp
int64_t daily = (note.amount / 10000) * R_annual / 365;
```

**Forbidden:**
```cpp
int64_t daily = (note.amount * R_annual) / 10000 / 365;  // ❌ Overflow
```

---

### 7. ProcessKHUTransaction: return bool et testé

**Obligatoire:**
```cpp
if (!ProcessKHUTransaction(tx, state, view, validationState))
    return false;  // Reject block
```

**Forbidden:**
```cpp
ProcessKHUTransaction(tx, ...);  // ❌ Return value ignored
```

---

## MODIFICATIONS APPORTÉES

### docs/03-architecture-overview.md

**Section ajoutée:** 3.5 DÉCISIONS ARCHITECTURALES PHASE 1 (CANONIQUES)

**Sous-sections:**
- 3.5.1 Reorg Strategy = STATE-BASED (DÉCISION FINALE)
- 3.5.2 Per-Note Ur Tracking = STREAMING VIA LEVELDB CURSOR (DÉCISION FINALE)
- 3.5.3 Rolling Frontier Tree = MARQUAGE, PAS SUPPRESSION (DÉCISION FINALE)
- 3.5.4 Rappels Critiques (OBLIGATOIRES)

**Lignes ajoutées:** 354 lignes
- Justifications détaillées pour chaque choix
- Alternatives rejetées avec raisons
- Code d'implémentation canonique
- Code forbidden avec explications
- Commandes de validation bash

**Placement:** Après section 3.4 (Reorg Handling), avant section 4 (Intégration Consensus)

---

## VALIDATION

### Cohérence avec documentation existante

**Vérification 1:** Décisions alignées avec doc 02 (Canonical Specification)
```bash
# Vérifier que KhuGlobalState est identique
diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
     <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md)
```
✅ **Passed:** Structures identiques (14 champs)

**Vérification 2:** Ordre d'exécution ConnectBlock() respecté
```bash
# Vérifier ordre canonique dans doc 02 et doc 03
grep -A10 "Ordre canonique ConnectBlock" docs/02-canonical-specification.md
grep -A20 "Ordre d'exécution canonique" docs/03-architecture-overview.md
```
✅ **Passed:** Ordre identique (Yield → Transactions)

**Vérification 3:** Rappels critiques alignés avec ANTI-DÉRIVE (doc 06)
```bash
# Vérifier mentions LOCK2, yield order, etc.
grep -c "LOCK2.*cs_khu" docs/03-architecture-overview.md
# Expected: au moins 3 occurrences
```
✅ **Passed:** 4 occurrences

---

### Complétude des décisions

**Checklist:**
- ✅ Reorg strategy clairement définie (STATE-BASED)
- ✅ Per-note Ur tracking clairement défini (STREAMING)
- ✅ Rolling Frontier Tree clairement défini (MARQUAGE)
- ✅ Alternatives rejetées documentées avec raisons
- ✅ Code canonique fourni pour chaque décision
- ✅ Code forbidden documenté pour chaque décision
- ✅ Commandes de validation fournies
- ✅ Rappels critiques inclus (7 règles)

---

### Non-ambiguïté

**Questions résolues:**

**Q1:** Faut-il stocker deltas ou état complet pour reorg?
**R:** **État complet** (STATE-BASED). Décision finale doc 03 section 3.5.1.

**Q2:** Faut-il charger toutes les notes en RAM pour yield?
**R:** **NON. Streaming via cursor.** Décision finale doc 03 section 3.5.2.

**Q3:** Faut-il pruner les commitments Sapling anciens?
**R:** **NON. Append-only + flag fSpent.** Décision finale doc 03 section 3.5.3.

**Q4:** Quel ordre pour yield et transactions?
**R:** **Yield AVANT transactions.** Rappel critique #1 doc 03 section 3.5.4.

---

## IMPACT SUR IMPLÉMENTATION PHASE 1

**Avant cette mise à jour:**
- Ambiguïté sur reorg strategy (delta vs state)
- Ambiguïté sur per-note tracking (RAM vs streaming)
- Ambiguïté sur frontier tree (pruning vs append-only)

**Après cette mise à jour:**
- ✅ Toutes les décisions architecturales sont **CLAIRES** et **NON-AMBIGUËS**
- ✅ Code canonique fourni pour chaque composant critique
- ✅ Code forbidden explicitement interdit
- ✅ Commandes de validation automatisées disponibles

**Prochaines étapes:**

1. ✅ **Documentation complète** (décisions intégrées)
2. ⏭️ **Implémentation C++ Phase 1** (suivre section 3.5)
3. ⏭️ **Tests unitaires** (valider décisions)
4. ⏭️ **Code review** (vérifier compliance avec section 3.5)

---

## RÈGLES DE COMPLIANCE

Toute implémentation Phase 1 DOIT:

1. Suivre **EXACTEMENT** le code canonique de la section 3.5
2. **JAMAIS** implémenter le code forbidden de la section 3.5
3. Passer **TOUTES** les commandes de validation bash de la section 3.5
4. Respecter **TOUS** les rappels critiques de la section 3.5.4

Toute violation de ces règles constitue une **violation architecturale** et sera rejetée en code review.

---

## STATISTIQUES

**Document modifié:**
- `docs/03-architecture-overview.md`

**Lignes ajoutées:** +354 lignes
- Section 3.5: 350 lignes
- Ajustement numérotation sections: 4 lignes

**Sections ajoutées:** 1 section principale (3.5) avec 4 sous-sections

**Code examples:** 15 blocs de code canonique + 10 blocs de code forbidden

**Commandes de validation:** 8 scripts bash automatisés

**Rappels critiques:** 7 règles obligatoires

---

## CONCLUSION

Les **décisions architecturales définitives** pour PIVX-V6-KHU Phase 1 ont été formalisées et intégrées dans la documentation officielle (doc 03 section 3.5).

**Statut:** READY FOR IMPLEMENTATION

**Compliance:** Toute implémentation Phase 1 doit suivre **strictement** la section 3.5 du document 03.

**Prochaine étape:** Commencer implémentation C++ en suivant le code canonique de la section 3.5.

---

**Fin du rapport**
