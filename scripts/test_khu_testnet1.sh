#!/bin/bash
#
# TESTNET 1 "KHU Pur" - Script de validation complet
# Conforme au Blueprint 10: Émission ZÉRO, pipeline KHU validé
#
# Tests:
#   T1-MINT     : MINT 100 PIV → 100 KHU
#   T1-REDEEM   : REDEEM 50 KHU → 50 PIV
#   T1-STAKE    : STAKE 25 KHU → 25 ZKHU
#   T1-INVARIANT: Vérifier C == U + Z après chaque opération
#   T1-PERSIST  : Restart daemon, vérifier state identique
#

# Don't exit on error - we handle errors ourselves
# set -e

# Configuration
SRC_DIR="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATA_DIR="/tmp/khu_testnet1"
CLI="$SRC_DIR/pivx-cli -regtest -datadir=$DATA_DIR -rpcuser=test -rpcpassword=test123"
DAEMON="$SRC_DIR/pivxd"

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

TESTS_PASSED=0
TESTS_FAILED=0

print_header() {
    echo ""
    echo "════════════════════════════════════════════════════════════════════"
    echo "  $1"
    echo "════════════════════════════════════════════════════════════════════"
}

print_test() {
    echo -e "${YELLOW}TEST:${NC} $1"
}

pass() {
    echo -e "${GREEN}✓ PASS:${NC} $1"
    ((TESTS_PASSED++))
}

fail() {
    echo -e "${RED}✗ FAIL:${NC} $1"
    ((TESTS_FAILED++))
}

check_invariants() {
    local state=$($CLI getkhustate 2>/dev/null)
    local C=$(echo "$state" | grep -o '"C": [0-9]*' | grep -o '[0-9]*')
    local U=$(echo "$state" | grep -o '"U": [0-9]*' | grep -o '[0-9]*')
    local Z=$(echo "$state" | grep -o '"Z": [0-9]*' | grep -o '[0-9]*')

    local sum=$((U + Z))
    if [ "$C" == "$sum" ]; then
        pass "Invariant C == U + Z: $C == $U + $Z"
        return 0
    else
        fail "Invariant C == U + Z: $C != $U + $Z (sum=$sum)"
        return 1
    fi
}

cleanup() {
    echo "Cleaning up..."
    $CLI stop 2>/dev/null || true
    pkill -9 pivxd 2>/dev/null || true
    sleep 2
    rm -rf "$DATA_DIR"
}

# Cleanup on exit
trap cleanup EXIT

print_header "TESTNET 1: KHU Pur - Validation Pipeline"
echo "Date: $(date)"
echo "Data dir: $DATA_DIR"
echo ""

# Phase 0: Setup
print_header "PHASE 0: Setup"
pkill -9 pivxd 2>/dev/null || true
sleep 2
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

echo "Starting pivxd..."
$DAEMON -regtest -datadir=$DATA_DIR -daemon -debug=khu -rpcuser=test -rpcpassword=test123
sleep 12

BLOCK=$($CLI getblockcount 2>&1)
if [[ "$BLOCK" =~ ^[0-9]+$ ]]; then
    pass "pivxd started, block=$BLOCK"
else
    fail "pivxd failed to start: $BLOCK"
    exit 1
fi

# Phase 1: Generate blocks for V6 activation
print_header "PHASE 1: V6 Activation"
echo "Generating 200 blocks..."
$CLI generate 200 > /dev/null
BLOCK=$($CLI getblockcount)
echo "Block height: $BLOCK"

STATE=$($CLI getkhustate 2>&1)
if echo "$STATE" | grep -q '"C":'; then
    pass "V6 activated, KHU state available"
else
    fail "V6 not activated: $STATE"
    exit 1
fi

# Record initial balance
INITIAL_BALANCE=$($CLI getbalance)
echo "Initial PIV balance: $INITIAL_BALANCE"

# Phase 2: T1-MINT
print_header "PHASE 2: T1-MINT (100 PIV → 100 KHU)"
print_test "Minting 100 KHU..."

MINT_RESULT=$($CLI khumint 100 2>&1)
echo "MINT result: $MINT_RESULT"

$CLI generate 1 > /dev/null
sleep 1

KHU_BAL=$($CLI khubalance 2>&1)
echo "KHU balance: $KHU_BAL"

TRANSPARENT=$(echo "$KHU_BAL" | grep -o '"transparent": [0-9.]*' | grep -o '[0-9.]*' | head -1)
if [ "$TRANSPARENT" == "100" ] || [ "$TRANSPARENT" == "100.00000000" ]; then
    pass "T1-MINT: 100 KHU minted"
else
    fail "T1-MINT: Expected 100 KHU, got $TRANSPARENT"
fi

check_invariants

# Phase 3: T1-REDEEM
print_header "PHASE 3: T1-REDEEM (50 KHU → 50 PIV)"
print_test "Redeeming 50 KHU..."

REDEEM_RESULT=$($CLI khuredeem 50 2>&1)
echo "REDEEM result: $REDEEM_RESULT"

$CLI generate 1 > /dev/null
sleep 1

KHU_BAL=$($CLI khubalance 2>&1)
echo "KHU balance after redeem: $KHU_BAL"

TRANSPARENT=$(echo "$KHU_BAL" | grep -o '"transparent": [0-9.]*' | grep -o '[0-9.]*' | head -1)
if [ "$TRANSPARENT" == "50" ] || [ "$TRANSPARENT" == "50.00000000" ]; then
    pass "T1-REDEEM: 50 KHU remaining after redeem"
else
    fail "T1-REDEEM: Expected 50 KHU remaining, got $TRANSPARENT"
fi

check_invariants

# Phase 4: T1-STAKE
print_header "PHASE 4: T1-STAKE (25 KHU → 25 ZKHU)"
print_test "Staking 25 KHU..."

STAKE_RESULT=$($CLI khustake 25 2>&1)
echo "STAKE result: $STAKE_RESULT"

$CLI generate 1 > /dev/null
sleep 1

KHU_BAL=$($CLI khubalance 2>&1)
echo "KHU balance after stake: $KHU_BAL"

STAKED=$(echo "$KHU_BAL" | grep -o '"staked": [0-9.]*' | grep -o '[0-9.]*' | head -1)
TRANSPARENT=$(echo "$KHU_BAL" | grep -o '"transparent": [0-9.]*' | grep -o '[0-9.]*' | head -1)

if [ "$STAKED" == "25" ] || [ "$STAKED" == "25.00000000" ]; then
    pass "T1-STAKE: 25 ZKHU staked"
else
    fail "T1-STAKE: Expected 25 ZKHU staked, got $STAKED"
fi

if [ "$TRANSPARENT" == "25" ] || [ "$TRANSPARENT" == "25.00000000" ]; then
    pass "T1-STAKE: 25 KHU_T remaining"
else
    fail "T1-STAKE: Expected 25 KHU_T remaining, got $TRANSPARENT"
fi

check_invariants

# Phase 5: Record state before restart
print_header "PHASE 5: T1-PERSIST - Record State"
STATE_BEFORE=$($CLI getkhustate 2>&1)
echo "State BEFORE restart:"
echo "$STATE_BEFORE" | grep -E '"C"|"U"|"Z"'

C_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"C": [0-9]*' | grep -o '[0-9]*')
U_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"U": [0-9]*' | grep -o '[0-9]*')
Z_BEFORE=$(echo "$STATE_BEFORE" | grep -o '"Z": [0-9]*' | grep -o '[0-9]*')

KHU_BAL_BEFORE=$($CLI khubalance 2>&1)
BLOCK_BEFORE=$($CLI getblockcount)

# Phase 6: Restart
print_header "PHASE 6: T1-PERSIST - Restart Daemon"
echo "Stopping pivxd..."
$CLI stop
sleep 5

echo "Starting pivxd..."
$DAEMON -regtest -datadir=$DATA_DIR -daemon -debug=khu -rpcuser=test -rpcpassword=test123
sleep 15

BLOCK=$($CLI getblockcount 2>&1)
if [[ "$BLOCK" =~ ^[0-9]+$ ]]; then
    pass "pivxd restarted, block=$BLOCK"
else
    fail "pivxd failed to restart: $BLOCK"
    exit 1
fi

# Phase 7: Verify state after restart
print_header "PHASE 7: T1-PERSIST - Verify State"
STATE_AFTER=$($CLI getkhustate 2>&1)
echo "State AFTER restart:"
echo "$STATE_AFTER" | grep -E '"C"|"U"|"Z"'

C_AFTER=$(echo "$STATE_AFTER" | grep -o '"C": [0-9]*' | grep -o '[0-9]*')
U_AFTER=$(echo "$STATE_AFTER" | grep -o '"U": [0-9]*' | grep -o '[0-9]*')
Z_AFTER=$(echo "$STATE_AFTER" | grep -o '"Z": [0-9]*' | grep -o '[0-9]*')

if [ "$C_BEFORE" == "$C_AFTER" ]; then
    pass "T1-PERSIST: C unchanged ($C_BEFORE)"
else
    fail "T1-PERSIST: C changed ($C_BEFORE → $C_AFTER)"
fi

if [ "$U_BEFORE" == "$U_AFTER" ]; then
    pass "T1-PERSIST: U unchanged ($U_BEFORE)"
else
    fail "T1-PERSIST: U changed ($U_BEFORE → $U_AFTER)"
fi

if [ "$Z_BEFORE" == "$Z_AFTER" ]; then
    pass "T1-PERSIST: Z unchanged ($Z_BEFORE)"
else
    fail "T1-PERSIST: Z changed ($Z_BEFORE → $Z_AFTER)"
fi

# Verify KHU balance after restart
KHU_BAL_AFTER=$($CLI khubalance 2>&1)
echo ""
echo "KHU Balance after restart:"
echo "$KHU_BAL_AFTER"

STAKED_AFTER=$(echo "$KHU_BAL_AFTER" | grep -o '"staked": [0-9.]*' | grep -o '[0-9.]*' | head -1)
if [ "$STAKED_AFTER" == "25" ] || [ "$STAKED_AFTER" == "25.00000000" ]; then
    pass "T1-PERSIST: ZKHU notes preserved (25 staked)"
else
    fail "T1-PERSIST: ZKHU notes lost (expected 25, got $STAKED_AFTER)"
fi

check_invariants

# Final Summary
print_header "SUMMARY"
echo ""
echo "Tests passed: $TESTS_PASSED"
echo "Tests failed: $TESTS_FAILED"
echo ""

if [ $TESTS_FAILED -eq 0 ]; then
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  ALL TESTS PASSED - TESTNET 1 READY FOR DEPLOYMENT                ${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════════${NC}"
    exit 0
else
    echo -e "${RED}═══════════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}  SOME TESTS FAILED - FIX BEFORE TESTNET DEPLOYMENT                ${NC}"
    echo -e "${RED}═══════════════════════════════════════════════════════════════════${NC}"
    exit 1
fi
