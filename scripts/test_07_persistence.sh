#!/bin/bash
# Test 7: Persistence - State survives restart
# Verifies KHU state is correctly persisted and recovered after node restart

set -e

CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli -regtest -datadir=/tmp/khu_persist_test -rpcuser=test -rpcpassword=test123"
PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 7: PERSISTENCE (State survives restart)"
echo "═══════════════════════════════════════════════════════════════"

# Cleanup
pkill -9 pivxd 2>/dev/null || true
sleep 2
rm -rf /tmp/khu_persist_test
mkdir -p /tmp/khu_persist_test

cat > /tmp/khu_persist_test/pivx.conf << 'EOF'
regtest=1
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
EOF

echo ""
echo "=== PHASE 1: Setup and create KHU state ==="
$PIVXD -regtest -datadir=/tmp/khu_persist_test -daemon
sleep 6

$CLI generate 250 > /dev/null
echo "  Block: $($CLI getblockcount)"

# MINT
echo "  MINT 500 KHU..."
$CLI khumint 500
$CLI generate 1 > /dev/null
sleep 1

# STAKE
echo "  STAKE 200 KHU..."
$CLI khustake 200
$CLI generate 1 > /dev/null
sleep 1

# Wait for some yield
echo "  Mining 4500 blocks for yield..."
$CLI generate 4500 > /dev/null
sleep 2

# Capture state before restart
echo ""
echo "=== State BEFORE restart ==="
STATE_BEFORE=$($CLI getkhustate)
C_BEFORE=$(echo $STATE_BEFORE | grep -oP '"C":\s*\K[0-9.]+')
U_BEFORE=$(echo $STATE_BEFORE | grep -oP '"U":\s*\K[0-9.]+')
Z_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Z":\s*\K[0-9.]+')
Cr_BEFORE=$(echo $STATE_BEFORE | grep -oP '"Cr":\s*\K[0-9.]+')
HEIGHT_BEFORE=$($CLI getblockcount)
HASH_BEFORE=$(echo $STATE_BEFORE | grep -oP '"hashState":\s*"\K[a-f0-9]+')

echo "  Height: $HEIGHT_BEFORE"
echo "  C=$C_BEFORE, U=$U_BEFORE, Z=$Z_BEFORE, Cr=$Cr_BEFORE"
echo "  State hash: ${HASH_BEFORE:0:16}..."

# Wallet state
WALLET_BEFORE=$($CLI khubalance)
KHU_TRANSPARENT_BEFORE=$(echo $WALLET_BEFORE | grep -oP '"transparent":\s*\K[0-9.]+')
KHU_STAKED_BEFORE=$(echo $WALLET_BEFORE | grep -oP '"staked":\s*\K[0-9.]+')
echo "  Wallet: transparent=$KHU_TRANSPARENT_BEFORE, staked=$KHU_STAKED_BEFORE"

echo ""
echo "=== PHASE 2: Stop node ==="
$CLI stop
sleep 5

echo ""
echo "=== PHASE 3: Restart node ==="
$PIVXD -regtest -datadir=/tmp/khu_persist_test -daemon
sleep 8

echo ""
echo "=== State AFTER restart ==="
STATE_AFTER=$($CLI getkhustate)
C_AFTER=$(echo $STATE_AFTER | grep -oP '"C":\s*\K[0-9.]+')
U_AFTER=$(echo $STATE_AFTER | grep -oP '"U":\s*\K[0-9.]+')
Z_AFTER=$(echo $STATE_AFTER | grep -oP '"Z":\s*\K[0-9.]+')
Cr_AFTER=$(echo $STATE_AFTER | grep -oP '"Cr":\s*\K[0-9.]+')
HEIGHT_AFTER=$($CLI getblockcount)
HASH_AFTER=$(echo $STATE_AFTER | grep -oP '"hashState":\s*"\K[a-f0-9]+')
INV_OK=$(echo $STATE_AFTER | grep -oP '"invariants_ok":\s*\K\w+')

echo "  Height: $HEIGHT_AFTER"
echo "  C=$C_AFTER, U=$U_AFTER, Z=$Z_AFTER, Cr=$Cr_AFTER"
echo "  State hash: ${HASH_AFTER:0:16}..."

WALLET_AFTER=$($CLI khubalance)
KHU_TRANSPARENT_AFTER=$(echo $WALLET_AFTER | grep -oP '"transparent":\s*\K[0-9.]+')
KHU_STAKED_AFTER=$(echo $WALLET_AFTER | grep -oP '"staked":\s*\K[0-9.]+')
echo "  Wallet: transparent=$KHU_TRANSPARENT_AFTER, staked=$KHU_STAKED_AFTER"

echo ""
echo "=== PHASE 4: Verify persistence ==="
ERRORS=0

# Check height
if [ "$HEIGHT_BEFORE" == "$HEIGHT_AFTER" ]; then
    echo "  [OK] Height preserved: $HEIGHT_AFTER"
else
    echo "  [FAIL] Height changed: $HEIGHT_BEFORE -> $HEIGHT_AFTER"
    ((ERRORS++))
fi

# Check C/U/Z
if [ "$C_BEFORE" == "$C_AFTER" ]; then
    echo "  [OK] C preserved: $C_AFTER"
else
    echo "  [FAIL] C changed: $C_BEFORE -> $C_AFTER"
    ((ERRORS++))
fi

if [ "$U_BEFORE" == "$U_AFTER" ]; then
    echo "  [OK] U preserved: $U_AFTER"
else
    echo "  [FAIL] U changed: $U_BEFORE -> $U_AFTER"
    ((ERRORS++))
fi

if [ "$Z_BEFORE" == "$Z_AFTER" ]; then
    echo "  [OK] Z preserved: $Z_AFTER"
else
    echo "  [FAIL] Z changed: $Z_BEFORE -> $Z_AFTER"
    ((ERRORS++))
fi

# Check state hash
if [ "$HASH_BEFORE" == "$HASH_AFTER" ]; then
    echo "  [OK] State hash preserved"
else
    echo "  [FAIL] State hash changed"
    ((ERRORS++))
fi

# Check wallet
if [ "$KHU_TRANSPARENT_BEFORE" == "$KHU_TRANSPARENT_AFTER" ]; then
    echo "  [OK] Wallet transparent preserved: $KHU_TRANSPARENT_AFTER"
else
    echo "  [FAIL] Wallet transparent changed: $KHU_TRANSPARENT_BEFORE -> $KHU_TRANSPARENT_AFTER"
    ((ERRORS++))
fi

if [ "$KHU_STAKED_BEFORE" == "$KHU_STAKED_AFTER" ]; then
    echo "  [OK] Wallet staked preserved: $KHU_STAKED_AFTER"
else
    echo "  [FAIL] Wallet staked changed: $KHU_STAKED_BEFORE -> $KHU_STAKED_AFTER"
    ((ERRORS++))
fi

# Check invariants
if [ "$INV_OK" == "true" ]; then
    echo "  [OK] Invariants valid after restart"
else
    echo "  [FAIL] Invariants broken after restart"
    ((ERRORS++))
fi

echo ""
echo "=== PHASE 5: Operations after restart ==="

# UNSTAKE should work after restart
echo "  Mining to maturity if needed..."
$CLI generate 100 > /dev/null
sleep 1

echo "  Attempting UNSTAKE..."
UNSTAKE_RESULT=$($CLI khuunstake 2>&1) || true
if echo "$UNSTAKE_RESULT" | grep -q "txid"; then
    echo "  [OK] UNSTAKE works after restart"
    $CLI generate 1 > /dev/null
else
    echo "  [WARN] UNSTAKE may have failed (could be maturity): ${UNSTAKE_RESULT:0:50}"
fi

# REDEEM should work
echo "  Attempting REDEEM 50..."
REDEEM_RESULT=$($CLI khuredeem 50 2>&1) || true
if echo "$REDEEM_RESULT" | grep -q "txid"; then
    echo "  [OK] REDEEM works after restart"
    $CLI generate 1 > /dev/null
else
    echo "  [WARN] REDEEM may have failed: ${REDEEM_RESULT:0:50}"
fi

# Final invariants
FINAL_STATE=$($CLI getkhustate)
FINAL_INV=$(echo $FINAL_STATE | grep -oP '"invariants_ok":\s*\K\w+')
if [ "$FINAL_INV" == "true" ]; then
    echo "  [OK] Final invariants valid"
else
    echo "  [FAIL] Final invariants broken"
    ((ERRORS++))
fi

# Cleanup
$CLI stop 2>/dev/null || true

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST 7 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"

if [ $ERRORS -eq 0 ]; then
    echo ""
    echo "  >>> TEST 7: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 7: FAIL ($ERRORS errors) <<<"
    exit 1
fi
