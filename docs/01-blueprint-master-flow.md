# 01 — PIVX-V6-KHU BLUEPRINT MASTER-FLOW

### *Guide operationnel pour un build autonome et supervise*

**Date:** 2025-01-21
**Version:** 1.0
**Status:** ACTIF

---

## 1. REGLES FONDAMENTALES

### 1.1 Documents Immuables

Les documents suivants definissent le systeme et **NE DOIVENT JAMAIS ETRE MODIFIES** apres validation:

- **00-DECLARATION.md** — Vision et objectifs fondateurs
- **01-SPEC.md** — Specification mathematique complete
- **02-ARCHITECTURE.md** — Architecture technique et composants
- **03-ECONOMICS.md** — Proprietes economiques et theoremes
- **04-PROTOCOL.md** — Specification protocole et code C++
- **LOIS-SACREES.md** — 30 lois inviolables du systeme
- **blueprints/01-PIVX-INFLATION-DIMINUTION.md** — Regles emission canoniques

**Modification interdite sans validation architecte + review complete.**

### 1.2 Documents Modifiables

Les documents suivants evoluent au fur et a mesure du developpement:

- **05-ROADMAP.md** — Statut phases et progression
- **06-BLUEPRINT-MASTER-FLOW.md** (ce document) — Guide operationnel
- **MEMO-ANTI-VRILLAGE.md** — Rappels et garde-fous
- **RAPPORT_PHASE_X.md** — Rapports de phase (nouveaux a chaque phase)
- **DIVERGENCES.md** (si cree) — Log des divergences trouvees

Ces documents peuvent etre enrichis, clarifies, completes.

### 1.3 Lois Absolues

Les regles suivantes sont **NON-NEGOCIABLES** et doivent etre verifiees a chaque commit:

#### 1.3.1 Invariants Mathematiques

```
C == U                    (collateral == supply)
Cr == Ur                  (reward pool == reward rights)
CD == 1                   (collateral diversification)
CDr == 1                  (reward pool diversification)
```

#### 1.3.2 Operations Autorisees

```
MINT      : C+, U+        (preserve C==U)
REDEEM    : C-, U-        (preserve C==U, destroy KHU)
DAILY_YIELD: Cr+, Ur+     (preserve Cr==Ur)
UNSTAKE   : Cr-, Ur-      (preserve Cr==Ur, bonus KHU)
```

Aucune autre operation ne doit modifier C, U, Cr, Ur.

#### 1.3.3 Emission PIVX (-1/an)

```cpp
int64_t reward_year = std::max(6 - year, 0LL) * COIN;

// Egalite stricte:
staker_reward == mn_reward == dao_reward == reward_year
```

**INTERDICTIONS ABSOLUES:**
- Pas d'interpolation (transition douce)
- Pas de year_fraction (annee = entier uniquement)
- Pas de table alternative (lookup, cache)
- Pas de strategie DOMC sur emission (DOMC ne gouverne QUE R%)
- Pas de fonction risque-dependent
- Pas de modulation network

**La formule `max(6 - year, 0)` est SACREE.**

#### 1.3.4 Yield KHU (R% gouverne par DOMC)

```
R% annuel dans [0, R_MAX_dynamic]
Yield lineaire (pas de compounding)
Maturity: 4320 blocs (3 jours) obligatoire
```

#### 1.3.5 Separation Emission / Yield

```
reward_year (emission PIVX) != R% (yield KHU)
Deux systemes INDEPENDANTS
Ne doivent JAMAIS s'influencer
```

#### 1.3.6 HTLC Cross-Chain (Gateway Compatible)

**⚠️ RÈGLE FONDAMENTALE : HTLC KHU = HTLC BITCOIN STANDARD**

```
✅ PIVX supporte DÉJÀ les scripts HTLC (Bitcoin-compatible)
✅ KHU = Token UTXO standard (comme PIV)
✅ HTLC KHU fonctionne EXACTEMENT comme HTLC PIV
❌ AUCUNE implémentation HTLC spéciale nécessaire pour KHU
```

**Opcodes standard Bitcoin (déjà dans PIVX) :**

```cpp
OP_IF                    // Conditionnel
OP_ELSE                  // Alternative
OP_ENDIF                 // Fin conditionnel
OP_HASH160               // Hash du secret
OP_EQUALVERIFY           // Vérification égalité
OP_CHECKLOCKTIMEVERIFY   // Timelock
OP_CHECKSIG              // Vérification signature
OP_DROP                  // Suppression stack

→ C'est TOUT ce qu'il faut pour HTLC !
→ Marche pour PIV, marche pour KHU automatiquement !
```

**Lois HTLC (spécification Phase 7) :**

```
18. Hashlock = SHA256(secret) UNIQUEMENT (pas SHA3, blake2, keccak)
19. Timelock = block height UNIQUEMENT (pas timestamp)
20. Script HTLC = template Bitcoin standard (OP_IF / OP_HASH160 / CLTV)
21. HTLC ne modifie JAMAIS C/U/Cr/Ur (ownership transfer only)
22. KHU_T uniquement (pas ZKHU, pas PIV direct)
23. RPC createhtlc/redeemhtlc/refundhtlc (standard Bitcoin pattern)
24. Aucune logique off-chain on-chain (pas de price oracle, pas de khu_equivalent)
```

**KHU = Unité de compte INVISIBLE :**

```
❌ User n'a PAS besoin de wallet KHU pour swap !
✅ Gateway utilise KHU pour calculer équivalences (invisible)

Exemple swap BTC → USDC :
  1. Gateway calcule : 1 BTC = 50,000 KHU (rate market)
  2. Gateway calcule : 50,000 KHU = 100,000 USDC (rate market)
  3. Gateway matche Alice (BTC) ↔ Bob (USDC)
  4. Atomic swap direct BTC ↔ USDC via HTLC standard
  5. KHU n'apparaît JAMAIS dans les transactions !

KHU = comme le MÈTRE (unité de mesure)
  ❌ Tu n'as pas besoin de "posséder des mètres"
  ✅ Le mètre sert juste à mesurer
  ❌ Alice n'a pas besoin de "posséder du KHU"
  ✅ KHU sert juste à calculer équivalences
```

**Qui a besoin de KHU ? (OPTIONNEL) :**

```
1. Market Makers / LP avancés
   → Veulent exposer plusieurs assets via pool KHU

2. Utilisateurs PIVX
   → Veulent staker ZKHU (privacy + yield)
   → Veulent participer governance PIVX

3. Hedge contre volatilité
   → Sortir de BTC/ZEC volatile
   → Rester dans écosystème décentralisé (pas USDC)

Pour swaps simples : KHU PAS NÉCESSAIRE !
```

**Interdictions absolues HTLC :**

```
❌ Z→Z HTLC (ZKHU → ZKHU)
❌ Timelock timestamp (block height uniquement)
❌ SHA3/blake2/keccak hashlock (SHA256 uniquement)
❌ KHU burn via HTLC
❌ Metadata on-chain
❌ KHU→PIV direct via HTLC
❌ Oracle de prix on-chain
❌ Format HTLC propriétaire (Bitcoin standard uniquement)
❌ Code HTLC spécial pour KHU (utiliser code Bitcoin existant)
```

**Séparation stricte :**

```
Gateway (off-chain):
  • Price discovery (KHU comme unité de compte)
  • Matching engine (Alice BTC ↔ Bob USDC)
  • Order book
  • Instructions HTLC

PIVX Core (on-chain):
  • HTLC execution (scripts Bitcoin standard)
  • Atomicity (hash lock + time lock)
  • Secret propagation
  • Aucun calcul de prix on-chain
```

**Code nécessaire PIVX Core v6 :**

```cpp
// ✅ KHU Token (~500 lignes)
class CKHUToken {
    bool MintKHU(amount);    // PIV → KHU (1:1)
    bool BurnKHU(amount);    // KHU → PIV (1:1)
    bool TransferKHU(...);   // Standard UTXO transfer
};

// ✅ RPC Commands (~300 lignes)
UniValue mintkhu(...);
UniValue burnkhu(...);
UniValue sendkhu(...);
UniValue createhtlc(...);   // Probablement déjà existant pour PIV !
UniValue redeemhtlc(...);
UniValue refundhtlc(...);

// ❌ AUCUN code HTLC spécial pour KHU nécessaire
// Les scripts HTLC standard fonctionnent automatiquement !
```

**Référence détaillée :** `docs/blueprints/08-KHU-HTLC-GATEWAY.md`

#### 1.3.7 AXIOME ZKHU — RÈGLES ABSOLUES (IMMUTABLES)

**🔴 SECTION VERROUILLÉE — CES RÈGLES NE PEUVENT JAMAIS ÊTRE REMISES EN QUESTION**

**1. AUCUN ZEROCOIN / ZPIV**

```
❌ PIVX-V6-KHU n'utilise AUCUN code, pattern, UTXO, DB key, script ou modèle issu de Zerocoin ou zPIV
❌ Toute référence à "mint/spend", "CZerocoinDB", "coinsupply", "pools Zerocoin" est strictement interdite
❌ Zerocoin est MORT — ne JAMAIS le mentionner, l'imiter, ou s'en inspirer
```

**2. SAPLING UNIQUEMENT (PIVX SHIELD)**

```
✅ Le projet utilise EXCLUSIVEMENT la cryptographie Sapling (PIVX Shield) déjà existante
✅ Pas de nouveaux circuits, pas de modifications cryptographiques, pas de Z→Z
✅ Réutilisation des primitives éprouvées : commitments, nullifiers, proofs zk-SNARK
```

**3. ZKHU = STAKING-ONLY**

```
✅ Les notes ZKHU sont des notes Sapling utilisées UNIQUEMENT pour le staking privé des KHU
❌ Elles ne constituent PAS un mécanisme de privacy général
❌ Elles ne sont PAS un analogue de zPIV
❌ Pas de transferts Z→Z, pas de join-split, pas de shielding automatique
```

**4. AUCUN POOL SAPLING PARTAGÉ**

```
✅ Les données Sapling publiques (Shield) et les données ZKHU ne partagent AUCUN espace de stockage DB
✅ Aucune clé LevelDB Sapling ('S', 's') ne doit être utilisée pour ZKHU
✅ ZKHU utilise ses propres clés ('K' + ...)
❌ Aucun anonymity set partagé, aucun pool mixing
```

**5. SAPLING CRYPTO COMMUN, STOCKAGE SÉPARÉ**

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
```

**6. PIPELINE IMMUTABLE**

```
KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T

✅ Aucun transfert Z→Z
✅ Aucun contournement
✅ Aucun autre pipeline n'existe ou ne peut être proposé
```

**7. PAS DE POOL, PAS DE MIXING**

```
❌ ZKHU ne s'inscrit PAS dans un anonymity set comme zPIV
❌ Aucun join-split, aucun shielding automatique
✅ ZKHU = notes Sapling pour staking uniquement
```

**8. CLÉS LEVELDB CANONIQUES (IMMUABLES)**

```cpp
// ZKHU (namespace 'K' — OBLIGATOIRE)
'K' + 'A' + anchor_khu     → ZKHU SaplingMerkleTree
'K' + 'N' + nullifier_khu  → ZKHU nullifier spent flag
'K' + 'T' + note_id        → ZKHUNoteData (amount, Ur, height)

// Shield (PIVX Sapling public — namespace 'S'/'s')
'S' + anchor      → Shield SaplingMerkleTree
's' + nullifier   → Shield nullifier spent flag

// ✅ Aucun chevauchement de clés — isolation complète
```

**9. COMPLEXITÉ IMPLÉMENTATION ZKHU: FAIBLE-MOYENNE**

```
ZKHU = Wrapper Sapling existant (8 jours-homme)

❌ Pas de nouveau circuit cryptographique
❌ Pas de modification code Sapling
✅ Appel fonctions PIVX Shield existantes
✅ Namespace 'K' isolation (clés LevelDB différentes)
```

**Breakdown effort:**
- STAKE (T→Z): 3 jours (appel CreateSaplingNote())
- UNSTAKE (Z→T): 3 jours (appel ValidateSaplingSpend())
- Namespace 'K' setup: 1 jour (DB keys)
- Tests: 1 jour

**Total Phase 7 ZKHU: 8 jours** (pas 18 jours!)

**Raison:** Tout est déjà dans PIVX. ZKHU juste réutilise avec namespace séparé.

**RÉFÉRENCE DÉTAILLÉE:** `docs/blueprints/05-ZKHU-SAPLING-STAKE.md`

### 1.4 Nomenclature PIVX Obligatoire

Le code doit respecter les conventions PIVX:

```cpp
// Threading:
RecursiveMutex cs_khu;              // PAS CCriticalSection
LOCK(cs_khu);                       // Protection acces

// Database:
CDBWrapper                          // Classe parente DB
CDBBatch                            // Batch atomique

// Types:
CAmount                             // int64_t pour montants
uint256                             // Hash 256 bits

// Logging:
LogPrint(BCLog::KHU, "message");   // Categorie KHU
LogPrintf("KHU: message");         // Log general

// Validation:
CValidationState                    // Etat validation
```

---

## 2. MACHINE A ETATS (PHASES 1-10)

### 2.1 Progression Sequentielle

```
Phase 1 → Phase 2 → Phase 3 → ... → Phase 10
```

**REGLE:** Une phase N ne peut commencer que si Phase N-1 est VALIDEE.

### 2.2 Criteres de Validation Phase

Pour qu'une phase soit consideree VALIDEE:

1. **Code:** Compile sans erreurs
2. **Tests:** Unitaires + fonctionnels passent
3. **Invariants:** Verifies dans tous les tests
4. **Documentation:** Mise a jour (05-ROADMAP.md)
5. **Rapport:** RAPPORT_PHASE_N.md complete et signe
6. **Review:** Validation architecte

### 2.3 Etats de Phase

```
PENDING     : Pas encore commencee
IN_PROGRESS : En developpement
BLOCKED     : Bloquee par erreur/divergence
TESTING     : Tests en cours
VALIDEE     : Criteres remplis, prete pour phase suivante
```

### 2.4 Tableau Phases

| Phase | Description | Status | Dependances |
|-------|-------------|--------|-------------|
| 1 | Foundation (State, DB, RPC) | IN_PROGRESS | — |
| 2 | MINT/REDEEM operations | PENDING | Phase 1 |
| 3 | DAILY_YIELD computation | PENDING | Phase 2 |
| 4 | UNSTAKE bonus | PENDING | Phase 3 |
| 5 | DOMC governance (R% vote) | PENDING | Phase 4 |
| 6 | SAFU (timelock rescue) | PENDING | Phase 5 |
| 7 | GUI integration | PENDING | Phase 6 |
| 8 | Security audit | PENDING | Phase 7 |
| 9 | Testnet deployment | PENDING | Phase 8 |
| 10 | Mainnet preparation | PENDING | Phase 9 |

---

## 3. FLOW PAR PHASE

### Phase 1: Foundation (State, DB, RPC)

**Objectif:** Etablir l'infrastructure de base pour KHU.

**Deliverables:**
- [x] `khu_state.h/cpp` : Structure KhuGlobalState + CheckInvariants
- [x] `khu_db.h/cpp` : Persistance LevelDB via CDBWrapper
- [ ] `khu_rpc.cpp` : RPCs getkhustate, setkhustate (regtest)
- [ ] Integration validation.cpp : ConnectBlock/DisconnectBlock hooks
- [ ] Tests unitaires : test_khu_state.cpp
- [ ] Tests fonctionnels : khu_basic.py

**Verification:**
```bash
make check                          # Tests unitaires
test/functional/test_runner.py     # Tests fonctionnels
```

**Invariants testes:**
- Initialisation: C=0, U=0, Cr=0, Ur=0
- CheckInvariants() retourne true
- Lecture/ecriture DB preserve l'etat

**Git:**
- Branche: `khu-phase1-consensus`
- Commits: Atomiques, messages descriptifs
- Merge: Vers `develop` apres validation

---

### Phase 2: MINT/REDEEM Operations

**Objectif:** Implementer creation/destruction KHU.

**Deliverables:**
- [ ] OP_MINT : Lock PIV → Mint KHU (C+, U+)
- [ ] OP_REDEEM : Burn KHU → Unlock PIV (C-, U-)
- [ ] Validation consensus dans validation.cpp
- [ ] Tests unitaires : test_khu_mint_redeem.cpp
- [ ] Tests fonctionnels : khu_mint_redeem.py

**Verification:**
```cpp
// Apres MINT:
assert(C == U);
assert(new_C == old_C + piv_locked);
assert(new_U == old_U + khu_minted);

// Apres REDEEM:
assert(C == U);
assert(new_C == old_C - piv_unlocked);
assert(new_U == old_U - khu_burned);
```

**Git:**
- Branche: `khu-phase2-mint-redeem`
- Base: `khu-phase1-consensus` (merged)

---

### Phase 3: DAILY_YIELD Computation

**Objectif:** Calculer et distribuer yield journalier.

**Deliverables:**
- [ ] CalculateDailyYield(stake_amount, R_annual)
- [ ] Distribution dans ConnectBlock (Cr+, Ur+)
- [ ] Maturity 4320 blocs enforcement
- [ ] Tests unitaires : test_khu_yield.cpp
- [ ] Tests fonctionnels : khu_yield.py

**IMPORTANT — Timing de mise à jour du yield:**

Tous les updates de yield DOIVENT se produire **AVANT** l'exécution des transactions dans ConnectBlock.

**Ordre canonique:**
```
1. ApplyDailyYieldIfNeeded()   // ← YIELD ICI (Phase 3)
2. ProcessKHUTransactions()     // MINT, REDEEM, STAKE, UNSTAKE
3. ApplyBlockReward()           // Emission PIVX
4. CheckInvariants()
5. PersistState()
```

**Raison:** Éviter mismatch entre Cr/Ur accumulés et UNSTAKE dans le même bloc.

Le yield est appliqué en début de bloc, PUIS les transactions sont traitées avec l'état yield à jour.

**Verification:**
```cpp
CAmount annual_yield = (stake_amount * R_annual) / 10000;
CAmount daily_yield = annual_yield / 365;

// Apres yield:
assert(Cr == Ur);
assert(new_Cr == old_Cr + daily_yield);
assert(new_Ur == old_Ur + daily_yield);
```

**Git:**
- Branche: `khu-phase3-daily-yield`

---

### Phase 4: UNSTAKE Bonus

**Objectif:** Implementer retrait anticipe avec bonus KHU.

**Deliverables:**
- [ ] OP_UNSTAKE : Retrait anticipe (Cr-, Ur-)
- [ ] Bonus KHU vers nouvelle adresse (getnewaddress)
- [ ] Validation consensus
- [ ] Tests unitaires : test_khu_unstake.cpp
- [ ] Tests fonctionnels : khu_unstake.py

**Verification:**
```cpp
// Apres UNSTAKE:
assert(Cr == Ur);
assert(new_Cr == old_Cr - bonus_khu);
assert(new_Ur == old_Ur - bonus_khu);
// Output KHU vers NOUVELLE adresse (privacy)
```

**Git:**
- Branche: `khu-phase4-unstake`

---

### Phase 5: R% Governance (DOMC Commit-Reveal)

**Objectif:** Masternodes votent R% via commit-reveal avec calendrier fixe et préavis LP.

**ARCHITECTURE:** Commit-Reveal + Auto-Proposal DAO
- Extension `CMasternodePing` avec 3 champs: `nRCommitment`, `nRProposal`, `nRSecret`
- Vote caché (commit SHA256) pendant 2 semaines
- Reveal automatique au bloc fixe (149760)
- Auto-proposal création avec R_next (préavis 2 semaines)
- Cycle complet: 169920 blocs (4 mois)

**CYCLE 4 MOIS (4 Phases):**
1. **Phase 1 — ACTIF** (129600 blocs = 3 mois)
   - R% verrouillé (garantie LP)
   - Aucun changement possible

2. **Phase 2 — COMMIT** (20160 blocs = 2 semaines)
   - MN créent commitments SHA256(R_proposal || secret)
   - Votes totalement cachés (privacy)

3. **Phase 3 — REVEAL** (bloc 149760 fixe)
   - Validation automatique reveals
   - Consensus = moyenne(reveals_valides)
   - Auto-proposal créée: "KHU_R_22.50_NEXT"

4. **Phase 4 — PRÉAVIS** (20160 blocs = 2 semaines)
   - R_next visible dans auto-proposal réseau
   - LP adaptent stratégies (prévisibilité)

5. **Activation** (bloc 169920)
   - R_next activé → nouveau cycle Phase 1

**Deliverables:**
- [ ] Extension `CMasternodePing` (nRCommitment, nRProposal, nRSecret)
- [ ] RPC `masternode commitkhu <R%>` — Créer commitment SHA256
- [ ] `ProcessKHUReveal()` — Validation reveals au bloc 195360
- [ ] `CreateKHUAutoProposal()` — Création auto-proposal avec R_consensus
- [ ] `GetKHURNextFromProposal()` — Lecture R_next depuis proposal
- [ ] Activation automatique bloc 215520 (ConnectBlock)
- [ ] RPC `getkhugovernance` — Status cycle complet
- [ ] Constantes cycle (consensus/params.h)
- [ ] Tests unitaires : test_khu_governance.cpp
- [ ] Tests fonctionnels : khu_governance.py

**Verification:**
```cpp
// Commit valide (SHA256)
CHashWriter ss(SER_GETHASH, 0);
ss << R_proposal << secret;
assert(ss.GetHash() == commitment);

// Reveal valide
assert(R_proposal <= R_MAX_dynamic);
assert(ValidateReveal());

// Consensus (moyenne arithmétique)
uint64_t sum = 0;
for (uint16_t r : valid_reveals) sum += r;
uint16_t R_consensus = sum / valid_reveals.size();

// Cycle positions
int pos = GetKHUCyclePosition(nHeight, nActivationHeight);
assert(pos >= 0 && pos < KHU_R_CYCLE_BLOCKS);

// Dates fixes (prévisibilité)
assert(revealHeight == cycleStart + 129600 + 20160);
assert(activationHeight == cycleStart + 169920);
```

**Exemple usage:**
```bash
# PHASE 2 (Commit): MN vote R% (caché)
./pivx-cli masternode commitkhu 22.50
{
  "status": "committed",
  "R_proposal": 2250,
  "commitment": "a3f5b2c7d1e9...",
  "reveal_height": 149760
}

# PHASE 3 (Reveal): Automatique au bloc 149760
# → ProcessKHUReveal() valide tous les reveals
# → Crée auto-proposal "KHU_R_22.70_NEXT"

# PHASE 4 (Préavis): Query status cycle
./pivx-cli getkhugovernance
{
  "cycle_position": 155000,
  "phase": "notice",
  "R_current": 25.00,
  "R_next": 22.70,
  "R_max": 29.00,
  "reveal_height": 149760,
  "activation_height": 169920,
  "valid_commits": 350
}

# PHASE 1 (Actif): Nouveau cycle après activation
# → R% = 22.70% verrouillé 3 mois
```

**Complexité:** MOYENNE (~6-7 jours)
- Commit-reveal logic (SHA256 + validation)
- Auto-proposal création automatique
- Cycle 4 phases (calendrier fixe)
- ~300 lignes code total

**Git:**
- Branche: `khu-phase5-governance`

---

### Phase 6: SAFU (Timelock Rescue)

**Objectif:** Mecanisme de secours pour fonds bloques.

**Deliverables:**
- [ ] Timelock rescue logic
- [ ] Integration validation
- [ ] Tests : test_khu_safu.cpp, khu_safu.py

**Git:**
- Branche: `khu-phase6-safu`

---

### Phase 7: GUI Integration

**Objectif:** Interface utilisateur pour KHU.

**Deliverables:**
- [ ] Tab "KHU" dans wallet GUI
- [ ] Boutons : Mint, Redeem, Unstake
- [ ] Affichage : Solde KHU, yield rate, state
- [ ] Tests : GUI tests

**Git:**
- Branche: `khu-phase7-gui`

---

### Phase 8: Security Audit

**Objectif:** Audit externe du code KHU.

**Deliverables:**
- [ ] Rapport audit externe
- [ ] Corrections bugs identifies
- [ ] Re-test complet

**Git:**
- Branche: `khu-phase8-audit-fixes`

---

### Phase 9: Testnet Deployment

**Objectif:** Deploiement sur testnet public.

**Deliverables:**
- [ ] Activation height testnet
- [ ] Documentation utilisateur testnet
- [ ] Monitoring + metrics
- [ ] Feedback period (4 semaines minimum)

**Git:**
- Branche: `khu-phase9-testnet`

---

### Phase 10: Mainnet Preparation

**Objectif:** Preparation lancement mainnet.

**Deliverables:**
- [ ] Activation height mainnet (vote communaute)
- [ ] Documentation finale
- [ ] Release binaries
- [ ] Annonce publique

**Git:**
- Branche: `khu-phase10-mainnet`
- Tag: `v6.0.0-khu`

---

## 4. CYCLE D'AUTOMATISATION

### 4.1 Freeze/Review Cycle

Avant chaque merge vers `develop`:

```
1. FREEZE CODE
   └─ Aucun nouveau commit sur branche phase

2. RUN TESTS
   ├─ make check (tests unitaires)
   ├─ test_runner.py (tests fonctionnels)
   └─ Verification invariants

3. GENERATE RAPPORT
   └─ RAPPORT_PHASE_N.md (template RAPPORT_PHASE_TEMPLATE.md)

4. REVIEW ARCHITECTE
   ├─ Lecture code
   ├─ Verification lois sacrees
   ├─ Validation rapport
   └─ Signature RAPPORT_PHASE_N.md

5. MERGE
   ├─ git checkout develop
   ├─ git merge khu-phaseN-xxx
   └─ git push origin develop

6. PHASE SUIVANTE
   └─ git checkout -b khu-phaseN+1-xxx develop
```

### 4.2 Strategie Git Branching

```
master (mainnet releases uniquement)
   ↑
develop (integration continue)
   ↑
khu-phase1-consensus ----→ MERGE apres validation
   ↑
khu-phase2-mint-redeem ---→ MERGE apres validation
   ↑
khu-phase3-daily-yield ---→ MERGE apres validation
   ...
```

**Regles:**
- 1 branche = 1 phase
- Merge uniquement apres validation complete
- Pas de merge direct vers `master` (sauf releases)
- Tags version sur `master` uniquement

### 4.3 CLI Demos Obligatoires

Chaque phase doit fournir un demo CLI dans RAPPORT_PHASE_N.md:

**Exemple Phase 1:**
```bash
$ ./pivx-cli -regtest getkhustate
{
  "C": 0,
  "U": 0,
  "Cr": 0,
  "Ur": 0,
  "R_annual": 500,
  "activation_height": 0,
  "invariants_ok": true
}
```

**Exemple Phase 2:**
```bash
$ ./pivx-cli -regtest mintkhu 100
{
  "txid": "abc123...",
  "khu_minted": 100.00000000,
  "C": 100.00000000,
  "U": 100.00000000
}
```

**Exemple Phase 3:**
```bash
$ ./pivx-cli -regtest getkhubalance
{
  "balance": 105.50000000,
  "staked": 100.00000000,
  "yield_earned": 5.50000000
}
```

---

## 5. REGLES ANTI-DERIVE

Les 15 interdictions suivantes doivent etre verifiees AVANT chaque commit:

### 5.1 Interdictions Emission

1. ❌ **Pas d'interpolation** (annee = entier uniquement)
2. ❌ **Pas de year_fraction** (pas de transition douce)
3. ❌ **Pas de table alternative** (lookup, cache, pre-compute)
4. ❌ **Pas de strategie DOMC sur emission** (DOMC ne touche QUE R%)
5. ❌ **Pas de fonction risque-dependent** (emission fixe)

### 5.2 Interdictions Invariants

6. ❌ **Jamais modifier C/U en dehors MINT/REDEEM**
7. ❌ **Jamais modifier Cr/Ur en dehors DAILY_YIELD/UNSTAKE**
8. ❌ **Jamais bruler KHU sauf REDEEM** (violation C==U)
9. ❌ **Jamais UNSTAKE en PIV** (bonus = KHU uniquement)

### 5.3 Interdictions Architecture

10. ❌ **Pas de compounding** (yield = lineaire uniquement)
11. ❌ **Pas de maturity < 4320 blocs** (3 jours obligatoire)
12. ❌ **Pas de float/double** (integer division uniquement)
13. ❌ **Pas de reutilisation adresse UNSTAKE** (privacy)

### 5.4 Interdictions Procedure

14. ❌ **Pas de merge sans tests** (make check obligatoire)
15. ❌ **Pas de merge sans rapport** (RAPPORT_PHASE_N.md obligatoire)

---

## 6. DECLARATION FINALE

Ce document est le **GUIDE OPERATIONNEL** du projet PIVX-V6-KHU.

**Il definit:**
- Les regles immuables (emission, invariants, nomenclature)
- La progression sequentielle (phases 1-10)
- Le cycle de validation (freeze, tests, review, merge)
- Les interdictions absolues (15 regles anti-derive)

**Toute deviation de ce blueprint doit etre:**
1. Documentee dans DIVERGENCES.md
2. Justifiee par architecte
3. Validee par review

**En cas de conflit:**
- LOIS-SACREES.md > ce document
- 01-SPEC.md > interpretation libre
- Architecte > propositions externes

---

**FIN DU BLUEPRINT MASTER-FLOW**

**Version:** 1.0
**Date:** 2025-01-21
**Status:** ACTIF
