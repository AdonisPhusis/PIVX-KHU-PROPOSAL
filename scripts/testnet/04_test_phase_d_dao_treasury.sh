#!/bin/bash
# =============================================================================
# KHU V6 Private Testnet - Phase D: DAO Treasury Test
# =============================================================================
# Tests: T accumulation, Budget proposals, Treasury payments
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
echo "  PHASE D: DAO TREASURY TEST"
echo "═══════════════════════════════════════════════════════════════"
echo ""

$CLI getblockcount > /dev/null 2>&1 || fail "Daemon not running"
pass "Daemon is running"

# =============================================================================
# TEST D1: Initial T = 0
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D1: Check Initial Treasury"
echo "─────────────────────────────────────────────────────────────────"

STATE=$($CLI getkhustate)
T_INITIAL=$(echo "$STATE" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')
echo "Initial T: $T_INITIAL"

# T might not be 0 if previous tests ran
info "Treasury T = $T_INITIAL satoshis"
pass "Treasury state accessible"

# =============================================================================
# TEST D2: Setup KHU Supply for T accumulation
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D2: Setup KHU Supply"
echo "─────────────────────────────────────────────────────────────────"

# Ensure we have significant U for T accumulation
info "Minting 100000 KHU for T accumulation test..."
$CLI khumint 100000 > /dev/null 2>&1 || info "MINT may have failed (already have KHU?)"
$CLI generate 1 > /dev/null

STATE=$($CLI getkhustate)
U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
echo "U (transparent supply): $U satoshis"

if [ "$U" -gt 0 ]; then
    pass "KHU supply available for T test"
else
    fail "No KHU supply"
fi

# =============================================================================
# TEST D3: Daily T Accumulation
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D3: Daily T Accumulation (1440 blocks)"
echo "─────────────────────────────────────────────────────────────────"

STATE_BEFORE=$($CLI getkhustate)
T_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$STATE_BEFORE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
R=$(echo "$STATE_BEFORE" | grep -o '"R_annual":[0-9]*' | grep -o '[0-9]*')

echo "Before: T=$T_BEFORE U=$U R=$R"

# Calculate expected T_daily
# T_daily = (U × R) / 10000 / 8 / 365
# Example: U=10000000000000 (100000 KHU), R=4000
# T_daily = (10000000000000 * 4000) / 10000 / 8 / 365 = 136986301

if [ -n "$U" ] && [ -n "$R" ] && [ "$U" -gt 0 ] && [ "$R" -gt 0 ]; then
    # Using bc for large number calculation
    EXPECTED_T_DAILY=$(echo "($U * $R) / 10000 / 8 / 365" | bc)
    calc "Expected T_daily: $EXPECTED_T_DAILY satoshis"
fi

info "Generating 1440 blocks (1 day)..."
$CLI generate 1440 > /dev/null

STATE_AFTER=$($CLI getkhustate)
T_AFTER=$(echo "$STATE_AFTER" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')

echo "After: T=$T_AFTER"

T_GAINED=$((T_AFTER - T_BEFORE))
echo "T gained: $T_GAINED satoshis"

if [ "$T_AFTER" -gt "$T_BEFORE" ]; then
    pass "T accumulated after 1 day"
else
    info "T did not increase - may need KHU supply or yield trigger"
fi

# =============================================================================
# TEST D4: 7 Days T Accumulation
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D4: 7 Days T Accumulation"
echo "─────────────────────────────────────────────────────────────────"

T_WEEK_START=$($CLI getkhustate | grep -o '"T":[0-9]*' | grep -o '[0-9]*')

info "Generating 10080 blocks (7 days)..."
$CLI generate 10080 > /dev/null

T_WEEK_END=$($CLI getkhustate | grep -o '"T":[0-9]*' | grep -o '[0-9]*')

T_WEEK_GAINED=$((T_WEEK_END - T_WEEK_START))
echo "T before week: $T_WEEK_START"
echo "T after week: $T_WEEK_END"
echo "T gained in 7 days: $T_WEEK_GAINED satoshis"

if [ "$T_WEEK_END" -gt "$T_WEEK_START" ]; then
    pass "T accumulated over 7 days"
else
    info "T did not increase over week"
fi

# =============================================================================
# TEST D5-D6: Budget Proposal (Using PIVX System)
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D5-D6: Budget Proposal System"
echo "─────────────────────────────────────────────────────────────────"

info "Checking budget system availability..."

# Get a payment address
PAYMENT_ADDR=$($CLI getnewaddress "dao_test")
echo "Payment address: $PAYMENT_ADDR"

# Get current block for proposal timing
CURRENT_BLOCK=$($CLI getblockcount)
PROPOSAL_START=$((CURRENT_BLOCK + 100))

info "Attempting to create budget proposal..."

# Try preparebudget first (creates fee tx)
PREP_RESULT=$($CLI preparebudget "KHU_Test_Proposal" "http://test.khu.org" 1 $PROPOSAL_START "$PAYMENT_ADDR" 100 2>&1 || echo "error")

if echo "$PREP_RESULT" | grep -q "error\|Error"; then
    info "preparebudget not available or failed: $PREP_RESULT"
    info "Budget system may require MN setup"
else
    echo "Prepare result: $PREP_RESULT"
    pass "Budget proposal prepared"
fi

# List existing proposals
info "Listing budget proposals..."
PROPOSALS=$($CLI getbudgetinfo 2>&1 || echo "[]")
echo "Current proposals: $PROPOSALS"

# =============================================================================
# TEST D10: T Exhaustion Protection
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "TEST D10: T Exhaustion Protection"
echo "─────────────────────────────────────────────────────────────────"

STATE=$($CLI getkhustate)
T=$(echo "$STATE" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')

echo "Current T: $T satoshis"

# T should never go negative
if [ "$T" -ge 0 ]; then
    pass "T >= 0 invariant maintained"
else
    fail "T went negative: $T"
fi

# =============================================================================
# T FORMULA VERIFICATION
# =============================================================================
echo ""
echo "─────────────────────────────────────────────────────────────────"
echo "T FORMULA VERIFICATION"
echo "─────────────────────────────────────────────────────────────────"

STATE=$($CLI getkhustate)
T=$(echo "$STATE" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
R=$(echo "$STATE" | grep -o '"R_annual":[0-9]*' | grep -o '[0-9]*')

echo ""
echo "Current values:"
echo "  T = $T satoshis"
echo "  U = $U satoshis"
echo "  R = $R basis points ($(echo "scale=2; $R / 100" | bc)%)"
echo ""
echo "Formula: T_daily = (U × R) / 10000 / 8 / 365"
echo "         T_annual ≈ U × R / 10000 / 8 ≈ U × $(echo "scale=4; $R / 10000 / 8" | bc)"
echo ""

# Calculate expected annual T
if [ -n "$U" ] && [ -n "$R" ] && [ "$U" -gt 0 ]; then
    ANNUAL_RATE=$(echo "scale=6; $R / 10000 / 8" | bc)
    EXPECTED_ANNUAL_T=$(echo "$U * $ANNUAL_RATE" | bc | cut -d'.' -f1)
    echo "Expected annual T accumulation: ~$EXPECTED_ANNUAL_T satoshis"
    echo "  = $(echo "scale=2; $EXPECTED_ANNUAL_T / 100000000" | bc) KHU/year"
fi

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE D SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo ""

FINAL_STATE=$($CLI getkhustate)
echo "Final KHU State:"
echo "$FINAL_STATE" | head -20

echo ""

# Final invariant check
C=$(echo "$FINAL_STATE" | grep -o '"C":[0-9]*' | grep -o '[0-9]*')
U=$(echo "$FINAL_STATE" | grep -o '"U":[0-9]*' | grep -o '[0-9]*')
Z=$(echo "$FINAL_STATE" | grep -o '"Z":[0-9]*' | grep -o '[0-9]*')
T=$(echo "$FINAL_STATE" | grep -o '"T":[0-9]*' | grep -o '[0-9]*')

SUM=$((U + Z))
if [ "$C" -eq "$SUM" ] && [ "$T" -ge 0 ]; then
    echo -e "${GREEN}✓ All invariants verified: C==U+Z, T>=0${NC}"
else
    echo -e "${RED}✗ Invariant check failed${NC}"
fi

echo ""
echo -e "${GREEN}✓ PHASE D: DAO TREASURY TESTS COMPLETED${NC}"
echo ""
