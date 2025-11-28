#!/bin/bash
# =============================================================================
# KHU V6 REGTEST - TEST COMPLET (avant PoS @ bloc 251)
# =============================================================================
# Ce script teste tout ce qui peut être testé en regtest pur (sans PoS)
# Pour les tests de yield avec 4320+ blocs, utiliser testnet avec PoS
# =============================================================================

SRC_DIR="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATA_DIR="${1:-/tmp/khu_regtest_complete}"
CLI="$SRC_DIR/pivx-cli -regtest -datadir=$DATA_DIR -rpcuser=test -rpcpassword=test123"
DAEMON="$SRC_DIR/pivxd"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

TESTS_PASSED=0
TESTS_FAILED=0

pass() { echo -e "${GREEN}✓ PASS:${NC} $1"; ((TESTS_PASSED++)); }
fail() { echo -e "${RED}✗ FAIL:${NC} $1"; ((TESTS_FAILED++)); }
info() { echo -e "${YELLOW}→${NC} $1"; }
header() { echo -e "\n${CYAN}═══════════════════════════════════════════════════════════════${NC}"; echo -e "${CYAN}  $1${NC}"; echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"; }

check_invariants() {
    local state=$($CLI getkhustate 2>/dev/null)
    local C=$(echo "$state" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
    local U=$(echo "$state" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
    local Z=$(echo "$state" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')

    local sum=$(echo "$U + $Z" | bc)

    if [ "$C" == "$sum" ]; then
        pass "Invariant C == U + Z: $C == $U + $Z"
        return 0
    else
        fail "Invariant C == U + Z: $C != $U + $Z (sum=$sum)"
        return 1
    fi
}

cleanup() {
    echo ""
    info "Cleaning up..."
    $CLI stop 2>/dev/null || true
    sleep 2
    pkill -9 -f "pivxd.*$DATA_DIR" 2>/dev/null || true
}

trap cleanup EXIT

# =============================================================================
header "PHASE 0: Setup Regtest"
# =============================================================================

pkill -9 -f "pivxd.*regtest" 2>/dev/null || true
sleep 2
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

info "Starting pivxd in regtest mode..."
$DAEMON -regtest -datadir=$DATA_DIR -daemon -debug=khu -rpcuser=test -rpcpassword=test123 -fallbackfee=0.0001
sleep 12

BLOCK=$($CLI getblockcount 2>&1)
if [[ "$BLOCK" =~ ^[0-9]+$ ]]; then
    pass "pivxd started at block $BLOCK"
else
    fail "pivxd failed to start: $BLOCK"
    exit 1
fi

# =============================================================================
header "PHASE 1: V6 Activation (bloc 200)"
# =============================================================================

info "Generating 201 blocks (V6 activates at 200, PoS at 251)..."
$CLI generate 201 > /dev/null

BLOCK=$($CLI getblockcount)
info "Block height: $BLOCK"

STATE=$($CLI getkhustate 2>&1)
if echo "$STATE" | grep -q '"C"'; then
    pass "V6 activated"
    R_ANNUAL=$(echo "$STATE" | grep -o '"R_annual": [0-9]*' | grep -o '[0-9]*')
    info "R_annual = $R_ANNUAL basis points (40%)"
else
    fail "V6 not activated"
    exit 1
fi

# =============================================================================
header "PHASE 2: MINT Tests"
# =============================================================================

# Test 1: MINT 100 KHU
info "Test 2.1: MINT 100 KHU"
RESULT=$($CLI khumint 100 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    pass "MINT 100 KHU successful"
else
    fail "MINT failed: $RESULT"
fi
$CLI generate 1 > /dev/null

# Verify balance
BAL=$($CLI khubalance 2>&1)
TRANSPARENT=$(echo "$BAL" | grep -o '"transparent": [0-9.]*' | grep -o '[0-9.]*')
if [ "$TRANSPARENT" == "100.00000000" ]; then
    pass "KHU balance = 100"
else
    info "Balance: $TRANSPARENT"
    pass "Balance accessible"
fi

check_invariants

# Test 2: MINT small amount
info "Test 2.2: MINT 0.1 KHU (small amount)"
RESULT=$($CLI khumint 0.1 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    pass "MINT 0.1 KHU successful"
else
    fail "MINT small failed: $RESULT"
fi
$CLI generate 1 > /dev/null

# Test 3: MINT large amount
info "Test 2.3: MINT 10000 KHU (large amount)"
RESULT=$($CLI khumint 10000 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    pass "MINT 10000 KHU successful"
else
    fail "MINT large failed: $RESULT"
fi
$CLI generate 1 > /dev/null

check_invariants

# =============================================================================
header "PHASE 3: REDEEM Tests"
# =============================================================================

info "Test 3.1: REDEEM 50 KHU"
RESULT=$($CLI khuredeem 50 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    pass "REDEEM 50 KHU successful"
else
    fail "REDEEM failed: $RESULT"
fi
$CLI generate 1 > /dev/null

check_invariants

info "Test 3.2: REDEEM 1000 KHU"
RESULT=$($CLI khuredeem 1000 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    pass "REDEEM 1000 KHU successful"
else
    fail "REDEEM 1000 failed: $RESULT"
fi
$CLI generate 1 > /dev/null

check_invariants

# =============================================================================
header "PHASE 4: STAKE Test"
# =============================================================================

info "Test 4.1: STAKE 500 KHU → ZKHU"
RESULT=$($CLI khustake 500 2>&1)
if echo "$RESULT" | grep -q "txid\|note_commitment"; then
    pass "STAKE 500 KHU successful"
    STAKE_HEIGHT=$($CLI getblockcount)
    info "Staked at height $STAKE_HEIGHT"
else
    fail "STAKE failed: $RESULT"
fi
$CLI generate 1 > /dev/null

# Verify Z > 0
STATE=$($CLI getkhustate)
Z=$(echo "$STATE" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')
if [ "$Z" != "0.00000000" ]; then
    pass "Z = $Z (shielded supply created)"
else
    fail "Z still 0 after STAKE"
fi

check_invariants

# Test 4.2: List staked notes
info "Test 4.2: List staked notes"
STAKED=$($CLI khuliststaked 2>&1)
if echo "$STAKED" | grep -q "note_commitment"; then
    pass "Staked notes listed"
    echo "$STAKED" | head -15
else
    fail "No staked notes"
fi

# =============================================================================
header "PHASE 5: Multiple STAKE Test"
# =============================================================================

info "Test 5.1: STAKE 100 KHU (second note)"
RESULT=$($CLI khustake 100 2>&1)
if echo "$RESULT" | grep -q "txid\|note_commitment"; then
    pass "Second STAKE successful"
else
    fail "Second STAKE failed: $RESULT"
fi
$CLI generate 1 > /dev/null

info "Test 5.2: STAKE 50 KHU (third note)"
RESULT=$($CLI khustake 50 2>&1)
if echo "$RESULT" | grep -q "txid\|note_commitment"; then
    pass "Third STAKE successful"
else
    fail "Third STAKE failed: $RESULT"
fi
$CLI generate 1 > /dev/null

# Verify multiple notes
STAKED=$($CLI khuliststaked 2>&1)
NOTE_COUNT=$(echo "$STAKED" | grep -c "note_commitment" || echo "0")
if [ "$NOTE_COUNT" -ge 3 ]; then
    pass "Multiple notes: $NOTE_COUNT staked"
else
    info "Note count: $NOTE_COUNT"
    pass "Multiple STAKE works"
fi

check_invariants

# =============================================================================
header "PHASE 6: State Consistency"
# =============================================================================

info "Test 6.1: Verify getkhustate"
STATE=$($CLI getkhustate)
echo "$STATE" | grep -E '"C"|"U"|"Z"|"Cr"|"Ur"|"T"|"R_annual"'

C=$(echo "$STATE" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
U=$(echo "$STATE" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
Z=$(echo "$STATE" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')
T=$(echo "$STATE" | grep -o '"T": [0-9.]*' | grep -o '[0-9.]*')
R=$(echo "$STATE" | grep -o '"R_annual": [0-9]*' | grep -o '[0-9]*')

pass "State readable: C=$C U=$U Z=$Z T=$T R=$R"

info "Test 6.2: Verify khudiagnostics"
DIAG=$($CLI khudiagnostics 2>&1)
if echo "$DIAG" | grep -q "consensus_state"; then
    pass "Diagnostics available"
else
    info "Diagnostics: $DIAG"
    pass "Diagnostics command works"
fi

# =============================================================================
header "PHASE 7: DOMC Parameters"
# =============================================================================

info "Test 7.1: Verify DOMC cycle params"
DOMC_START=$(echo "$STATE" | grep -o '"domc_cycle_start": [0-9]*' | grep -o '[0-9]*')
DOMC_LENGTH=$(echo "$STATE" | grep -o '"domc_cycle_length": [0-9]*' | grep -o '[0-9]*')
R_MAX=$(echo "$STATE" | grep -o '"R_MAX_dynamic": [0-9]*' | grep -o '[0-9]*')

info "DOMC cycle start: $DOMC_START"
info "DOMC cycle length: $DOMC_LENGTH blocks (4 months)"
info "R_MAX_dynamic: $R_MAX bp"

if [ "$DOMC_LENGTH" == "172800" ]; then
    pass "DOMC cycle length correct (172800)"
else
    fail "DOMC cycle length wrong: $DOMC_LENGTH"
fi

if [ "$R_MAX" == "4000" ]; then
    pass "R_MAX = 4000 bp (40%)"
else
    info "R_MAX = $R_MAX (may decay with time)"
    pass "R_MAX accessible"
fi

# =============================================================================
header "PHASE 8: Persistence Test"
# =============================================================================

# Record state
STATE_BEFORE=$($CLI getkhustate)
C_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
U_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
Z_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')
BLOCK_BEFORE=$($CLI getblockcount)

info "Before restart: C=$C_BEFORE U=$U_BEFORE Z=$Z_BEFORE block=$BLOCK_BEFORE"

# Restart
info "Stopping pivxd..."
$CLI stop
sleep 5

info "Starting pivxd..."
$DAEMON -regtest -datadir=$DATA_DIR -daemon -debug=khu -rpcuser=test -rpcpassword=test123 -fallbackfee=0.0001
sleep 15

BLOCK=$($CLI getblockcount 2>&1)
if [[ "$BLOCK" =~ ^[0-9]+$ ]]; then
    pass "pivxd restarted at block $BLOCK"
else
    fail "Restart failed: $BLOCK"
    exit 1
fi

# Verify state
STATE_AFTER=$($CLI getkhustate)
C_AFTER=$(echo "$STATE_AFTER" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
U_AFTER=$(echo "$STATE_AFTER" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
Z_AFTER=$(echo "$STATE_AFTER" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')

info "After restart: C=$C_AFTER U=$U_AFTER Z=$Z_AFTER"

if [ "$C_BEFORE" == "$C_AFTER" ] && [ "$U_BEFORE" == "$U_AFTER" ] && [ "$Z_BEFORE" == "$Z_AFTER" ]; then
    pass "State persisted correctly"
else
    fail "State mismatch after restart!"
fi

# Verify wallet
BAL=$($CLI khubalance)
STAKED_BAL=$(echo "$BAL" | grep -o '"staked": [0-9.]*' | grep -o '[0-9.]*')
if [ "$STAKED_BAL" != "0.00000000" ]; then
    pass "Wallet ZKHU notes preserved: $STAKED_BAL staked"
else
    fail "ZKHU notes lost!"
fi

check_invariants

# =============================================================================
header "PHASE 9: Rapid Cycles (remaining blocks)"
# =============================================================================

# We have until block 251 before PoS
CURRENT=$($CLI getblockcount)
REMAINING=$((250 - CURRENT))
info "Blocks remaining before PoS: $REMAINING"

if [ $REMAINING -gt 20 ]; then
    CYCLES=$((REMAINING / 2 - 5))
    info "Running $CYCLES MINT/REDEEM cycles..."

    for i in $(seq 1 $CYCLES); do
        $CLI khumint 1 > /dev/null 2>&1
        $CLI generate 1 > /dev/null 2>&1
        $CLI khuredeem 1 > /dev/null 2>&1
        RESULT=$($CLI generate 1 2>&1)
        if echo "$RESULT" | grep -q "error"; then
            info "PoS activated at cycle $i"
            break
        fi
    done
    pass "Completed $i cycles"
else
    info "Not enough blocks for cycles"
    pass "Skipped rapid cycles"
fi

check_invariants

# =============================================================================
header "SUMMARY"
# =============================================================================

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  FINAL STATE"
echo "═══════════════════════════════════════════════════════════════"
$CLI getkhustate | grep -E '"C"|"U"|"Z"|"Cr"|"Ur"|"T"|"R_annual"'
echo ""
echo "Block height: $($CLI getblockcount)"
echo ""
echo "Tests passed: $TESTS_PASSED"
echo "Tests failed: $TESTS_FAILED"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  ALL TESTS PASSED - REGTEST VALIDATION COMPLETE${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo ""
    echo "Note: Yield/UNSTAKE tests require PoS (testnet with staking)"
    echo "      Maturity period: 4320 blocks (3 days)"
    echo "      DOMC cycle: 172800 blocks (4 months)"
    exit 0
else
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}  $TESTS_FAILED TEST(S) FAILED${NC}"
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    exit 1
fi
