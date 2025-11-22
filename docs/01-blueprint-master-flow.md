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
- **06-SOUS-BLUEPRINT-EMISSION.md** — Regles emission canoniques

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

**Lois HTLC (spécification Phase 7):**

```
18. Hashlock = SHA256(secret) UNIQUEMENT (pas SHA3, blake2, keccak)
19. Timelock = block height UNIQUEMENT (pas timestamp)
20. Script HTLC = template Bitcoin standard (OP_IF / OP_SHA256 / CLTV)
21. HTLC ne modifie JAMAIS C/U/Cr/Ur (ownership transfer only)
22. KHU_T uniquement (pas ZKHU, pas PIV direct)
23. RPC khu_listhtlcs et khu_gethtlc obligatoires (Gateway indexation)
24. Aucune logique off-chain on-chain (pas de price oracle, pas de khu_equivalent)
```

**Interdictions absolues HTLC:**
```
❌ Z→Z HTLC (ZKHU → ZKHU)
❌ Timelock timestamp
❌ SHA3/blake2/keccak hashlock
❌ KHU burn via HTLC
❌ Metadata on-chain
❌ KHU→PIV direct via HTLC
❌ Oracle de prix on-chain
❌ Format HTLC propriétaire (non-Bitcoin compatible)
```

**Séparation stricte:**
```
Gateway (off-chain) = price discovery + matching + order book
PIVX Core (on-chain) = HTLC execution + atomicity + secret propagation
```

**Référence:** `docs/blueprint-khu-htlc-gw.md`

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

### Phase 5: DOMC Governance

**Objectif:** Masternodes votent R% annuel.

**Deliverables:**
- [ ] Vote proposal : SetKhuYieldRate(R_new)
- [ ] Validation bornes [0, R_MAX_dynamic]
- [ ] Activation vote dans ConnectBlock
- [ ] RPC : getkhugovernance, votekhurate (MN only)
- [ ] Tests : test_khu_governance.cpp, khu_governance.py

**Verification:**
```cpp
// Vote valide:
assert(R_new >= 0 && R_new <= R_MAX_dynamic);
// Egalite: DOMC ne gouverne QUE R%
assert(reward_year == max(6 - year, 0));  // Emission inchangee
```

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
