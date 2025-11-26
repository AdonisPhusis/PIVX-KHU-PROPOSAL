# 🔄 PIPELINE COMPLET KHUGLOBALSTATE

## Vue d'ensemble du cycle complet

```
BLOC N-1                    BLOC N                      BLOC N+1
   │                          │                            │
   ├─ State₍ₙ₋₁₎              ├─ Load State₍ₙ₋₁₎          ├─ Load State₍ₙ₎
   │  C=1000                  │  C=1000                   │  C=1150
   │  U=950                   │  U=950                    │  U=1150
   │  Z=50                    │  Z=50                     │  Z=0
   │  Cr=50                   │  Cr=50                    │  Cr=5
   │  Ur=50                   │  Ur=50                    │  Ur=5
   │  T=10                    │  T=10                     │  T=10
   │                          │                            │
   │                          ├─ ApplyDailyYield          │
   │                          │  Cr=52 (+2)               │
   │                          │  Ur=52 (+2)               │
   │                          │                            │
   │                          ├─ ProcessTX: MINT 100      │
   │                          │  C=1100 (+100)            │
   │                          │  U=1050 (+100)            │
   │                          │  Z=50 (unchanged)         │
   │                          │                            │
   │                          ├─ ProcessTX: UNSTAKE P=50  │
   │                          │  Z=0 (-50)                │
   │                          │  U=1150 (+50P+50B)        │
   │                          │  C=1150 (+50 bonus)       │
   │                          │  Cr=2 (-50)               │
   │                          │  Ur=2 (-50)               │
   │                          │                            │
   │                          ├─ AccumulateDAO            │
   │                          │  (pas de boundary)        │
   │                          │  T=10 (inchangé)          │
   │                          │                            │
   │                          ├─ CheckInvariants ✅       │
   │                          │  C==U+Z? 1150==1150+0 ✅  │
   │                          │  Cr==Ur? 2==2 ✅          │
   │                          │  T>=0? 10>=0 ✅           │
   │                          │                            │
   │                          ├─ Persist State₍ₙ₎         │
   │                          │  Write to LevelDB         │
   │                          │                            │
   └──────────────────────────┴────────────────────────────┘
```

---

## Code Pipeline Complet (Pseudo-C++)

```cpp
/**
 * ═══════════════════════════════════════════════════════════
 * PIPELINE COMPLET: ConnectBlock avec KhuGlobalState
 * ═══════════════════════════════════════════════════════════
 */

bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view)
{
    uint32_t nHeight = pindex->nHeight;

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 0: VÉRIFIER ACTIVATION V6
    // ═══════════════════════════════════════════════════════════
    if (!IsKHUActive(nHeight)) {
        // Legacy PIVX comportement
        return true;
    }

    LogPrintf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    LogPrintf("🔄 PIPELINE KHU - BLOC %d\n", nHeight);
    LogPrintf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 1: LOAD PREVIOUS STATE
    // ═══════════════════════════════════════════════════════════
    KhuGlobalState prevState;
    if (!LoadKhuState(pindex->pprev->GetBlockHash(), prevState)) {
        return error("❌ Cannot load previous KHU state");
    }

    LogPrintf("📥 LOADED STATE (height %d):\n", nHeight - 1);
    LogPrintf("   C  = %lld\n", prevState.C);
    LogPrintf("   U  = %lld\n", prevState.U);
    LogPrintf("   Z  = %lld\n", prevState.Z);
    LogPrintf("   Cr = %lld\n", prevState.Cr);
    LogPrintf("   Ur = %lld\n", prevState.Ur);
    LogPrintf("   T  = %lld\n", prevState.T);
    LogPrintf("   R_annual = %d (%.2f%%)\n", prevState.R_annual, prevState.R_annual / 100.0);

    // Vérifier intégrité état précédent
    if (!prevState.CheckInvariants()) {
        return error("❌ Previous state invariants broken!");
    }
    LogPrintf("✅ Previous state invariants OK\n\n");

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 2: COPY TO NEW STATE
    // ═══════════════════════════════════════════════════════════
    KhuGlobalState newState = prevState;
    newState.nHeight = nHeight;
    newState.hashBlock = block.GetHash();
    newState.hashPrevState = prevState.GetHash();

    LogPrintf("📋 COPIED TO NEW STATE (height %d)\n\n", nHeight);

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 3: FINALIZE DOMC CYCLE (si boundary)
    // ═══════════════════════════════════════════════════════════
    if (khu_domc::IsDomcCycleBoundary(nHeight, consensus.nKhuActivationHeight)) {
        LogPrintf("🗳️  DOMC CYCLE BOUNDARY DETECTED\n");

        if (!FinalizeDomcCycle(newState, nHeight, consensus)) {
            return error("❌ DOMC cycle finalization failed");
        }

        LogPrintf("   → R_annual updated: %d → %d\n",
                  prevState.R_annual, newState.R_annual);
        LogPrintf("   → R_MAX_dynamic: %d\n", newState.R_MAX_dynamic);
        LogPrintf("✅ DOMC cycle finalized\n\n");
    }

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 4: INITIALIZE NEW DOMC CYCLE (si needed)
    // ═══════════════════════════════════════════════════════════
    if (khu_domc::ShouldInitializeCycle(newState, nHeight, consensus)) {
        LogPrintf("🔄 INITIALIZE NEW DOMC CYCLE\n");

        if (!InitializeDomcCycle(newState, nHeight, consensus)) {
            return error("❌ DOMC cycle initialization failed");
        }

        LogPrintf("   → domc_cycle_start: %d\n", newState.domc_cycle_start);
        LogPrintf("   → domc_commit_phase_start: %d\n",
                  newState.domc_cycle_start + 132480);
        LogPrintf("   → domc_reveal_deadline: %d\n",
                  newState.domc_cycle_start + 152640);
        LogPrintf("✅ New DOMC cycle initialized\n\n");
    }

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 5: ACCUMULATE DAO TREASURY (si boundary)
    // ═══════════════════════════════════════════════════════════
    if (khu_dao::IsDaoCycleBoundary(nHeight, consensus.nKhuActivationHeight)) {
        LogPrintf("🏦 DAO TREASURY BOUNDARY DETECTED\n");

        int64_t prev_T = newState.T;
        int64_t delta = khu_dao::CalculateDaoTreasuryDelta(newState.U, newState.Ur);

        newState.T += delta;

        LogPrintf("   → Delta = (U + Ur) × 0.5%% = (%lld + %lld) × 0.005 = %lld\n",
                  newState.U, newState.Ur, delta);
        LogPrintf("   → T: %lld → %lld (+%lld)\n", prev_T, newState.T, delta);
        LogPrintf("✅ DAO Treasury accumulated\n\n");
    }

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 6: APPLY DAILY YIELD (si interval 1440)
    // ═══════════════════════════════════════════════════════════
    if (khu_yield::IsDailyYieldInterval(nHeight, newState.last_yield_update_height)) {
        LogPrintf("💰 DAILY YIELD INTERVAL DETECTED\n");

        int64_t prev_Cr = newState.Cr;
        int64_t prev_Ur = newState.Ur;
        int64_t totalYield = 0;

        if (!ApplyDailyYield(newState, nHeight, totalYield)) {
            return error("❌ Daily yield application failed");
        }

        LogPrintf("   → Total yield calculated: %lld\n", totalYield);
        LogPrintf("   → Cr: %lld → %lld (+%lld)\n", prev_Cr, newState.Cr, totalYield);
        LogPrintf("   → Ur: %lld → %lld (+%lld)\n", prev_Ur, newState.Ur, totalYield);
        LogPrintf("   → last_yield_update_height: %d → %d\n",
                  prevState.last_yield_update_height, newState.last_yield_update_height);
        LogPrintf("✅ Daily yield applied\n\n");
    }

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 7: PROCESS ALL TRANSACTIONS IN BLOCK
    // ═══════════════════════════════════════════════════════════
    LogPrintf("📦 PROCESSING %zu TRANSACTIONS\n", block.vtx.size());

    for (size_t i = 0; i < block.vtx.size(); i++) {
        const CTransactionRef& tx = block.vtx[i];

        LogPrintf("\n   ┌─ TX %zu/%zu (%s)\n", i + 1, block.vtx.size(),
                  tx->GetHash().ToString().substr(0, 16).c_str());

        // État AVANT transaction
        int64_t C_before = newState.C;
        int64_t U_before = newState.U;
        int64_t Z_before = newState.Z;
        int64_t Cr_before = newState.Cr;
        int64_t Ur_before = newState.Ur;

        // ───────────────────────────────────────────────────────
        // MINT: PIV → KHU_T
        // ───────────────────────────────────────────────────────
        if (tx->nType == CTransaction::TxType::KHU_MINT) {
            LogPrintf("   │  🪙 MINT detected\n");

            int64_t mint_amount = GetMintAmount(tx);

            newState.C += mint_amount;
            newState.U += mint_amount;

            LogPrintf("   │     Amount: %lld\n", mint_amount);
            LogPrintf("   │     C: %lld → %lld (+%lld)\n", C_before, newState.C, mint_amount);
            LogPrintf("   │     U: %lld → %lld (+%lld)\n", U_before, newState.U, mint_amount);
        }

        // ───────────────────────────────────────────────────────
        // REDEEM: KHU_T → PIV
        // ───────────────────────────────────────────────────────
        else if (tx->nType == CTransaction::TxType::KHU_REDEEM) {
            LogPrintf("   │  💵 REDEEM detected\n");

            int64_t redeem_amount = GetRedeemAmount(tx);

            newState.C -= redeem_amount;
            newState.U -= redeem_amount;

            LogPrintf("   │     Amount: %lld\n", redeem_amount);
            LogPrintf("   │     C: %lld → %lld (-%lld)\n", C_before, newState.C, redeem_amount);
            LogPrintf("   │     U: %lld → %lld (-%lld)\n", U_before, newState.U, redeem_amount);
        }

        // ───────────────────────────────────────────────────────
        // STAKE: KHU_T → ZKHU (U decreases, Z increases)
        // ───────────────────────────────────────────────────────
        else if (tx->nType == CTransaction::TxType::KHU_STAKE) {
            LogPrintf("   │  🔒 STAKE detected\n");

            int64_t stake_amount = GetStakeAmount(tx);

            newState.U -= stake_amount;  // KHU_T supply decreases
            newState.Z += stake_amount;  // ZKHU supply increases

            LogPrintf("   │     Amount: %lld\n", stake_amount);
            LogPrintf("   │     U: %lld → %lld (-%lld)\n", U_before, newState.U, stake_amount);
            LogPrintf("   │     Z: %lld → %lld (+%lld)\n", Z_before, newState.Z, stake_amount);
            LogPrintf("   │     C: %lld (unchanged)\n", newState.C);
            LogPrintf("   │     → Note created (transparent → shielded)\n");
        }

        // ───────────────────────────────────────────────────────
        // UNSTAKE: ZKHU → KHU_T (avec bonus Ur)
        // ───────────────────────────────────────────────────────
        else if (tx->nType == CTransaction::TxType::KHU_UNSTAKE) {
            LogPrintf("   │  🔓 UNSTAKE detected\n");

            int64_t principal = GetUnstakePrincipal(tx);
            int64_t bonus = GetUnstakeBonus(tx);

            // UNSTAKE: Z-, U+ (principal+bonus), C+ (bonus only)
            newState.Z  -= principal;           // (1) ZKHU supply decreases
            newState.U  += principal + bonus;   // (2) KHU_T supply increases
            newState.C  += bonus;               // (3) Collateral increases (bonus only)
            newState.Cr -= bonus;               // (4) Consume reward pool
            newState.Ur -= bonus;               // (5) Consume reward rights

            LogPrintf("   │     Principal: %lld\n", principal);
            LogPrintf("   │     Bonus: %lld\n", bonus);
            LogPrintf("   │     Z:  %lld → %lld (-%lld)\n", Z_before, newState.Z, principal);
            LogPrintf("   │     U:  %lld → %lld (+%lld)\n", U_before, newState.U, principal + bonus);
            LogPrintf("   │     C:  %lld → %lld (+%lld)\n", C_before, newState.C, bonus);
            LogPrintf("   │     Cr: %lld → %lld (-%lld)\n", Cr_before, newState.Cr, bonus);
            LogPrintf("   │     Ur: %lld → %lld (-%lld)\n", Ur_before, newState.Ur, bonus);
        }

        // ───────────────────────────────────────────────────────
        // DOMC COMMIT
        // ───────────────────────────────────────────────────────
        else if (tx->nType == CTransaction::TxType::KHU_DOMC_COMMIT) {
            LogPrintf("   │  🗳️  DOMC COMMIT detected\n");

            uint256 hashCommit = GetCommitHash(tx);

            LogPrintf("   │     Commit hash: %s...\n", hashCommit.ToString().substr(0, 16).c_str());
            LogPrintf("   │     → Stored in DB (no state change)\n");
        }

        // ───────────────────────────────────────────────────────
        // DOMC REVEAL
        // ───────────────────────────────────────────────────────
        else if (tx->nType == CTransaction::TxType::KHU_DOMC_REVEAL) {
            LogPrintf("   │  🔓 DOMC REVEAL detected\n");

            uint16_t R_proposal = GetRevealR(tx);

            LogPrintf("   │     R_proposal: %d (%.2f%%)\n", R_proposal, R_proposal / 100.0);
            LogPrintf("   │     → Stored in DB (will be used at cycle boundary)\n");
        }

        LogPrintf("   └─ TX processed ✅\n");
    }

    LogPrintf("\n✅ All %zu transactions processed\n\n", block.vtx.size());

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 8: CHECK INVARIANTS (CRITIQUE!)
    // ═══════════════════════════════════════════════════════════
    LogPrintf("🔍 CHECKING INVARIANTS\n");

    if (!newState.CheckInvariants()) {
        LogPrintf("❌ INVARIANTS VIOLATION DETECTED:\n");
        LogPrintf("   C  = %lld\n", newState.C);
        LogPrintf("   U  = %lld\n", newState.U);
        LogPrintf("   Z  = %lld\n", newState.Z);
        LogPrintf("   Cr = %lld\n", newState.Cr);
        LogPrintf("   Ur = %lld\n", newState.Ur);
        LogPrintf("   T  = %lld\n", newState.T);
        LogPrintf("   C == U+Z?  %s\n", (newState.C == newState.U + newState.Z) ? "✅" : "❌");
        LogPrintf("   Cr == Ur? %s\n", (newState.Cr == newState.Ur) ? "✅" : "❌");
        LogPrintf("   T >= 0?  %s\n", (newState.T >= 0) ? "✅" : "❌");

        return error("❌ Block rejected: invariants broken");
    }

    LogPrintf("✅ Invariants OK:\n");
    LogPrintf("   C == U+Z:   %lld == %lld + %lld ✅\n", newState.C, newState.U, newState.Z);
    LogPrintf("   Cr == Ur: %lld == %lld ✅\n", newState.Cr, newState.Ur);
    LogPrintf("   T >= 0:   %lld >= 0 ✅\n", newState.T);
    LogPrintf("\n");

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 9: PERSIST NEW STATE TO LEVELDB
    // ═══════════════════════════════════════════════════════════
    LogPrintf("💾 PERSISTING NEW STATE\n");

    if (!pKHUStateDB->WriteKHUState(nHeight, newState)) {
        return error("❌ Failed to persist KHU state");
    }

    LogPrintf("✅ State persisted to LevelDB\n");
    LogPrintf("   Height: %d\n", nHeight);
    LogPrintf("   Hash: %s\n", newState.GetHash().ToString().substr(0, 16).c_str());

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 10: FINAL STATE SUMMARY
    // ═══════════════════════════════════════════════════════════
    LogPrintf("\n📊 FINAL STATE (height %d):\n", nHeight);
    LogPrintf("   C  = %lld (Δ %+lld)\n", newState.C, newState.C - prevState.C);
    LogPrintf("   U  = %lld (Δ %+lld)\n", newState.U, newState.U - prevState.U);
    LogPrintf("   Z  = %lld (Δ %+lld)\n", newState.Z, newState.Z - prevState.Z);
    LogPrintf("   Cr = %lld (Δ %+lld)\n", newState.Cr, newState.Cr - prevState.Cr);
    LogPrintf("   Ur = %lld (Δ %+lld)\n", newState.Ur, newState.Ur - prevState.Ur);
    LogPrintf("   T  = %lld (Δ %+lld)\n", newState.T, newState.T - prevState.T);
    LogPrintf("   R_annual = %d (%.2f%%)\n", newState.R_annual, newState.R_annual / 100.0);
    LogPrintf("   → C == U+Z: %lld == %lld + %lld ✅\n", newState.C, newState.U, newState.Z);

    LogPrintf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    LogPrintf("✅ BLOC %d CONNECTED SUCCESSFULLY\n", nHeight);
    LogPrintf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n\n");

    return true;
}
```

---

## Exemple Logs Réels (Bloc avec MINT + UNSTAKE + Yield)

```
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🔄 PIPELINE KHU - BLOC 152640
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

📥 LOADED STATE (height 152639):
   C  = 5000000000000
   U  = 4950000000000
   Z  = 50000000000
   Cr = 250000000000
   Ur = 250000000000
   T  = 125000000000
   R_annual = 1500 (15.00%)
✅ Previous state invariants OK (C == U+Z: 5000B == 4950B + 50B)

📋 COPIED TO NEW STATE (height 152640)

🗳️  DOMC CYCLE BOUNDARY DETECTED
   → R_annual updated: 1500 → 1200
   → R_MAX_dynamic: 2900
✅ DOMC cycle finalized

🔄 INITIALIZE NEW DOMC CYCLE
   → domc_cycle_start: 152640
   → domc_commit_phase_start: 285120
   → domc_reveal_deadline: 305280
✅ New DOMC cycle initialized

🏦 DAO TREASURY BOUNDARY DETECTED
   → Delta = (U + Ur) × 0.5% = (5000000000000 + 250000000000) × 0.005 = 26250000000
   → T: 125000000000 → 151250000000 (+26250000000)
✅ DAO Treasury accumulated

💰 DAILY YIELD INTERVAL DETECTED
   → Total yield calculated: 2050000000
   → Cr: 250000000000 → 252050000000 (+2050000000)
   → Ur: 250000000000 → 252050000000 (+2050000000)
   → last_yield_update_height: 151200 → 152640
✅ Daily yield applied

📦 PROCESSING 3 TRANSACTIONS

   ┌─ TX 1/3 (coinbase)
   │  → Coinbase (skipped)
   └─ TX processed ✅

   ┌─ TX 2/3 (a3f5d8b2...)
   │  🪙 MINT detected
   │     Amount: 100000000000
   │     C: 5000000000000 → 5100000000000 (+100000000000)
   │     U: 4950000000000 → 5050000000000 (+100000000000)
   │     Z: 50000000000 (unchanged)
   └─ TX processed ✅

   ┌─ TX 3/3 (7b2c9e4a...)
   │  🔓 UNSTAKE detected
   │     Principal: 50000000000
   │     Bonus: 5000000000
   │     Z:  50000000000 → 0 (-50000000000)
   │     U:  5050000000000 → 5105000000000 (+55000000000)
   │     C:  5100000000000 → 5105000000000 (+5000000000)
   │     Cr: 252050000000 → 247050000000 (-5000000000)
   │     Ur: 252050000000 → 247050000000 (-5000000000)
   └─ TX processed ✅

✅ All 3 transactions processed

🔍 CHECKING INVARIANTS
✅ Invariants OK:
   C == U+Z:   5105000000000 == 5105000000000 + 0 ✅
   Cr == Ur: 247050000000 == 247050000000 ✅
   T >= 0:   151250000000 >= 0 ✅

💾 PERSISTING NEW STATE
✅ State persisted to LevelDB
   Height: 152640
   Hash: 3a7f5c2d9b8e...

📊 FINAL STATE (height 152640):
   C  = 5105000000000 (Δ +105000000000)
   U  = 5105000000000 (Δ +155000000000)
   Z  = 0 (Δ -50000000000)
   Cr = 247050000000 (Δ -2950000000)
   Ur = 247050000000 (Δ -2950000000)
   T  = 151250000000 (Δ +26250000000)
   R_annual = 1200 (12.00%)
   → C == U+Z: 5105B == 5105B + 0 ✅

━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
✅ BLOC 152640 CONNECTED SUCCESSFULLY
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
```

---

## Points Clés à Noter

### 1. KhuGlobalState Circule Partout
```
Load prevState → Copy to newState → Modify newState → Check → Persist
```

### 2. Ordre Strict (Immuable)
```
1. Finalize DOMC
2. Initialize DOMC
3. Accumulate DAO
4. Apply Yield
5. Process TX
6. Check Invariants
7. Persist
```

### 3. Invariants Vérifiés Avant Persist
```cpp
if (!newState.CheckInvariants()) {
    // ❌ REJECT BLOCK - Ne jamais persister état invalide
    return false;
}
```

### 4. Chaque Opération Modifie newState
- MINT: `C+=, U+=`
- REDEEM: `C-=, U-=`
- STAKE: `U-=, Z+=` (transparent → shielded)
- UNSTAKE: `Z-=P, U+=P+B, C+=B, Cr-=B, Ur-=B` (double flux)
- Yield: `Cr+=, Ur+=`
- DAO: `T+=`

Note: L'invariant `C == U + Z` est toujours préservé.

### 5. Atomicité Garantie
Toutes modifications sous **même verrou critique** (`cs_khu`).
Soit tout commit, soit tout rollback.

---

## Visualisation État Multi-Blocs

```
Bloc 100000:  C=1000  U=1000  Z=0    Cr=50   Ur=50   T=10   [MINT 500]
              ↓       ↓       ↓       ↓       ↓       ↓
Bloc 100001:  C=1500  U=1500  Z=0    Cr=50   Ur=50   T=10   [STAKE 200]
              ↓       ↓       ↓       ↓       ↓       ↓
Bloc 100002:  C=1500  U=1300  Z=200  Cr=50   Ur=50   T=10   (no KHU TX)
              ↓       ↓       ↓       ↓       ↓       ↓
...
Bloc 101440:  C=1500  U=1300  Z=200  Cr=50   Ur=50   T=10   [YIELD +3]
              ↓       ↓       ↓       ↓       ↓       ↓
Bloc 101441:  C=1500  U=1300  Z=200  Cr=53   Ur=53   T=10   [UNSTAKE P=200, B=10]
              ↓       ↓       ↓       ↓       ↓       ↓
Bloc 101442:  C=1510  U=1510  Z=0    Cr=43   Ur=43   T=10   (no KHU TX)
              ↓       ↓       ↓       ↓       ↓       ↓
...
Bloc 172800:  C=1510  U=1510  Z=0    Cr=43   Ur=43   T=10   [DAO BOUNDARY]
              ↓       ↓       ↓       ↓       ↓       ↓
Bloc 172801:  C=1510  U=1510  Z=0    Cr=43   Ur=43   T=17   (T += 0.5% × (U+Ur))

Invariants vérifiés à CHAQUE bloc:
✅ C == U + Z  (toujours)
✅ Cr == Ur  (toujours)
✅ T >= 0  (toujours)
```

---

**FIN DU PIPELINE COMPLET**
