# 06 — PIVX-V6-KHU PROTOCOL REFERENCE

Version: 1.0.0
Status: IMPLEMENTATION GUIDE
Target: PIVX Core v6.0+

---

## 1. DOCUMENT OBJECTIVE

Complete C++ implementation reference for PIVX-V6-KHU.

Describes:
- Exact C++ structures
- File locations
- Function signatures
- Consensus algorithms
- Validation logic
- Pseudo-code for all operations

No commentary. No justification. Implementation only.

---

## 2. C++ STRUCTURES

### 2.1 KhuGlobalState

**File:** `src/khu/khu_state.h`

```cpp
struct KhuGlobalState {
    // Collateral and supply
    int64_t C;                      // PIV collateral (satoshis)
    int64_t U;                      // KHU_T supply (satoshis)

    // Reward pool
    int64_t Cr;                     // Reward pool (satoshis)
    int64_t Ur;                     // Reward rights (satoshis)

    // DOMC parameters
    uint16_t R_annual;              // Basis points (0-3000)
    uint16_t R_MAX_dynamic;         // max(400, 3000 - year*100)
    uint32_t last_domc_height;      // Last DOMC update height

    // DOMC cycle
    uint32_t domc_cycle_start;      // Cycle start height
    uint32_t domc_cycle_length;     // 43200 blocks (30 days)
    uint32_t domc_phase_length;     // 4320 blocks (3 days)

    // Yield scheduler
    uint32_t last_yield_update_height;  // Last 1440-block update

    // Chain tracking
    uint32_t nHeight;               // State height
    uint256 hashBlock;              // Block hash
    uint256 hashPrevState;          // Previous state hash (chain)

    // Methods
    bool CheckInvariants() const {
        bool cd_ok = (U == 0 || C == U);
        bool cdr_ok = (Ur == 0 || Cr == Ur);
        return cd_ok && cdr_ok;
    }

    uint256 GetHash() const;

    SERIALIZE_METHODS(KhuGlobalState, obj) {
        READWRITE(obj.C, obj.U, obj.Cr, obj.Ur);
        READWRITE(obj.R_annual, obj.R_MAX_dynamic, obj.last_domc_height);
        READWRITE(obj.domc_cycle_start, obj.domc_cycle_length, obj.domc_phase_length);
        READWRITE(obj.last_yield_update_height);
        READWRITE(obj.nHeight, obj.hashBlock, obj.hashPrevState);
    }
};
```

### 2.2 CKHUUTXO

**File:** `src/khu/khu_utxo.h`

```cpp
struct CKHUUTXO {
    int64_t amount;                 // KHU_T amount (satoshis)
    CScript scriptPubKey;           // Owner script
    uint32_t nHeight;               // Creation height

    // Staking metadata
    bool fStaked;                   // Currently staked as ZKHU
    uint32_t nStakeStartHeight;     // Stake start height (0 if not staked)

    SERIALIZE_METHODS(CKHUUTXO, obj) {
        READWRITE(obj.amount, obj.scriptPubKey, obj.nHeight);
        READWRITE(obj.fStaked, obj.nStakeStartHeight);
    }

    bool IsSpent() const { return amount == 0; }
    bool IsStaked() const { return fStaked; }
};
```

### 2.3 Transaction Types

**File:** `src/primitives/transaction.h`

```cpp
enum TxType: int16_t {
    NORMAL = 0,
    PROREG = 1,
    PROUPSERV = 2,
    PROUPREG = 3,
    PROUPREV = 4,
    LLMQCOMM = 5,

    // KHU transaction types
    KHU_MINT = 10,          // PIV → KHU_T
    KHU_REDEEM = 11,        // KHU_T → PIV
    KHU_STAKE = 12,         // KHU_T → ZKHU
    KHU_UNSTAKE = 13,       // ZKHU → KHU_T
    DOMC_VOTE = 14,         // DOMC R% vote
    HTLC_CREATE = 15,       // Create HTLC
    HTLC_CLAIM = 16,        // Claim HTLC
    HTLC_REFUND = 17,       // Refund HTLC
};
```

### 2.4 MINT Payload

**File:** `src/khu/khu_mint.h`

```cpp
class CKHUMintPayload {
public:
    uint16_t nVersion;              // Payload version
    int64_t pivAmount;              // PIV burned (satoshis)
    CScript khuRecipient;           // KHU_T recipient script

    SERIALIZE_METHODS(CKHUMintPayload, obj) {
        READWRITE(obj.nVersion, obj.pivAmount, obj.khuRecipient);
    }
};
```

### 2.5 REDEEM Payload

**File:** `src/khu/khu_redeem.h`

```cpp
class CKHURedeemPayload {
public:
    uint16_t nVersion;
    int64_t khuAmount;              // KHU_T burned (satoshis)
    CScript pivRecipient;           // PIV recipient script

    SERIALIZE_METHODS(CKHURedeemPayload, obj) {
        READWRITE(obj.nVersion, obj.khuAmount, obj.pivRecipient);
    }
};
```

### 2.6 STAKE Payload

**File:** `src/khu/khu_stake.h`

```cpp
class CKHUStakePayload {
public:
    uint16_t nVersion;
    int64_t khuAmount;              // KHU_T staked
    uint256 noteCommitment;         // Sapling note commitment

    SERIALIZE_METHODS(CKHUStakePayload, obj) {
        READWRITE(obj.nVersion, obj.khuAmount, obj.noteCommitment);
    }
};
```

### 2.7 UNSTAKE Payload

**File:** `src/khu/khu_unstake.h`

```cpp
class CKHUUnstakePayload {
public:
    uint16_t nVersion;
    uint256 nullifier;              // ZKHU nullifier
    int64_t principal;              // Original stake amount
    int64_t bonus;                  // Ur_accumulated

    SERIALIZE_METHODS(CKHUUnstakePayload, obj) {
        READWRITE(obj.nVersion, obj.nullifier, obj.principal, obj.bonus);
    }
};
```

### 2.8 DOMC Vote Payload

**File:** `src/khu/khu_domc.h`

```cpp
class CDOMCVotePayload {
public:
    uint16_t nVersion;
    uint16_t R_proposal;            // Proposed R% (basis points)
    uint256 commitment;             // Hash(R_proposal || secret) [COMMIT phase]
    uint256 secret;                 // Revealed in REVEAL phase
    CPubKey voterPubKey;            // Masternode public key
    std::vector<unsigned char> vchSig;  // Signature

    SERIALIZE_METHODS(CDOMCVotePayload, obj) {
        READWRITE(obj.nVersion, obj.R_proposal, obj.commitment, obj.secret);
        READWRITE(obj.voterPubKey, obj.vchSig);
    }
};
```

### 2.9 HTLC Payload

**File:** `src/khu/khu_htlc.h`

```cpp
class CHTLCPayload {
public:
    uint16_t nVersion;
    uint256 hashlock;               // SHA256(preimage)
    uint32_t timelock;              // Absolute block height
    int64_t amount;                 // KHU_T locked
    CScript recipientScript;        // Recipient if claimed
    CScript refundScript;           // Refund if timeout

    SERIALIZE_METHODS(CHTLCPayload, obj) {
        READWRITE(obj.nVersion, obj.hashlock, obj.timelock, obj.amount);
        READWRITE(obj.recipientScript, obj.refundScript);
    }
};
```

### 2.10 ZKHU Note Data

**File:** `src/khu/khu_stake.h`

```cpp
struct ZKHUNoteData {
    uint256 nullifier;              // Unique nullifier
    int64_t amount;                 // Stake amount
    uint32_t stakeStartHeight;      // Stake start height
    int64_t Ur_accumulated;         // Accumulated yield
    libzcash::SaplingPaymentAddress address;  // Owner address

    SERIALIZE_METHODS(ZKHUNoteData, obj) {
        READWRITE(obj.nullifier, obj.amount, obj.stakeStartHeight);
        READWRITE(obj.Ur_accumulated, obj.address);
    }
};
```

---

## 3. ORDER OF OPERATIONS IN CONNECTBLOCK

**File:** `src/validation.cpp`

**Canonical execution order for KHU consensus:**

```cpp
bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        // ============================================================
        // CRITICAL LOCK: Protect all KHU state mutations
        // ============================================================
        LOCK(cs_khu);

        KhuGlobalState prevState = LoadKhuState(pindex->pprev);
        KhuGlobalState newState = prevState;

        // 1. Apply daily yield (if needed, BEFORE transaction processing)
        ApplyDailyYieldIfNeeded(newState, pindex->nHeight);

        // 2. Process KHU transactions (MINT, REDEEM, STAKE, UNSTAKE, DOMC, HTLC)
        // ⚠️ ANTI-DÉRIVE: Return value MUST be checked for EVERY transaction
        for (const auto& tx : block.vtx) {
            // CRITICAL: Check return value and stop immediately on failure
            // DO NOT use patterns like:
            //   - ProcessKHUTransaction(tx, ...); (ignoring return value)
            //   - if (ProcessKHUTransaction(...)) continue; (inverted logic)
            //   - try { ProcessKHUTransaction(...); } catch(...) {} (silent failures)
            if (!ProcessKHUTransaction(tx, newState, view, state))
                return false;  // Stop immediately - block is INVALID, do not persist
        }

        // 3. Apply block reward (emission)
        ApplyBlockReward(newState, pindex->nHeight);

        // 4. Invariant check (MANDATORY)
        if (!newState.CheckInvariants()) {
            return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation");
        }

        // 5. Persist state
        pKHUStateDB->WriteKHUState(newState);
    }

    return true;
}
```

**Critical rules:**

1. **Yield MUST be applied BEFORE transactions** to avoid mismatch with UNSTAKE operations in the same block
2. **Invariants checked AFTER all operations** (no partial state allowed)
3. **State persistence is atomic** (all-or-nothing)

> ⚠️ **ANTI-DÉRIVE CRITIQUE : YIELD BEFORE TRANSACTIONS (IMMUABLE)**
>
> **Step 1 (ApplyDailyYieldIfNeeded) MUST execute BEFORE Step 2 (ProcessKHUTransaction).**
>
> **WHY THIS ORDER IS ABSOLUTELY MANDATORY:**
>
> Consider a block at height N where:
> - N % 1440 == 0 (daily yield update is triggered)
> - Block contains an UNSTAKE transaction of a mature ZKHU note
>
> **WRONG ORDER (transactions → yield) CAUSES CONSENSUS FAILURE:**
>
> ```cpp
> // ❌ FORBIDDEN ORDER
> for (const auto& tx : block.vtx) {
>     ProcessKHUTransaction(tx, newState, view, state);  // UNSTAKE executed FIRST
> }
> ApplyDailyYieldIfNeeded(newState, nHeight);  // Yield applied SECOND
>
> // WHAT HAPPENS:
> // 1. UNSTAKE reads note.Ur_accumulated (does NOT include today's yield yet)
> // 2. UNSTAKE calculates B = note.Ur_accumulated (WRONG, too small)
> // 3. UNSTAKE executes: state.Ur -= B, state.Cr -= B (WRONG amounts)
> // 4. ApplyDailyYield executes: state.Ur += daily, state.Cr += daily
> // 5. Result: state.Cr != state.Ur → INVARIANT VIOLATION → block rejected
> ```
>
> **CORRECT ORDER (yield → transactions) PRESERVES INVARIANTS:**
>
> ```cpp
> // ✅ REQUIRED ORDER
> ApplyDailyYieldIfNeeded(newState, nHeight);  // Yield applied FIRST
> for (const auto& tx : block.vtx) {
>     ProcessKHUTransaction(tx, newState, view, state);  // UNSTAKE executed SECOND
> }
>
> // WHAT HAPPENS:
> // 1. ApplyDailyYield updates: note.Ur_accumulated += daily, state.Ur += daily, state.Cr += daily
> // 2. UNSTAKE reads note.Ur_accumulated (INCLUDES today's yield)
> // 3. UNSTAKE calculates B = note.Ur_accumulated (CORRECT, includes all yield)
> // 4. UNSTAKE executes: state.Ur -= B, state.Cr -= B (CORRECT amounts)
> // 5. Result: state.Cr == state.Ur → INVARIANTS OK → block accepted
> ```
>
> **ENFORCEMENT:**
>
> This order is **HARDCODED** in the consensus specification and **CANNOT BE CHANGED** for:
> - Performance optimization
> - Code refactoring
> - Parallel execution
> - Any other reason whatsoever
>
> **VERIFICATION:**
>
> Before Phase 1 implementation, verify:
> ```bash
> # Ensure ApplyDailyYield is called BEFORE transaction processing loop
> grep -A10 "ApplyDailyYield" src/validation.cpp | grep -B5 "for.*vtx"
> ```
>
> If transaction processing appears BEFORE ApplyDailyYield in the code, **STOP IMMEDIATELY** and fix.

**Implementation phases:**
- Phase 1: Steps 4-5 only (invariants + persistence)
- Phase 2: Add step 2 (MINT/REDEEM)
- Phase 3: Add step 1 (yield scheduler)
- Phase 5: Complete step 2 (DOMC)
- Phase 7: Complete step 2 (HTLC)

---

## 3.1 UNSTAKE TRANSACTION PROCESSING (ATOMIC DOUBLE FLUX)

**File:** `src/khu/khu_validation.cpp`

**Purpose:** Process UNSTAKE transaction with atomic double-flux update.

**Critical requirement:** All four state mutations (U, C, Cr, Ur) MUST execute atomically under `cs_khu` lock.

**Pseudo-code:**

```cpp
bool ApplyKhuUnstake(const CTransaction& tx, KhuGlobalState& state,
                     CCoinsViewCache& view, CValidationState& validationState,
                     uint32_t nHeight) {
    // ============================================================
    // ⚠️ CRITICAL LOCK: MUST hold cs_khu for entire function
    // ============================================================
    // This lock guarantees atomic execution of the 4-way state mutation
    // (C, U, Cr, Ur) in the double flux below.
    // NEVER release this lock before all 4 mutations are complete.
    LOCK(cs_khu);

    // 1. Extract UNSTAKE payload
    CKHUUnstakePayload payload;
    if (!GetTxPayload(tx, payload))
        return validationState.Invalid("khu-unstake-invalid-payload");

    // 2. Extract ZKHU note from shielded spend
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // 3. Verify nullifier not already spent
    if (zkhuNullifierSet.count(spend.nullifier))
        return validationState.Invalid("khu-unstake-double-spend");

    // 4. Verify anchor matches ZKHU tree (not zPIV tree)
    if (spend.anchor != zkhuTree.root())
        return validationState.Invalid("khu-unstake-invalid-anchor");

    // 5. Extract note data from witness
    ZKHUNoteData noteData;
    if (!GetZKHUNoteData(spend.nullifier, noteData))
        return validationState.Invalid("khu-unstake-note-not-found");

    // 6. Verify maturity (minimum 4320 blocks staked)
    uint32_t stakeAge = nHeight - noteData.stakeStartHeight;
    if (stakeAge < KHU_MATURITY_BLOCKS)
        return validationState.Invalid("khu-unstake-immature",
                                     strprintf("Stake age %d < %d", stakeAge, KHU_MATURITY_BLOCKS));

    // 7. Extract amounts
    int64_t principal = noteData.amount;           // Original stake
    int64_t B = noteData.Ur_accumulated;           // Accumulated yield (bonus)

    // 8. Verify consistency with payload
    if (B != payload.bonus || principal != payload.principal)
        return validationState.Invalid("khu-unstake-amount-mismatch");

    // 9. Verify sufficient reward pool
    if (state.Cr < B || state.Ur < B)
        return validationState.Invalid("khu-unstake-insufficient-rewards",
                                     strprintf("Cr=%d Ur=%d B=%d", state.Cr, state.Ur, B));

    // ============================================================
    // 10. ATOMIC DOUBLE FLUX (CRITICAL SECTION - DO NOT MODIFY)
    // ============================================================
    // These four updates MUST execute atomically in ConnectBlock
    // under cs_khu lock, in this exact order, without interruption.
    //
    // FORBIDDEN:
    // - Separate these 4 lines into different functions
    // - Execute U+=B and C+=B in one block, Cr-=B and Ur-=B in another
    // - Add ANY code between these 4 lines that could fail/return
    // - Release cs_khu lock between these 4 lines
    //
    // If ANY of these 4 mutations fails, the ENTIRE block is rejected.
    // No partial execution. No rollback. All-or-nothing.

    state.U  += B;   // (1) Create B units of KHU_T for staker (flux ascendant)
    state.C  += B;   // (2) Increase collateralization by B (flux ascendant)
    state.Cr -= B;   // (3) Consume reward pool (flux descendant)
    state.Ur -= B;   // (4) Consume reward rights (flux descendant)

    // END OF ATOMIC SECTION
    // ============================================================

    // 11. Verify invariants after atomic mutation
    if (!state.CheckInvariants()) {
        // This should NEVER happen if yield calculation is correct
        // If it happens, block MUST be rejected
        return validationState.Invalid("khu-unstake-invariant-violation",
                                     strprintf("After UNSTAKE: C=%d U=%d Cr=%d Ur=%d",
                                             state.C, state.U, state.Cr, state.Ur));
    }

    // 12. Create KHU_T UTXO for staker (principal + bonus)
    CKHUUTXO newCoin;
    newCoin.amount = principal + B;
    newCoin.scriptPubKey = tx.vout[0].scriptPubKey;  // Recipient from tx output
    newCoin.nHeight = nHeight;
    newCoin.fStaked = false;
    newCoin.nStakeStartHeight = 0;

    // 13. Add UTXO to coins view
    view.AddKHUCoin(tx.GetHash(), 0, newCoin);

    // 14. Mark nullifier as spent (prevent double-spend)
    zkhuNullifierSet.insert(spend.nullifier);

    // 15. Log successful UNSTAKE
    LogPrint(BCLog::KHU, "UNSTAKE: principal=%d bonus=%d total=%d height=%d\n",
             principal, B, principal + B, nHeight);

    return true;
}
```

**Critical invariants:**

1. **Atomicity**: All four state mutations (steps 10.1-10.4) execute in single ConnectBlock
2. **No rollback**: If invariant check fails, entire block is rejected (no partial state)
3. **Lock protection**: `cs_khu` must be held during entire function execution
4. **Maturity enforcement**: UNSTAKE rejected if `stakeAge < 4320 blocks`
5. **Pool sufficiency**: `Cr >= B` and `Ur >= B` verified before mutation
6. **Nullifier uniqueness**: Each ZKHU note can only be unstaked once

**Failure modes:**

- `khu-unstake-immature`: Stake age < 4320 blocks → reject transaction
- `khu-unstake-insufficient-rewards`: Cr or Ur < B → reject block (yield calculation bug)
- `khu-unstake-invariant-violation`: Invariants broken after mutation → reject block (logic bug)
- `khu-unstake-double-spend`: Nullifier already spent → reject transaction

**Testing requirements:**

1. UNSTAKE at exact maturity (4320 blocks)
2. UNSTAKE before maturity (must reject)
3. UNSTAKE with zero yield (B=0, first day)
4. UNSTAKE with maximum yield (after many years)
5. Multiple UNSTAKEs in same block
6. UNSTAKE in same block as yield update
7. Double-spend attempt (same nullifier twice)

### 3.1.1 Edge Cases and Error Handling

**Case 1: UNSTAKE with yield not yet applied**

Scenario: Block N contains both yield update and UNSTAKE of a note benefiting from that yield.

Correct order (guaranteed by ConnectBlock):
```cpp
// 1. Yield BEFORE transactions
ApplyDailyYield(newState, N);
// → note.Ur_accumulated += daily
// → state.Ur += daily
// → state.Cr += daily

// 2. UNSTAKE uses updated yield
ApplyKhuUnstake(tx, newState, ...);
// → uses note.Ur_accumulated INCLUDING today's yield
```

If order reversed (BUG): UNSTAKE fails because `state.Ur < B` (yield not yet added).

**Case 2: UNSTAKE with insufficient Ur**

Scenario: Bug in yield calculation → `state.Ur < B`

Behavior:
```cpp
if (state.Cr < B || state.Ur < B)
    return validationState.Invalid("khu-unstake-insufficient-rewards");
```

Consequence: Block rejected, staker loses nothing (transaction remains in mempool).

Diagnostic: Log error with `Cr`, `Ur`, `B`, `height` for audit.

**Case 3: Double UNSTAKE in same block**

Scenario: Block contains two UNSTAKEs of different notes.

Sequential processing:
```cpp
for (const auto& tx : block.vtx) {
    if (tx.nType == TxType::KHU_UNSTAKE) {
        // First UNSTAKE
        state.Ur -= B1;
        state.Cr -= B1;

        // Second UNSTAKE
        state.Ur -= B2;
        state.Cr -= B2;
    }
}

// Invariants checked ONCE at end of block
assert(state.CheckInvariants());
```

Safety: As long as `Ur >= B1 + B2` before block, both UNSTAKEs succeed.

**Case 4: Invariant violation after UNSTAKE**

Scenario: Logic bug → invariants broken after double flux.

Behavior:
```cpp
if (!newState.CheckInvariants()) {
    return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation",
                       strprintf("C=%d U=%d Cr=%d Ur=%d", C, U, Cr, Ur));
}
```

Consequence:
- Block rejected
- Peer banned (score -100)
- Detailed logs for investigation
- **No automatic correction**

Possible causes:
- Overflow int64_t (impossible with current emission)
- Bug in ApplyDailyYield
- Memory corruption (detected by CheckInvariants)

### 3.1.2 Yield Accumulation (Ur) Implementation

**Daily update rule (every 1440 blocks):**

```cpp
void ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view) {
    // ⚠️ CRITICAL: This function MUST be called under cs_khu lock
    // to guarantee atomic updates of Cr and Ur
    AssertLockHeld(cs_khu);

    if ((nHeight - state.last_yield_update_height) < BLOCKS_PER_DAY)
        return;  // Not yet time

    int64_t Ur_increment = 0;

    // Calculate for each mature ZKHU note
    // Note: Cannot use const reference because we modify Ur_accumulated
    for (ZKHUNoteData& note : GetMatureZKHUNotes(view, nHeight)) {
        // Maturity: staked for >= 4320 blocks
        if ((nHeight - note.stakeStartHeight) < KHU_MATURITY_BLOCKS)
            continue;

        // Annual yield for this note
        // Divide first to prevent overflow (note.amount * R_annual could overflow)
        int64_t annual = (note.amount / 10000) * state.R_annual;
        int64_t daily = annual / 365;

        Ur_increment += daily;

        // Update note data
        note.Ur_accumulated += daily;
        UpdateNoteData(note);
    }

    // ============================================================
    // ATOMIC UPDATE: Ur and Cr MUST be updated together
    // ============================================================
    // These two updates are inseparable - if one fails, both fail
    // No intermediate state where Ur is updated but Cr is not

    state.Ur += Ur_increment;  // (1) Update reward rights
    state.Cr += Ur_increment;  // (2) Update reward pool
                               // INVARIANT_2: Cr == Ur (always)

    state.last_yield_update_height = nHeight;

    // Post-mutation verification (MUST use Invalid, not assert)
    if (!state.CheckInvariants()) {
        return error("ApplyDailyYield(): invariant violation after yield update");
    }
}
```

**Atomicity guarantees:**

1. **Lock protection:** Function MUST be called under `cs_khu` lock
2. **Paired updates:** `Ur` and `Cr` updated in same function, same execution
3. **No separation:** Never update `Ur` without updating `Cr` in same operation
4. **Invariant preserved:** After each yield update, `Cr == Ur` (strict equality)

**Critical:** Yield compounding forbidden - yield only calculated on `note.amount`, never on `note.Ur_accumulated`.

---

## 3.2 ATOMICITY ENFORCEMENT RULES

**All state mutations (C, U, Cr, Ur) MUST follow these rules:**

### 3.2.1 MINT Operations

```cpp
bool ApplyKhuMint(const CTransaction& tx, KhuGlobalState& state, ...) {
    LOCK(cs_khu);  // Required

    int64_t amount = GetMintAmount(tx);

    // Atomic paired update
    state.C += amount;  // MUST be immediately followed by:
    state.U += amount;  // No code between these two lines

    assert(state.CheckInvariants());
    return true;
}
```

**Rule:** C and U MUST be updated in same function, under same lock, in same ConnectBlock.

### 3.2.2 REDEEM Operations

```cpp
bool ApplyKhuRedeem(const CTransaction& tx, KhuGlobalState& state, ...) {
    LOCK(cs_khu);  // Required

    int64_t amount = GetRedeemAmount(tx);

    // Atomic paired update
    state.C -= amount;  // MUST be immediately followed by:
    state.U -= amount;  // No code between these two lines

    assert(state.CheckInvariants());
    return true;
}
```

**Rule:** C and U MUST be updated in same function, under same lock, in same ConnectBlock.

### 3.2.3 Yield Update Operations

See section 3.1.2 for complete ApplyDailyYield implementation.

**Rule:** Cr and Ur MUST be updated together, under cs_khu lock, every 1440 blocks.

**Pattern:**
```cpp
state.Ur += daily_increment;  // MUST be immediately followed by:
state.Cr += daily_increment;  // No code between these two lines
```

### 3.2.4 UNSTAKE Operations (Double Flux)

See section 3.1 for complete ApplyKhuUnstake implementation.

**Rule:** All 4 mutations (U, C, Cr, Ur) MUST execute atomically under cs_khu lock.

**Pattern:**
```cpp
state.U  += B;  // Line 1
state.C  += B;  // Line 2 (immediately after line 1)
state.Cr -= B;  // Line 3 (immediately after line 2)
state.Ur -= B;  // Line 4 (immediately after line 3)
```

**No code allowed between these 4 lines. No exceptions.**

### 3.2.5 Forbidden Patterns

❌ **NEVER do this:**

```cpp
// BAD: Separate C and U updates
state.C += amount;
DoSomethingElse();  // ❌ Code between mutations
state.U += amount;

// BAD: C and U in different functions
void UpdateC(int64_t amount) { state.C += amount; }  // ❌
void UpdateU(int64_t amount) { state.U += amount; }  // ❌

// BAD: Updates in different blocks
if (height % 2 == 0)
    state.C += amount;  // ❌ C in one block
else
    state.U += amount;  // ❌ U in another block

// BAD: Conditional updates
state.C += amount;
if (some_condition)  // ❌ Conditional between mutations
    state.U += amount;

// BAD: Try-catch separation
state.C += amount;
try {
    DoSomething();  // ❌ May throw
    state.U += amount;
} catch (...) {}
```

### 3.2.6 Enforcement Checklist

For every function that modifies C, U, Cr, or Ur:

1. ✅ Function holds `cs_khu` lock (use `AssertLockHeld(cs_khu)`)
2. ✅ Paired variables updated consecutively (no code between)
3. ✅ All mutations in same function (no cross-function splits)
4. ✅ All mutations in same ConnectBlock execution
5. ✅ `CheckInvariants()` called after mutations
6. ✅ No early returns between paired updates
7. ✅ No exceptions thrown between paired updates
8. ✅ No conditional logic between paired updates

**Violation of ANY of these rules = consensus failure.**

---

## 3.3 LOCK VERIFICATION CHECKLIST

**⚠️ ANTI-DÉRIVE: Lock Requirement Enforcement**

The following functions **MUST** be called under `cs_khu` lock to prevent race conditions and ensure atomic state mutations.

### 3.3.1 Functions Requiring cs_khu Lock

**CRITICAL REQUIREMENT:** Before calling ANY of these functions, verify that `cs_khu` lock is held.

```cpp
// State mutation functions (MUST hold cs_khu)
bool ApplyDailyYield(KhuGlobalState& state, ...);
bool ApplyKhuMint(const CTransaction& tx, KhuGlobalState& state, ...);
bool ApplyKhuRedeem(const CTransaction& tx, KhuGlobalState& state, ...);
bool ApplyKhuStake(const CTransaction& tx, KhuGlobalState& state, ...);
bool ApplyKhuUnstake(const CTransaction& tx, KhuGlobalState& state, ...);
bool ProcessKHUTransaction(const CTransaction& tx, KhuGlobalState& state, ...);

// State read functions (MUST hold cs_khu for consistency)
KhuGlobalState LoadKhuState(const CBlockIndex* pindex);
bool KhuGlobalState::CheckInvariants() const;
```

### 3.3.2 Lock Order Rule

**MANDATORY ORDER:** `LOCK2(cs_main, cs_khu)`

**Never reverse this order.** Always acquire `cs_main` first, then `cs_khu`.

```cpp
// ✅ CORRECT
LOCK2(cs_main, cs_khu);
ApplyDailyYield(newState, nHeight, view);

// ❌ FORBIDDEN - Wrong lock order (deadlock risk)
LOCK2(cs_khu, cs_main);

// ❌ FORBIDDEN - cs_khu only without cs_main
LOCK(cs_khu);
ApplyDailyYield(newState, nHeight, view);
```

### 3.3.3 ConnectBlock Lock Pattern

**File:** `src/validation.cpp`

**REQUIRED PATTERN for all KHU processing in ConnectBlock:**

```cpp
bool ConnectBlock(const CBlock& block, CBlockIndex* pindex, CCoinsViewCache& view) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        // ============================================================
        // ⚠️ STEP 1: ACQUIRE LOCKS FIRST (before ANY KHU operations)
        // ============================================================
        LOCK2(cs_main, cs_khu);

        // STEP 2: Load state
        KhuGlobalState prevState = LoadKhuState(pindex->pprev);
        KhuGlobalState newState = prevState;

        // STEP 3: Apply yield (if needed)
        ApplyDailyYieldIfNeeded(newState, pindex->nHeight);

        // STEP 4: Process transactions
        for (const auto& tx : block.vtx) {
            ProcessKHUTransaction(tx, newState, view, state);
        }

        // STEP 5: Check invariants
        newState.CheckInvariants();

        // STEP 6: Persist state
        pKHUStateDB->WriteKHUState(newState);

        // Lock released automatically at end of scope
    }

    return true;
}
```

### 3.3.4 Implementation Verification Checklist

**BEFORE COMMITTING ANY KHU CODE, VERIFY:**

1. ✅ `ConnectBlock()` acquires `LOCK2(cs_main, cs_khu)` at start of KHU processing
2. ✅ All calls to `ApplyDailyYield()` are under `cs_khu` lock
3. ✅ All calls to `ApplyKhuUnstake()` are under `cs_khu` lock
4. ✅ All calls to `ApplyKhuMint()` are under `cs_khu` lock
5. ✅ All calls to `ApplyKhuRedeem()` are under `cs_khu` lock
6. ✅ `ApplyDailyYield()` contains `AssertLockHeld(cs_khu)` at top
7. ✅ `ApplyKhuUnstake()` contains `AssertLockHeld(cs_khu)` at top
8. ✅ Lock order is always `cs_main` first, `cs_khu` second
9. ✅ No lock is released between paired state mutations (C/U, Cr/Ur)
10. ✅ `DisconnectBlock()` also acquires `LOCK2(cs_main, cs_khu)` for reorgs

### 3.3.5 AssertLockHeld Usage

**All state mutation functions MUST assert lock is held:**

```cpp
void ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, CCoinsViewCache& view) {
    // First line: verify lock is held (will crash in debug if not)
    AssertLockHeld(cs_khu);

    // ... rest of function
}

bool ApplyKhuUnstake(const CTransaction& tx, KhuGlobalState& state, ...) {
    // First line: verify lock is held
    AssertLockHeld(cs_khu);

    // ... rest of function
}
```

**Rationale:** Catches lock violations during development/testing before they cause production bugs.

### 3.3.6 Automated Search for Lock Violations

**Before Phase 1 implementation, run these checks:**

```bash
# Search for ApplyDailyYield calls without preceding LOCK
grep -B5 "ApplyDailyYield" src/**/*.cpp | grep -v "LOCK"

# Search for ApplyKhuUnstake calls without preceding LOCK
grep -B5 "ApplyKhuUnstake" src/**/*.cpp | grep -v "LOCK"

# Search for ProcessKHUTransaction calls without preceding LOCK
grep -B5 "ProcessKHUTransaction" src/**/*.cpp | grep -v "LOCK"

# Verify all state mutation functions have AssertLockHeld
grep -A1 "^void ApplyDailyYield\|^bool ApplyKhu" src/khu/*.cpp | grep "AssertLockHeld(cs_khu)"
```

**If ANY of these searches return results without locks, STOP and fix before proceeding.**

### 3.3.7 Debug Build Verification

**Compile with lock debugging enabled:**

```bash
./configure --enable-debug CXXFLAGS="-DDEBUG_LOCKORDER"
make -j$(nproc)
```

**This enables runtime lock order verification and deadlock detection.**

---

## 4. HTLC MEMPOOL RULES

**File:** `src/txmempool.cpp`

**Specification:**

HTLC transactions (HTLC_CREATE, HTLC_CLAIM, HTLC_REFUND) respect **identical mempool rules** as Bitcoin/PIVX standard transactions.

**No special handling:**
- ❌ No preimage sniffing
- ❌ No secret leakage detection
- ❌ No prioritization
- ❌ No mempool isolation
- ❌ No HTLC-specific validation beyond consensus

**Standard mempool checks apply:**
- Fee rate validation (standard PIVX fees)
- Transaction size limits
- Script validity
- Double-spend detection
- Rate limiting

**Rationale:**

HTLC security relies on:
1. **Timelock enforcement** (consensus rule, not mempool)
2. **Hashlock correctness** (verified on-chain)
3. **Atomic execution** (blockchain guarantee)

Mempool does NOT need HTLC-aware logic.

**Gateway responsibility:**

Secret protection is handled **off-chain** by Gateway watchers, NOT by PIVX Core mempool.

---

## 5. STATE HASH AND CHAINING

**File:** `src/khu/khu_state.cpp`

**Purpose:**
- Chain KHU states via `hashPrevState`
- Support finality signatures
- Enable audit and verification

**Implementation:**

```cpp
uint256 KhuGlobalState::GetHash() const {
    CHashWriter ss(SER_GETHASH, 0);

    // Serialize all consensus fields
    ss << C;
    ss << U;
    ss << Cr;
    ss << Ur;
    ss << R_annual;
    ss << R_MAX_dynamic;
    ss << last_domc_height;
    ss << domc_cycle_start;
    ss << domc_cycle_length;
    ss << domc_phase_length;
    ss << last_yield_update_height;
    ss << nHeight;
    ss << hashBlock;
    ss << hashPrevState;

    return ss.GetHash();  // SHA256d (double SHA256)
}
```

**Rules:**
- All consensus fields MUST be included
- Deterministic order (struct declaration order)
- SHA256d (Bitcoin/PIVX standard)
- Used in finality signatures and state verification

> ⚠️ **ANTI-DÉRIVE CRITIQUE: SERIALIZATION ORDER IS CONSENSUS (IMMUTABLE)**
>
> **THE ORDER OF FIELDS IN GetHash() MUST NEVER CHANGE.**
>
> Changing the serialization order changes the hash → **INSTANT CONSENSUS FORK**.
>
> **MANDATORY SERIALIZATION ORDER (14 fields in EXACT order):**
>
> ```cpp
> uint256 KhuGlobalState::GetHash() const {
>     CHashWriter ss(SER_GETHASH, 0);
>
>     // ⚠️ CRITICAL: Fields MUST be serialized in THIS EXACT ORDER
>     // Changing order = changing hash = HARD FORK
>     // DO NOT REORDER. DO NOT OPTIMIZE. DO NOT REFACTOR.
>
>     ss << C;                          // Field 1 - MUST be first
>     ss << U;                          // Field 2
>     ss << Cr;                         // Field 3
>     ss << Ur;                         // Field 4
>     ss << R_annual;                   // Field 5
>     ss << R_MAX_dynamic;              // Field 6
>     ss << last_domc_height;           // Field 7
>     ss << domc_cycle_start;           // Field 8
>     ss << domc_cycle_length;          // Field 9
>     ss << domc_phase_length;          // Field 10
>     ss << last_yield_update_height;   // Field 11
>     ss << nHeight;                    // Field 12
>     ss << hashBlock;                  // Field 13
>     ss << hashPrevState;              // Field 14 - MUST be last
>
>     return ss.GetHash();  // SHA256d
> }
> ```
>
> **FORBIDDEN MODIFICATIONS:**
>
> ```cpp
> // ❌ NEVER reorder fields (even for "readability")
> ss << nHeight;  // Field 12
> ss << C;        // Field 1  ← WRONG ORDER = FORK
>
> // ❌ NEVER skip fields (even if "unused")
> ss << C << U << Cr << Ur;
> // Missing R_annual, etc. ← WRONG = FORK
>
> // ❌ NEVER add new fields in middle
> ss << C << U << NEW_FIELD << Cr;  ← WRONG = FORK
>
> // ❌ NEVER change serialization method
> ss.write((char*)&C, sizeof(C));  // Different from << operator ← WRONG = FORK
>
> // ❌ NEVER use different hash algorithm
> return SHA256(ss);  // Not SHA256d ← WRONG = FORK
> ```
>
> **ADDING NEW FIELDS (future upgrades):**
>
> If a hard fork adds new fields to KhuGlobalState:
>
> 1. ✅ New fields MUST be appended at END (before `return`)
> 2. ✅ Existing field order MUST remain unchanged
> 3. ✅ Document the fork height and new hash format
> 4. ✅ Old nodes will reject blocks (expected behavior)
>
> ```cpp
> // Example: Adding new field in version 2 (hypothetical future fork)
> ss << C;                          // Field 1 - unchanged
> ss << U;                          // Field 2 - unchanged
> // ... all 14 original fields in original order ...
> ss << hashPrevState;              // Field 14 - unchanged
>
> // NEW FIELD (only if hard fork at height X)
> if (nHeight >= HARD_FORK_HEIGHT) {
>     ss << new_field;              // Field 15 - APPENDED at end
> }
>
> return ss.GetHash();
> ```
>
> **VERIFICATION BEFORE PHASE 1:**
>
> ```bash
> # Verify GetHash() uses all 14 fields in correct order
> grep -A20 "GetHash.*const" src/khu/khu_state.cpp | \
>   grep "ss <<" | \
>   awk '{print $3}' > /tmp/hash_order.txt
>
> # Expected order (MUST match exactly):
> cat > /tmp/expected_order.txt <<EOF
> C;
> U;
> Cr;
> Ur;
> R_annual;
> R_MAX_dynamic;
> last_domc_height;
> domc_cycle_start;
> domc_cycle_length;
> domc_phase_length;
> last_yield_update_height;
> nHeight;
> hashBlock;
> hashPrevState;
> EOF
>
> diff /tmp/hash_order.txt /tmp/expected_order.txt
> # If diff shows ANY difference → SERIALIZATION ORDER VIOLATED → FIX IMMEDIATELY
> ```
>
> **CONSEQUENCE OF VIOLATION:**
>
> If serialization order changes after mainnet launch:
> - Every node calculates different state hashes
> - Finality signatures fail to verify
> - Network splits into incompatible chains
> - **COMPLETE CONSENSUS FAILURE**
>
> This is NOT recoverable without coordinated hard fork.

---

## 6. NETWORK ACTIVATION

**File:** `src/chainparams.cpp`

**Per-network activation heights:**

```cpp
// Mainnet
class CMainParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 9999999;  // TBD
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nProtocolVersion = 70023;
};

// Testnet
class CTestNetParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 500000;  // TBD
};

// Regtest (immediate activation for testing)
class CRegTestParams : public CChainParams {
    consensus.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight = 1;
};
```

**Activation check:**

```cpp
// src/validation.cpp
bool ConnectBlock(...) {
    if (NetworkUpgradeActive(pindex->nHeight, Consensus::UPGRADE_KHU)) {
        // KHU consensus rules active
    }
}
```

---

## 7. STATE RECONSTRUCTION

**File:** `src/khu/khu_db.cpp`

**Purpose:** Rebuild KHU state after DB corruption or resync.

**Implementation:**

```cpp
bool CKHUStateDB::ReconstructState(uint32_t fromHeight, uint32_t toHeight) {
    // 1. Genesis state
    KhuGlobalState state;
    state.C = 0;
    state.U = 0;
    state.Cr = 0;
    state.Ur = 0;
    state.R_annual = 500;  // 5% initial
    state.R_MAX_dynamic = 3000;
    state.last_yield_update_height = fromHeight;
    state.domc_cycle_start = fromHeight + 525600;  // 1 year
    state.domc_cycle_length = 43200;
    state.domc_phase_length = 4320;
    state.nHeight = fromHeight;
    state.hashPrevState.SetNull();

    // 2. Replay blocks
    for (uint32_t h = fromHeight + 1; h <= toHeight; h++) {
        CBlockIndex* pindex = chainActive[h];
        CBlock block;
        ReadBlockFromDisk(block, pindex, consensusParams);

        // Apply KHU transactions
        for (const auto& tx : block.vtx) {
            ApplyKHUTransaction(*tx, state, view, validationState);
        }

        // Apply daily yield
        if ((h - state.last_yield_update_height) >= 1440) {
            UpdateDailyYield(state, h, view);
            state.last_yield_update_height = h;
        }

        // Update state metadata
        uint256 prevHash = state.GetHash();
        state.nHeight = h;
        state.hashBlock = pindex->GetBlockHash();
        state.hashPrevState = prevHash;

        // Checkpoint every 1000 blocks
        if (h % 1000 == 0) {
            WriteKHUState(state);
        }
    }

    // Final write
    return WriteKHUState(state);
}
```

---

## 8. ATOMIC DATABASE WRITES

**File:** `src/khu/khu_db.cpp`

**Purpose:** Guarantee all-or-nothing state persistence.

**Implementation:**

```cpp
bool CKHUStateDB::WriteKHUState(const KhuGlobalState& state) {
    CDBBatch batch(*this);

    // Write state at height
    batch.Write(std::make_pair('K', std::make_pair('S', state.nHeight)), state);

    // Write state by hash
    batch.Write(std::make_pair('K', std::make_pair('H', state.GetHash())), state);

    // Write best state hash
    batch.Write(std::make_pair('K', 'B'), state.GetHash());

    // Atomic commit with fsync
    return WriteBatch(batch, true);  // fSync=true
}
```

**Rules:**
- Use `CDBBatch` for multiple writes
- `fSync=true` forces disk synchronization
- All writes succeed or all fail (atomicity)
- No partial state corruption possible

---

## 9. GENESIS STATE INITIALIZATION

**File:** `src/validation.cpp`

**Purpose:** Initialize KHU state at activation height.

**Implementation:**

```cpp
bool ConnectBlock(...) {
    uint32_t activationHeight = consensusParams.vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight;

    if (pindex->nHeight == activationHeight) {
        // Initialize genesis KHU state
        KhuGlobalState genesisState;
        genesisState.C = 0;
        genesisState.U = 0;
        genesisState.Cr = 0;
        genesisState.Ur = 0;

        // DOMC parameters
        genesisState.R_annual = 500;  // 5% initial
        genesisState.R_MAX_dynamic = 3000;  // 30% year 0
        genesisState.last_domc_height = activationHeight;
        genesisState.domc_cycle_start = activationHeight + 525600;  // 1 year delay
        genesisState.domc_cycle_length = 43200;  // 30 days
        genesisState.domc_phase_length = 4320;   // 3 days

        // Yield scheduler
        genesisState.last_yield_update_height = activationHeight;  // Critical

        // Chain tracking
        genesisState.nHeight = activationHeight;
        genesisState.hashBlock = pindex->GetBlockHash();
        genesisState.hashPrevState.SetNull();

        // Persist
        if (!pKHUStateDB->WriteKHUState(genesisState))
            return state.Invalid("khu-genesis-write-failed");
    }

    return true;
}
```

**Critical:** `last_yield_update_height = activationHeight` ensures first yield at height `activationHeight + 1440`.

---

## 10. LEVELDB KEY AUDIT

**File:** `src/txdb.h`

**Existing PIVX prefixes:**

```
'c' → Coins (UTXO)
'B' → Best block hash
'f' → Flags (tx index, etc.)
'l' → Last block file
'R' → Reindex flag
't' → Transaction index
'b' → Block index
'S' → Sapling anchors
'N' → Sapling nullifiers
's' → Shield stake data
'z' → Zerocoin (legacy)
```

**New KHU prefixes (no collision):**

```
'K' + 'S' + height      → KhuGlobalState at height
'K' + 'B'               → Best KHU state hash
'K' + 'H' + hash        → KhuGlobalState by hash
'K' + 'C' + height      → KHU finality commitment
'K' + 'U' + outpoint    → CKHUUTXO
'K' + 'N' + nullifier   → ZKHU note data
'K' + 'D' + cycle       → DOMC vote data
'K' + 'T' + outpoint    → HTLC data
```

**Verified:** No collision with existing PIVX keys.

---

## 11. INVARIANT VIOLATION HANDLING

**File:** `src/validation.cpp`

**Policy:** Immediate block rejection, no tolerance, no correction.

**Implementation:**

```cpp
bool ConnectBlock(...) {
    // ... process KHU transactions ...

    // Check invariants
    if (!newState.CheckInvariants()) {
        return state.Invalid(false, REJECT_INVALID, "khu-invariant-violation",
                           strprintf("Invariant violation: C=%d U=%d Cr=%d Ur=%d at height %d",
                                   newState.C, newState.U, newState.Cr, newState.Ur,
                                   pindex->nHeight));
    }

    // ... persist state ...
}
```

**CheckInvariants implementation:**

```cpp
// src/khu/khu_state.h
bool KhuGlobalState::CheckInvariants() const {
    // INVARIANT_1: C == U (allow U=0 case)
    bool cd_ok = (U == 0 || C == U);

    // INVARIANT_2: Cr == Ur (allow Ur=0 case)
    bool cdr_ok = (Ur == 0 || Cr == Ur);

    return cd_ok && cdr_ok;
}
```

**Behavior on violation:**
- Block rejected
- Peer banned (score -100)
- Detailed logging (C, U, Cr, Ur, height)
- No automatic correction attempted

---

## 12. LOCK ORDERING

**File:** `src/khu/khu_state.cpp`

**Rule:** Always `LOCK2(cs_main, cs_khu)` (never reversed).

**Declaration:**

```cpp
// Global lock for KHU state
CCriticalSection cs_khu;
```

**Usage:**

```cpp
// src/khu/khu_state.cpp
bool UpdateKHUState(...) {
    LOCK2(cs_main, cs_khu);  // Correct order

    // Modify khuGlobalState

    return true;
}

// src/rpc/khu.cpp
UniValue getkhuglobalstate(...) {
    LOCK2(cs_main, cs_khu);  // Same order in RPC

    KhuGlobalState state = khuGlobalState;

    return JSONFromState(state);
}
```

**Rationale:**
- `cs_main` = global blockchain lock (highest priority)
- `cs_khu` = KHU-specific lock (lower priority)
- Consistent order prevents deadlocks

**Detection:** Compile with `-DDEBUG_LOCKORDER` to detect violations.

---

## 13. EMISSION OVERFLOW PROTECTION

**File:** `src/init.cpp`

**Purpose:** Verify emission constants cannot overflow int64_t.

**Implementation:**

```cpp
bool AppInitMain() {
    // ... initialization ...

    // Compile-time check
    static_assert(6 * COIN < std::numeric_limits<int64_t>::max() / 3,
                  "Emission overflow: max reward exceeds int64_t");

    // Runtime verification
    int64_t total_emission = 0;
    for (int year = 0; year < 6; year++) {
        int64_t reward_year = (6 - year) * COIN;
        int64_t year_emission = reward_year * 3 * 525600;

        assert(total_emission + year_emission > total_emission);  // No overflow
        total_emission += year_emission;
    }

    LogPrintf("KHU: Total emission cap = %d PIV\n", total_emission / COIN);
    // Expected: 33,112,800 PIV

    return true;
}
```

**Verification:**
- Max reward per block = 18 PIV = 1,800,000,000 satoshis
- `int64_t::max` = 9,223,372,036,854,775,807
- 1,800,000,000 << int64_t::max ✓ (safe)

---

## 14. DAO REWARD DISTRIBUTION

**File:** `src/validation.cpp`

**Purpose:** Route DAO share to PIVX budget system treasury.

**Implementation:**

```cpp
void CreateCoinStake(CBlock& block, const CChainParams& params) {
    CAmount blockReward = GetBlockSubsidy(block.nHeight, params.GetConsensus());

    // Three equal outputs

    // 1. Staker
    CTxOut stakerOut(blockReward, GetScriptForStaker());
    block.vtx[1]->vout.push_back(stakerOut);

    // 2. Masternode
    CTxOut mnOut(blockReward, GetScriptForMasternode());
    block.vtx[1]->vout.push_back(mnOut);

    // 3. DAO → Treasury address
    CScript treasuryScript = GetScriptForDestination(params.GetTreasuryAddress());
    CTxOut daoOut(blockReward, treasuryScript);
    block.vtx[1]->vout.push_back(daoOut);
}
```

**Treasury address definition:**

```cpp
// src/chainparams.cpp
class CMainParams : public CChainParams {
    // Treasury multisig address (controlled by governance)
    strTreasuryAddress = "DPhJsztbZafDc1YeyrRqSjmKjkmLJpQpUn";  // Example
};
```

**Rules:**
- Three equal shares (staker == masternode == DAO)
- DAO funds go to existing treasury system
- No new payout mechanism needed

---

## 15. YIELD PRECISION (INT64)

**File:** `src/khu/khu_yield.cpp`

**Issue:** Integer division may lose fractional satoshis.

**Analysis:**

```cpp
// Example: 1000 KHU staked at R=500 (5%)
int64_t amount = 1000 * COIN;          // 100,000,000,000 satoshis
uint16_t R_annual = 500;               // 5%

// Annual yield
int64_t annual = (amount * R_annual) / 10000;
// = (100,000,000,000 * 500) / 10,000
// = 500,000,000 satoshis (5 KHU)

// Daily yield
int64_t daily = annual / 365;
// = 500,000,000 / 365
// = 1,369,863 satoshis/day

// Loss per division
double exact = 500000000.0 / 365.0;   // 1,369,863.0136986...
int64_t integer = 500000000 / 365;    // 1,369,863
double loss = exact - integer;        // ~0.014 satoshis/day
```

**Conclusion:**
- Loss: ~0.014 satoshis/day per 1000 KHU
- Over 1 year: ~5 satoshis lost (0.00000005 KHU)
- **Negligible and acceptable**
- Integer division = deterministic (required for consensus)
- Rounding down = conservative (no accidental inflation)

**Implementation:**

```cpp
int64_t CalculateDailyYield(int64_t amount, uint16_t R_annual) {
    // All integer arithmetic (deterministic)
    int64_t annual = (amount * R_annual) / 10000;
    int64_t daily = annual / 365;

    // Rounds down (conservative)
    return daily;
}
```

**Prohibition:** Floating point arithmetic forbidden (non-deterministic).

---

## 16. HTLC PREIMAGE VISIBILITY

**File:** `src/khu/khu_htlc.cpp`

**Issue:** Is HTLC preimage visible in mempool?

**Answer:** Yes, and this is expected behavior.

**Implementation:**

```cpp
bool CheckHTLCClaim(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // 1. Load HTLC
    HTLCState htlc;
    if (!view.GetHTLC(tx.vin[0].prevout, htlc))
        return state.Invalid("htlc-not-found");

    // 2. Extract preimage (visible on-chain and in mempool)
    std::vector<unsigned char> preimage;
    if (!tx.GetHTLCPreimage(preimage))
        return state.Invalid("htlc-missing-preimage");

    // 3. Verify hashlock
    uint256 hash = Hash(preimage.begin(), preimage.end());
    if (hash != htlc.hashlock)
        return state.Invalid("htlc-invalid-preimage");

    // 4. Verify timelock not expired
    if (chainActive.Height() > htlc.timelock)
        return state.Invalid("htlc-timelock-expired");

    // 5. Transfer KHU_T to recipient
    return true;
}
```

**Cross-chain atomic swap flow:**

```
1. Alice: HTLC_CREATE on PIVX (hashlock=H, timelock=T1)
2. Bob:   HTLC_CREATE on Monero (hashlock=H, timelock=T2, T2 < T1)
3. Alice: HTLC_CLAIM on Monero (reveals preimage P)
4. Bob:   Sees P in Monero blockchain
5. Bob:   HTLC_CLAIM on PIVX (uses P)
```

**Mempool visibility:**
- Preimage visible in mempool when HTLC_CLAIM broadcast
- Gateway watchers monitor both chains
- No special mempool isolation needed
- Standard RBF (Replace-By-Fee) rules apply

**Security:**
- HTLC security relies on timelock enforcement (consensus)
- Not on mempool privacy (impossible to achieve)
- Gateway watchers handle race conditions

**Mempool rules:** No special handling (see section 4).

---

## 17. REDEEM TRANSACTION EXEMPTION

**File:** `src/consensus/tx_verify.cpp`

**Issue:** REDEEM creates PIV without PIV inputs. How?

**Solution:** Transaction type exemption from standard validation.

**Implementation:**

```cpp
bool Consensus::CheckTxInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view) {
    // Exempt special transaction types
    if (tx.nType == CTransaction::TX_KHU_REDEEM) {
        // Validation done in CheckKHURedeem() instead
        return true;
    }

    // Standard input validation for normal transactions
    CAmount nValueIn = 0;
    for (const auto& txin : tx.vin) {
        const Coin& coin = view.AccessCoin(txin.prevout);
        if (coin.IsSpent())
            return state.Invalid("bad-txns-inputs-spent");

        nValueIn += coin.out.nValue;
    }

    // Standard checks
    if (nValueIn < tx.GetValueOut())
        return state.Invalid("bad-txns-in-belowout");

    return true;
}

// KHU-specific validation
bool CheckKHURedeem(const CTransaction& tx, CValidationState& state, const KhuGlobalState& khuState) {
    CKHURedeemPayload payload;
    GetTxPayload(tx, payload);

    // Verify sufficient collateral
    if (khuState.C < payload.khuAmount)
        return state.Invalid("khu-insufficient-collateral");

    // Verify KHU_T inputs are spent
    for (const auto& txin : tx.vin) {
        CKHUUTXO khuCoin;
        if (!view.GetKHUCoin(txin.prevout, khuCoin))
            return state.Invalid("khu-redeem-missing-input");
    }

    // PIV output creation is authorized
    return true;
}
```

**Pattern:** Same as ProRegTx, shield stake, etc. in PIVX.

---

## 18. SAPLING POOL SEPARATION

**File:** `src/sapling/sapling_state.h`

**Issue:** How to separate ZKHU from zPIV Sapling notes?

**Solution:** Separate Merkle trees (anchors).

**Implementation:**

```cpp
class SaplingState {
public:
    // zPIV Sapling pool
    SaplingMerkleTree saplingTree;
    std::set<uint256> nullifierSet;

    // ZKHU pool (separate)
    SaplingMerkleTree zkhuTree;
    std::set<uint256> zkhuNullifierSet;

    SERIALIZE_METHODS(SaplingState, obj) {
        READWRITE(obj.saplingTree, obj.nullifierSet);
        READWRITE(obj.zkhuTree, obj.zkhuNullifierSet);
    }
};

// STAKE: Add to ZKHU tree
bool ProcessKHUStake(const CTransaction& tx, SaplingState& saplingState) {
    const OutputDescription& output = tx.sapData->vShieldedOutput[0];

    // Add commitment to ZKHU tree (not zPIV tree)
    saplingState.zkhuTree.append(output.cmu);

    return true;
}

// UNSTAKE: Verify against ZKHU tree
bool ProcessKHUUnstake(const CTransaction& tx, SaplingState& saplingState) {
    const SpendDescription& spend = tx.sapData->vShieldedSpend[0];

    // Check nullifier in ZKHU set (not zPIV set)
    if (saplingState.zkhuNullifierSet.count(spend.nullifier))
        return false;  // Double-spend

    // Verify anchor matches ZKHU tree root (not zPIV tree)
    if (spend.anchor != saplingState.zkhuTree.root())
        return false;  // Invalid anchor

    saplingState.zkhuNullifierSet.insert(spend.nullifier);

    return true;
}
```

**Consequences:**
- ZKHU and zPIV completely isolated
- No conversion between pools possible
- Independent anonymity sets
- Separate audit trails

> ⚠️ **ANTI-DÉRIVE: ENFORCED POOL SEPARATION**
>
> **COMPILE-TIME VERIFICATION (MANDATORY):**
>
> ```cpp
> // File: src/sapling/sapling_state.h
>
> // Static assertion to prevent accidental pool merging
> static_assert(offsetof(SaplingState, zkhuTree) != offsetof(SaplingState, saplingTree),
>               "ZKHU and zPIV trees MUST be separate members");
>
> static_assert(sizeof(SaplingState::zkhuTree) == sizeof(SaplingState::saplingTree),
>               "Tree types must match (but be separate instances)");
>
> // Runtime check in debug builds
> #ifdef DEBUG
> inline void VerifyPoolSeparation() {
>     assert(&saplingTree != &zkhuTree);
>     assert(&nullifierSet != &zkhuNullifierSet);
> }
> #endif
> ```
>
> **FORBIDDEN PATTERNS:**
>
> ```cpp
> // ❌ NEVER do this - using same tree for both pools
> saplingTree.append(zkhu_commitment);  // WRONG - ZKHU must use zkhuTree
>
> // ❌ NEVER do this - checking ZKHU nullifier in zPIV set
> if (nullifierSet.count(zkhu_nullifier))  // WRONG - use zkhuNullifierSet
>
> // ❌ NEVER do this - merging pools
> zkhuTree = saplingTree;  // FORBIDDEN - pools are separate by design
>
> // ❌ NEVER do this - cross-pool anchor validation
> if (spend.anchor == saplingTree.root())  // WRONG if spending ZKHU
> ```
>
> **CORRECT PATTERNS:**
>
> ```cpp
> // ✅ STAKE: Add to ZKHU tree (not zPIV tree)
> zkhuTree.append(khu_commitment);
> LogPrint(BCLog::KHU, "Added commitment to ZKHU tree (NOT zPIV)\n");
>
> // ✅ UNSTAKE: Check ZKHU nullifier set (not zPIV set)
> if (zkhuNullifierSet.count(spend.nullifier))
>     return error("ZKHU double-spend");
>
> // ✅ UNSTAKE: Verify anchor against ZKHU tree (not zPIV tree)
> if (spend.anchor != zkhuTree.root())
>     return error("Invalid ZKHU anchor");
> ```
>
> **IMPLEMENTATION CHECKLIST:**
>
> Before implementing Sapling integration:
>
> 1. ✅ Verify `SaplingState` has TWO separate `SaplingMerkleTree` members
> 2. ✅ Verify `SaplingState` has TWO separate nullifier sets
> 3. ✅ Add static_assert checks to prevent accidental merging
> 4. ✅ All STAKE operations use `zkhuTree` (never `saplingTree`)
> 5. ✅ All UNSTAKE operations validate against `zkhuTree.root()`
> 6. ✅ ZKHU nullifiers go to `zkhuNullifierSet` (never `nullifierSet`)
> 7. ✅ Separate LevelDB keys for ZKHU anchors (`'K' + 'A'` vs `'S'`)
> 8. ✅ Wallet tracks ZKHU and zPIV notes in separate collections
> 9. ✅ RPC commands clearly distinguish ZKHU vs zPIV operations
> 10. ✅ No code path allows conversion between ZKHU and zPIV
>
> **AUDIT COMMAND:**
>
> ```bash
> # Search for incorrect pool usage
> grep -r "saplingTree.append.*KHU" src/  # Should return NOTHING
> grep -r "nullifierSet.*zkhu" src/       # Should return NOTHING
> grep -r "zkhuTree.append.*zPIV" src/    # Should return NOTHING
> ```
>
> **If ANY of these searches return results → POOL CONTAMINATION → FIX IMMEDIATELY.**

**Storage:**

```
LevelDB keys:
'S' + height → zPIV Sapling anchor
'K' + 'A' + height → ZKHU anchor (new)
```

---

## 19. RPC IMPLEMENTATION REFERENCE

**File:** `src/rpc/khu.cpp`

**Global state query:**

```cpp
UniValue getkhuglobalstate(const JSONRPCRequest& request) {
    LOCK2(cs_main, cs_khu);

    KhuGlobalState state;
    pKHUStateDB->ReadBestKHUState(state);

    UniValue result(UniValue::VOBJ);
    result.pushKV("height", (int)state.nHeight);
    result.pushKV("blockhash", state.hashBlock.GetHex());
    result.pushKV("C", ValueFromAmount(state.C));
    result.pushKV("U", ValueFromAmount(state.U));
    result.pushKV("Cr", ValueFromAmount(state.Cr));
    result.pushKV("Ur", ValueFromAmount(state.Ur));
    result.pushKV("R_annual", state.R_annual);
    result.pushKV("R_MAX_dynamic", state.R_MAX_dynamic);
    result.pushKV("CD", (state.U == 0) ? 0.0 : (double)state.C / state.U);
    result.pushKV("CDr", (state.Ur == 0) ? 0.0 : (double)state.Cr / state.Ur);
    result.pushKV("invariants_ok", state.CheckInvariants());

    return result;
}
```

**Full RPC interface:**

```cpp
// State queries
UniValue getkhuglobalstate(const JSONRPCRequest& request);
UniValue getkhustateathash(const JSONRPCRequest& request);
UniValue getkhustatehistory(const JSONRPCRequest& request);

// Wallet operations
UniValue mintkhu(const JSONRPCRequest& request);         // PIV → KHU_T
UniValue redeemkhu(const JSONRPCRequest& request);       // KHU_T → PIV
UniValue stakekhu(const JSONRPCRequest& request);        // KHU_T → ZKHU
UniValue unstakekhu(const JSONRPCRequest& request);      // ZKHU → KHU_T

// DOMC
UniValue votedomc(const JSONRPCRequest& request);        // Submit vote
UniValue getdomcstatus(const JSONRPCRequest& request);   // Cycle info
UniValue getdomcvotes(const JSONRPCRequest& request);    // List votes

// HTLC
UniValue createhtlc(const JSONRPCRequest& request);
UniValue claimhtlc(const JSONRPCRequest& request);
UniValue refundhtlc(const JSONRPCRequest& request);
UniValue listhtlcs(const JSONRPCRequest& request);
```

---

## 20. ANTI-DÉRIVE: CONSOLIDATED SAFEGUARDS

**⚠️ CRITICAL SECTION FOR PHASE 1 IMPLEMENTATION**

This section consolidates all ANTI-DÉRIVE safeguards documented throughout this specification. Before implementing ANY KHU functionality, verify compliance with ALL items in this checklist.

### 20.1 Lock Safeguards (Section 3.3)

**Risk:** Race conditions, invariant violations, undefined behavior

**Enforcement:**

1. ✅ `ConnectBlock()` MUST acquire `LOCK2(cs_main, cs_khu)` before ANY KHU state access
2. ✅ Lock order is MANDATORY: `cs_main` first, `cs_khu` second (never reversed)
3. ✅ `ApplyDailyYield()` MUST have `AssertLockHeld(cs_khu)` as first line
4. ✅ `ApplyKhuUnstake()` MUST have `AssertLockHeld(cs_khu)` as first line
5. ✅ All state mutation functions MUST be called under `cs_khu` lock
6. ✅ Compile with `-DDEBUG_LOCKORDER` to detect violations during testing

**Automated verification:**
```bash
grep -B5 "ApplyDailyYield\|ApplyKhuUnstake" src/**/*.cpp | grep "LOCK2.*cs_khu"
grep "AssertLockHeld(cs_khu)" src/khu/*.cpp
```

**Consequence of violation:** Runtime crash, data corruption, consensus failure

---

### 20.2 Execution Order Safeguards (Section 3)

**Risk:** UNSTAKE with incorrect Ur, invariant violation, consensus failure

**Enforcement:**

1. ✅ `ApplyDailyYieldIfNeeded()` MUST execute BEFORE `ProcessKHUTransactions()` in `ConnectBlock()`
2. ✅ NEVER reverse this order for performance, refactoring, or any other reason
3. ✅ Canonical order: Load → Yield → Transactions → Reward → Invariants → Save
4. ✅ If block N triggers daily yield AND contains UNSTAKE, yield MUST be applied first

**Automated verification:**
```bash
grep -A10 "ApplyDailyYield" src/validation.cpp | grep -B5 "for.*vtx"
# ApplyDailyYield MUST appear BEFORE transaction loop
```

**Consequence of violation:** UNSTAKE calculates wrong bonus → `state.Cr != state.Ur` → block rejected

---

### 20.3 Transaction Error Handling Safeguards (Section 3.3)

**Risk:** Invalid transactions silently ignored, state corruption, consensus failure

**Enforcement:**

1. ✅ EVERY call to `ProcessKHUTransaction()` MUST check return value
2. ✅ Pattern: `if (!ProcessKHUTransaction(...)) return false;`
3. ✅ NEVER ignore return values (no silent failures)
4. ✅ NEVER use inverted logic (`if (success) continue` instead of `if (!success) return false`)
5. ✅ Block MUST be rejected immediately on first transaction failure

**Forbidden patterns:**
```cpp
ProcessKHUTransaction(tx, ...);  // ❌ Ignoring return value
try { ProcessKHUTransaction(...); } catch(...) {}  // ❌ Silent catch
```

**Consequence of violation:** Invalid transactions accepted → corrupted state → consensus divergence

---

### 20.4 Struct Synchronization Safeguards (Section 2.1 in docs 02/03)

**Risk:** Documentation desynchronization, implementation bugs, consensus failure

**Enforcement:**

1. ✅ `KhuGlobalState` in doc 02 MUST be identical to doc 03
2. ✅ Same 14 fields, same order, same types
3. ✅ Run diff verification before Phase 1 implementation
4. ✅ If modifying struct: update doc 02 first, then doc 03, then verify with diff

**Automated verification:**
```bash
diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
     <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md)
```

**Consequence of violation:** Implementation uses wrong struct → state hash mismatch → consensus failure

---

### 20.5 Sapling Pool Separation Safeguards (Section 18)

**Risk:** ZKHU/zPIV pool contamination, double-spend, audit failure, privacy leak

**Enforcement:**

1. ✅ `SaplingState` MUST have TWO separate `SaplingMerkleTree` members
2. ✅ `zkhuTree` and `saplingTree` MUST be distinct objects
3. ✅ ZKHU nullifiers go to `zkhuNullifierSet` (never `nullifierSet`)
4. ✅ STAKE operations use `zkhuTree.append()` (never `saplingTree.append()`)
5. ✅ UNSTAKE validation checks `spend.anchor == zkhuTree.root()` (not `saplingTree.root()`)
6. ✅ Add `static_assert()` to prevent accidental merging at compile time
7. ✅ Separate LevelDB keys: ZKHU uses `'K' + 'A'`, zPIV uses `'S'`

**Automated verification:**
```bash
grep -r "saplingTree.append.*KHU" src/     # MUST return nothing
grep -r "nullifierSet.*zkhu" src/          # MUST return nothing
grep -r "zkhuTree.append.*zPIV" src/       # MUST return nothing
```

**Consequence of violation:** Pool contamination → anonymity loss → audit failure → security breach

---

### 20.6 Serialization Order Safeguards (Section 5)

**Risk:** Hash mismatch, finality signature failure, network fork

**Enforcement:**

1. ✅ `GetHash()` MUST serialize all 14 fields in EXACT ORDER
2. ✅ Order: C, U, Cr, Ur, R_annual, R_MAX_dynamic, last_domc_height, domc_cycle_start, domc_cycle_length, domc_phase_length, last_yield_update_height, nHeight, hashBlock, hashPrevState
3. ✅ NEVER reorder fields (even for "readability")
4. ✅ NEVER skip fields
5. ✅ NEVER insert new fields in middle (append at end only, during hard fork)
6. ✅ Use SHA256d (double SHA256), never change hash algorithm

**Automated verification:**
```bash
grep -A20 "GetHash.*const" src/khu/khu_state.cpp | grep "ss <<" | awk '{print $3}'
# MUST match exact order: C; U; Cr; Ur; ... hashPrevState;
```

**Consequence of violation:** State hash changes → finality signatures invalid → network splits → TOTAL CONSENSUS FAILURE

---

### 20.7 Atomicity Safeguards (Section 3.2)

**Risk:** Partial state mutations, invariant violations, consensus failure

**Enforcement:**

1. ✅ MINT: `C += amount` and `U += amount` in same function, consecutive lines, no code between
2. ✅ REDEEM: `C -= amount` and `U -= amount` in same function, consecutive lines, no code between
3. ✅ Yield: `Cr += delta` and `Ur += delta` in same function, consecutive lines, no code between
4. ✅ UNSTAKE: `U += B`, `C += B`, `Cr -= B`, `Ur -= B` in same function, consecutive lines, no code between
5. ✅ NEVER separate paired updates into different functions
6. ✅ NEVER put conditional logic between paired updates
7. ✅ NEVER allow early returns between paired updates

**Forbidden patterns:**
```cpp
state.C += amount;
DoSomethingElse();  // ❌ Code between paired updates
state.U += amount;
```

**Consequence of violation:** `C != U` or `Cr != Ur` → invariant check fails → block rejected

---

### 20.8 Master Checklist (Run Before Phase 1)

**BEFORE writing ANY KHU implementation code, verify:**

```bash
#!/bin/bash
# Save as: scripts/verify_anti_derive.sh

echo "=== ANTI-DÉRIVE VERIFICATION SUITE ==="
echo ""

# 1. Lock verification
echo "[1/6] Verifying lock usage..."
grep -B5 "ApplyDailyYield\|ApplyKhuUnstake" src/**/*.cpp 2>/dev/null | grep -q "LOCK2.*cs_khu"
if [ $? -eq 0 ]; then echo "✅ Locks present"; else echo "❌ MISSING LOCKS"; exit 1; fi

# 2. Execution order
echo "[2/6] Verifying execution order..."
grep -A10 "ApplyDailyYield" src/validation.cpp 2>/dev/null | grep -B5 "for.*vtx" | head -1 | grep -q "ApplyDailyYield"
if [ $? -eq 0 ]; then echo "✅ Yield before transactions"; else echo "❌ WRONG ORDER"; exit 1; fi

# 3. Transaction error handling
echo "[3/6] Verifying error handling..."
grep "ProcessKHUTransaction" src/validation.cpp 2>/dev/null | grep -q "if (!.*return false"
if [ $? -eq 0 ]; then echo "✅ Error checking present"; else echo "❌ MISSING ERROR CHECK"; exit 1; fi

# 4. Struct synchronization
echo "[4/6] Verifying struct sync..."
diff <(grep -A30 "^struct KhuGlobalState" docs/02-canonical-specification.md) \
     <(grep -A30 "^struct KhuGlobalState" docs/03-architecture-overview.md) > /dev/null 2>&1
if [ $? -eq 0 ]; then echo "✅ Structs synchronized"; else echo "❌ STRUCT MISMATCH"; exit 1; fi

# 5. Pool separation
echo "[5/6] Verifying pool separation..."
grep -r "saplingTree.append.*KHU" src/ 2>/dev/null
if [ $? -ne 0 ]; then echo "✅ Pools separated"; else echo "❌ POOL CONTAMINATION"; exit 1; fi

# 6. Serialization order
echo "[6/6] Verifying serialization order..."
grep -A20 "GetHash.*const" src/khu/khu_state.cpp 2>/dev/null | grep "ss <<" | awk '{print $3}' > /tmp/order.txt
expected="C;
U;
Cr;
Ur;
R_annual;
R_MAX_dynamic;
last_domc_height;
domc_cycle_start;
domc_cycle_length;
domc_phase_length;
last_yield_update_height;
nHeight;
hashBlock;
hashPrevState;"
echo "$expected" > /tmp/expected.txt
diff /tmp/order.txt /tmp/expected.txt > /dev/null 2>&1
if [ $? -eq 0 ]; then echo "✅ Serialization order correct"; else echo "❌ WRONG SERIALIZATION"; exit 1; fi

echo ""
echo "=== ALL ANTI-DÉRIVE CHECKS PASSED ==="
```

**Run this script before EVERY commit during Phase 1 implementation.**

---

### 20.9 Consequence Table

| Violation | Impact | Recovery |
|-----------|--------|----------|
| Missing lock | Runtime crash, data corruption | Restart node, possible reindex |
| Wrong execution order | Invariant violation, block rejection | Automatic (node rejects bad block) |
| Ignored transaction error | Invalid state, consensus divergence | Hard fork required |
| Struct desynchronization | Implementation bug, variable severity | Fix code, possible reindex |
| Pool contamination | Privacy leak, security breach | Hard fork required |
| Wrong serialization order | Network split, total consensus failure | Hard fork required |
| Broken atomicity | Invariant violation, block rejection | Automatic (node rejects bad block) |

**Severity levels:**
- 🟢 **Low:** Automatic recovery via consensus rejection
- 🟡 **Medium:** Requires node restart or reindex
- 🔴 **Critical:** Requires coordinated hard fork

---

### 20.10 Final Implementation Rule

**BEFORE merging ANY KHU code to main branch:**

1. ✅ All unit tests pass
2. ✅ All functional tests pass
3. ✅ `verify_anti_derive.sh` passes with exit code 0
4. ✅ Manual code review confirms compliance with ALL sections above
5. ✅ Integration test includes all ANTI-DÉRIVE edge cases
6. ✅ Documentation updated if ANY consensus rule changed

**If ANY check fails → DO NOT MERGE.**

---

## END OF PROTOCOL REFERENCE

This document defines complete C++ implementation for PIVX-V6-KHU.

All structures defined.
All algorithms specified.
All file locations determined.
All functions prototyped.
All consensus decisions documented.

Implementation must follow this reference exactly.

No deviation permitted.
