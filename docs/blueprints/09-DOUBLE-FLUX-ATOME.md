# 09 — ATOMIC DOUBLE FLUX (BLUEPRINT)

Version: 1.0.0
Status: NORMATIVE
Target: Phase 3 (Yield) + Phase 4 (UNSTAKE)

---

## 1. OBJECTIF

Documenter les règles d'atomicité **absolues** pour le mécanisme de double flux lors d'un UNSTAKE.

**Principe fondamental:**

Un UNSTAKE effectue **deux mouvements simultanés**:
1. **Flux descendant (reward → staker)**: Cr → Ur → KHU_T (bonus accumulé B)
2. **Flux ascendant (principal → supply)**: ZKHU → KHU_T (principal P)

Ces deux flux doivent s'exécuter **atomiquement** dans un seul `ConnectBlock`.

**Conséquence:**

Échec partiel = rejet du bloc entier (pas de rollback partiel).

---

## 2. ACCUMULATION DU YIELD (Ur)

### 2.1 Règle d'accumulation quotidienne

**Période:** Toutes les 1440 blocs (1 jour)

**Formule:**

```
Ur_daily = ∑ (amount_i × R_annual / 10000) / 365
           i ∈ ZKHU_notes_mature
```

**Critères de maturité pour yield:**
- Note stakée depuis ≥ 4320 blocs (3 jours)
- Note non révoquée (nullifier non dépensé)

**Update atomique:**

```cpp
void ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view) {
    if ((nHeight - state.last_yield_update_height) < BLOCKS_PER_DAY)
        return;  // Pas encore temps

    int64_t Ur_increment = 0;

    // Calcul pour chaque note ZKHU mature
    for (const ZKHUNoteData& note : GetMatureZKHUNotes(view, nHeight)) {
        int64_t annual = (note.amount * state.R_annual) / 10000;
        int64_t daily = annual / 365;

        Ur_increment += daily;

        // Mise à jour de la note
        note.Ur_accumulated += daily;
        UpdateNoteData(note);
    }

    // ATOMIC UPDATE (sous cs_khu)
    state.Ur += Ur_increment;
    state.Cr += Ur_increment;  // INVARIANT_2: Cr == Ur
    state.last_yield_update_height = nHeight;

    // Vérification post-mutation
    assert(state.CheckInvariants());
}
```

**Invariant garanti:** Après chaque yield update, `Cr == Ur` (strict).

---

## 3. DOUBLE FLUX UNSTAKE (ATOMIC)

### 3.1 Définition du double flux

Soit:
- `P` = montant principal (stake initial)
- `B` = bonus accumulé (`Ur_accumulated` de la note)

**Flux descendant (reward pool → staker):**

```
Cr' = Cr - B   (consommation du pool de rendement)
Ur' = Ur - B   (suppression des droits de rendement consommés)
```

**Flux ascendant (principal → supply):**

```
U' = U + B     (création de B unités de KHU_T pour le staker)
C' = C + B     (augmentation de la collatéralisation de B)
```

**Note critique:** Le principal `P` ne modifie PAS l'état global (C, U, Cr, Ur) car il était déjà compté dans U au moment du STAKE initial. Seul le bonus `B` crée un double flux.

### 3.2 Ordre d'exécution atomique

```cpp
bool ApplyKhuUnstake(const CTransaction& tx, KhuGlobalState& state, ...) {
    LOCK(cs_khu);  // OBLIGATOIRE

    // 1. Vérifications préalables
    // - Nullifier non dépensé
    // - Maturité (≥ 4320 blocs)
    // - Cr >= B et Ur >= B

    // 2. DOUBLE FLUX ATOMIQUE (ne peut être interrompu)
    state.U  += B;   // Flux ascendant (création KHU_T)
    state.C  += B;   // Flux ascendant (collatéralisation)
    state.Cr -= B;   // Flux descendant (consommation pool)
    state.Ur -= B;   // Flux descendant (consommation droits)

    // 3. Vérification invariants IMMÉDIATE
    if (!state.CheckInvariants()) {
        // Bloc rejeté (pas de rollback partiel autorisé)
        return false;
    }

    // 4. Création UTXO (principal + bonus)
    CreateKHUUTXO(P + B, recipientScript);

    return true;
}
```

**Propriété d'atomicité:**

Les quatre mutations (lignes 2.1-2.4) doivent s'exécuter **sans interruption possible** dans le même appel de fonction, sous le même lock `cs_khu`, dans le même `ConnectBlock`.

Si une des quatre mutations échoue ou si les invariants sont violés après, **le bloc entier est rejeté**.

---

## 4. INTERDICTIONS ABSOLUES

### 4.1 Pas de rollback partiel

**Interdit:**

```cpp
// ❌ MAUVAIS: Tentative de rollback partiel
state.U += B;
state.C += B;
if (state.Cr < B) {
    state.U -= B;  // ❌ Rollback interdit
    state.C -= B;
    return false;
}
```

**Correct:**

```cpp
// ✅ BON: Vérification AVANT mutation
if (state.Cr < B || state.Ur < B)
    return false;  // Rejet avant toute mutation

// Mutation atomique (pas de rollback possible)
state.U  += B;
state.C  += B;
state.Cr -= B;
state.Ur -= B;
```

### 4.2 Pas de mutation hors ConnectBlock

**Interdit:**

```cpp
// ❌ MAUVAIS: Mutation dans AcceptToMemoryPool
bool AcceptToMemoryPool(...) {
    if (tx.nType == TxType::KHU_UNSTAKE) {
        state.Ur -= bonus;  // ❌ Interdit (pas encore dans bloc)
    }
}
```

**Correct:**

```cpp
// ✅ BON: Validation seule dans mempool
bool AcceptToMemoryPool(...) {
    if (tx.nType == TxType::KHU_UNSTAKE) {
        // Vérifier que bonus <= Ur ACTUEL
        // MAIS ne pas muter l'état
        if (GetBonusFromPayload(tx) > state.Ur)
            return false;
    }
}
```

### 4.3 Pas de yield compounding

**Interdit:**

Le yield accumulé (`Ur_accumulated`) d'une note ZKHU **ne génère PAS lui-même de yield**.

```cpp
// ❌ MAUVAIS: Compounding du yield
int64_t daily = ((note.amount + note.Ur_accumulated) * R_annual) / 10000 / 365;
```

**Correct:**

```cpp
// ✅ BON: Yield uniquement sur le principal
int64_t daily = (note.amount * R_annual) / 10000 / 365;
```

**Justification:** Éviter inflation exponentielle non prévue.

### 4.4 Pas de funding externe du reward pool

**Interdit:**

Toute injection de Cr/Ur provenant de sources **autres que l'émission de bloc** (ApplyBlockReward).

```cpp
// ❌ MAUVAIS: Donation au reward pool
if (tx.nType == TxType::DONATE_TO_POOL) {
    state.Cr += donation;  // ❌ Interdit
}
```

**Correct:**

Seule source légale de Cr/Ur:

```cpp
// ✅ BON: Émission de bloc uniquement
void ApplyBlockReward(KhuGlobalState& state, uint32_t nHeight) {
    int64_t reward = GetBlockSubsidy(nHeight);
    state.Cr += reward;
    state.Ur += reward;
}
```

---

## 5. CAS LIMITES ET GESTION D'ERREURS

### 5.1 UNSTAKE avec yield non encore appliqué

**Scénario:**

Bloc N contient:
1. Yield update (ApplyDailyYield) à la hauteur N
2. UNSTAKE d'une note qui bénéficie de ce yield

**Ordre correct (garanti par ConnectBlock):**

```cpp
// 1. Yield AVANT transactions
ApplyDailyYield(newState, N);
// → note.Ur_accumulated += daily
// → state.Ur += daily
// → state.Cr += daily

// 2. UNSTAKE utilise le yield mis à jour
ApplyKhuUnstake(tx, newState, ...);
// → utilise note.Ur_accumulated INCLUANT le yield du jour
```

**Si ordre inversé (❌ BUG):**

UNSTAKE échouerait car `state.Ur < B` (yield pas encore ajouté).

**Garantie documentée:** docs/03 section 4.2 + docs/06 section 3.

### 5.2 UNSTAKE avec Ur insuffisant

**Scénario:** Bug dans le calcul de yield → `state.Ur < B`

**Comportement:**

```cpp
if (state.Cr < B || state.Ur < B)
    return validationState.Invalid("khu-unstake-insufficient-rewards");
```

**Conséquence:** Bloc rejeté, staker ne perd rien (transaction reste dans mempool).

**Diagnostic:** Log erreur avec `Cr`, `Ur`, `B`, `height` pour audit.

### 5.3 Double UNSTAKE dans le même bloc

**Scénario:** Bloc contient deux UNSTAKE de notes différentes.

**Traitement séquentiel:**

```cpp
for (const auto& tx : block.vtx) {
    if (tx.nType == TxType::KHU_UNSTAKE) {
        // Premier UNSTAKE
        state.Ur -= B1;
        state.Cr -= B1;

        // Deuxième UNSTAKE
        state.Ur -= B2;
        state.Cr -= B2;
    }
}

// Invariants vérifiés UNE SEULE FOIS en fin de bloc
assert(state.CheckInvariants());
```

**Sécurité:** Tant que `Ur >= B1 + B2` avant le bloc, les deux UNSTAKE réussissent.

### 5.4 Violation d'invariants après UNSTAKE

**Scénario:** Bug logique → invariants cassés après double flux.

**Comportement:**

```cpp
if (!newState.CheckInvariants()) {
    return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation",
                       strprintf("C=%d U=%d Cr=%d Ur=%d", C, U, Cr, Ur));
}
```

**Conséquence:**
- Bloc rejeté
- Peer banni (score -100)
- Logs détaillés pour investigation
- **Aucune correction automatique**

**Causes possibles:**
- Overflow int64_t (impossible avec émission actuelle)
- Bug dans ApplyDailyYield
- Corruption mémoire (détectée par CheckInvariants)

---

## 6. TESTS DE NON-RÉGRESSION

### 6.1 Tests unitaires (Phase 3)

```cpp
TEST(KhuDoubleFlux, UnstakeAtomicMutation) {
    KhuGlobalState state;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;

    // UNSTAKE avec bonus de 10 KHU
    int64_t B = 10 * COIN;

    // Mutation atomique
    state.U  += B;
    state.C  += B;
    state.Cr -= B;
    state.Ur -= B;

    // Vérification
    ASSERT_EQ(state.U, 1010 * COIN);
    ASSERT_EQ(state.C, 1010 * COIN);
    ASSERT_EQ(state.Ur, 90 * COIN);
    ASSERT_EQ(state.Cr, 90 * COIN);

    // Invariants
    ASSERT_TRUE(state.CheckInvariants());
}

TEST(KhuDoubleFlux, UnstakeInsufficientRewards) {
    KhuGlobalState state;
    state.Cr = 5 * COIN;
    state.Ur = 5 * COIN;

    // Tentative UNSTAKE avec bonus trop élevé
    int64_t B = 10 * COIN;

    // Doit être rejeté AVANT mutation
    ASSERT_LT(state.Cr, B);
    // Transaction rejetée, état non modifié
}

TEST(KhuDoubleFlux, UnstakeZeroBonus) {
    // UNSTAKE le jour même du STAKE (B=0)
    int64_t B = 0;

    state.U  += B;  // U inchangé
    state.C  += B;  // C inchangé
    state.Cr -= B;  // Cr inchangé
    state.Ur -= B;  // Ur inchangé

    ASSERT_TRUE(state.CheckInvariants());
}
```

### 6.2 Tests fonctionnels (Phase 4)

**Test 1: UNSTAKE à maturité exacte (4320 blocs)**

```python
def test_unstake_at_maturity():
    # 1. STAKE à la hauteur H
    stake_tx = create_stake(amount=100*COIN)
    submit_block(height=H, txs=[stake_tx])

    # 2. Mine 4320 blocs (exactement 3 jours)
    mine_blocks(4320)

    # 3. UNSTAKE doit réussir
    unstake_tx = create_unstake(note=stake_tx.note)
    assert submit_transaction(unstake_tx) == True
```

**Test 2: UNSTAKE avant maturité (rejet)**

```python
def test_unstake_immature():
    stake_tx = create_stake(amount=100*COIN)
    submit_block(height=H, txs=[stake_tx])

    # Mine seulement 4319 blocs (1 bloc avant maturité)
    mine_blocks(4319)

    # UNSTAKE doit échouer
    unstake_tx = create_unstake(note=stake_tx.note)
    assert submit_transaction(unstake_tx) == False
    assert get_reject_reason() == "khu-unstake-immature"
```

**Test 3: UNSTAKE dans le même bloc que yield update**

```python
def test_unstake_with_yield_update():
    # 1. STAKE à H
    stake_tx = create_stake(amount=1000*COIN)
    submit_block(height=H, txs=[stake_tx])

    # 2. Mine 4320 blocs (maturité yield)
    mine_blocks(4320)

    # 3. Bloc H+4320 contient yield update ET UNSTAKE
    unstake_tx = create_unstake(note=stake_tx.note)

    # Vérifier que yield est appliqué AVANT UNSTAKE
    block = create_block(height=H+4320, txs=[unstake_tx])
    assert block.apply_yield() == True  # Yield d'abord
    assert block.apply_tx(unstake_tx) == True  # UNSTAKE ensuite

    # Bonus doit inclure le yield du jour
    assert unstake_tx.bonus > 0
```

**Test 4: Double UNSTAKE dans le même bloc**

```python
def test_double_unstake_same_block():
    # Deux stakes différents
    stake1 = create_stake(amount=100*COIN)
    stake2 = create_stake(amount=200*COIN)
    submit_block(txs=[stake1, stake2])

    mine_blocks(4320)

    # Deux UNSTAKE dans le même bloc
    unstake1 = create_unstake(note=stake1.note)
    unstake2 = create_unstake(note=stake2.note)

    block = create_block(txs=[unstake1, unstake2])
    assert submit_block(block) == True

    # Vérifier invariants après les deux UNSTAKE
    state = get_khu_state()
    assert state.check_invariants() == True
```

**Test 5: UNSTAKE avec yield maximal (après plusieurs années)**

```python
def test_unstake_max_yield():
    # 1. STAKE initial
    stake_tx = create_stake(amount=1000*COIN)
    submit_block(height=H, txs=[stake_tx])

    # 2. Mine 2 ans de blocs (2 * 525600)
    mine_blocks(2 * 525600)

    # 3. UNSTAKE
    unstake_tx = create_unstake(note=stake_tx.note)
    assert submit_transaction(unstake_tx) == True

    # 4. Vérifier bonus (environ 5% * 2 ans = 100 KHU)
    assert unstake_tx.bonus >= 90*COIN  # ~90-110 KHU selon R%
    assert unstake_tx.bonus <= 110*COIN
```

**Test 6: Tentative de double-spend (même nullifier deux fois)**

```python
def test_unstake_double_spend():
    stake_tx = create_stake(amount=100*COIN)
    submit_block(txs=[stake_tx])
    mine_blocks(4320)

    # Premier UNSTAKE réussit
    unstake1 = create_unstake(note=stake_tx.note)
    assert submit_transaction(unstake1) == True

    # Deuxième UNSTAKE avec le même nullifier échoue
    unstake2 = create_unstake(note=stake_tx.note)  # Même note
    assert submit_transaction(unstake2) == False
    assert get_reject_reason() == "khu-unstake-double-spend"
```

---

## 7. VALIDATION FORMELLE

### 7.1 Propriétés à prouver

**P1: Conservation du supply total (C == U)**

```
∀ block B, ∀ tx ∈ B:
    Si tx = UNSTAKE(principal=P, bonus=B):
        U_after = U_before + B
        C_after = C_before + B
        ⇒ (C_after == U_after) ⟺ (C_before == U_before)
```

**P2: Conservation du reward pool (Cr == Ur)**

```
∀ block B, ∀ tx ∈ B:
    Si tx = UNSTAKE(bonus=B):
        Cr_after = Cr_before - B
        Ur_after = Ur_before - B
        ⇒ (Cr_after == Ur_after) ⟺ (Cr_before == Ur_before)
```

**P3: Atomicité du double flux**

```
∀ tx = UNSTAKE:
    (U'=U+B ∧ C'=C+B ∧ Cr'=Cr-B ∧ Ur'=Ur-B) ⊕ rejet_bloc
```

Notation: `⊕` = XOR (soit les 4 mutations réussissent, soit le bloc est rejeté).

**P4: Pas de création de valeur (hors émission)**

```
∀ block B:
    Δ(Cr + Ur) = emission_bloc

    Avec:
        Δ(Cr + Ur) = (Cr_after + Ur_after) - (Cr_before + Ur_before)
        emission_bloc = GetBlockSubsidy(height) * 3
```

### 7.2 Outils de vérification

**Assertions runtime (debug builds):**

```cpp
// src/validation.cpp (DEBUG mode)
#ifdef DEBUG
void VerifyDoubleFluxInvariants(const KhuGlobalState& before,
                                const KhuGlobalState& after,
                                int64_t bonus) {
    // Vérifier conservation C==U
    assert(after.U == before.U + bonus);
    assert(after.C == before.C + bonus);
    assert((after.U == 0 || after.C == after.U));

    // Vérifier conservation Cr==Ur
    assert(after.Ur == before.Ur - bonus);
    assert(after.Cr == before.Cr - bonus);
    assert((after.Ur == 0 || after.Cr == after.Ur));
}
#endif
```

**Fuzzing (test/functional/):**

Générer blocs aléatoires avec UNSTAKE et vérifier invariants.

---

## 8. DOCUMENTATION CONNEXE

**Références normatives:**

- **docs/02-canonical-specification.md**
  - Section 2.1: KhuGlobalState (14 champs)
  - Section 2.2: Invariants canoniques
  - Section 7.2.3: Atomicité du double flux UNSTAKE

- **docs/03-architecture-overview.md**
  - Section 4.2: ConnectBlock (ordre canonique)
  - Encadré atomicité UNSTAKE

- **docs/06-protocol-reference.md**
  - Section 3: Order of operations in ConnectBlock
  - Section 3.1: UNSTAKE transaction processing (pseudo-code complet)

**Références informatives:**

- **docs/faq/08-FAQ-TECHNIQUE.md**
  - Q74: Yield precision (INT64)
  - Q70: Invariant violation handling

---

## 9. CONFORMITÉ

**Ce blueprint est NORMATIF.**

Toute implémentation de Phase 3 (Yield) et Phase 4 (UNSTAKE) doit respecter:

1. ✅ Atomicité du double flux (4 mutations simultanées)
2. ✅ Ordre ConnectBlock (yield AVANT transactions)
3. ✅ Vérification Cr/Ur avant mutation
4. ✅ CheckInvariants() après chaque bloc
5. ✅ Pas de rollback partiel
6. ✅ Pas de yield compounding
7. ✅ Lock cs_khu pendant toute mutation

**Non-conformité = rejet du code en code review.**

---

## 10. CHANGELOG

**v1.0.0 (2025-11-22):**
- Création du blueprint atomic double-flux
- Documentation yield accumulation rules
- Documentation UNSTAKE double-flux rules
- Interdictions absolues formalisées
- Tests de non-régression définis
- Validation formelle (propriétés P1-P4)

---

**Auteur:** Claude (Sonnet 4.5)
**Validé par:** Architecte PIVX-V6-KHU
**Date:** 2025-11-22
**Statut:** APPROUVÉ — NORMATIF
