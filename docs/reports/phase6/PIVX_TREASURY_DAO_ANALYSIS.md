# PIVX Treasury/DAO System Analysis — V6.0 KHU Implementation

**Date:** 2025-11-24  
**Version:** 1.0  
**Scope:** Complete analysis of PIVX treasury/DAO payment mechanism

---

## EXECUTIVE SUMMARY

PIVX implements a **hybrid treasury/DAO system** with two distinct layers:

1. **Legacy PIVX Budget System** — Masternode-voted superblocks (governance-based)
2. **New KHU Phase 6 DAO System** — Automatic budget allocation (deterministic, no voting)

Both systems work in parallel, with the DAO system currently **defined but not yet integrated** into block creation.

---

## TABLE OF CONTENTS

1. [Current Treasury Architecture](#current-treasury-architecture)
2. [Block Reward Distribution (V6.0)](#block-reward-distribution-v60)
3. [PIVX Legacy Budget System](#pivx-legacy-budget-system)
4. [KHU Phase 6 Automatic DAO System](#khu-phase-6-automatic-dao-system)
5. [Treasury Address Handling](#treasury-address-handling)
6. [Integration Points](#integration-points)
7. [Code Flow Diagrams](#code-flow-diagrams)
8. [Key Files Reference](#key-files-reference)

---

## CURRENT TREASURY ARCHITECTURE

### Two-Tier System

```
┌─────────────────────────────────────────────────┐
│         BLOCK CREATION FLOW (v6.0)              │
├─────────────────────────────────────────────────┤
│                                                 │
│  1. CreateCoinStake (wallet/wallet.cpp:3367)   │
│     ├─ Creates coinstake TX                    │
│     ├─ Adds block reward: GetBlockValue()      │
│     └─ Adds MN payment: GetMasternodePayment() │
│                                                 │
│  2. FillBlockPayee (masternode-payments.cpp)   │
│     ├─ Check: SPORK_13_ENABLE_SUPERBLOCKS?    │
│     ├─ IF YES → g_budgetman.FillBlockPayee()  │
│     │   └─ [Legacy PIVX Budget]                │
│     └─ IF NO → masternodePayments.FillBlockPayee()
│         └─ [Regular MN Payment]                │
│                                                 │
│  3. Block Validation                           │
│     ├─ IsBlockValueValid()                     │
│     └─ IsBlockPayeeValid()                     │
│                                                 │
└─────────────────────────────────────────────────┘
```

### System Decision Tree

**FOR BLOCK CREATION:**
```
If SPORK_13_ENABLE_SUPERBLOCKS is active:
  → Check if block height is superblock
  → If YES: Pay budget proposal (g_budgetman)
  → If NO: Pay masternode
Else:
  → Always pay masternode (MN payment)
```

**FOR KHU DAO (NOT YET INTEGRATED):**
```
If IsDaoCycleBoundary(height, activation):
  → Calculate: DAO_budget = (U + Ur) × 0.5%
  → Mint: DAO_budget PIV → DAO treasury address
  → (Would be added in future phases)
```

---

## BLOCK REWARD DISTRIBUTION (V6.0)

### GetBlockValue() — Staker Compartment

**Location:** `/PIVX/src/validation.cpp:820-871`

```cpp
CAmount GetBlockValue(int nHeight)
{
    // V6.0 NEW EMISSION: 6→0 per year
    if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V6_0)) {
        const int nActivationHeight = 
            consensus.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
        const int64_t year = (nHeight - nActivationHeight) / BLOCKS_PER_YEAR;
        const int64_t reward_year = max(6LL - year, 0LL);
        return reward_year * COIN;  // ← STAKER ONLY
    }
    // ... Legacy V5.5 logic
}
```

**What it returns:**
- **Staker reward only** (not including MN or DAO)
- Year 0: 6 PIV/block staker
- Year 1: 5 PIV/block staker
- ...
- Year 6+: 0 PIV/block staker

### GetMasternodePayment() — Masternode Compartment

**Location:** `/PIVX/src/validation.cpp:873-892`

```cpp
int64_t GetMasternodePayment(int nHeight)
{
    if (NetworkUpgradeActive(nHeight, Consensus::UPGRADE_V6_0)) {
        const int nActivationHeight = 
            consensus.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight;
        const int64_t year = (nHeight - nActivationHeight) / BLOCKS_PER_YEAR;
        const int64_t reward_year = max(6LL - year, 0LL);
        return reward_year * COIN;  // ← MASTERNODE ONLY
    }
    // ... Legacy V5.5: 6 COIN
}
```

**What it returns:**
- **Masternode reward only** (not including staker)
- Year 0-6: Same as staker (equal distribution)
- Identical schedule to staker

### Total Block Emission (V6.0)

```
Block Emission = Staker + MN + DAO
                = reward_year + reward_year + reward_year
                = 3 × reward_year

Year 0: 18 PIV/block (6 staker + 6 MN + 6 DAO)
Year 1: 15 PIV/block
Year 2: 12 PIV/block
...
Year 6: 0 PIV/block (all compartments end)
```

---

## PIVX LEGACY BUDGET SYSTEM

### Architecture

The legacy PIVX Budget system is a **governance-based treasury** using:
- Masternode voting (YES/NO/ABSTAIN)
- Finalized budgets (superblocks)
- Budget proposals with collateral

### Key Components

#### 1. Budget Manager (`/PIVX/src/budget/budgetmanager.cpp`)

```cpp
class CBudgetManager {
    std::map<uint256, CBudgetProposal> mapProposals;        // Proposals
    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets; // Finalized budgets
    std::map<uint256, CBudgetVote> mapSeenProposalVotes;     // Votes
    // ... vote tracking and management
    
    // Core functions:
    bool FillBlockPayee(CMutableTransaction& txCoinbase, 
                       CMutableTransaction& txCoinstake, 
                       int nHeight, bool fProofOfStake);
    bool IsBudgetPaymentBlock(int nBlockHeight);
    CAmount GetTotalBudget(int nHeight);
    bool GetPayeeAndAmount(int nHeight, CScript& payee, 
                          CAmount& amount);
};
```

#### 2. Budget Cycle

**Location:** `/PIVX/src/consensus/params.h:178`

```cpp
struct Consensus::Params {
    int nBudgetCycleBlocks;  // Budget cycle length
    // ... other params
};

// Mainnet: 43,200 blocks (≈30 days)
// Testnet: 144 blocks (≈2.4 hours for testing)
```

#### 3. Total Budget Calculation

**Location:** `/PIVX/src/budget/budgetmanager.cpp:858-869`

```cpp
CAmount CBudgetManager::GetTotalBudget(int nHeight)
{
    const int nBlocksPerCycle = Params().GetConsensus().nBudgetCycleBlocks;
    
    // Calculate subsidy for this block
    CAmount nSubsidy = GetBlockValue(nHeight);
    
    // Total budget = subsidy × blocks per cycle
    return nSubsidy * nBlocksPerCycle;
}
```

**Example (Mainnet, pre-V6.0):**
- Block value: 45 PIV
- Cycle: 43,200 blocks
- Total: 45 × 43,200 = 1,944,000 PIV per cycle

#### 4. Block Payee Selection

**Function:** `FillBlockPayee()` in `/PIVX/src/masternode-payments.cpp:276-284`

```cpp
void FillBlockPayee(CMutableTransaction& txCoinbase, 
                   CMutableTransaction& txCoinstake, 
                   const CBlockIndex* pindexPrev, 
                   bool fProofOfStake)
{
    if (!sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS) ||
        !g_budgetman.FillBlockPayee(txCoinbase, txCoinstake, 
                                    pindexPrev->nHeight + 1, 
                                    fProofOfStake)) {
        // No superblock OR budget not approved
        // → Pay regular masternode
        masternodePayments.FillBlockPayee(txCoinbase, txCoinstake, 
                                         pindexPrev, fProofOfStake);
    }
}
```

**Decision Logic:**
1. Check SPORK_13 (superblocks enabled?)
2. If YES + is superblock: Use budget payment
3. Otherwise: Pay masternode

#### 5. Budget Payment in Coinstake

**Location:** `/PIVX/src/budget/budgetmanager.cpp:518-559`

```cpp
bool CBudgetManager::FillBlockPayee(...)
{
    CScript payee;
    CAmount nAmount = 0;
    
    if (!GetPayeeAndAmount(nHeight, payee, nAmount))
        return false;  // No approved proposal for this height
    
    // For PoS blocks (v6.0+): pay in coinbase only
    const bool fPayCoinstake = fProofOfStake && 
                              !Params().GetConsensus()
                              .NetworkUpgradeActive(nHeight, 
                                                   UPGRADE_V6_0);
    
    if (fProofOfStake && !fPayCoinstake) {
        // V6.0: Pay in coinbase
        txCoinbase.vout.clear();
        txCoinbase.vout[0].scriptPubKey = payee;
        txCoinbase.vout[0].nValue = nAmount;
    } else if (fProofOfStake && fPayCoinstake) {
        // Pre-V6.0: Pay in coinstake
        txCoinstake.vout.emplace_back(nAmount, payee);
    } else {
        // PoW blocks: add extra output
        txCoinbase.vout.resize(2);
        txCoinbase.vout[1].scriptPubKey = payee;
        txCoinbase.vout[1].nValue = nAmount;
    }
    
    return true;
}
```

#### 6. Block Value Validation

**Location:** `/PIVX/src/masternode-payments.cpp:197-225`

```cpp
bool IsBlockValueValid(int nHeight, CAmount& nExpectedValue, 
                       CAmount nMinted, CAmount& nBudgetAmt)
{
    // Add budget amount to expected value
    if (sporkManager.IsSporkActive(SPORK_13_ENABLE_SUPERBLOCKS)) {
        if (g_budgetman.GetExpectedPayeeAmount(nHeight, nBudgetAmt)) {
            nExpectedValue += nBudgetAmt;  // Budget adds to block value
        }
    }
    
    // Validation: minted <= expected
    return nMinted <= nExpectedValue;
}
```

### Budget Flow Example

**Scenario: Mainnet superblock at height 43,200**

```
1. Proposal Creation
   └─ User submits proposal (10 PIV collateral)
   └─ Name: "dev-fund", Payee: D7x..., Amount: 50 PIV

2. Masternode Voting (Cycle 1)
   └─ MNs vote YES/NO/ABSTAIN for 30 days
   └─ If votes(YES) > votes(NO): Proposal passes

3. Finalized Budget
   └─ Proposal bundled into finalized budget
   └─ Another round of MN votes on finalized budget
   └─ If approved: added to CBudgetManager

4. Block Creation (Height = 43,200)
   └─ CreateCoinStake()
   │  └─ Add: staker reward (GetBlockValue) + MN payment
   │  └─ nCredit = staker_reward + block_reward - MN_payment
   └─ FillBlockPayee()
      └─ Is SPORK_13 active? YES
      └─ Is height 43,200 a budget payment block? YES
      └─ Get budget payee: D7x...
      └─ Output: 50 PIV to D7x...
      └─ (Extra output in coinbase for v6.0)

5. Validation
   └─ IsBlockValueValid()
   │  └─ nExpectedValue = block_reward
   │  └─ nBudgetAmt = 50 PIV
   │  └─ nExpectedValue += nBudgetAmt = block_reward + 50
   └─ Check: nMinted (block_reward + 50) ≤ nExpectedValue ✓
```

---

## KHU PHASE 6 AUTOMATIC DAO SYSTEM

### Overview

**Status:** Fully defined and coded, NOT YET INTEGRATED into block creation

The KHU DAO system replaces governance voting with automatic, deterministic budget allocation.

### Architecture

**Files:**
- Header: `/PIVX/src/khu/khu_dao.h` (60 lines)
- Implementation: `/PIVX/src/khu/khu_dao.cpp` (64 lines)
- Documentation: `/docs/reports/phase6/CHANGEMENTS_DAO_SIMPLIFICATION.md`

### Three Core Functions

#### 1. IsDaoCycleBoundary()

**Purpose:** Detect when DAO budget cycle ends

**Location:** `/PIVX/src/khu/khu_dao.cpp:11-19`

```cpp
bool IsDaoCycleBoundary(uint32_t nHeight, uint32_t nActivationHeight)
{
    if (nHeight <= nActivationHeight) {
        return false;
    }
    
    uint32_t blocks_since_activation = nHeight - nActivationHeight;
    return (blocks_since_activation % DAO_CYCLE_LENGTH) == 0;
}
```

**Constants:**
```cpp
static const uint32_t DAO_CYCLE_LENGTH = 172800;  // 4 months (172,800 blocks)
static const uint32_t BLOCKS_PER_YEAR = 525600;   // 365 days × 1440 blocks/day
```

**Cycle Boundaries (relative to activation):**
- Cycle 1: blocks 0-172,799 → trigger at 172,800
- Cycle 2: blocks 172,800-345,599 → trigger at 345,600
- etc.

**Example:**
```
Activation height: 5,000,000
Cycle 1 trigger: 5,000,000 + 172,800 = 5,172,800
Cycle 2 trigger: 5,000,000 + 345,600 = 5,345,600
```

#### 2. CalculateDAOBudget()

**Purpose:** Calculate automatic DAO budget from KHU state

**Location:** `/PIVX/src/khu/khu_dao.cpp:21-36`

```cpp
CAmount CalculateDAOBudget(const KhuGlobalState& state)
{
    // DAO_budget = (U + Ur) × 0.5% = (U + Ur) × 5 / 1000
    
    __int128 total = (__int128)state.U + (__int128)state.Ur;
    __int128 budget = (total * 5) / 1000;
    
    // Overflow protection
    if (budget < 0 || budget > MAX_MONEY) {
        LogPrintf("WARNING: CalculateDAOBudget overflow\n");
        return 0;
    }
    
    return (CAmount)budget;
}
```

**Formula:**
```
DAO_budget = floor((U + Ur) × 0.005)
           = floor((U + Ur) × 5 / 1000)

Where:
  U   = Committed PIV (staked, locked, matured)
  Ur  = Recently committed PIV (staked but immature)
```

**Overflow Protection:**
- Uses `__int128` for intermediate calculation
- Checks result against `MAX_MONEY`
- Returns 0 on overflow (safe fallback)

**Example:**
```
U = 1,000,000 PIV
Ur = 500,000 PIV
Total = 1,500,000 PIV

DAO_budget = (1,500,000 × 5) / 1000
           = 7,500,000 / 1000
           = 7,500 PIV per cycle
```

#### 3. AddDaoPaymentToCoinstake()

**Purpose:** Add DAO payment to coinstake transaction

**Location:** `/PIVX/src/khu/khu_dao.cpp:38-64`

```cpp
bool AddDaoPaymentToCoinstake(CMutableTransaction& txCoinstake, 
                              CAmount daoAmount)
{
    if (daoAmount <= 0) {
        return true;  // No payment needed
    }
    
    // Get DAO treasury address from chainparams
    const Consensus::Params& consensusParams = 
        Params().GetConsensus();
    const std::string& strDaoAddress = 
        consensusParams.strDaoTreasuryAddress;
    
    // Convert string → CTxDestination
    CTxDestination dest = DecodeDestination(strDaoAddress);
    if (!IsValidDestination(dest)) {
        return error("AddDaoPaymentToCoinstake: Invalid address: %s", 
                    strDaoAddress);
    }
    
    // Create payment script
    CScript daoTreasury = GetScriptForDestination(dest);
    
    // Add output to coinstake (mints new PIV)
    txCoinstake.vout.emplace_back(daoAmount, daoTreasury);
    
    LogPrint(BCLog::KHU, "DAO Budget: Minting %lld PIV to treasury\n",
            daoAmount);
    
    return true;
}
```

**Process:**
1. Get DAO treasury address from chainparams
2. Validate address format
3. Create payment script
4. Add output to coinstake transaction
5. Log transaction

### Treasury Address Definition

**Location:** `/PIVX/src/consensus/params.h:197` & `/PIVX/src/chainparams.cpp:249-251, 417-418, 568`

```cpp
// In consensus::Params struct
std::string strDaoTreasuryAddress;

// Mainnet (chainparams.cpp:251)
consensus.strDaoTreasuryAddress = "DPLACEHOLDERMainnetDaoTreasuryAddressHere";

// Testnet (chainparams.cpp:418)
consensus.strDaoTreasuryAddress = "yPLACEHOLDERTestnetDaoTreasuryAddressHere";

// Regtest (chainparams.cpp:568)
consensus.strDaoTreasuryAddress = "yPLACEHOLDERRegtestDaoTreasuryAddressHere";
```

**Status:** Currently PLACEHOLDERS — need actual multisig addresses before mainnet deployment.

### DAO Budget Characteristics

```
Trigger:    Every 172,800 blocks (4 months)
Calculation: (U + Ur) × 0.5% (deterministic)
Payment:    Automatic mint to treasury (no voting)
External:   Separate from KHU state (PIV only)
Impact:     No effect on C/U/Cr/Ur compartments
Consensus:  Fully deterministic
```

---

## TREASURY ADDRESS HANDLING

### Current Implementation

#### 1. Chainparams Definition

**File:** `/PIVX/src/chainparams.cpp`

```cpp
// Lines 249-251 (Mainnet)
// KHU DAO Treasury (Phase 6)
// TODO: Replace with actual multisig DAO council address
consensus.strDaoTreasuryAddress = "DPLACEHOLDERMainnetDaoTreasuryAddressHere";

// Lines 417-418 (Testnet)
consensus.strDaoTreasuryAddress = "yPLACEHOLDERTestnetDaoTreasuryAddressHere";

// Lines 568 (Regtest)
consensus.strDaoTreasuryAddress = "yPLACEHOLDERRegtestDaoTreasuryAddressHere";
```

#### 2. Consensus Params Storage

**File:** `/PIVX/src/consensus/params.h:197`

```cpp
struct Params {
    // ... other fields
    std::string strDaoTreasuryAddress;  // DAO treasury address
    // ... other fields
};
```

#### 3. Address Validation

**Function:** `AddDaoPaymentToCoinstake()` validates at runtime:

```cpp
// Validate address format
CTxDestination dest = DecodeDestination(strDaoAddress);
if (!IsValidDestination(dest)) {
    return error("AddDaoPaymentToCoinstake: Invalid address: %s", 
                strDaoAddress);
}

// Create script for payment
CScript daoTreasury = GetScriptForDestination(dest);
```

### Budget System Address Handling

The legacy PIVX Budget system handles addresses differently:

1. **Per-Proposal:** Each CBudgetProposal has its own payee address
   ```cpp
   class CBudgetProposal {
       CScript scriptPubKey;  // Proposal payee
       // ... other fields
   };
   ```

2. **Dynamic Voting:** Addresses determined by proposal votes
3. **Multiple Payees:** Multiple proposals = multiple addresses per cycle

---

## INTEGRATION POINTS

### Where DAO Integration Is Needed

#### 1. CreateCoinStake() Enhancement

**Current Location:** `/PIVX/src/wallet/wallet.cpp:3367-3473`

**What to add:**
```cpp
bool CWallet::CreateCoinStake(...)
{
    // ... existing code ...
    
    // After line 3443: txNew.vout.insert(...)
    
    // NEW: Add DAO payment if applicable
    if (IsDaoCycleBoundary(nHeight, nActivationHeight)) {
        CAmount daoAmount = CalculateDAOBudget(khuState);
        if (!AddDaoPaymentToCoinstake(txNew, daoAmount)) {
            return error("CreateCoinStake: DAO payment failed");
        }
        // Adjust nCredit to account for new output
        nCredit += daoAmount;
    }
    
    // ... rest of function ...
}
```

**Required includes:**
```cpp
#include "khu/khu_dao.h"
#include "khu/khu_state.h"
```

#### 2. Block Validation Enhancement

**Location:** `/PIVX/src/masternode-payments.cpp:197-225`

**What to add in IsBlockValueValid():**
```cpp
bool IsBlockValueValid(...)
{
    // ... existing budget code ...
    
    // NEW: Account for DAO budget
    if (IsDaoCycleBoundary(nHeight, activation)) {
        CAmount daoBudget = CalculateDAOBudget(khuState);
        nExpectedValue += daoBudget;
        // nMinted should include DAO payment
    }
    
    return nMinted <= nExpectedValue;
}
```

#### 3. ConnectBlock Integration

**Location:** `/PIVX/src/validation.cpp` ConnectBlock function

**What to add:**
```cpp
// In ConnectBlock, after block reward handling:

// Apply DAO budget if applicable
if (IsDaoCycleBoundary(nHeight, nActivationHeight)) {
    CAmount daoBudget = CalculateDAOBudget(khuGlobalState);
    // Budget will be included in coinstake (AddDaoPaymentToCoinstake)
    LogPrint(BCLog::KHU, "DAO Budget at height %d: %lld PIV\n", 
            nHeight, daoBudget);
}
```

### Integration Status

| Component | Status | Notes |
|-----------|--------|-------|
| khu_dao.h | ✅ Done | Header complete, 3 functions defined |
| khu_dao.cpp | ✅ Done | Implementation complete |
| Chainparams | ⏳ Partial | Addresses defined as placeholders |
| CreateCoinStake | ⏳ TODO | Need to add DAO payment |
| IsBlockValueValid | ⏳ TODO | Need to validate DAO amount |
| ConnectBlock | ⏳ TODO | Need to trigger DAO budget |
| Tests | ⏳ TODO | 5 unit tests needed |
| Functional tests | ⏳ TODO | Python integration tests |

---

## CODE FLOW DIAGRAMS

### Mainnet Block Creation (Current, pre-DAO)

```
Block Creation Flow
═══════════════════

pwallet->CreateCoinStake(pindexPrev, ...)
    ↓
    Finds kernel & calculates nCredit
    ├─ nCredit = UTXOvalue + GetBlockValue(height)
    ├─ nCredit -= GetMasternodePayment(height)
    └─ Creates coinstake outputs
    ↓
CreateCoinbaseTx() / FillBlockPayee()
    ├─ Is SPORK_13 active?
    │  ├─ YES: g_budgetman.FillBlockPayee()
    │  │   ├─ Is superblock? → Pay proposal
    │  │   └─ No proposal? → Return false
    │  └─ NO: Skip budget
    │
    ├─ masternodePayments.FillBlockPayee()
    │  ├─ Get MN payee for height
    │  └─ Add to coinbase/coinstake
    │
    └─ Coinstake now has: [staker, MN payment, ...]
       or Coinbase has: [MN payment, budget, ...]

Block Added to Blockchain
    ↓
IsBlockPayeeValid()
    ├─ If budget block: g_budgetman.IsTransactionValid()
    ├─ If MN block: masternodePayments.IsTransactionValid()
    └─ Return: valid?
```

### With KHU DAO (Future)

```
Block Creation Flow (With DAO)
═══════════════════════════════

pwallet->CreateCoinStake(pindexPrev, ...)
    ↓
    Finds kernel & calculates nCredit
    ├─ nCredit = UTXOvalue + GetBlockValue(height)
    ├─ nCredit -= GetMasternodePayment(height)
    ├─ [NEW] Check: IsDaoCycleBoundary(height)?
    │  └─ YES: daoAmount = CalculateDAOBudget(state)
    │          AddDaoPaymentToCoinstake(txCoinstake, daoAmount)
    │          nCredit += daoAmount
    └─ Creates coinstake outputs
    ↓
CreateCoinbaseTx() / FillBlockPayee()
    ├─ Is SPORK_13 active?
    │  ├─ YES: g_budgetman.FillBlockPayee()
    │  └─ NO: masternodePayments.FillBlockPayee()
    │
    └─ Coinstake now has: [staker, MN, DAO, ...]

Block Added to Blockchain
    ↓
IsBlockPayeeValid() & IsBlockValueValid()
    ├─ [NEW] If DAO boundary: nExpectedValue += daoBudget
    └─ Validation: nMinted ≤ nExpectedValue
```

### Budget Decision Logic (SPORK_13)

```
FillBlockPayee() Decision Tree
═══════════════════════════════

Is SPORK_13 enabled?
├─ NO
│  └─ Pay regular masternode (masternodePayments)
│
└─ YES
   └─ g_budgetman.FillBlockPayee()
      ├─ Is height a superblock?
      │  ├─ NO: return false
      │  └─ YES: Continue...
      │
      ├─ GetPayeeAndAmount(height, payee, amount)
      │  ├─ Found? → return true (payee set)
      │  └─ Not found? → return false
      │
      └─ If return false: Fall back to MN payment
```

---

## KEY FILES REFERENCE

### Core Block Reward Files

| File | Lines | Purpose |
|------|-------|---------|
| `/PIVX/src/validation.cpp` | 820-892 | GetBlockValue(), GetMasternodePayment() |
| `/PIVX/src/blockassembler.cpp` | 69-128 | SolveProofOfStake(), CreateCoinbaseTx() |
| `/PIVX/src/wallet/wallet.cpp` | 3367-3487 | CreateCoinStake(), SignCoinStake() |

### Budget System Files

| File | Lines | Purpose |
|------|-------|---------|
| `/PIVX/src/budget/budgetmanager.h` | 1-180+ | CBudgetManager class definition |
| `/PIVX/src/budget/budgetmanager.cpp` | 518-559 | FillBlockPayee() implementation |
| `/PIVX/src/budget/budgetmanager.cpp` | 858-869 | GetTotalBudget() calculation |
| `/PIVX/src/masternode-payments.cpp` | 197-225 | IsBlockValueValid() |
| `/PIVX/src/masternode-payments.cpp` | 227-273 | IsBlockPayeeValid() |
| `/PIVX/src/masternode-payments.cpp` | 276-284 | FillBlockPayee() dispatcher |

### KHU DAO Files

| File | Lines | Purpose |
|------|-------|---------|
| `/PIVX/src/khu/khu_dao.h` | 1-60 | DAO function declarations |
| `/PIVX/src/khu/khu_dao.cpp` | 1-64 | DAO implementation |
| `/PIVX/src/consensus/params.h` | 197 | strDaoTreasuryAddress definition |
| `/PIVX/src/chainparams.cpp` | 249-251, 417-418, 568 | Address initialization |

### Consensus Configuration

| File | Lines | Purpose |
|------|-------|---------|
| `/PIVX/src/consensus/params.h` | 171-293 | Consensus::Params struct |
| `/PIVX/src/chainparams.cpp` | 213-376 | CMainParams class |
| `/PIVX/src/chainparams.cpp` | 381-482 | CTestNetParams class |

### Test Files

| File | Purpose |
|------|---------|
| `/PIVX/src/test/budget_tests.cpp` | Budget validation tests |
| `/PIVX/test/functional/rpc_budget.py` | Budget RPC tests |

---

## DETAILED FLOW: BLOCK CREATION → VALIDATION

### Step-by-Step Example: Testnet PoS Block

**Scenario:** Height 144 (superblock), SPORK_13 active, budget approved

#### 1. **CreateCoinStake (wallet.cpp:3367)**

```
Input:  pindexPrev (height 143), nBits, txCoinstake (empty)
        availableCoins, stopOnNewBlock

Process:
  A. Find kernel in available UTXOs
     └─ stakeInput.GetValue() = 1000 PIV

  B. Calculate rewards
     ├─ nCredit = 1000 + GetBlockValue(144)
     ├─ nCredit = 1000 + 250 COIN (testnet V5.5)
     ├─ nMasternodePayment = GetMasternodePayment(144) = 6 COIN
     └─ nCredit = 1244 PIV

  C. CreateCoinstakeOuts(stakeInput, vout, nCredit - nMasternodePayment)
     ├─ Creates output: [staker: 1238 PIV]
     └─ vout = [empty, staker output]

  D. Build coinstake TX
     ├─ vin: [kernel input]
     └─ vout: [empty, 1238 PIV]

Output: txCoinstake (unsigned, ready to sign)
```

#### 2. **FillBlockPayee (masternode-payments.cpp:276)**

```
Input:  txCoinbase (empty), txCoinstake (from step 1)
        pindexPrev, fProofOfStake=true

Process:
  A. Is SPORK_13 active? YES
  
  B. g_budgetman.FillBlockPayee(txCoinbase, txCoinstake, 144, true)
  
     i. GetPayeeAndAmount(144, payee, amount)
        └─ Returns: payee=D7x..., amount=50 PIV
        
     ii. For PoS + V6.0: fPayCoinstake = false
        └─ Pay in coinbase (not coinstake)
        
     iii. txCoinbase.vout.clear()
         └─ txCoinbase.vout[0] = [50 PIV to D7x...]
        
     iv. Return: true (budget paid)

Output: 
  - txCoinbase: [50 PIV → D7x...]
  - txCoinstake: [1238 PIV → staker]
```

#### 3. **SignCoinStake (wallet.cpp:3475)**

```
Input:  txCoinstake (unsigned)

Process:
  A. Sign coinstake input
     └─ Signature added to vin[0].scriptSig
  
  B. Verify signature
     └─ OK ✓

Output: txCoinstake (signed, ready to broadcast)
```

#### 4. **CreateBlock**

```
Block contents:
  Block height: 144
  vtx[0]: txCoinbase [50 PIV → D7x...]
  vtx[1]: txCoinstake [1238 PIV → staker]
  
Block value breakdown:
  Block reward: 250 PIV
  Budget payout: 50 PIV
  MN payment: 6 PIV (already subtracted from coinstake)
  Total emission: 250 PIV (block value) + 50 PIV (budget)
```

#### 5. **IsBlockValueValid (masternode-payments.cpp:197)**

```
Input:  nHeight=144, nExpectedValue=250 PIV, nMinted=300 PIV

Process:
  A. Is SPORK_13 active? YES
  
  B. g_budgetman.GetExpectedPayeeAmount(144, nBudgetAmt)
     └─ nBudgetAmt = 50 PIV
     
  C. nExpectedValue += nBudgetAmt
     └─ nExpectedValue = 250 + 50 = 300 PIV
     
  D. nMinted <= nExpectedValue?
     └─ 300 <= 300? YES ✓

Output: true (block value valid)
```

#### 6. **IsBlockPayeeValid (masternode-payments.cpp:227)**

```
Input:  block, pindexPrev

Process:
  A. Is SPORK_13 active? YES
  
  B. g_budgetman.IsBudgetPaymentBlock(144)?
     └─ YES (superblock)
     
  C. g_budgetman.IsTransactionValid(txCoinbase, blockHash, 144)
     ├─ Get expected payee: D7x...
     ├─ Check actual payee in txCoinbase: D7x... ✓
     ├─ Check amount: 50 PIV ✓
     └─ Return: TrxValidationStatus::Valid
     
  D. Status == Valid? YES ✓

Output: true (block payee valid)
```

#### 7. **Block Accepted**

✓ Block added to chain
✓ 250 PIV block reward distributed
✓ 50 PIV budget payment to D7x...
✓ 6 PIV MN payment (implicit in coinstake reduction)

---

## COMPARISON: SYSTEM VS SYSTEM

### Legacy PIVX Budget vs KHU DAO

| Aspect | Legacy PIVX Budget | KHU DAO |
|--------|-------------------|---------|
| **Trigger** | Spork-enabled, MN votes | Height-based (172,800 block cycle) |
| **Calculation** | Proposal submissions | Automatic: (U + Ur) × 0.5% |
| **Governance** | MN voting (YES/NO/ABSTAIN) | None (deterministic) |
| **Payee** | Per-proposal (dynamic) | Fixed treasury address |
| **Collateral** | 10 PIV per proposal | None |
| **Block Timing** | Every ~30 days (cycle) | Every 4 months (cycle) |
| **Code Complexity** | ~1000 lines (vote mgmt) | ~100 lines (auto calc) |
| **Changes on Reorg** | Can change if votes change | Deterministic (no change) |
| **Integration** | Active (SPORK_13) | TODO (not yet called) |

---

## IMPORTANT NOTES

### 1. **DAO Is Separate from Block Reward**

The KHU DAO budget is **external to the block reward distribution**:

```
Block Reward: staker + MN (both compartments, 0-6 PIV/block)
DAO Budget:   Separate automatic allocation (0.5% of (U+Ur))
Total:        Block reward + DAO budget independently
```

### 2. **Treasury Address Is Critical**

The `strDaoTreasuryAddress` in `consensus.Params` is the sole point of DAO payment. 

**Current Status:** Placeholder values
```
"DPLACEHOLDERMainnetDaoTreasuryAddressHere"
```

**Before Mainnet:** Replace with actual multisig address (3-of-5 recommended)

### 3. **KHU State Required**

DAO budget calculation depends on KHU global state (U, Ur values):

```cpp
CAmount CalculateDAOBudget(const KhuGlobalState& state)
{
    // Needs: state.U (committed)
    //        state.Ur (recently committed)
}
```

This means DAO cannot be triggered before KHU state is fully operational.

### 4. **Two Completely Different Timescales**

- **PIVX Budget:** ~30 day cycle (43,200 mainnet blocks)
- **KHU DAO:** 4 month cycle (172,800 blocks)

Both can coexist but operate independently.

### 5. **Validation Must Account for Both**

When validating block value, code must check:

```cpp
nExpectedValue = GetBlockValue(nHeight);  // Base reward

// Legacy budget
if (isSuperblock) nExpectedValue += budgetAmount;

// Future: KHU DAO
if (isDaoCycleBoundary) nExpectedValue += daoAmount;
```

---

## REFERENCES & DOCUMENTATION

**Repository:** `/home/ubuntu/PIVX-V6-KHU/`

**Key Documents:**
- `/docs/reports/phase6/PHASE6_ARCHITECTURE.md` — Full Phase 6 design
- `/docs/reports/phase6/CHANGEMENTS_DAO_SIMPLIFICATION.md` — DAO simplification
- `/PIVX/src/khu/khu_dao.h` — DAO function declarations
- `/PIVX/src/khu/khu_dao.cpp` — DAO implementation

**Recent Commits:**
- `e8c505d` — docs(roadmap): Phase 4/5 COMPLETED, Phase 6 DAO Budget added
- `3a97ade` — test(phase5): Comprehensive Phase 5 test suite
- `948704d` — feat(phase5): Complete ZKHU Sapling & DB integration

---

**END OF ANALYSIS**

Generated: 2025-11-24  
Analyst: Claude Code (File Search Specialist)  
Status: Complete Treasury/DAO System Analysis
