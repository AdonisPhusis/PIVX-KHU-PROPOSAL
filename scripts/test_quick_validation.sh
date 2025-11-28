#!/bin/bash
# Quick Validation Test - Covers all KHU V6 functionality
# Runtime: ~3 minutes

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       KHU V6 - QUICK VALIDATION TEST                          ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "Testnet not running. Starting setup..."
    cd /home/ubuntu/PIVX-V6-KHU/scripts
    ./setup_testnet_2nodes.sh > /dev/null 2>&1
fi

ERRORS=0

check_invariants() {
    local STEP=$1
    local STATE=$($MINER getkhustate)
    local INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')
    local C=$(echo $STATE | grep -oP '"C":\s*\K[0-9.]+')
    local U=$(echo $STATE | grep -oP '"U":\s*\K[0-9.]+')
    local Z=$(echo $STATE | grep -oP '"Z":\s*\K[0-9.]+')

    if [ "$INV" == "true" ]; then
        printf "  %-25s C=%-8s U=%-8s Z=%-8s [OK]\n" "$STEP" "$C" "$U" "$Z"
    else
        printf "  %-25s C=%-8s U=%-8s Z=%-8s [FAIL]\n" "$STEP" "$C" "$U" "$Z"
        ((ERRORS++))
    fi
}

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 1: MINT (5 transactions)"
echo "═══════════════════════════════════════════════════════════════"
for i in 1 2 3 4 5; do
    $WALLET khumint 100 > /dev/null 2>&1 || ((ERRORS++))
    $RELAY > /dev/null 2>&1
    $MINER generate 1 > /dev/null
done
sleep 2
check_invariants "After MINT 500 KHU"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 2: STAKE (3 notes)"
echo "═══════════════════════════════════════════════════════════════"
$WALLET khustake 50 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

$WALLET khustake 100 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

$WALLET khustake 50 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants "After STAKE 200 KHU"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 3: REDEEM (from transparent balance)"
echo "═══════════════════════════════════════════════════════════════"
$WALLET khuredeem 50 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants "After REDEEM 50 KHU"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 4: MATURITY (4320 blocks)"
echo "═══════════════════════════════════════════════════════════════"
echo "  Mining 4320 blocks for maturity..."
$MINER generate 4320 > /dev/null
sleep 3
check_invariants "After maturity"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 5: YIELD ACCUMULATION (1440 blocks = 1 day)"
echo "═══════════════════════════════════════════════════════════════"
STATE_BEFORE=$($MINER getkhustate)
Cr_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Cr":\s*\K[0-9.]+')
echo "  Cr before: $Cr_BEFORE"

$MINER generate 1440 > /dev/null
sleep 2

STATE_AFTER=$($MINER getkhustate)
Cr_AFTER=$(echo $STATE_AFTER | grep -oP '"Cr":\s*\K[0-9.]+')
echo "  Cr after:  $Cr_AFTER"

if (( $(echo "$Cr_AFTER > $Cr_BEFORE" | bc -l) )); then
    echo "  Yield accumulated: +$(echo "$Cr_AFTER - $Cr_BEFORE" | bc) [OK]"
else
    echo "  No yield accumulated [FAIL]"
    ((ERRORS++))
fi
check_invariants "After yield day"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 6: UNSTAKE (atomic flux Cr->C)"
echo "═══════════════════════════════════════════════════════════════"
STATE_BEFORE=$($MINER getkhustate)
Z_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Z":\s*\K[0-9.]+')
C_BEFORE=$(echo $STATE_BEFORE | grep -oP '"C":\s*\K[0-9.]+')
echo "  Before: Z=$Z_BEFORE, C=$C_BEFORE"

$WALLET khuunstake > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

STATE_AFTER=$($MINER getkhustate)
Z_AFTER=$(echo $STATE_AFTER | grep -oP '"Z":\s*\K[0-9.]+')
C_AFTER=$(echo $STATE_AFTER | grep -oP '"C":\s*\K[0-9.]+')
echo "  After:  Z=$Z_AFTER, C=$C_AFTER"

if (( $(echo "$Z_AFTER < $Z_BEFORE" | bc -l) )); then
    echo "  UNSTAKE successful [OK]"
else
    echo "  UNSTAKE failed [FAIL]"
    ((ERRORS++))
fi
check_invariants "After UNSTAKE"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 7: REDEEM after UNSTAKE"
echo "═══════════════════════════════════════════════════════════════"
$WALLET khuredeem 30 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants "After REDEEM post-unstake"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 8: Full Pipeline (MINT -> STAKE -> UNSTAKE -> REDEEM)"
echo "═══════════════════════════════════════════════════════════════"
# Fresh MINT
$WALLET khumint 100 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null

# STAKE
$WALLET khustake 80 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null

# Wait maturity
$MINER generate 4320 > /dev/null
sleep 3

# UNSTAKE
$WALLET khuunstake > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null

# REDEEM
$WALLET khuredeem 50 > /dev/null 2>&1 || ((ERRORS++))
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants "After full pipeline"

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║                    FINAL RESULTS                               ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

FINAL_STATE=$($MINER getkhustate)
echo ""
echo "Final KHU State:"
echo "$FINAL_STATE" | grep -E '"C"|"U"|"Z"|"Cr"|"Ur"|"T"|"invariants"'

echo ""
echo "Wallet Balances:"
echo "  PIV: $($WALLET getbalance)"
echo "  KHU: $($WALLET khubalance 2>/dev/null | grep transparent || echo '0')"

echo ""
if [ $ERRORS -eq 0 ]; then
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║  ALL TESTS PASSED - KHU V6 READY FOR TESTNET                  ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    exit 0
else
    echo "╔═══════════════════════════════════════════════════════════════╗"
    echo "║  TESTS FAILED: $ERRORS errors                                      ║"
    echo "╚═══════════════════════════════════════════════════════════════╝"
    exit 1
fi
