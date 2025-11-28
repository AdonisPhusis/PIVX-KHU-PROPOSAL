#!/bin/bash
# Test 3: Invariants Verification (C/U/Cr/Ur/T)
# Continuous monitoring during operations

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 3: INVARIANTS VERIFICATION (C/U/Cr/Ur/T)"
echo "═══════════════════════════════════════════════════════════════"

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running"
    exit 1
fi

VIOLATIONS=0

verify_invariants() {
    local STEP=$1
    local STATE=$($MINER getkhustate)

    local C=$(echo $STATE | grep -oP '"C":\s*\K[0-9.]+' | cut -d'.' -f1)
    local U=$(echo $STATE | grep -oP '"U":\s*\K[0-9.]+' | cut -d'.' -f1)
    local Z=$(echo $STATE | grep -oP '"Z":\s*\K[0-9.]+' | cut -d'.' -f1)
    local Cr=$(echo $STATE | grep -oP '"Cr":\s*\K[0-9.]+' | cut -d'.' -f1)
    local Ur=$(echo $STATE | grep -oP '"Ur":\s*\K[0-9.]+' | cut -d'.' -f1)
    local T=$(echo $STATE | grep -oP '"T":\s*\K[0-9.]+' | cut -d'.' -f1)
    local INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')

    # Manual check: C == U + Z
    local UZ=$((U + Z))

    printf "  %-20s C=%d U=%d Z=%d (U+Z=%d) Cr=%d Ur=%d T=%d => %s\n" \
        "$STEP:" "$C" "$U" "$Z" "$UZ" "$Cr" "$Ur" "$T" "$INV"

    if [ "$INV" != "true" ]; then
        echo "    *** VIOLATION DETECTED ***"
        ((VIOLATIONS++))
    fi

    # Additional manual checks
    if [ "$C" != "$UZ" ]; then
        echo "    *** C != U+Z (C=$C, U+Z=$UZ) ***"
        ((VIOLATIONS++))
    fi

    if [ "$Cr" != "$Ur" ] && [ "$Cr" != "0" ]; then
        echo "    *** Cr != Ur (Cr=$Cr, Ur=$Ur) ***"
        ((VIOLATIONS++))
    fi
}

echo ""
echo "=== Initial State ==="
verify_invariants "INITIAL"

echo ""
echo "=== MINT Operations ==="
for i in 1 2 3 4 5; do
    $WALLET khumint 100 > /dev/null 2>&1
    $RELAY > /dev/null 2>&1
    $MINER generate 1 > /dev/null
    sleep 1
    verify_invariants "MINT_$i"
done

echo ""
echo "=== STAKE Operations ==="
for i in 1 2 3; do
    $WALLET khustake 50 > /dev/null 2>&1
    $RELAY > /dev/null 2>&1
    $MINER generate 1 > /dev/null
    sleep 1
    verify_invariants "STAKE_$i"
done

echo ""
echo "=== Yield Accumulation (4320 blocks for maturity + 1440 for 1 day yield) ==="
$MINER generate 4320 > /dev/null
sleep 2
verify_invariants "MATURITY"

$MINER generate 1440 > /dev/null
sleep 2
verify_invariants "YIELD_DAY_1"

echo ""
echo "=== UNSTAKE Operations ==="
# Unstake first mature note
$WALLET khuunstake > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
verify_invariants "UNSTAKE_1"

echo ""
echo "=== REDEEM Operations ==="
for i in 1 2 3; do
    $WALLET khuredeem 20 > /dev/null 2>&1
    $RELAY > /dev/null 2>&1
    $MINER generate 1 > /dev/null
    sleep 1
    verify_invariants "REDEEM_$i"
done

echo ""
echo "=== Mixed Operations ==="
# MINT + STAKE + REDEEM in same block
$WALLET khumint 50 > /dev/null 2>&1
$WALLET khustake 30 > /dev/null 2>&1
$WALLET khuredeem 10 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
verify_invariants "MIXED_OPS"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 3 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo "  Total invariant violations: $VIOLATIONS"

if [ $VIOLATIONS -eq 0 ]; then
    echo ""
    echo "  >>> TEST 3: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 3: FAIL ($VIOLATIONS violations) <<<"
    exit 1
fi
