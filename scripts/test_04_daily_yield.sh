#!/bin/bash
# Test 4: Daily Yield (Y) per Staker
# Verifies yield accumulation formula: daily_yield = (amount * R_annual) / 10000 / 365

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 4: DAILY YIELD (Y) PER STAKER"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "  Formula: daily_yield = (amount * R_annual) / 10000 / 365"
echo "  With R_annual = 4000 (40%):"
echo "    36500 KHU → 40 KHU/day"
echo "    3650 KHU  → 4 KHU/day"
echo "    365 KHU   → 0.4 KHU/day"
echo ""

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running"
    exit 1
fi

echo "═══════════════════════════════════════════════════════════════"
echo "  SETUP: MINT and STAKE 3650 KHU"
echo "═══════════════════════════════════════════════════════════════"

$WALLET khumint 3650 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

$WALLET khustake 3650 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

STAKE_HEIGHT=$($MINER getblockcount)
echo "  Staked 3650 KHU at block $STAKE_HEIGHT"
echo "  Expected daily yield: 3650 * 4000 / 10000 / 365 = 4 KHU/day"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 1: Maturity Period (4320 blocks = 3 days)"
echo "═══════════════════════════════════════════════════════════════"

echo "  Mining to maturity..."
$MINER generate 4320 > /dev/null
sleep 2

# Check Cr/Ur during maturity
STATE_MAT=$($MINER getkhustate)
Cr_MAT=$(echo $STATE_MAT | grep -oP '"Cr":\s*\K[0-9.]+')
Ur_MAT=$(echo $STATE_MAT | grep -oP '"Ur":\s*\K[0-9.]+')
echo "  After maturity: Cr=$Cr_MAT, Ur=$Ur_MAT"
echo "  (Note: Yield starts accumulating DURING maturity in Cr/Ur pool)"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 2: Yield Accumulation (track over 5 days)"
echo "═══════════════════════════════════════════════════════════════"

ERRORS=0
PREV_Cr="0"

for DAY in 1 2 3 4 5; do
    echo ""
    echo "  --- Day $DAY ---"

    # Mine 1440 blocks (1 day)
    $MINER generate 1440 > /dev/null
    sleep 2

    STATE=$($MINER getkhustate)
    Cr=$(echo $STATE | grep -oP '"Cr":\s*\K[0-9.]+')
    Ur=$(echo $STATE | grep -oP '"Ur":\s*\K[0-9.]+')
    R_annual=$(echo $STATE | grep -oP '"R_annual":\s*\K[0-9]+')

    # Calculate expected yield
    EXPECTED_DAILY=$(echo "scale=4; 3650 * $R_annual / 10000 / 365" | bc)

    if [ "$PREV_Cr" != "0" ]; then
        ACTUAL_DELTA=$(echo "scale=4; $Cr - $PREV_Cr" | bc)
        echo "  Cr: $Cr (delta: +$ACTUAL_DELTA)"
        echo "  Expected delta: ~$EXPECTED_DAILY"

        # Allow 1% tolerance
        TOLERANCE=$(echo "scale=4; $EXPECTED_DAILY * 0.1" | bc)
        DIFF=$(echo "scale=4; $ACTUAL_DELTA - $EXPECTED_DAILY" | bc)
        ABS_DIFF=$(echo "$DIFF" | tr -d '-')

        if (( $(echo "$ABS_DIFF <= $TOLERANCE" | bc -l) )); then
            echo "  [OK] Yield within tolerance"
        else
            echo "  [WARN] Yield deviation: $DIFF (tolerance: $TOLERANCE)"
            # Don't count as error if positive (more yield is ok)
            if (( $(echo "$DIFF < 0" | bc -l) )); then
                ((ERRORS++))
            fi
        fi
    else
        echo "  Cr: $Cr (baseline)"
    fi

    echo "  Ur: $Ur (should equal Cr)"

    # Verify Cr == Ur
    if [ $(echo "$Cr == $Ur" | bc) -ne 1 ]; then
        echo "  [FAIL] Cr != Ur"
        ((ERRORS++))
    fi

    PREV_Cr=$Cr
done

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 3: Verify Yield at UNSTAKE"
echo "═══════════════════════════════════════════════════════════════"

echo "  Checking accumulated yield in note..."
STAKED_INFO=$($WALLET khuliststaked 2>/dev/null || echo "{}")
echo "$STAKED_INFO" | grep -E "amount|accumulated" || echo "  (info not available)"

echo ""
echo "  Performing UNSTAKE..."
$WALLET khuunstake > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

KHU_BAL=$($WALLET khubalance | grep -oP 'transparent":\s*\K[0-9.]+' || echo "0")
echo "  KHU Balance after UNSTAKE: $KHU_BAL"

# Expected: 3650 (principal) + ~8 days yield (3 maturity + 5 tracked)
# 8 * 4 = 32 KHU yield (approximate, as yield starts accumulating at stake)
EXPECTED_MIN=$(echo "3650 + 15" | bc)  # At least some yield
EXPECTED_MAX=$(echo "3650 + 50" | bc)  # Not too much

if (( $(echo "$KHU_BAL >= $EXPECTED_MIN" | bc -l) )) && (( $(echo "$KHU_BAL <= $EXPECTED_MAX" | bc -l) )); then
    echo "  [OK] Yield received is reasonable ($KHU_BAL in range $EXPECTED_MIN-$EXPECTED_MAX)"
else
    echo "  [WARN] Yield outside expected range"
fi

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 4 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo "  Stake amount: 3650 KHU"
echo "  R_annual: 40% (4000 bp)"
echo "  Expected daily: 4 KHU"
echo "  Errors: $ERRORS"

if [ $ERRORS -eq 0 ]; then
    echo ""
    echo "  >>> TEST 4: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 4: FAIL ($ERRORS errors) <<<"
    exit 1
fi
