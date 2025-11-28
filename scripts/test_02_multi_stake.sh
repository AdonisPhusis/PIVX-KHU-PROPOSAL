#!/bin/bash
# Test 2: Multi-STAKE Operations
# Tests: multiple stakes, add stake, partial unstake, restake

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 2: MULTI-STAKE OPERATIONS"
echo "═══════════════════════════════════════════════════════════════"

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running. Run setup_testnet_2nodes.sh first"
    exit 1
fi

check_invariants() {
    local STATE=$($MINER getkhustate)
    local INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')
    if [ "$INV" != "true" ]; then
        echo "FAIL: Invariants violated!"
        echo "$STATE"
        exit 1
    fi
    echo "  Invariants: OK"
}

echo ""
echo "=== PHASE A: Setup - MINT 1000 KHU ==="
$WALLET khumint 1000 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
echo "  KHU Balance: $($WALLET khubalance | grep transparent)"
check_invariants

echo ""
echo "=== PHASE B: Create 3 Stakes ==="
echo "  STAKE 1: 100 KHU"
$WALLET khustake 100 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

echo "  STAKE 2: 200 KHU"
$WALLET khustake 200 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

echo "  STAKE 3: 300 KHU"
$WALLET khustake 300 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

echo ""
echo "  Staked notes:"
$WALLET khuliststaked 2>/dev/null | grep -E "amount|stake_height|is_mature" | head -12 || echo "  (checking...)"
check_invariants

STATE=$($MINER getkhustate)
Z_NOW=$(echo $STATE | grep -oP '"Z":\s*\K[0-9.]+')
echo "  Total staked (Z): $Z_NOW"

echo ""
echo "=== PHASE C: Wait for maturity (4320 blocks) ==="
echo "  Mining 4320 blocks..."
$MINER generate 4320 > /dev/null
sleep 3
echo "  Block: $($MINER getblockcount)"

echo ""
echo "  Checking maturity:"
$WALLET khuliststaked 2>/dev/null | grep -E "is_mature" | head -3 || echo "  (all should be mature)"
check_invariants

echo ""
echo "=== PHASE D: Partial UNSTAKE (first note only) ==="
STATE_BEFORE=$($MINER getkhustate)
Z_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Z":\s*\K[0-9.]+')
Cr_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Cr":\s*\K[0-9.]+')
echo "  Before: Z=$Z_BEFORE, Cr=$Cr_BEFORE"

$WALLET khuunstake > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

STATE_AFTER=$($MINER getkhustate)
Z_AFTER=$(echo $STATE_AFTER | grep -oP '"Z":\s*\K[0-9.]+')
Cr_AFTER=$(echo $STATE_AFTER | grep -oP '"Cr":\s*\K[0-9.]+')
echo "  After:  Z=$Z_AFTER, Cr=$Cr_AFTER"
check_invariants

echo ""
echo "  Remaining staked notes:"
$WALLET khuliststaked 2>/dev/null | grep -E "amount" | head -3 || echo "  (checking...)"

echo ""
echo "=== PHASE E: Add new STAKE while others mature ==="
echo "  Adding STAKE 4: 150 KHU"
$WALLET khustake 150 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

echo "  Current notes (mixed maturity):"
$WALLET khuliststaked 2>/dev/null | grep -E "amount|is_mature" | head -8 || echo "  (checking...)"
check_invariants

echo ""
echo "=== PHASE F: RESTAKE recovered KHU ==="
KHU_BAL=$($WALLET khubalance | grep -oP 'transparent":\s*\K[0-9.]+' || echo "0")
echo "  Available KHU: $KHU_BAL"

if (( $(echo "$KHU_BAL > 50" | bc -l) )); then
    echo "  Restaking 50 KHU..."
    $WALLET khustake 50 > /dev/null 2>&1
    $RELAY > /dev/null 2>&1
    $MINER generate 1 > /dev/null
    sleep 2
    check_invariants
else
    echo "  Skip restake (insufficient balance)"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  FINAL STATE"
echo "═══════════════════════════════════════════════════════════════"

FINAL_STATE=$($MINER getkhustate)
echo "  C:  $(echo $FINAL_STATE | grep -oP '"C":\s*\K[0-9.]+')"
echo "  U:  $(echo $FINAL_STATE | grep -oP '"U":\s*\K[0-9.]+')"
echo "  Z:  $(echo $FINAL_STATE | grep -oP '"Z":\s*\K[0-9.]+')"
echo "  Cr: $(echo $FINAL_STATE | grep -oP '"Cr":\s*\K[0-9.]+')"
echo "  Ur: $(echo $FINAL_STATE | grep -oP '"Ur":\s*\K[0-9.]+')"

INV_FINAL=$(echo $FINAL_STATE | grep -oP '"invariants_ok":\s*\K\w+')

echo ""
echo "  Staked notes count: $($WALLET khuliststaked 2>/dev/null | grep -c 'amount' || echo '?')"

echo ""
if [ "$INV_FINAL" == "true" ]; then
    echo "  >>> TEST 2: PASS <<<"
    exit 0
else
    echo "  >>> TEST 2: FAIL <<<"
    exit 1
fi
