# 🔴 ATTAQUE OVERFLOW - CASSER C==U

## VECTEUR D'ATTAQUE: Integer Overflow MINT

### Code Vulnérable
```cpp
// khu_mint.cpp ligne 152-153
state.C += amount;  // ⚠️ Pas de vérification overflow AVANT
state.U += amount;  // ⚠️ Pas de vérification overflow AVANT
```

### Scénario d'Attaque

**État Initial:**
```
C = 9223372036854775807  (MAX_INT64 = 2^63 - 1)
U = 9223372036854775807
```

**Transaction MINT:**
```
amount = 1  (1 satoshi)
```

**Exécution:**
```cpp
state.C += 1;  // C = 9223372036854775807 + 1
               // → OVERFLOW → C = -9223372036854775808 (MIN_INT64)

state.U += 1;  // U = 9223372036854775807 + 1
               // → OVERFLOW → U = -9223372036854775808
```

**Résultat:**
```
C = -9223372036854775808
U = -9223372036854775808
```

**Vérification Invariants (ligne 156):**
```cpp
bool CheckInvariants() const {
    if (C < 0 || U < 0 || Cr < 0 || Ur < 0) {
        return false;  // ✅ DÉTECTÉ!
    }
    // ...
}
```

**Défense:** ✅ BLOQUÉ par check `C < 0`

---

## VECTEUR D'ATTAQUE: Wraparound Positif

### Scénario Plus Subtil

**État Initial:**
```
C = 9223372036854775806  (MAX_INT64 - 1)
U = 9223372036854775806
```

**Transaction MINT:**
```
amount = 2
```

**Exécution:**
```cpp
state.C += 2;  // C = 9223372036854775806 + 2
               // → OVERFLOW (undefined behavior en C++)
               // Selon compilateur: peut donner MIN_INT64 + 1

state.U += 2;  // Même chose
```

**Comportement Compilateur:**
- GCC avec optimisations: Peut assumer "pas d'overflow" → undefined behavior
- MSVC: Wraparound vers négatif
- Clang: Peut optimiser en supposant pas d'overflow

**Résultat Possible (selon compilateur):**
```
C = -9223372036854775807  (MIN_INT64 + 1)
U = -9223372036854775807
```

**Défense:** ✅ BLOQUÉ par check `C < 0`

---

## VECTEUR D'ATTAQUE: Overflow Différentiel

### Attaque Sophistiquée

**Hypothèse:** Exploit timing entre les deux lignes

**Code:**
```cpp
state.C += amount;  // ← Ligne 152
// ⚠️ FENÊTRE DE TEMPS (quelques cycles CPU)
state.U += amount;  // ← Ligne 153
```

**Attaque:**
1. Thread 1: Exécute MINT avec overflow sur C
2. Thread 2: Entre la ligne 152 et 153, lit l'état
3. Thread 2 voit: C = overflowed, U = old value
4. **C != U observable!**

**Défense:** ✅ BLOQUÉ par `cs_khu` lock (ligne 130)
```cpp
AssertLockHeld(cs_khu);
```

Lock pris dans ProcessKHUBlock(), donc pas de concurrent access.

---

## VECTEUR D'ATTAQUE: Overflow Check Bypass

### Tentative de Bypass CheckInvariants

**Question:** Peut-on faire overflow qui donne C == U mais valeurs incorrectes?

**Scénario:**
```
C = 9223372036854775807
U = 9223372036854775807
amount = 10

C += 10 → overflow → C = -9223372036854775799
U += 10 → overflow → U = -9223372036854775799

C == U → true ✅
Mais C < 0 → DÉTECTÉ ✅
```

**Défense:** ✅ BLOQUÉ par check `C < 0` ligne 95

---

## VECTEUR D'ATTAQUE: Overflow Asymétrique

### Exploitation de UB (Undefined Behavior)

**Problème:** En C++, signed integer overflow est **undefined behavior**

**Attaque Théorique:**
```cpp
// Si compilateur optimise différemment C et U:
state.C += amount;  // Compiler assume no overflow → optimized
state.U += amount;  // Compiler assume no overflow → optimized differently
```

**Scénario:**
- Compilateur peut optimiser `C += amount` en assumant pas d'overflow
- Si overflow se produit, comportement imprévisible
- Peut donner C != U après overflow

**Exploitation:**
1. Créer état proche MAX_INT64
2. MINT avec amount qui cause overflow
3. UB du compilateur peut donner C != U

**Probabilité:** FAIBLE mais POSSIBLE selon optimisations

**Défense Actuelle:** Check post-invariant (ligne 156) détecte si C != U après mutation

**MAIS:** Si UB corrompt memory ou donne résultats imprévisibles, check peut être bypassé

---

## ÉVALUATION

| Attaque | Bloquée | Défense |
|---------|---------|---------|
| Overflow simple (négatif) | ✅ OUI | C < 0 check |
| Wraparound | ✅ OUI | C < 0 check |
| Race condition | ✅ OUI | cs_khu lock |
| Overflow asymétrique (UB) | ⚠️ POSSIBLE | Post-invariant check (fragile) |

---

## RECOMMANDATION CRITIQUE

**AJOUTER VÉRIFICATION OVERFLOW AVANT MUTATION:**

```cpp
// khu_mint.cpp ligne 152 (AVANT mutation)
// CHECK OVERFLOW AVANT d'incrémenter
if (state.C > (std::numeric_limits<CAmount>::max() - amount)) {
    return error("ApplyKHUMint: Overflow would occur (C=%d amount=%d)",
                 state.C, amount);
}

if (state.U > (std::numeric_limits<CAmount>::max() - amount)) {
    return error("ApplyKHUMint: Overflow would occur (U=%d amount=%d)",
                 state.U, amount);
}

state.C += amount;  // Safe now
state.U += amount;  // Safe now
```

**Même chose pour REDEEM (underflow):**

```cpp
// khu_redeem.cpp ligne 154 (AVANT mutation)
// CHECK déjà présent ligne 143 mais renforcer:
if (state.C < amount) {
    return error("ApplyKHURedeem: Underflow C (C=%d amount=%d)", state.C, amount);
}

if (state.U < amount) {
    return error("ApplyKHURedeem: Underflow U (U=%d amount=%d)", state.U, amount);
}

state.C -= amount;  // Safe now
state.U -= amount;  // Safe now
```

---

## PRIORITÉ

**SÉVÉRITÉ:** MOYENNE à HAUTE
**PROBABILITÉ:** FAIBLE (nécessite état proche MAX_INT64)
**IMPACT:** CRITIQUE si exploité (corruption invariants)

**ACTION:** Ajouter checks overflow AVANT mutations
