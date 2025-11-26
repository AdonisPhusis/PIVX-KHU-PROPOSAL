# AUDIT CANONICITÉ & IMPLÉMENTABILITÉ C++

**Auditeur:** Ingénieur C++ Senior
**Date:** 2025-11-22
**Scope:** Documentation technique PIVX-V6-KHU
**Standards:** C++17, PIVX Core style guide

---

## 1. ANALYSE DE LA STRUCTURE KhuGlobalState

### 1.1 Validité C++17

**Doc 02 lignes 28-47 / Doc 06 lignes 32-75:**

```cpp
struct KhuGlobalState {
    int64_t  C;
    int64_t  U;
    int64_t  Cr;
    int64_t  Ur;
    uint16_t R_annual;
    uint16_t R_MAX_dynamic;
    uint32_t last_domc_height;
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;
    uint32_t domc_phase_length;
    uint32_t last_yield_update_height;
    uint32_t nHeight;
    uint256  hashPrevState;
    uint256  hashBlock;
};
```

✅ **Validations:**
- Types standard C++17: `int64_t`, `uint16_t`, `uint32_t` ✅
- `uint256` : Type PIVX custom (défini dans `uint256.h`) ✅
- Ordre des champs: logique (économiques → DOMC → yield → chain) ✅
- Taille estimée: 8+8+8+8 + 2+2 + 4+4+4+4+4 + 4 + 32+32 = **126 bytes** (acceptable)

### 1.2 Problèmes détectés

⚠️ **Padding potentiel:**
```cpp
uint16_t R_annual;        // 2 bytes
uint16_t R_MAX_dynamic;   // 2 bytes → 4 bytes au total
uint32_t last_domc_height; // 4 bytes
```
Le compilateur peut ajouter 0-2 bytes de padding entre `R_MAX_dynamic` et `last_domc_height`.

**Impact:** Aucun (serialization via `SERIALIZE_METHODS` gère cela)

❌ **CRITIQUE - Méthode CheckInvariants() manquante dans structure:**

Doc 02/03 mentionnent `CheckInvariants()` comme méthode de la structure, mais:
- Doc 06 ligne 60-64 montre l'implémentation
- **Manque:** Cette méthode doit être ajoutée dans la définition de structure docs 02/03

**Recommandation:**
```cpp
struct KhuGlobalState {
    // ... champs ...

    bool CheckInvariants() const {
        bool cd_ok = (U == 0 || C == U);
        bool cdr_ok = (Ur == 0 || Cr == Ur);
        return cd_ok && cdr_ok;
    }
};
```

### 1.3 Serialization

**Doc 06 lignes 68-74:**
```cpp
SERIALIZE_METHODS(KhuGlobalState, obj) {
    READWRITE(obj.C, obj.U, obj.Cr, obj.Ur);
    READWRITE(obj.R_annual, obj.R_MAX_dynamic, obj.last_domc_height);
    READWRITE(obj.domc_cycle_start, obj.domc_cycle_length, obj.domc_phase_length);
    READWRITE(obj.last_yield_update_height);
    READWRITE(obj.nHeight, obj.hashBlock, obj.hashPrevState);
}
```

✅ **Validations:**
- Macro PIVX standard `SERIALIZE_METHODS` ✅
- Tous les 14 champs sérialisés ✅
- Ordre cohérent avec définition struct ✅

⚠️ **Ordre de sérialisation:**
- Ligne 72: `hashBlock` avant `hashPrevState`
- Définition struct: `hashPrevState` avant `hashBlock`

**Impact:** Mineur (les deux ordres fonctionnent)
**Recommandation:** Aligner sur ordre struct pour clarté

---

## 2. ANALYSE DES INVARIANTS

### 2.1 Implémentation CheckInvariants()

**Doc 06 lignes 60-64:**
```cpp
bool CheckInvariants() const {
    bool cd_ok = (U == 0 || C == U);
    bool cdr_ok = (Ur == 0 || Cr == Ur);
    return cd_ok && cdr_ok;
}
```

✅ **Validations:**
- Logique correcte ✅
- Short-circuit evaluation optimale (`||` et `&&`) ✅
- Const-correctness (`const`) ✅

⚠️ **Problème potentiel - Overflow:**
```cpp
(U == 0 || C == U)
```

Si `U` et `C` sont très grands (près de `INT64_MAX`), aucun problème.
Si `U` ou `C` sont négatifs (bug), l'invariant peut passer alors qu'il devrait échouer.

**Recommandation:** Ajouter vérification de non-négativité:
```cpp
bool CheckInvariants() const {
    // Vérifier non-négativité
    if (C < 0 || U < 0 || Cr < 0 || Ur < 0)
        return false;

    // Invariants
    bool cd_ok = (U == 0 || C == U);
    bool cdr_ok = (Ur == 0 || Cr == Ur);
    return cd_ok && cdr_ok;
}
```

### 2.2 Assertions vs Return

**Doc 06 ligne 852:**
```cpp
assert(state.C == state.U);
assert(state.Cr == state.Ur);
```

⚠️ **Problème:** `assert()` disparaît en release build (quand `NDEBUG` est défini)

**Contexte consensus:** Les invariants DOIVENT être vérifiés même en production.

**Recommandation:** Utiliser la méthode `CheckInvariants()` au lieu de `assert()`:
```cpp
if (!state.CheckInvariants()) {
    return state.Invalid("khu-invariant-violation");
}
```

---

## 3. ANALYSE DE ConnectBlock

### 3.1 Ordre d'exécution

**Doc 06 lignes 266-292:**
```cpp
bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        KhuGlobalState prevState = LoadKhuState(pindex->pprev);  // ✅ (1)
        KhuGlobalState newState = prevState;

        ApplyDailyYieldIfNeeded(newState, pindex->nHeight);      // ✅ (2)

        for (const auto& tx : block.vtx) {
            ProcessKHUTransaction(tx, newState, view, state);    // ✅ (3)
        }

        ApplyBlockReward(newState, pindex->nHeight);             // ✅ (4)

        if (!newState.CheckInvariants()) {                       // ✅ (5)
            return state.Invalid(...);
        }

        pKHUStateDB->WriteKHUState(newState);                    // ✅ (6)
    }
    return true;
}
```

✅ **Validations:**
- Ordre conforme à spec (doc 02 section 3.8) ✅
- Gestion d'erreur correcte (`return state.Invalid()`) ✅
- NetworkUpgradeActive guard ✅

❌ **CRITIQUE - Gestion d'erreur incomplète:**

Ligne 275-277: Boucle `ProcessKHUTransaction` ne vérifie pas le retour !

```cpp
for (const auto& tx : block.vtx) {
    ProcessKHUTransaction(tx, newState, view, state);  // ❌ Pas de vérification !
}
```

**Devrait être:**
```cpp
for (const auto& tx : block.vtx) {
    if (!ProcessKHUTransaction(tx, newState, view, state))
        return false;  // Arrêt immédiat si transaction invalide
}
```

❌ **CRITIQUE - Race condition potentielle:**

Si `LoadKhuState()` et `WriteKHUState()` ne sont pas atomiques, deux blocs peuvent se chevaucher.

**Recommandation:** Ajouter lock explicite:
```cpp
LOCK(cs_khu);
KhuGlobalState prevState = LoadKhuState(pindex->pprev);
// ... mutations ...
pKHUStateDB->WriteKHUState(newState);
```

### 3.2 ApplyDailyYield

**Doc 06 lignes 523-564:**
```cpp
void ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view) {
    AssertLockHeld(cs_khu);  // ✅ Bonne pratique

    if ((nHeight - state.last_yield_update_height) < BLOCKS_PER_DAY)
        return;

    int64_t Ur_increment = 0;

    for (const ZKHUNoteData& note : GetMatureZKHUNotes(view, nHeight)) {
        if ((nHeight - note.stakeStartHeight) < KHU_MATURITY_BLOCKS)
            continue;

        int64_t annual = (note.amount * state.R_annual) / 10000;
        int64_t daily = annual / 365;

        Ur_increment += daily;

        note.Ur_accumulated += daily;  // ❌ PROBLÈME !
        UpdateNoteData(note);
    }

    state.Ur += Ur_increment;
    state.Cr += Ur_increment;
    state.last_yield_update_height = nHeight;

    assert(state.CheckInvariants());  // ⚠️ Voir section 2.2
}
```

✅ **Validations:**
- Lock assertion ✅
- Logique yield correcte ✅
- Mise à jour atomique Ur/Cr ✅

❌ **CRITIQUE - Modification de const reference:**

Ligne 546-547:
```cpp
for (const ZKHUNoteData& note : GetMatureZKHUNotes(...)) {
    // ...
    note.Ur_accumulated += daily;  // ❌ note est const !
}
```

**Erreur de compilation:** Impossible de modifier `note.Ur_accumulated` si `note` est `const`.

**Recommandation:**
```cpp
for (ZKHUNoteData& note : GetMatureZKHUNotes(...)) {  // Enlever const
    // ... OK
}
```

OU mieux:
```cpp
std::vector<ZKHUNoteData> notes = GetMatureZKHUNotes(...);
for (auto& note : notes) {
    // ... modifier
    UpdateNoteData(note);  // Persister
}
```

### 3.3 ApplyKhuUnstake

**Doc 06 lignes 321-407:**

✅ **Validations:**
- LOCK(cs_khu) au début ✅
- Vérifications préalables avant mutations ✅
- 4 mutations atomiques consécutives ✅
- Vérification invariants après ✅

⚠️ **Optimisation possible - Pré-allocation:**

Lignes 366-370:
```cpp
int64_t principal = noteData.amount;
int64_t B = noteData.Ur_accumulated;

if (B != payload.bonus || principal != payload.principal)
    return validationState.Invalid("khu-unstake-amount-mismatch");
```

**Performance:** Ces validations sont après l'extraction de données.
**Recommandation:** Valider payload AVANT d'extraire `noteData` (économie DB lookup)

---

## 4. ANALYSE TYPES ET LIMITES

### 4.1 Overflow int64_t

**Max supply KHU_T:**
- Émission totale PIV: 33,112,800 PIV (doc 07)
- Si tout est converti en KHU: 33,112,800 KHU_T
- En satoshis: 33,112,800 × 10^8 = 3,311,280,000,000,000 satoshis
- `INT64_MAX` = 9,223,372,036,854,775,807

**Marge de sécurité:** 2785× (très sûr ✅)

**Yield maximal:**
- R_MAX initial = 3000 basis points (30%)
- Sur 1 million KHU pendant 100 ans à 30%: énorme mais toujours < INT64_MAX ✅

### 4.2 Division par zéro

**Doc 06 ligne 537:**
```cpp
int64_t annual = (note.amount * state.R_annual) / 10000;
int64_t daily = annual / 365;
```

✅ Pas de risque: 10000 et 365 sont des constantes non-nulles

**Doc 06 ligne 771:**
```cpp
uint32_t days_staked = (current_height - stake_start) / BLOCKS_PER_DAY;
```

✅ `BLOCKS_PER_DAY = 1440` (constant non-null)

### 4.3 Integer overflow multiplication

**Doc 06 ligne 536:**
```cpp
int64_t annual = (note.amount * state.R_annual) / 10000;
```

**Analyse:**
- `note.amount` : max ~33 million × 10^8 = 3.3 × 10^15
- `state.R_annual` : max 3000
- Produit: 3.3 × 10^15 × 3000 = 9.9 × 10^18

**INT64_MAX = 9.2 × 10^18**

⚠️ **CRITIQUE - Overflow possible dans cas extrême !**

Si `note.amount` est très élevé (disons 50 million KHU staked), alors:
- 50,000,000 × 10^8 × 3000 = 1.5 × 10^19 > INT64_MAX

**Recommandation:** Utiliser multiplication 128-bit ou réorganiser:
```cpp
int64_t annual = (note.amount / 10000) * state.R_annual;  // Division d'abord
int64_t daily = annual / 365;
```

OU utiliser arithmétique 128-bit sécurisée:
```cpp
arith_uint256 annual_big = arith_uint256(note.amount) * state.R_annual / 10000;
if (annual_big > INT64_MAX)
    return error("yield overflow");
int64_t annual = annual_big.GetLow64();
```

---

## 5. ANALYSE PERFORMANCE

### 5.1 Yield calculation loop

**Doc 06 ligne 534:**
```cpp
for (const ZKHUNoteData& note : GetMatureZKHUNotes(view, nHeight)) {
```

**Complexité:** O(n) où n = nombre de notes ZKHU actives

**Problème:** Si n est grand (disons 100,000 notes), le calcul peut être lent.

**Mitigation existante:**
- Yield calculé seulement tous les 1440 blocs (1 fois par jour)
- 100,000 notes × ~100 ops = 10 millions ops → ~10ms (acceptable)

✅ Performance acceptable

### 5.2 State serialization

**Doc 06 ligne 626:**
```cpp
batch.Write(std::make_pair('K', std::make_pair('S', state.nHeight)), state);
```

**Taille:** 126 bytes par état
**Fréquence:** Chaque bloc (1 minute)
**Impact:** Négligeable ✅

### 5.3 Database lookups

**GetZKHUNoteData (ligne 345):**
```cpp
if (!GetZKHUNoteData(spend.nullifier, noteData))
```

**Chaque UNSTAKE nécessite:**
- 1 lookup nullifier → note
- 1 write pour marquer nullifier dépensé

**Index requis:** Nullifier → NoteData (B-tree LevelDB)

✅ Performance acceptable (O(log n) lookup)

---

## 6. ANALYSE SÉCURITÉ

### 6.1 Race conditions

❌ **CRITIQUE - cs_khu lock scope:**

**Problème détecté dans plusieurs endroits:**

Doc 06 ligne 330:
```cpp
LOCK(cs_khu);  // Dans ApplyKhuUnstake
```

Doc 06 ligne 526:
```cpp
AssertLockHeld(cs_khu);  // Dans ApplyDailyYield
```

**Question:** `cs_khu` est-il acquis dans `ConnectBlock()` avant d'appeler ces fonctions ?

**Réponse:** NON (doc 06 ligne 266-292 ne montre pas de LOCK)

**Conséquence:** `ApplyDailyYield` va planter avec `AssertLockHeld` si appelé depuis `ConnectBlock` sans lock.

**Recommandation:** SOIT:
1. Acquérir `cs_khu` au début de `ConnectBlock()`
2. OU enlever `AssertLockHeld` dans `ApplyDailyYield`

**Meilleure pratique:** Option 1 (lock global dans ConnectBlock)

### 6.2 Integer underflow

**Doc 06 ligne 389-390:**
```cpp
state.Cr -= B;
state.Ur -= B;
```

**Protection existante (ligne 368-370):**
```cpp
if (state.Cr < B || state.Ur < B)
    return validationState.Invalid("khu-unstake-insufficient-rewards");
```

✅ Underflow impossible (vérifié avant mutation)

### 6.3 Nullifier double-spend

**Doc 06 ligne 335-336:**
```cpp
if (zkhuNullifierSet.count(spend.nullifier))
    return validationState.Invalid("khu-unstake-double-spend");
```

✅ Protection correcte

**Mais:** `zkhuNullifierSet` doit être persistant (pas juste en mémoire).

**Recommandation:** Vérifier que `zkhuNullifierSet` est stocké dans LevelDB.

---

## RÉSUMÉ EXÉCUTIF

### ✅ Points forts

1. Structure KhuGlobalState bien définie (14 champs, 126 bytes)
2. Invariants logiquement corrects
3. Ordre ConnectBlock conforme à spec
4. Atomicité UNSTAKE correctement implémentée (4 mutations consécutives)
5. Pas de risque overflow général (marge 2785×)
6. Performance acceptable (yield O(n), 1 fois/jour)
7. Protection underflow correcte (vérification avant mutation)

### ❌ Problèmes critiques

1. **CheckInvariants() manquante** dans définition struct (docs 02/03)
2. **Gestion d'erreur incomplète** dans boucle ProcessKHUTransaction (doc 06 ligne 275)
3. **Lock manquant** dans ConnectBlock (doc 06 ligne 266)
4. **AssertLockHeld** va planter si lock non acquis (doc 06 ligne 526)
5. **Modification const reference** dans ApplyDailyYield (doc 06 ligne 546)
6. **Overflow multiplication** possible dans cas extrême (doc 06 ligne 536)

### ⚠️ Problèmes mineurs

1. Ordre sérialisation vs struct (hashBlock/hashPrevState)
2. Padding potentiel (uint16_t → uint32_t)
3. assert() au lieu de CheckInvariants() (disparaît en release)
4. Optimisation pré-validation payload possible

---

## RECOMMANDATIONS PRIORITAIRES

### CRITIQUE (MUST FIX avant Phase 1):

1. **Ajouter CheckInvariants() dans struct:**
```cpp
struct KhuGlobalState {
    // ... champs ...
    bool CheckInvariants() const;
};
```

2. **Corriger boucle ProcessKHUTransaction:**
```cpp
for (const auto& tx : block.vtx) {
    if (!ProcessKHUTransaction(...))
        return false;
}
```

3. **Ajouter LOCK(cs_khu) dans ConnectBlock:**
```cpp
bool ConnectBlock(...) {
    if (NetworkUpgradeActive(...)) {
        LOCK(cs_khu);  // ← AJOUTER
        // ... reste du code ...
    }
}
```

4. **Corriger const reference dans ApplyDailyYield:**
```cpp
for (ZKHUNoteData& note : GetMatureZKHUNotes(...)) {  // Enlever const
```

5. **Sécuriser multiplication yield:**
```cpp
int64_t annual = (note.amount / 10000) * state.R_annual;  // Division d'abord
```

### IMPORTANT (SHOULD FIX avant Phase 2):

6. Remplacer assert() par CheckInvariants() + return Invalid()
7. Vérifier non-négativité dans CheckInvariants()
8. Aligner ordre sérialisation sur ordre struct
9. Optimiser pré-validation payload

### NICE TO HAVE:

10. Uniformiser langue commentaires (anglais)
11. Ajouter métriques performance (logging yield calculation time)

---

**Score d'implémentabilité: 75/100**
- **Structure:** 95/100
- **Logique:** 90/100
- **Sécurité:** 60/100 (problèmes locks critiques)
- **Performance:** 85/100

**Conclusion:** Code documenté est **implémentable** mais nécessite **corrections critiques** avant Phase 1.
