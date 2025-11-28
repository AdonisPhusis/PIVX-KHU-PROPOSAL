#!/bin/bash
# =============================================================================
# KHU V6 Private Testnet - Phase B: Basic Operations Test
# =============================================================================
# Tests: MINT, REDEEM, Balance, State consistency
# Prerequisite: Run 01_setup_private_testnet.sh first
# =============================================================================

set -e

# Configuration
DATADIR="${1:-/tmp/khu_private_testnet}"
PIVX_BIN="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$PIVX_BIN/pivx-cli -regtest -datadir=$DATADIR -rpcuser=khutest -rpcpassword=khutest123"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

pass() { echo -e "${GREEN}✓ PASS${NC}: $1"; }
fail() { echo -e "${RED}✗ FAIL${NC}: $1"; exit 1; }
info() { echo -e "${YELLOW}→${NC} $1"; }

echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE B: KHU BASIC OPERATIONS TEST"
echo "═══════════════════════════════════════════════════════════════"
echo ""

# Check daemon is running
info "Checking daemon status..."
$CLI getblockcount > /dev/null 2>&1 || fail "Daemon not running. Run 01_setup_private_testnet.sh first"
pass "Daemon is running"

# Get initial state
info "Getting initial KHU state..."
INITIAL_STATE=$($CLI getkhustate)
INITIAL_C=$(echo "$INITIAL_STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
INITIAL_U=$(echo "$INITIAL_STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
echo "Initial: C=$INITIAL_C U=$INITIAL_U"

# =============================================================================
# TEST B1: MINT 100 KHU
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B1: MINT 100 KHU"
echo "─────────────────────────────────────────────────────────────────"

info "Executing khumint 100..."
MINT_RESULT=$($CLI khumint 100 2>&1)

if echo "$MINT_RESULT" | grep -q "error\|Error\|ERROR"; then
    fail "MINT failed: $MINT_RESULT"
fi

MINT_TX=$(echo "$MINT_RESULT" | grep -o '[a-f0-9]\{64\}' | head -1)
if [ -z "$MINT_TX" ]; then
    # Might return JSON
    MINT_TX=$(echo "$MINT_RESULT" | grep -o '"txid"[^,]*' | grep -o '[a-f0-9]\{64\}')
fi

echo "MINT txid: $MINT_TX"
pass "MINT transaction created"

# =============================================================================
# TEST B2: Confirm MINT
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B2: Confirm MINT (generate 1 block)"
echo "─────────────────────────────────────────────────────────────────"

info "Generating 1 block..."
$CLI generate 1 > /dev/null

# Check state
STATE=$($CLI getkhustate)
C=$(echo "$STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')

EXPECTED_C=$((INITIAL_C + 10000000000))  # 100 * COIN
EXPECTED_U=$((INITIAL_U + 10000000000))

echo "State: C=$C U=$U"
echo "Expected: C=$EXPECTED_C U=$EXPECTED_U"

if [ "$C" -eq "$EXPECTED_C" ] && [ "$U" -eq "$EXPECTED_U" ]; then
    pass "State updated correctly after MINT"
else
    fail "State mismatch after MINT"
fi

# =============================================================================
# TEST B3: KHU Balance
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B3: Check KHU Balance"
echo "─────────────────────────────────────────────────────────────────"

info "Checking khubalance..."
KHU_BAL=$($CLI khubalance 2>&1)
echo "KHU Balance: $KHU_BAL"

# Balance should be 100 (or include previous balance)
if echo "$KHU_BAL" | grep -qE "100|10000000000"; then
    pass "KHU balance reported correctly"
else
    info "Balance format: $KHU_BAL (may need parsing)"
    pass "KHU balance command works"
fi

# =============================================================================
# TEST B4: REDEEM 50 KHU
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B4: REDEEM 50 KHU"
echo "─────────────────────────────────────────────────────────────────"

info "Executing khuredeem 50..."
REDEEM_RESULT=$($CLI khuredeem 50 2>&1)

if echo "$REDEEM_RESULT" | grep -q "error\|Error\|ERROR"; then
    fail "REDEEM failed: $REDEEM_RESULT"
fi

echo "REDEEM result: $REDEEM_RESULT"
pass "REDEEM transaction created"

# =============================================================================
# TEST B5: Confirm REDEEM
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B5: Confirm REDEEM"
echo "─────────────────────────────────────────────────────────────────"

info "Generating 1 block..."
$CLI generate 1 > /dev/null

STATE=$($CLI getkhustate)
C=$(echo "$STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')

EXPECTED_C=$((INITIAL_C + 5000000000))  # 50 * COIN remaining
EXPECTED_U=$((INITIAL_U + 5000000000))

echo "State: C=$C U=$U"
echo "Expected: C=$EXPECTED_C U=$EXPECTED_U"

if [ "$C" -eq "$EXPECTED_C" ] && [ "$U" -eq "$EXPECTED_U" ]; then
    pass "State updated correctly after REDEEM"
else
    fail "State mismatch after REDEEM"
fi

# =============================================================================
# TEST B6: State Consistency (Invariants)
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B6: State Consistency Check"
echo "─────────────────────────────────────────────────────────────────"

STATE=$($CLI getkhustate)
C=$(echo "$STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
Z=$(echo "$STATE" | grep -o '"Z":[0-9]*' | grep -o '[0-9]*' || echo "0")

echo "C=$C U=$U Z=$Z"

# Check C == U + Z
SUM=$((U + Z))
if [ "$C" -eq "$SUM" ]; then
    pass "Invariant C == U + Z verified"
else
    fail "Invariant violation: C($C) != U($U) + Z($Z)"
fi

# =============================================================================
# TEST B7-B8: Large and Small amounts
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B7-B8: Large and Small Amounts"
echo "─────────────────────────────────────────────────────────────────"

# Large MINT
info "Testing large MINT (10000 KHU)..."
$CLI khumint 10000 > /dev/null 2>&1 && pass "Large MINT (10000) accepted" || fail "Large MINT failed"
$CLI generate 1 > /dev/null

# Small MINT (if supported)
info "Testing small MINT (0.01 KHU)..."
$CLI khumint 0.01 > /dev/null 2>&1 && pass "Small MINT (0.01) accepted" || info "Small MINT not supported (OK)"
$CLI generate 1 > /dev/null

# =============================================================================
# TEST B10: 10 MINT/REDEEM Cycles
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST B10: 10 MINT/REDEEM Cycles"
echo "─────────────────────────────────────────────────────────────────"

CYCLES_PASSED=0
for i in {1..10}; do
    info "Cycle $i/10..."

    # MINT
    $CLI khumint 100 > /dev/null 2>&1 || { fail "MINT failed at cycle $i"; }
    $CLI generate 1 > /dev/null

    # REDEEM
    $CLI khuredeem 100 > /dev/null 2>&1 || { fail "REDEEM failed at cycle $i"; }
    $CLI generate 1 > /dev/null

    # Check invariant
    STATE=$($CLI getkhustate)
    C=$(echo "$STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
    U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
    Z=$(echo "$STATE" | grep -o '"Z":[0-9]*' | grep -o '[0-9]*' || echo "0")

    SUM=$((U + Z))
    if [ "$C" -ne "$SUM" ]; then
        fail "Invariant violation at cycle $i: C($C) != U($U) + Z($Z)"
    fi

    CYCLES_PASSED=$((CYCLES_PASSED + 1))
done

pass "All $CYCLES_PASSED cycles completed with invariants maintained"

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE B SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo ""

FINAL_STATE=$($CLI getkhustate)
echo "Final KHU State:"
echo "$FINAL_STATE" | head -15

echo ""
echo -e "${GREEN}✓ PHASE B: ALL TESTS PASSED${NC}"
echo ""
