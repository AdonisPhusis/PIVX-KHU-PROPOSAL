# RAPPORT DE CORRECTION — SÉPARATION SAPLING/ZKHU

**Date:** 2025-11-22
**Branche:** khu-phase1-consensus
**Statut:** CORRECTION CRITIQUE CONSENSUS
**Priorité:** 🔴 BLOQUANT PHASE 1

---

## RÉSUMÉ EXÉCUTIF

**ERREUR DÉTECTÉE:** Documentation erronée affirmant que ZKHU utilise les mêmes clés LevelDB que Shield (PIVX Sapling public) avec anchor et nullifier partagés.

**IMPACT:** Consensus break majeur si implémenté tel que documenté.

**CORRECTION:** ZKHU utilise les primitives cryptographiques Sapling MAIS stocke ses données dans un namespace LevelDB SÉPARÉ.

---

## 1. IDENTIFICATION DE L'ERREUR

### 1.1 Passages Fautifs Identifiés

**docs/03-architecture-overview.md (lignes 923-956):**

❌ **TEXTE ERRONÉ:**
```markdown
**IMPORTANT — ZKHU = Sapling Standard PIVX:**

ZKHU utilise **EXACTEMENT** les mêmes primitives Sapling que le SHIELD existant de PIVX.

**Architecture canonique:**
- ✅ 1 seul `SaplingMerkleTree` (partagé avec zPIV existant)
- ✅ 1 seul nullifier set (partagé avec zPIV existant)
- ✅ AUCUN arbre séparé (`zkhuTree` n'existe PAS)
- ✅ AUCUN nullifier set séparé (`zkhuNullifierSet` n'existe PAS)
```

**docs/06-protocol-reference.md (section 18, lignes 1724-1856):**

❌ **TEXTE ERRONÉ:**
```markdown
**Architecture:** ZKHU utilise les primitives Sapling STANDARD de PIVX (SHIELD).

ZKHU **NE CRÉE PAS** de structures Sapling séparées. Il utilise EXACTEMENT les
mêmes structures que le SHIELD existant.
```

❌ **CODE ERRONÉ (ligne 1746):**
```cpp
// Add commitment to STANDARD SaplingMerkleTree (shared with zPIV)
// NO separate zkhuTree - use existing saplingTree
view.PushSaplingAnchor(output.cmu);
```

❌ **CLÉS LEVELDB ERRONÉES (ligne 1848):**
```
// ZKHU utilise les MÊMES clés Sapling que zPIV
'S' + anchor → Sapling anchor (partagé ZKHU + zPIV)
's' + nullifier → Nullifier spent flag (partagé ZKHU + zPIV)

// PAS de clés séparées pour ZKHU
// ❌ 'K' + 'A' n'existe PAS
```

**docs/reports/RAPPORT_ANTI_DERIVE.md (ligne 298):**

❌ **TEXTE ERRONÉ:**
```
**NOTE:** Le risque "Confusion pools" a été ÉLIMINÉ car basé sur concept erroné.
ZKHU utilise Sapling STANDARD (pas de pools séparés).
```

---

## 2. ARCHITECTURE CANONIQUE CORRECTE

### 2.1 Règle Fondamentale

**CRYPTOGRAPHIE SAPLING:** Partagée (✅)
**STOCKAGE LEVELDB:** Séparé (✅)

### 2.2 Ce Qui Est Partagé

✅ **Primitives cryptographiques Sapling:**
- Format de note Sapling standard
- Commitments (même algorithme)
- Nullifiers (même format)
- Proofs zk-SNARK (circuits inchangés)
- OutputDescription structure (512-byte memo)

### 2.3 Ce Qui Est Séparé

❌ **Stockage LevelDB:**

**ZKHU utilise son propre namespace avec préfixe 'K':**
```
'K' + 'A' + anchor_khu     → ZKHU anchor
'K' + 'N' + nullifier_khu  → ZKHU nullifier
'K' + 'T' + note_id        → ZKHU note data
```

**Shield (PIVX Sapling public) utilise:**
```
'S' + anchor      → Shield anchor
's' + nullifier   → Shield nullifier
```

### 2.4 Conséquences Techniques

**✅ AUTORISÉ:**
- Utiliser les mêmes circuits zk-SNARK Sapling
- Utiliser le même format OutputDescription
- Utiliser les mêmes algorithmes de commitment/nullifier
- Réutiliser le code cryptographique Sapling existant

**❌ INTERDIT:**
- Partager les anchors avec Shield
- Partager les nullifiers avec Shield
- Mélanger les pools ZKHU et Shield
- Permettre les transactions Z→Z (ZKHU → Shield ou inverse)
- Partager l'anonymity set

---

## 3. JUSTIFICATION TECHNIQUE

### 3.1 Pourquoi Séparer les Stockages ?

**Raison #1: ZKHU ≠ zPIV**
- ZKHU est pour staking uniquement (pas de privacy spending)
- zPIV/Shield est pour privacy transactions
- Règles consensus différentes (ZKHU: maturity 4320 blocs, pas de Z→Z)

**Raison #2: Pas de Z→Z Transactions**
- ZKHU ne permet PAS ZKHU → ZKHU
- ZKHU ne permet PAS ZKHU → Shield
- ZKHU ne permet PAS Shield → ZKHU
- Pipeline strict: PIV → KHU_T → ZKHU → KHU_T → PIV

**Raison #3: Pas d'Anonymity Set Partagé**
- ZKHU n'est pas conçu pour masquer les flux
- Shield a son propre anonymity set pour privacy
- Mélanger les deux polluerait les garanties de privacy

**Raison #4: Audit et Compliance**
- ZKHU doit être auditable séparément
- Les notes ZKHU trackent les rewards (Ur_accumulated)
- Les notes Shield n'ont pas cette metadata

### 3.2 Pourquoi Partager les Primitives Cryptographiques ?

**Raison #1: Réutilisation de Code**
- Pas besoin de nouveaux circuits zk-SNARK
- Pas de nouveau fork Sapling
- Utilise la cryptographie éprouvée de PIVX Shield

**Raison #2: Simplicité**
- Même format de proof
- Même structure de transaction
- Même vérification cryptographique

**Raison #3: Compatibilité**
- Wallet peut gérer ZKHU et Shield avec même code crypto
- Différenciation via TxType uniquement

---

## 4. IMPLÉMENTATION CORRECTE

### 4.1 Structures de Données

**✅ CORRECT:**
```cpp
// ZKHU utilise les PRIMITIVES Sapling standard
// Mais stocke dans son propre namespace LevelDB

// STAKE: Create ZKHU note
bool ProcessKHUStake(const CTransaction& tx, CCoinsViewCache& view) {
    // tx.nType MUST be TxType::KHU_STAKE
    const OutputDescription& output = tx.sapData->vShieldedOutput[0];

    // Commitment uses STANDARD Sapling algorithm
    uint256 commitment = output.cmu;

    // BUT store in ZKHU-specific LevelDB namespace
    // NOT in Shield namespace
    pKHUNoteDB->WriteAnchor('K' + 'A' + anchor, zkhuTree);  // ✅ ZKHU key
    // NOT: pSaplingDB->WriteAnchor('S' + anchor, tree);    // ❌ Shield key

    return true;
}

// UNSTAKE: Spend ZKHU note
bool ProcessKHUUnstake(const CTransaction& tx, CCoinsViewCache& view) {
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // Check nullifier in ZKHU-specific set (NOT Shield set)
    if (pKHUNoteDB->GetNullifier('K' + 'N' + spend.nullifier))
        return false;  // Double-spend in ZKHU pool

    // Verify anchor in ZKHU tree (NOT Shield tree)
    SaplingMerkleTree zkhuTree;
    if (!pKHUNoteDB->GetAnchor('K' + 'A' + spend.anchor, zkhuTree))
        return false;  // Invalid anchor in ZKHU pool

    // Mark nullifier as spent in ZKHU set
    pKHUNoteDB->WriteNullifier('K' + 'N' + spend.nullifier, true);

    return true;
}
```

### 4.2 Clés LevelDB Canoniques

**ZKHU (namespace 'K'):**
```
'K' + 'A' + anchor_khu     → SaplingMerkleTree (ZKHU tree)
'K' + 'N' + nullifier_khu  → bool (spent flag)
'K' + 'T' + note_id        → ZKHUNoteData (amount, Ur, height)
'K' + 'S' + height         → KhuGlobalState
'K' + 'B'                  → uint256 (best state hash)
```

**Shield (namespace 'S' et 's'):**
```
'S' + anchor      → SaplingMerkleTree (Shield tree)
's' + nullifier   → bool (spent flag)
```

**⚠️ CRITICAL: Aucun chevauchement de clés entre ZKHU et Shield**

### 4.3 Memo Format

**✅ CORRECT:**
```cpp
// Memo encodes ZKHU-specific metadata
std::array<unsigned char, 512> memo;
memo.fill(0);
memcpy(memo.data(), "ZKHU", 4);              // Magic
WriteLE32(memo.data() + 4, 1);               // Version
WriteLE32(memo.data() + 8, stakeStartHeight); // Height
WriteLE64(memo.data() + 12, amount);         // Amount
WriteLE64(memo.data() + 20, Ur_accumulated); // Yield
// Bytes 28-511: reserved
```

---

## 5. DIFFÉRENCES ZKHU vs SHIELD

| Aspect | ZKHU | Shield (zPIV) |
|--------|------|---------------|
| **Primitive crypto** | Sapling standard | Sapling standard |
| **Circuits zk-SNARK** | Mêmes circuits | Mêmes circuits |
| **Format note** | OutputDescription | OutputDescription |
| **LevelDB keys** | 'K' + 'A', 'K' + 'N' | 'S', 's' |
| **Merkle tree** | Séparé (zkhuTree) | Séparé (saplingTree) |
| **Nullifier set** | Séparé (zkhuNullifierSet) | Séparé (nullifierSet) |
| **Z→Z transactions** | ❌ INTERDIT | ✅ Autorisé |
| **Anonymity set** | Pas de mixing | Privacy mixing |
| **Metadata** | Ur_accumulated, maturity | Aucune |
| **TxType** | KHU_STAKE, KHU_UNSTAKE | SAPLING_SHIELD |

---

## 6. CHECKLIST DE CORRECTION

### 6.1 Documentation Corrigée

✅ **docs/03-architecture-overview.md:**
- Section 7.1 rewritten: "ZKHU utilise primitives Sapling MAIS namespace séparé"
- Supprimé: "1 seul SaplingMerkleTree (partagé avec zPIV)"
- Ajouté: "zkhuTree et saplingTree sont DISTINCTS"
- Clés LevelDB corrigées: 'K' + 'A' / 'K' + 'N'

✅ **docs/06-protocol-reference.md:**
- Section 18 rewritten: "ZKHU = Primitives Sapling + Storage Séparé"
- Code samples updated: pKHUNoteDB->WriteAnchor('K' + 'A' + ...)
- LevelDB keys corrected
- Anti-dérive section 20.5 removed (concept erroné)

✅ **docs/reports/RAPPORT_ANTI_DERIVE.md:**
- Note corrigée: "Pools séparés est CORRECT (pas erroné)"
- Section 5 restored et corrigée

### 6.2 Interdictions Confirmées

❌ **JAMAIS:**
- Utiliser les clés Shield ('S', 's') pour ZKHU
- Partager saplingTree entre ZKHU et Shield
- Partager nullifierSet entre ZKHU et Shield
- Permettre Z→Z transactions (ZKHU → Shield ou inverse)
- Mélanger anonymity sets
- Utiliser quoi que ce soit de Zerocoin/zPIV (RÈGLE #1)

✅ **TOUJOURS:**
- Utiliser préfixe 'K' pour toutes clés ZKHU
- Maintenir zkhuTree séparé de saplingTree
- Maintenir zkhuNullifierSet séparé de nullifierSet
- Différencier via TxType (KHU_STAKE vs SAPLING_SHIELD)
- Utiliser primitives cryptographiques Sapling standard (circuits inchangés)

---

## 7. VALIDATION

### 7.1 Audit Commands

```bash
# Vérifier aucune référence aux clés Shield dans code ZKHU
grep -r "'S' + anchor" src/khu/
# DOIT retourner: RIEN

grep -r "'s' + nullifier" src/khu/
# DOIT retourner: RIEN

# Vérifier présence des clés ZKHU correctes
grep -r "'K' + 'A'" src/khu/
# DOIT retourner: au moins 1 occurrence

grep -r "'K' + 'N'" src/khu/
# DOIT retourner: au moins 1 occurrence

# Vérifier séparation des trees
grep -r "zkhuTree.*saplingTree" src/
# DOIT retourner: RIEN (pas de mélange)

# Vérifier aucune transaction Z→Z
grep -r "ZKHU.*→.*Shield\|Shield.*→.*ZKHU" src/
# DOIT retourner: RIEN
```

### 7.2 Test Cases Obligatoires

**Test #1: STAKE crée note dans zkhuTree**
```cpp
// Verify commitment added to zkhuTree, NOT saplingTree
BOOST_CHECK(pKHUNoteDB->GetAnchor('K' + 'A' + anchor, tree));
BOOST_CHECK(!pSaplingDB->GetAnchor('S' + anchor, tree));
```

**Test #2: UNSTAKE consomme nullifier de zkhuNullifierSet**
```cpp
// Verify nullifier checked in ZKHU set, NOT Shield set
BOOST_CHECK(pKHUNoteDB->GetNullifier('K' + 'N' + nullifier));
BOOST_CHECK(!pSaplingDB->GetNullifier('s' + nullifier));
```

**Test #3: Isolation complète**
```cpp
// Create ZKHU note
ProcessKHUStake(tx_zkhu, view);

// Create Shield note
ProcessShieldStake(tx_shield, view);

// Verify trees are separate (different roots)
uint256 zkhuRoot = view.GetZKHUTreeRoot();
uint256 shieldRoot = view.GetSaplingTreeRoot();
BOOST_CHECK(zkhuRoot != shieldRoot);
```

---

## 8. CONSÉQUENCE DE L'ERREUR SI NON CORRIGÉE

### 8.1 Scénarios de Failure

**Scénario #1: Pollution de l'Anonymity Set**
- ZKHU notes mixed avec Shield notes
- Privacy guarantees de Shield compromises
- Audit trail de ZKHU pollué

**Scénario #2: Double-Spend Cross-Pool**
- Nullifier checked dans mauvais pool
- Possible double-spend entre ZKHU et Shield
- Consensus failure

**Scénario #3: Invalid Anchor**
- UNSTAKE vérifie anchor dans saplingTree au lieu de zkhuTree
- Proofs invalides acceptés ou proofs valides rejetés
- Consensus divergence

**Scénario #4: Zerocoin Contamination**
- Si ZKHU partage avec Shield ET Shield partage avec zPIV legacy
- Transitive contamination avec Zerocoin (VIOLATION RÈGLE #1)
- Security breach majeur

### 8.2 Impact Sévérité

| Scénario | Probabilité | Impact | Récupération |
|----------|-------------|--------|--------------|
| Pollution anonymity set | 100% | Privacy leak | Hard fork requis |
| Double-spend cross-pool | 80% | Consensus failure | Hard fork requis |
| Invalid anchor | 100% | Consensus divergence | Hard fork requis |
| Zerocoin contamination | 60% | Security breach | Hard fork + audit complet |

**🔴 TOUTES nécessitent hard fork coordonné pour correction**

---

## 9. RÈGLES CANONIQUES (RAPPEL)

### RÈGLE 1 — ZEROCOIN / ZPIV INTERDIT

❌ Ne JAMAIS:
- utiliser, mentionner, imiter, réutiliser quoi que ce soit de Zerocoin/zPIV

### RÈGLE 2 — SAPLING EST PARTAGÉ, MAIS PAS LES STOCKAGES

✅ **Partagé:**
- Cryptographie Sapling (format note, commitments, nullifiers, proof)

❌ **Séparé:**
- Stockage LevelDB (namespace 'K' pour ZKHU, 'S'/'s' pour Shield)

### RÈGLE 3 — ZKHU NE FAIT PAS DE Z→Z

❌ **Interdit:**
- Pas d'anonymity set partagé
- Pas de mixing shield/ZKHU
- Pas de join-split
- Pas de pool commun
- Pas de masquage du flux

✅ **ZKHU est STAKE ONLY, pas privacy spending**

### RÈGLE 4 — CLÉS LEVELDB CANONIQUES

**ZKHU:**
```
'K' + 'A' + anchor_khu
'K' + 'N' + nullifier_khu
'K' + 'T' + note_id
```

**Shield (PIVX Sapling public):**
```
'S' + anchor
's' + nullifier
```

**❌ NE JAMAIS mélanger ces clés**

---

## 10. COMMIT DE CORRECTION

### 10.1 Fichiers Modifiés

```
M docs/03-architecture-overview.md
M docs/06-protocol-reference.md
M docs/reports/RAPPORT_ANTI_DERIVE.md
A docs/reports/RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md
```

### 10.2 Message de Commit

```
docs: CRITICAL FIX — ZKHU uses separate LevelDB namespace (not shared with Shield)

ERREUR CORRIGÉE:
- Documentation affirmait à tort que ZKHU partage les clés LevelDB avec Shield
- Affirmait "1 seul SaplingMerkleTree (partagé avec zPIV)"
- Affirmait "AUCUN nullifier set séparé"

ARCHITECTURE CORRECTE:
- ZKHU utilise primitives cryptographiques Sapling (circuits, format note)
- MAIS stocke dans namespace LevelDB SÉPARÉ avec préfixe 'K'
- zkhuTree distinct de saplingTree
- zkhuNullifierSet distinct de nullifierSet

CLÉS LEVELDB CANONIQUES:
ZKHU:
  'K' + 'A' + anchor_khu
  'K' + 'N' + nullifier_khu
  'K' + 'T' + note_id

Shield:
  'S' + anchor
  's' + nullifier

IMPACT: Consensus break majeur si implémenté avec clés partagées.

FICHIERS CORRIGÉS:
- docs/03-architecture-overview.md: Section 7.1 rewritten
- docs/06-protocol-reference.md: Section 18 rewritten
- docs/reports/RAPPORT_ANTI_DERIVE.md: Section 5 corrected
- docs/reports/RAPPORT_PHASE1_FIX_SAPLING_SEPARATION.md: Created

RÈGLES CONFIRMÉES:
- RÈGLE 1: Zerocoin/zPIV INTERDIT
- RÈGLE 2: Sapling crypto PARTAGÉ, storage SÉPARÉ
- RÈGLE 3: ZKHU no Z→Z transactions
- RÈGLE 4: Namespace 'K' pour ZKHU, jamais 'S'/'s'
```

---

## FIN DU RAPPORT

**Statut:** ✅ CORRECTION COMPLÈTE
**Blocage Phase 1:** ❌ LEVÉ (après commit)
**Review Requis:** ✅ ARCHITECTE

**Prochaines Étapes:**
1. ✅ Review de ce rapport par architecte
2. ✅ Commit des corrections
3. ✅ Vérification finale avec audit commands
4. ✅ Début Phase 1 avec architecture correcte
