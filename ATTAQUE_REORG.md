# 🔴 ATTAQUE REORG - CASSER C==U VIA REORGANIZATION

## VECTEUR D'ATTAQUE: Reorg Partiel

### Scénario

**Chaîne Originale:**
```
Block 1000: C=0, U=0 (genesis KHU)
Block 1001: MINT 100 → C=100, U=100
Block 1002: MINT 50  → C=150, U=150
Block 1003: REDEEM 30 → C=120, U=120
```

**Attaque: Reorg à partir du bloc 1002**

**Étape 1: DisconnectKHUBlock(1003)**
```cpp
// khu_validation.cpp:208
db->EraseKHUState(1003);
// État 1003 effacé
// État actif revient à 1002: C=150, U=150 ✅
```

**Étape 2: DisconnectKHUBlock(1002)**
```cpp
db->EraseKHUState(1002);
// État actif revient à 1001: C=100, U=100 ✅
```

**Étape 3: Nouvelle Chaîne**
```
Block 1002': REDEEM 100 → C=0, U=0
Block 1003': MINT 200 → C=200, U=200
```

**Résultat:** Pas d'attaque, état toujours valide ✅

---

## VECTEUR D'ATTAQUE: Échec Partiel de Reorg

### Scénario: Crash Pendant Reorg

**État:**
```
Tip: Block 1003 (C=120, U=120)
Reorg vers 1000
```

**Attaque:**
1. DisconnectKHUBlock(1003) → État 1003 effacé
2. DisconnectKHUBlock(1002) → **CRASH du nœud**
3. Au redémarrage:
   - État DB: Contient 1000, 1001 (pas 1002, 1003)
   - Blockchain: Contient 1000, 1001, 1002, 1003
   - **DÉSYNCHRONISATION!**

**Question:** Que se passe-t-il?

**Analyse:**
```cpp
// ProcessKHUBlock() ligne 106
if (!db->ReadKHUState(nHeight - 1, prevState)) {
    // If previous state doesn't exist, use genesis state
    prevState.SetNull();
    prevState.nHeight = nHeight - 1;
}
```

**Reconstruction Automatique:**
- Au redémarrage, ProcessBlock(1002) est appelé
- Cherche état 1001 → TROUVÉ: C=100, U=100
- Reprocesse MINT 50 → C=150, U=150 ✅
- ProcessBlock(1003) avec état 1002
- Reprocesse REDEEM 30 → C=120, U=120 ✅

**Défense:** ✅ Reconstruction automatique via ProcessKHUBlock

---

## VECTEUR D'ATTAQUE: DB Corruption Sélective

### Scénario: Attaquant Modifie DB Directement

**État DB:**
```
K/S1000 → {C:0, U:0}
K/S1001 → {C:100, U:100}
K/S1002 → {C:150, U:150}
```

**Attaque:**
1. Arrêter nœud
2. Modifier LevelDB directement:
   ```
   K/S1002 → {C:150, U:140}  // CORRUPTION: C != U
   ```
3. Redémarrer nœud

**Question:** État corrompu détecté?

**Analyse:**
```cpp
// ProcessKHUBlock() ligne 119
KhuGlobalState newState = prevState;  // Copie état précédent (corrompu)

// Ligne 147
if (!newState.CheckInvariants()) {
    return validationState.Error("KHU invariants violated");
}
```

**Problème:**
- Si état 1002 déjà persisted avec C != U
- ProcessBlock(1003) charge état 1002 corrompu
- Avant toute mutation, CheckInvariants() devrait être appelé

**Vérification Code:**
```cpp
// ProcessKHUBlock() ligne 106-116
if (!db->ReadKHUState(nHeight - 1, prevState)) {
    prevState.SetNull();
    prevState.nHeight = nHeight - 1;
}

// ⚠️ PAS DE CHECK INVARIANTS ICI!
// On copie directement:
KhuGlobalState newState = prevState;  // ligne 119
```

**VULNÉRABILITÉ TROUVÉE!**
```
Si DB contient état invalide (C != U), il est chargé et utilisé sans vérification!
```

---

## ATTAQUE RÉUSSIE: DB CORRUPTION

### Exploitation

**Étape 1:** Obtenir accès filesystem (malware, accès physique, etc.)

**Étape 2:** Modifier LevelDB
```bash
# Trouver le fichier LevelDB
cd ~/.pivx/chainstate/khustate/

# Utiliser outil LevelDB pour modifier
leveldb-tool put "K/S\x00\x00\x03\xe8" "{C:100,U:90,Cr:0,Ur:0,...}"
```

**Étape 3:** Redémarrer PIVX

**Résultat:**
- ProcessKHUBlock(1001) charge état 1000 invalide
- Copie dans newState
- Mutations appliquées sur état invalide de base
- Invariants finaux peuvent être OK mais état de base corrompu

**Impact:** CORRUPTION PERMANENTE

---

## VECTEUR D'ATTAQUE: Reorg Race Condition

### Scénario: Double Reorg Concurrent

**Thread 1:**
```
DisconnectKHUBlock(1003)
DisconnectKHUBlock(1002)
```

**Thread 2 (concurrent):**
```
ProcessKHUBlock(1004)  // Nouvelle chaîne
```

**Race:**
1. Thread 1 efface état 1003
2. Thread 2 lit état 1003 → NOT FOUND
3. Thread 2 fallback genesis → **WRONG STATE**
4. Thread 2 process avec état incorrect

**Défense:**
```cpp
// khu_validation.cpp ligne 93, 165
LOCK(cs_khu);
```

✅ Lock pris, donc pas de race possible

---

## ÉVALUATION

| Attaque | Réussie | Impact |
|---------|---------|--------|
| Reorg partiel | ❌ NON | Reconstruction auto |
| Crash pendant reorg | ❌ NON | Reconstruction auto |
| **DB corruption directe** | ✅ **OUI** | **CRITICAL** |
| Reorg race | ❌ NON | Lock cs_khu |

---

## VULNÉRABILITÉ CRITIQUE TROUVÉE

**FIX NÉCESSAIRE:**

```cpp
// khu_validation.cpp ligne 106 (APRÈS ReadKHUState)

if (nHeight > 0) {
    if (!db->ReadKHUState(nHeight - 1, prevState)) {
        prevState.SetNull();
        prevState.nHeight = nHeight - 1;
    } else {
        // ⚠️ AJOUTER: Vérifier invariants de l'état chargé
        if (!prevState.CheckInvariants()) {
            return validationState.Error(strprintf(
                "khu-corrupted-prev-state: Previous state at height %d has invalid invariants (C=%d U=%d Cr=%d Ur=%d)",
                nHeight - 1, prevState.C, prevState.U, prevState.Cr, prevState.Ur));
        }
    }
}
```

**Même fix pour DisconnectKHUBlock si on recharge état:**

Actuellement DisconnectKHUBlock() ne recharge PAS l'état précédent (il efface juste), donc pas de problème là.

---

## RECOMMANDATION

**SÉVÉRITÉ:** CRITIQUE
**PROBABILITÉ:** FAIBLE (nécessite accès filesystem)
**IMPACT:** CRITIQUE (corruption permanente)

**ACTION REQUISE:** Ajouter vérification invariants après ReadKHUState()

**PROTECTION SUPPLÉMENTAIRE:**
- Checksum sur états DB
- Verification périodique de tous les états
- Rebuild from scratch si corruption détectée
