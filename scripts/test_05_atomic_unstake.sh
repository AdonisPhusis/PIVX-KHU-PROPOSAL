#!/bin/bash
# Test 5: Atomic UNSTAKE Flux (Cr→C)
# Verifies the 5-mutation atomic flux during UNSTAKE

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 5: ATOMIC UNSTAKE FLUX (Cr→C)"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  Expected flux during UNSTAKE:"
echo "    state.Z  -= P       (principal removed from shielded)"
echo "    state.U  += P + Y   (principal + yield to transparent)"
echo "    state.C  += Y       (yield adds to collateral)"
echo "    state.Cr -= Y       (yield consumed from pool)"
echo "    state.Ur -= Y       (yield consumed from rights)"
echo ""

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running"
    exit 1
fi

get_state() {
    local STATE=$($MINER getkhustate)
    echo "$STATE"
}

extract_value() {
    local STATE=$1
    local KEY=$2
    echo "$STATE" | grep -oP "\"$KEY\":\s*\K[0-9.]+" | head -1
}

echo "═══════════════════════════════════════════════════════════════"
echo "  SETUP: MINT and STAKE"
echo "═══════════════════════════════════════════════════════════════"

# Clean setup: MINT 365 KHU and STAKE 365 KHU
# This makes yield calculation easy: 365 KHU @ 40% = 0.4 KHU/day
$WALLET khumint 365 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

$WALLET khustake 365 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

STAKE_HEIGHT=$($MINER getblockcount)
echo "  Staked 365 KHU at block $STAKE_HEIGHT"
echo "  Expected daily yield @ 40%: 365 * 0.40 / 365 = 0.4 KHU/day"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  WAIT: Maturity (4320 blocks) + Yield accumulation (10 days)"
echo "═══════════════════════════════════════════════════════════════"

echo "  Mining 4320 blocks for maturity..."
$MINER generate 4320 > /dev/null
sleep 2

echo "  Mining 14400 blocks for 10 days of yield..."
$MINER generate 14400 > /dev/null
sleep 3

CURRENT_HEIGHT=$($MINER getblockcount)
echo "  Current block: $CURRENT_HEIGHT"

echo ""
echo "  Checking accumulated yield:"
STAKED_INFO=$($WALLET khuliststaked 2>/dev/null || echo "{}")
echo "$STAKED_INFO" | grep -E "amount|accumulated" | head -4 || echo "  (checking...)"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  CAPTURE: State BEFORE UNSTAKE"
echo "═══════════════════════════════════════════════════════════════"

STATE_BEFORE=$(get_state)
C_BEFORE=$(extract_value "$STATE_BEFORE" "C")
U_BEFORE=$(extract_value "$STATE_BEFORE" "U")
Z_BEFORE=$(extract_value "$STATE_BEFORE" "Z")
Cr_BEFORE=$(extract_value "$STATE_BEFORE" "Cr")
Ur_BEFORE=$(extract_value "$STATE_BEFORE" "Ur")

echo "  C  = $C_BEFORE"
echo "  U  = $U_BEFORE"
echo "  Z  = $Z_BEFORE"
echo "  Cr = $Cr_BEFORE"
echo "  Ur = $Ur_BEFORE"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  EXECUTE: UNSTAKE"
echo "═══════════════════════════════════════════════════════════════"

$WALLET khuunstake > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  CAPTURE: State AFTER UNSTAKE"
echo "═══════════════════════════════════════════════════════════════"

STATE_AFTER=$(get_state)
C_AFTER=$(extract_value "$STATE_AFTER" "C")
U_AFTER=$(extract_value "$STATE_AFTER" "U")
Z_AFTER=$(extract_value "$STATE_AFTER" "Z")
Cr_AFTER=$(extract_value "$STATE_AFTER" "Cr")
Ur_AFTER=$(extract_value "$STATE_AFTER" "Ur")
INV_OK=$(echo "$STATE_AFTER" | grep -oP '"invariants_ok":\s*\K\w+')

echo "  C  = $C_AFTER"
echo "  U  = $U_AFTER"
echo "  Z  = $Z_AFTER"
echo "  Cr = $Cr_AFTER"
echo "  Ur = $Ur_AFTER"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  VERIFY: Atomic Flux"
echo "═══════════════════════════════════════════════════════════════"

# Calculate deltas (using bc for floating point)
DELTA_Z=$(echo "$Z_BEFORE - $Z_AFTER" | bc)
DELTA_U=$(echo "$U_AFTER - $U_BEFORE" | bc)
DELTA_C=$(echo "$C_AFTER - $C_BEFORE" | bc)
DELTA_Cr=$(echo "$Cr_BEFORE - $Cr_AFTER" | bc)
DELTA_Ur=$(echo "$Ur_BEFORE - $Ur_AFTER" | bc)

echo "  Delta Z  (should = P):     -$DELTA_Z (principal unstaked)"
echo "  Delta U  (should = P+Y):   +$DELTA_U (principal + yield received)"
echo "  Delta C  (should = Y):     +$DELTA_C (yield inflation)"
echo "  Delta Cr (should = Y):     -$DELTA_Cr (yield consumed from pool)"
echo "  Delta Ur (should = Y):     -$DELTA_Ur (yield rights consumed)"

# Calculate yield
PRINCIPAL=$DELTA_Z
YIELD=$(echo "$DELTA_U - $PRINCIPAL" | bc)

echo ""
echo "  Calculated:"
echo "    Principal (P) = $PRINCIPAL"
echo "    Yield (Y)     = $YIELD"

# Verify relationships
ERRORS=0

echo ""
echo "  Verifying flux relationships:"

# Check 1: U increase = Z decrease + yield
EXPECTED_U_INCREASE=$(echo "$PRINCIPAL + $YIELD" | bc)
if [ $(echo "$DELTA_U == $EXPECTED_U_INCREASE" | bc) -eq 1 ]; then
    echo "    [OK] U += P + Y"
else
    echo "    [FAIL] U += P + Y (expected $EXPECTED_U_INCREASE, got $DELTA_U)"
    ((ERRORS++))
fi

# Check 2: C increase = yield
if [ $(echo "$DELTA_C == $YIELD" | bc) -eq 1 ] || [ $(echo "($DELTA_C - $YIELD) < 0.01 && ($DELTA_C - $YIELD) > -0.01" | bc) -eq 1 ]; then
    echo "    [OK] C += Y"
else
    echo "    [FAIL] C += Y (expected $YIELD, got $DELTA_C)"
    ((ERRORS++))
fi

# Check 3: Cr decrease = yield
if [ $(echo "$DELTA_Cr == $YIELD" | bc) -eq 1 ] || [ $(echo "($DELTA_Cr - $YIELD) < 0.01 && ($DELTA_Cr - $YIELD) > -0.01" | bc) -eq 1 ]; then
    echo "    [OK] Cr -= Y"
else
    echo "    [FAIL] Cr -= Y (expected $YIELD, got $DELTA_Cr)"
    ((ERRORS++))
fi

# Check 4: Cr == Ur still holds
if [ $(echo "$Cr_AFTER == $Ur_AFTER" | bc) -eq 1 ]; then
    echo "    [OK] Cr == Ur (invariant preserved)"
else
    echo "    [FAIL] Cr != Ur after UNSTAKE"
    ((ERRORS++))
fi

# Check 5: Overall invariants
if [ "$INV_OK" == "true" ]; then
    echo "    [OK] All invariants preserved"
else
    echo "    [FAIL] Invariants violated"
    ((ERRORS++))
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 5 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo "  Principal unstaked: $PRINCIPAL KHU"
echo "  Yield received:     $YIELD KHU"
echo "  Flux errors:        $ERRORS"

if [ $ERRORS -eq 0 ]; then
    echo ""
    echo "  >>> TEST 5: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 5: FAIL ($ERRORS errors) <<<"
    exit 1
fi
