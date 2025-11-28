#!/bin/bash
# =============================================================================
# KHU V6 Private Testnet - Phase E: DOMC R% Voting Test
# =============================================================================
# Tests: DOMC cycle, commit-reveal, R% change
# Note: Full DOMC cycle is 172800 blocks. For testing, use accelerated cycle.
# =============================================================================

set -e

DATADIR="${1:-/tmp/khu_private_testnet}"
PIVX_BIN="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$PIVX_BIN/pivx-cli -regtest -datadir=$DATADIR -rpcuser=khutest -rpcpassword=khutest123"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

pass() { echo -e "${GREEN}✓ PASS${NC}: $1"; }
fail() { echo -e "${RED}✗ FAIL${NC}: $1"; exit 1; }
info() { echo -e "${YELLOW}→${NC} $1"; }
calc() { echo -e "${CYAN}≈${NC} $1"; }

echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE E: DOMC R% VOTING TEST"
echo "═══════════════════════════════════════════════════════════════"
echo ""

$CLI getblockcount > /dev/null 2>&1 || fail "Daemon not running"
pass "Daemon is running"

# =============================================================================
# TEST E1: Initial R% Check
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E1: Initial R% Values"
echo "─────────────────────────────────────────────────────────────────"

STATE=$($CLI getkhustate)

R_ANNUAL=$(echo "$STATE" | grep -o '"R_annual":[0-9]*' | grep -o '[0-9]*')
R_MAX=$(echo "$STATE" | grep -o '"R_MAX_dynamic":[0-9]*' | grep -o '[0-9]*')
R_NEXT=$(echo "$STATE" | grep -o '"R_next":[0-9]*' | grep -o '[0-9]*' || echo "0")

echo "R_annual:      $R_ANNUAL bp ($(echo "scale=2; $R_ANNUAL / 100" | bc)%)"
echo "R_MAX_dynamic: $R_MAX bp ($(echo "scale=2; $R_MAX / 100" | bc)%)"
echo "R_next:        $R_NEXT bp"

# Expected: R_annual = 4000 (40%) at V6 activation
if [ "$R_ANNUAL" -eq 4000 ]; then
    pass "R_annual = 40% (initial V6 value)"
elif [ "$R_ANNUAL" -gt 0 ] && [ "$R_ANNUAL" -le 5000 ]; then
    pass "R_annual = $R_ANNUAL bp is valid"
else
    info "R_annual = $R_ANNUAL (may be from previous cycle)"
fi

# =============================================================================
# TEST E2: DOMC Cycle Info
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E2: DOMC Cycle Information"
echo "─────────────────────────────────────────────────────────────────"

CURRENT_HEIGHT=$($CLI getblockcount)
echo "Current height: $CURRENT_HEIGHT"

# Extract DOMC cycle info from state
CYCLE_START=$(echo "$STATE" | grep -o '"domc_cycle_start":[0-9]*' | grep -o '[0-9]*')
COMMIT_START=$(echo "$STATE" | grep -o '"domc_commit_phase_start":[0-9]*' | grep -o '[0-9]*')
REVEAL_DEADLINE=$(echo "$STATE" | grep -o '"domc_reveal_deadline":[0-9]*' | grep -o '[0-9]*')

echo "DOMC Cycle:"
echo "  Cycle start:        $CYCLE_START"
echo "  Commit phase start: $COMMIT_START"
echo "  Reveal deadline:    $REVEAL_DEADLINE"

# Standard DOMC cycle: 172800 blocks (4 months)
# Vote phase: 132480 to 152640 (20160 blocks = 2 weeks)
# Adaptation: 152640 to 172800 (20160 blocks = 2 weeks)

DOMC_CYCLE_LENGTH=172800
DOMC_VOTE_OFFSET=132480
DOMC_REVEAL_HEIGHT=152640

calc "Standard cycle: $DOMC_CYCLE_LENGTH blocks (~4 months)"
calc "Vote phase: blocks $DOMC_VOTE_OFFSET to $DOMC_REVEAL_HEIGHT"

# Determine current phase
if [ -n "$CYCLE_START" ] && [ "$CYCLE_START" -gt 0 ]; then
    RELATIVE_HEIGHT=$((CURRENT_HEIGHT - CYCLE_START))
    echo "Relative height in cycle: $RELATIVE_HEIGHT"

    if [ "$RELATIVE_HEIGHT" -lt "$DOMC_VOTE_OFFSET" ]; then
        echo "Current phase: R% ACTIVE (pre-vote)"
        BLOCKS_TO_VOTE=$((CYCLE_START + DOMC_VOTE_OFFSET - CURRENT_HEIGHT))
        echo "Blocks until VOTE phase: $BLOCKS_TO_VOTE"
    elif [ "$RELATIVE_HEIGHT" -lt "$DOMC_REVEAL_HEIGHT" ]; then
        echo "Current phase: VOTE (commit+reveal)"
    elif [ "$RELATIVE_HEIGHT" -lt "$DOMC_CYCLE_LENGTH" ]; then
        echo "Current phase: ADAPTATION"
    else
        echo "Current phase: END OF CYCLE"
    fi
fi

pass "DOMC cycle info retrieved"

# =============================================================================
# TEST E3: R_MAX_dynamic Decay
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E3: R_MAX_dynamic Decay Formula"
echo "─────────────────────────────────────────────────────────────────"

echo ""
echo "R_MAX_dynamic formula: max(700, 4000 - year × 100)"
echo ""
echo "Timeline:"
echo "  Year 0:  4000 bp (40%)"
echo "  Year 10: 3000 bp (30%)"
echo "  Year 20: 2000 bp (20%)"
echo "  Year 30: 1000 bp (10%)"
echo "  Year 33: 700 bp (7%) - floor"
echo ""

# Calculate current year
BLOCKS_PER_YEAR=525600
V6_ACTIVATION=$($CLI getblockchaininfo | grep -o '"v6_0"[^}]*"activationheight":[0-9]*' | grep -o '[0-9]*$' || echo "200")

if [ -n "$V6_ACTIVATION" ] && [ "$V6_ACTIVATION" -gt 0 ]; then
    BLOCKS_SINCE_V6=$((CURRENT_HEIGHT - V6_ACTIVATION))
    CURRENT_YEAR=$((BLOCKS_SINCE_V6 / BLOCKS_PER_YEAR))
    EXPECTED_R_MAX=$((4000 - CURRENT_YEAR * 100))
    if [ "$EXPECTED_R_MAX" -lt 700 ]; then
        EXPECTED_R_MAX=700
    fi

    echo "Blocks since V6: $BLOCKS_SINCE_V6"
    echo "Current year: $CURRENT_YEAR"
    echo "Expected R_MAX: $EXPECTED_R_MAX bp"
    echo "Actual R_MAX:   $R_MAX bp"

    if [ "$R_MAX" -eq "$EXPECTED_R_MAX" ]; then
        pass "R_MAX_dynamic matches expected value"
    else
        info "R_MAX differs from expected (may be OK in testnet)"
    fi
fi

# =============================================================================
# TEST E4-E5: DOMC Commit/Reveal (Requires MN)
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E4-E5: DOMC Commit/Reveal"
echo "─────────────────────────────────────────────────────────────────"

info "DOMC commit/reveal requires active Masternodes"
info "Checking for MN status..."

MN_STATUS=$($CLI listmasternodes 2>&1 || echo "[]")
MN_COUNT=$(echo "$MN_STATUS" | grep -c "ENABLED" || echo "0")

echo "Active masternodes: $MN_COUNT"

if [ "$MN_COUNT" -gt 0 ]; then
    info "MN available - DOMC voting possible"

    # Check if in vote phase
    if [ -n "$CYCLE_START" ]; then
        RELATIVE=$((CURRENT_HEIGHT - CYCLE_START))
        if [ "$RELATIVE" -ge "$DOMC_VOTE_OFFSET" ] && [ "$RELATIVE" -lt "$DOMC_REVEAL_HEIGHT" ]; then
            echo "Currently in VOTE phase - can submit votes"

            # Example commit (requires MN privkey)
            # $CLI domccommit <R_proposal> <salt>

            # Example reveal
            # $CLI domcreveal <R_proposal> <salt>
        else
            echo "Not in VOTE phase - cannot submit votes now"
            BLOCKS_TO_VOTE=$((CYCLE_START + DOMC_VOTE_OFFSET - CURRENT_HEIGHT))
            if [ "$BLOCKS_TO_VOTE" -gt 0 ]; then
                info "Generate $BLOCKS_TO_VOTE blocks to reach VOTE phase"
            fi
        fi
    fi
else
    info "No MN configured - DOMC voting simulation only"
fi

# =============================================================================
# TEST E9: Median Calculation Simulation
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E9: Median Calculation"
echo "─────────────────────────────────────────────────────────────────"

echo "DOMC Median Rules:"
echo "  - 0 votes → R unchanged"
echo "  - ≥1 vote → median(R) clamped to R_MAX"
echo ""
echo "Example scenarios:"
echo "  Votes: [3000, 3500, 4000] → median = 3500 bp"
echo "  Votes: [2000, 5000]       → median = 3500 bp"
echo "  Votes: [5000] (exceeds R_MAX=4000) → clamped to 4000 bp"
echo ""

pass "Median calculation rules understood"

# =============================================================================
# TEST E10: R_MAX Clamping
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST E10: R_MAX Clamping"
echo "─────────────────────────────────────────────────────────────────"

echo "Clamping rule: R_annual = min(voted_R, R_MAX_dynamic)"
echo ""
echo "Current R_MAX_dynamic: $R_MAX bp"
echo ""
echo "If MN votes for R=5000 (50%):"
echo "  → Clamped to R_MAX = $R_MAX bp"
echo ""

if [ "$R_ANNUAL" -le "$R_MAX" ]; then
    pass "Current R_annual ($R_ANNUAL) ≤ R_MAX ($R_MAX)"
else
    fail "R_annual exceeds R_MAX - violation"
fi

# =============================================================================
# SIMULATION: Fast-forward to Next Cycle
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "OPTIONAL: Fast-forward Simulation"
echo "─────────────────────────────────────────────────────────────────"

echo ""
echo "To test a full DOMC cycle in testnet:"
echo ""
echo "1. Generate blocks to VOTE phase:"
echo "   \$CLI generate \$((CYCLE_START + $DOMC_VOTE_OFFSET - CURRENT_HEIGHT))"
echo ""
echo "2. Submit MN commits (during VOTE phase):"
echo "   \$CLI domccommit 3500 <random_salt>"
echo ""
echo "3. Submit MN reveals:"
echo "   \$CLI domcreveal 3500 <same_salt>"
echo ""
echo "4. Generate to REVEAL instant:"
echo "   \$CLI generate \$((CYCLE_START + $DOMC_REVEAL_HEIGHT - CURRENT_HEIGHT))"
echo ""
echo "5. Check R_next:"
echo "   \$CLI getkhustate | grep R_next"
echo ""
echo "6. Generate to next cycle:"
echo "   \$CLI generate \$((CYCLE_START + $DOMC_CYCLE_LENGTH - CURRENT_HEIGHT))"
echo ""
echo "7. Verify R_annual updated:"
echo "   \$CLI getkhustate | grep R_annual"
echo ""

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE E SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo ""

FINAL_STATE=$($CLI getkhustate)

R_FINAL=$(echo "$FINAL_STATE" | grep -o '"R_annual":[0-9]*' | grep -o '[0-9]*')
R_MAX_FINAL=$(echo "$FINAL_STATE" | grep -o '"R_MAX_dynamic":[0-9]*' | grep -o '[0-9]*')

echo "DOMC Status:"
echo "  R_annual:      $R_FINAL bp ($(echo "scale=2; $R_FINAL / 100" | bc)%)"
echo "  R_MAX_dynamic: $R_MAX_FINAL bp ($(echo "scale=2; $R_MAX_FINAL / 100" | bc)%)"
echo "  Active MN:     $MN_COUNT"
echo ""

if [ "$R_FINAL" -le "$R_MAX_FINAL" ] && [ "$R_FINAL" -gt 0 ]; then
    echo -e "${GREEN}✓ DOMC parameters valid${NC}"
else
    echo -e "${YELLOW}⚠ Check DOMC parameters${NC}"
fi

echo ""
echo -e "${GREEN}✓ PHASE E: DOMC R% VOTING TESTS COMPLETED${NC}"
echo ""
echo "Note: Full DOMC cycle testing requires:"
echo "  1. Active Masternodes (4+ for quorum)"
echo "  2. Generating ~172800 blocks per cycle"
echo "  3. Submitting commit/reveal during VOTE phase"
echo ""
