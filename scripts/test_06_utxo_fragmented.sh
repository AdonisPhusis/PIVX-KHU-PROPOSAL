#!/bin/bash
# Test 6: UTXO Fragmentation Pipeline
# Verifies pipeline works with divided/fragmented UTXOs

set -e

MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"
RELAY="/tmp/khu_testnet/relay.sh"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 6: UTXO FRAGMENTATION PIPELINE"
echo "═══════════════════════════════════════════════════════════════"

# Check testnet
if ! $MINER getblockcount > /dev/null 2>&1; then
    echo "ERROR: Testnet not running"
    exit 1
fi

check_invariants() {
    local STATE=$($MINER getkhustate)
    local INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')
    if [ "$INV" != "true" ]; then
        echo "FAIL: Invariants violated!"
        exit 1
    fi
}

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 1: Create Fragmented KHU UTXOs"
echo "═══════════════════════════════════════════════════════════════"

# MINT initial KHU
echo "  MINT 100 KHU..."
$WALLET khumint 100 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants

# Get wallet address for self-sends
ADDR=$($WALLET getnewaddress)

# Fragment by sending to self
echo "  Fragmenting: send 30 KHU to self..."
$WALLET khusend $ADDR 30 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

echo "  Fragmenting: send 20 KHU to self..."
$WALLET khusend $ADDR 20 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1

echo "  Fragmenting: send 10 KHU to self..."
$WALLET khusend $ADDR 10 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2

check_invariants

echo ""
echo "  Current KHU UTXOs:"
UTXO_COUNT=$($WALLET khulistunspent 2>/dev/null | grep -c '"amount"' || echo "0")
echo "  UTXO count: $UTXO_COUNT"
$WALLET khulistunspent 2>/dev/null | grep '"amount"' | head -5 || echo "  (checking...)"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 2: Operations with Fragmented UTXOs"
echo "═══════════════════════════════════════════════════════════════"

# STAKE using fragmented UTXOs (should combine them)
echo ""
echo "  STAKE 45 KHU (requires combining UTXOs)..."
$WALLET khustake 45 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants

STATE=$($MINER getkhustate)
Z=$(echo $STATE | grep -oP '"Z":\s*\K[0-9.]+')
echo "  Staked (Z): $Z"

# REDEEM some (should handle remaining fragments)
echo ""
echo "  REDEEM 15 KHU..."
$WALLET khuredeem 15 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 3: Full Cycle with Fragments"
echo "═══════════════════════════════════════════════════════════════"

# MINT more
echo "  MINT 50 KHU..."
$WALLET khumint 50 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1
check_invariants

# STAKE partial
echo "  STAKE 25 KHU..."
$WALLET khustake 25 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 1
check_invariants

# Wait for maturity
echo "  Mining to maturity (4320 blocks)..."
$MINER generate 4320 > /dev/null
sleep 3

# UNSTAKE
echo "  UNSTAKE..."
$WALLET khuunstake > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants

# REDEEM some
echo "  REDEEM 20 KHU..."
$WALLET khuredeem 20 > /dev/null 2>&1
$RELAY > /dev/null 2>&1
$MINER generate 1 > /dev/null
sleep 2
check_invariants

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  PHASE 4: Verify Final State"
echo "═══════════════════════════════════════════════════════════════"

FINAL_STATE=$($MINER getkhustate)
C=$(echo $FINAL_STATE | grep -oP '"C":\s*\K[0-9.]+')
U=$(echo $FINAL_STATE | grep -oP '"U":\s*\K[0-9.]+')
Z=$(echo $FINAL_STATE | grep -oP '"Z":\s*\K[0-9.]+')
INV=$(echo $FINAL_STATE | grep -oP '"invariants_ok":\s*\K\w+')

echo "  C: $C"
echo "  U: $U"
echo "  Z: $Z"
echo "  Invariants: $INV"

echo ""
echo "  KHU Balance:"
$WALLET khubalance

echo ""
echo "  Final UTXO count: $($WALLET khulistunspent 2>/dev/null | grep -c '"amount"' || echo '0')"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 6 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"

if [ "$INV" == "true" ]; then
    echo ""
    echo "  >>> TEST 6: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 6: FAIL <<<"
    exit 1
fi
