# Blueprint 11: KHU V6 Final Specification

**Version:** 1.0
**Date:** 2025-11-27
**Status:** FINAL

---

## Overview

KHU (Knowledge Hedge Unit) is a collateralized colored coin system for PIVX V6.
This document contains the final technical specification.

---

## Core Parameters

### R% (Annual Yield Rate)

```
R_INITIAL = 4000 bp (40%)
R_FLOOR   = 700 bp  (7%)
R_DECAY   = 100 bp/year (-1%/year)
R_YEARS   = 33 years to floor

R_MAX_dynamic = max(700, 4000 - year * 100)
```

### Timing Constants

```cpp
BLOCKS_PER_DAY   = 1440
BLOCKS_PER_YEAR  = 525600
MATURITY_BLOCKS  = 4320   // 3 days
FINALITY_DEPTH   = 12     // LLMQ
DOMC_CYCLE       = 172800 // 4 months
```

### T (Treasury) Divisor

```
T_DIVISOR = 20
```

---

## Economic Model

### Post-V6 Activation

```
Block Reward = 0 PIV (no emission)

Yield:    Z * R% / 365 per day
Treasury: U * R% / 20 / 365 per day
```

### Formulas

```cpp
// Daily yield per ZKHU note (satoshis)
yield_daily = (note.amount * R_annual) / 10000 / 365;

// Daily treasury accumulation (satoshis)
T_daily = (U * R_annual) / 10000 / 20 / 365;

// R_MAX at year Y
R_MAX = std::max(700, 4000 - year * 100);
```

### Timeline R_MAX

| Year | R_MAX | Max Inflation* |
|------|-------|----------------|
| 0    | 40%   | ~10.5%         |
| 10   | 30%   | ~7.9%          |
| 20   | 20%   | ~5.3%          |
| 30   | 10%   | ~2.6%          |
| 33+  | 7%    | ~1.84%         |

*Calculated with 50/50 scenario (KHU=50% PIV, ZKHU=50% KHU)

---

## Invariants

```cpp
C == U + Z      // Collateral == transparent + shielded
Cr == Ur        // Reward pool == reward rights
T >= 0          // Treasury non-negative
```

---

## Transaction Types

```cpp
KHU_MINT       = 6   // PIV -> KHU_T
KHU_REDEEM     = 7   // KHU_T -> PIV
KHU_STAKE      = 8   // KHU_T -> ZKHU
KHU_UNSTAKE    = 9   // ZKHU -> KHU_T + yield
KHU_DOMC_COMMIT = 10 // DOMC commit
KHU_DOMC_REVEAL = 11 // DOMC reveal
```

---

## State Structure

```cpp
struct KhuGlobalState {
    int64_t C;                  // Collateral (satoshis)
    int64_t U;                  // Transparent supply
    int64_t Z;                  // Shielded supply
    int64_t Cr;                 // Reward pool
    int64_t Ur;                 // Reward rights
    int64_t T;                  // Treasury

    uint16_t R_annual;          // Current R% (bp)
    uint16_t R_MAX_dynamic;     // max(700, 4000 - year*100)

    // DOMC cycle tracking
    uint32_t domc_cycle_start;
    uint32_t domc_cycle_length;

    // Yield tracking
    uint32_t last_yield_update_height;

    // Chain state
    uint32_t nHeight;
    uint256 hashBlock;
    uint256 hashPrevState;
};
```

---

## Operations

### MINT

```
Input:  X PIV
Output: X KHU_T

State:
  C += X
  U += X
```

### REDEEM

```
Input:  X KHU_T
Output: X PIV

State:
  C -= X
  U -= X
```

### STAKE

```
Input:  X KHU_T
Output: X ZKHU (note)

State:
  U -= X
  Z += X
```

### UNSTAKE (after maturity)

```
Input:  ZKHU note (principal P, yield Y)
Output: (P + Y) KHU_T

State:
  Z  -= P
  U  += P + Y
  C  += Y
  Cr -= Y
  Ur -= Y
```

### Daily Updates

```cpp
void ApplyDailyUpdatesIfNeeded(KhuGlobalState& state, uint32_t height) {
    if (height - state.last_yield_update_height >= BLOCKS_PER_DAY) {
        // Treasury accumulation
        state.T += (state.U * state.R_annual) / 10000 / 20 / 365;

        // Yield for mature notes
        for (auto& note : matureNotes) {
            int64_t yield = (note.amount * state.R_annual) / 10000 / 365;
            note.accumulatedYield += yield;
            state.Cr += yield;
            state.Ur += yield;
        }

        state.last_yield_update_height = height;
    }
}
```

---

## DOMC (Decentralized Open Market Committee)

### Cycle Structure

```
Cycle length: 172800 blocks (4 months)
Commit phase: blocks 132480-152639
Reveal phase: blocks 152640-172799
```

### Vote Mechanism

```
1. MN commits: Hash(R_vote || salt)
2. MN reveals: R_vote + salt
3. Median calculation at cycle end
4. New R% = clamp(median, 0, R_MAX_dynamic)
```

---

## Implementation Files

```
src/khu/khu_state.h/cpp       - State management
src/khu/khu_validation.h/cpp  - Transaction validation
src/khu/khu_db.h/cpp          - LevelDB persistence
src/khu/khu_yield.h/cpp       - Yield calculation
src/khu/khu_domc.h/cpp        - DOMC voting
src/wallet/khu_wallet.h/cpp   - Wallet integration
src/wallet/rpc_khu.cpp        - RPC commands
```

---

## RPC Commands

```
getkhustate          - Global KHU state
khumint <amount>     - PIV -> KHU_T
khuredeem <amount>   - KHU_T -> PIV
khustake <amount>    - KHU_T -> ZKHU
khuunstake <id>      - ZKHU -> KHU_T
khubalance           - Wallet KHU balance
khulistunspent       - List KHU UTXOs
khuliststaked        - List ZKHU notes
domccommit           - Commit R% vote
domcreveal           - Reveal R% vote
```

---

**Document created:** 2025-11-27
**Author:** KHU Architecture Team
