# PLAN D'IMPLÉMENTATION — Alignement Code/Documentation

**Date:** 2025-11-26
**Branche:** khu-code-cleanup
**Objectif:** Aligner le code C++ avec la spécification technique

---

## ÉTAT ACTUEL

### Déjà Implémenté (commit 278dbc1)
- [x] Z field dans KhuGlobalState
- [x] CheckInvariants() vérifie C == U + Z
- [x] STAKE: U -= amount, Z += amount
- [x] UNSTAKE: 5 mutations atomiques (Z--, U++, C++, Cr--, Ur--)
- [x] DAILY_YIELD: Cr += yield, Ur += yield
- [x] Sérialisation avec Z

---

## DIVERGENCES À CORRIGER

### PRIORITÉ 1 — Critique (Consensus)

#### 1.1 Vérifier KhuGlobalState (17 champs)
**Spec dit:** 17 champs dans cet ordre exact
```
C, U, Z, Cr, Ur, T, R_annual, R_MAX_dynamic,
last_yield_update_height, last_yield_amount,
domc_cycle_start, domc_cycle_length, domc_commit_phase_start, domc_reveal_deadline,
nHeight, hashBlock, hashPrevState
```
**Action:** Vérifier que khu_state.h correspond exactement

#### 1.2 Vérifier les constantes
**Fichiers:** khu_yield.h, khu_domc.h, consensus/params.h
```cpp
BLOCKS_PER_DAY = 1440
MATURITY_BLOCKS = 4320
DOMC_CYCLE_LENGTH = 172800
DOMC_COMMIT_OFFSET = 132480
DOMC_REVEAL_OFFSET = 152640
INITIAL_R = 3700
MIN_R_MAX = 400
```

#### 1.3 Vérifier formule Yield (linéaire, pas composé)
**Spec dit:** `daily = (amount × R_annual) / 10000 / 365`
**Fichier:** khu_yield.cpp:CalculateDailyYieldForNote()
**Action:** Confirmer pas de compounding

#### 1.4 Vérifier formule DAO Treasury
**Spec dit:** `T_daily = (U + Ur) / 182500`
**Fichier:** khu_dao.cpp
**Action:** Vérifier diviseur = 182500, utilise (U + Ur)

---

### PRIORITÉ 2 — Important (Logique métier)

#### 2.1 Vérifier ConnectBlock order
**Spec dit:**
1. LOCK2(cs_main, cs_khu)
2. Load previous state
3. ApplyDailyUpdatesIfNeeded() — AVANT transactions
4. Process KHU transactions
5. Apply block reward
6. CheckInvariants()
7. Persist state

**Fichier:** khu_validation.cpp:ProcessKHUBlock()

#### 2.2 Vérifier maturity dans ApplyDailyYield
**Spec dit:** Seules les notes avec age >= 4320 reçoivent du yield
**Fichier:** khu_yield.cpp:IsNoteMature()

#### 2.3 Vérifier ZKHU namespace 'K'
**Spec dit:** ZKHU utilise namespace 'K', Shield utilise 'S'
**Fichier:** zkhu_db.cpp

---

### PRIORITÉ 3 — Wallet/RPC

#### 3.1 Vérifier RPCs implémentés
- [ ] getkhustate
- [ ] mintkhu
- [ ] redeemkhu
- [ ] stakekhu
- [ ] unstakekhu
- [ ] domccommit
- [ ] domcreveal

#### 3.2 Vérifier wallet tracking
**Fichier:** wallet/khu_wallet.cpp

---

### PRIORITÉ 4 — Tests

#### 4.1 Mettre à jour tests unitaires
Les tests doivent refléter les nouvelles mutations avec Z

#### 4.2 Vérifier test_pivx compile et passe

---

## PLAN D'EXÉCUTION

### Phase A: Audit (1-2h)
1. Lire chaque fichier khu/*.cpp et khu/*.h
2. Comparer avec spec ligne par ligne
3. Documenter toutes les divergences

### Phase B: Corrections critiques (2-4h)
1. Corriger KhuGlobalState si nécessaire
2. Corriger constantes
3. Corriger formules
4. Vérifier ConnectBlock order

### Phase C: Tests (1-2h)
1. Build complet
2. Tests unitaires
3. Tests fonctionnels si disponibles

### Phase D: Commit final
1. Commit groupé avec message descriptif
2. Push vers remote

---

## FICHIERS À AUDITER

```
src/khu/khu_state.h          ← KhuGlobalState (17 champs)
src/khu/khu_state.cpp        ← GetHash()
src/khu/khu_validation.cpp   ← ProcessKHUBlock, ConnectBlock order
src/khu/khu_yield.h          ← Constantes yield
src/khu/khu_yield.cpp        ← CalculateDailyYieldForNote, ApplyDailyYield
src/khu/khu_dao.h            ← Constantes DAO
src/khu/khu_dao.cpp          ← AccumulateDaoTreasuryIfNeeded
src/khu/khu_domc.h           ← Constantes DOMC
src/khu/khu_domc.cpp         ← DOMC cycle logic
src/khu/khu_stake.cpp        ← STAKE mutations
src/khu/khu_unstake.cpp      ← UNSTAKE mutations
src/khu/khu_mint.cpp         ← MINT mutations
src/khu/khu_redeem.cpp       ← REDEEM mutations
src/khu/zkhu_db.cpp          ← Namespace 'K'
src/wallet/rpc_khu.cpp       ← RPCs
src/wallet/khu_wallet.cpp    ← Wallet tracking
```

---

## CHECKLIST FINALE

- [ ] 17 champs KhuGlobalState dans l'ordre exact
- [ ] C == U + Z vérifié dans CheckInvariants()
- [ ] Cr == Ur vérifié dans CheckInvariants()
- [ ] T >= 0 vérifié
- [ ] MINT: C += P, U += P (atomique)
- [ ] REDEEM: C -= P, U -= P (atomique)
- [ ] STAKE: U -= P, Z += P (atomique)
- [ ] UNSTAKE: 5 mutations atomiques sans code entre
- [ ] DAILY_YIELD: Cr += Y, Ur += Y (atomique)
- [ ] DAO Treasury: T += (U + Ur) / 182500
- [ ] Yield linéaire (pas composé)
- [ ] Maturity = 4320 blocs
- [ ] DOMC cycle = 172800 blocs
- [ ] R_annual initial = 3700 (37%)
- [ ] ConnectBlock: daily updates AVANT transactions
- [ ] ZKHU namespace = 'K'
- [ ] Build OK
- [ ] Tests OK
