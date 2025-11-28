#!/bin/bash
# Test 1: MINT/REDEEM High Load (100+ transactions)
# Prerequisites: Run setup_testnet_2nodes.sh first

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 1: MINT/REDEEM HIGH LOAD (100+ transactions)"
echo "═══════════════════════════════════════════════════════════════"

# Check testnet is running
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running. Run setup_testnet_2nodes.sh first"
    exit 1
fi

echo ""
echo "Initial state:"
echo "  Wallet PIV: $($WALLET getbalance)"
echo "  KHU Balance: $($WALLET khubalance 2>/dev/null | grep transparent || echo '0')"
INITIAL_STATE=$($MINER getkhustate)
echo "  C: $(echo $INITIAL_STATE | grep -oP '"C":\s*\K[0-9.]+')"
echo "  U: $(echo $INITIAL_STATE | grep -oP '"U":\s*\K[0-9.]+')"

# Phase 1: MINT 50 transactions
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 1: MINT 50 transactions (10 KHU each = 500 KHU total)"
echo "═══════════════════════════════════════════════════════════════"

MINT_SUCCESS=0
MINT_FAIL=0
START_TIME=$(date +%s)

for i in $(seq 1 50); do
    if $WALLET khumint 10 > /dev/null 2>&1; then
        ((MINT_SUCCESS++))
        echo -n "+"
    else
        ((MINT_FAIL++))
        echo -n "x"
    fi

    # Relay and mine every 10 transactions
    if [ $((i % 10)) -eq 0 ]; then
        $RELAY > /dev/null 2>&1
        $MINER generate 1 > /dev/null
        sleep 1
        echo " [Block $($MINER getblockcount)]"
    fi
done

END_TIME=$(date +%s)
MINT_DURATION=$((END_TIME - START_TIME))

echo ""
echo "MINT Results:"
echo "  Success: $MINT_SUCCESS / 50"
echo "  Failed:  $MINT_FAIL"
echo "  Duration: ${MINT_DURATION}s"

# Check invariants after MINT
echo ""
echo "State after MINT:"
STATE_AFTER_MINT=$($MINER getkhustate)
C_MINT=$(echo $STATE_AFTER_MINT | grep -oP '"C":\s*\K[0-9.]+')
U_MINT=$(echo $STATE_AFTER_MINT | grep -oP '"U":\s*\K[0-9.]+')
Z_MINT=$(echo $STATE_AFTER_MINT | grep -oP '"Z":\s*\K[0-9.]+')
INV_OK=$(echo $STATE_AFTER_MINT | grep -oP '"invariants_ok":\s*\K\w+')
echo "  C=$C_MINT, U=$U_MINT, Z=$Z_MINT"
echo "  Invariants: $INV_OK"

if [ "$INV_OK" != "true" ]; then
    echo "FAIL: Invariants violated after MINT!"
    exit 1
fi

# Phase 2: REDEEM 25 transactions
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 2: REDEEM 25 transactions (10 KHU each = 250 KHU total)"
echo "═══════════════════════════════════════════════════════════════"

REDEEM_SUCCESS=0
REDEEM_FAIL=0
START_TIME=$(date +%s)

for i in $(seq 1 25); do
    if $WALLET khuredeem 10 > /dev/null 2>&1; then
        ((REDEEM_SUCCESS++))
        echo -n "+"
    else
        ((REDEEM_FAIL++))
        echo -n "x"
    fi

    # Relay and mine every 5 transactions
    if [ $((i % 5)) -eq 0 ]; then
        $RELAY > /dev/null 2>&1
        $MINER generate 1 > /dev/null
        sleep 1
        echo " [Block $($MINER getblockcount)]"
    fi
done

END_TIME=$(date +%s)
REDEEM_DURATION=$((END_TIME - START_TIME))

echo ""
echo "REDEEM Results:"
echo "  Success: $REDEEM_SUCCESS / 25"
echo "  Failed:  $REDEEM_FAIL"
echo "  Duration: ${REDEEM_DURATION}s"

# Final state check
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  FINAL STATE CHECK"
echo "═══════════════════════════════════════════════════════════════"

FINAL_STATE=$($MINER getkhustate)
C_FINAL=$(echo $FINAL_STATE | grep -oP '"C":\s*\K[0-9.]+')
U_FINAL=$(echo $FINAL_STATE | grep -oP '"U":\s*\K[0-9.]+')
Z_FINAL=$(echo $FINAL_STATE | grep -oP '"Z":\s*\K[0-9.]+')
INV_FINAL=$(echo $FINAL_STATE | grep -oP '"invariants_ok":\s*\K\w+')

echo "  C=$C_FINAL, U=$U_FINAL, Z=$Z_FINAL"
echo "  Invariants: $INV_FINAL"

# Expected: 500 minted - 250 redeemed = 250 KHU remaining
EXPECTED_KHU=250
ACTUAL_KHU=$(echo "$U_FINAL" | cut -d'.' -f1)

echo ""
echo "  Expected KHU: ~$EXPECTED_KHU"
echo "  Actual KHU:   $ACTUAL_KHU"

# Summary
echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 1 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo "  MINT:   $MINT_SUCCESS/50 success (${MINT_DURATION}s)"
echo "  REDEEM: $REDEEM_SUCCESS/25 success (${REDEEM_DURATION}s)"
echo "  Invariants preserved: $INV_FINAL"

if [ "$INV_FINAL" == "true" ] && [ $MINT_SUCCESS -ge 45 ] && [ $REDEEM_SUCCESS -ge 20 ]; then
    echo ""
    echo "  >>> TEST 1: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 1: FAIL <<<"
    exit 1
fi
