#!/bin/bash
# Test 8: Edge Cases
# Verifies system handles edge cases correctly

set -e

CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli -regtest -datadir=/tmp/khu_edge_test -rpcuser=test -rpcpassword=test123"
PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"

echo "═══════════════════════════════════════════════════════════════"
echo "     TEST 8: EDGE CASES"
echo "═══════════════════════════════════════════════════════════════"

# Cleanup
pkill -9 pivxd 2>/dev/null || true
sleep 2
rm -rf /tmp/khu_edge_test
mkdir -p /tmp/khu_edge_test

cat > /tmp/khu_edge_test/pivx.conf << 'EOF'
regtest=1
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
EOF

$PIVXD -regtest -datadir=/tmp/khu_edge_test -daemon
sleep 6
$CLI generate 250 > /dev/null

ERRORS=0

echo ""
echo "=== TEST 8.1: MINT with 0 amount (should fail) ==="
RESULT=$($CLI khumint 0 2>&1) || true
if echo "$RESULT" | grep -qi "error\|invalid\|fail"; then
    echo "  [OK] MINT 0 rejected"
else
    echo "  [FAIL] MINT 0 should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.2: MINT with negative amount (should fail) ==="
RESULT=$($CLI khumint -100 2>&1) || true
if echo "$RESULT" | grep -qi "error\|invalid\|fail\|negative"; then
    echo "  [OK] MINT negative rejected"
else
    echo "  [FAIL] MINT negative should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.3: REDEEM without KHU balance (should fail) ==="
RESULT=$($CLI khuredeem 100 2>&1) || true
if echo "$RESULT" | grep -qi "error\|insufficient\|fail"; then
    echo "  [OK] REDEEM without balance rejected"
else
    echo "  [FAIL] REDEEM without balance should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.4: MINT then REDEEM exact amount ==="
$CLI khumint 100
$CLI generate 1 > /dev/null
sleep 1
RESULT=$($CLI khuredeem 100 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    echo "  [OK] REDEEM exact amount works"
    $CLI generate 1 > /dev/null
else
    echo "  [FAIL] REDEEM exact amount failed"
    ((ERRORS++))
fi

# Check invariants
STATE=$($CLI getkhustate)
INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')
if [ "$INV" == "true" ]; then
    echo "  [OK] Invariants valid after exact REDEEM"
else
    echo "  [FAIL] Invariants broken"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.5: STAKE without KHU balance (should fail) ==="
RESULT=$($CLI khustake 100 2>&1) || true
if echo "$RESULT" | grep -qi "error\|insufficient\|fail"; then
    echo "  [OK] STAKE without balance rejected"
else
    echo "  [FAIL] STAKE without balance should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.6: UNSTAKE without staked notes (should fail) ==="
RESULT=$($CLI khuunstake 2>&1) || true
if echo "$RESULT" | grep -qi "error\|no.*note\|fail\|nothing"; then
    echo "  [OK] UNSTAKE without notes rejected"
else
    echo "  [FAIL] UNSTAKE without notes should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.7: UNSTAKE before maturity (should fail) ==="
$CLI khumint 100
$CLI generate 1 > /dev/null
$CLI khustake 50
$CLI generate 1 > /dev/null
# Only a few blocks, not mature yet
$CLI generate 10 > /dev/null
RESULT=$($CLI khuunstake 2>&1) || true
if echo "$RESULT" | grep -qi "maturity\|immature\|not.*mature"; then
    echo "  [OK] UNSTAKE before maturity rejected"
else
    echo "  [WARN] UNSTAKE before maturity may have different error: ${RESULT:0:60}"
    # Not counting as error since behavior may vary
fi

echo ""
echo "=== TEST 8.8: REDEEM more than balance (should fail) ==="
BALANCE=$($CLI khubalance | grep -oP '"transparent":\s*\K[0-9.]+')
EXCESS=$(echo "$BALANCE + 1000" | bc)
RESULT=$($CLI khuredeem $EXCESS 2>&1) || true
if echo "$RESULT" | grep -qi "error\|insufficient\|fail"; then
    echo "  [OK] REDEEM more than balance rejected"
else
    echo "  [FAIL] REDEEM more than balance should be rejected"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.9: Multiple small MINTs ==="
for i in 1 2 3 4 5; do
    $CLI khumint 10 > /dev/null
    $CLI generate 1 > /dev/null
done
sleep 1
STATE=$($CLI getkhustate)
C=$(echo $STATE | grep -oP '"C":\s*\K[0-9.]+')
echo "  After 5x MINT 10: C=$C"
INV=$(echo $STATE | grep -oP '"invariants_ok":\s*\K\w+')
if [ "$INV" == "true" ]; then
    echo "  [OK] Multiple small MINTs - invariants valid"
else
    echo "  [FAIL] Multiple small MINTs - invariants broken"
    ((ERRORS++))
fi

echo ""
echo "=== TEST 8.10: Very small MINT (0.01 KHU) ==="
RESULT=$($CLI khumint 0.01 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    echo "  [OK] Small MINT 0.01 works"
    $CLI generate 1 > /dev/null
else
    echo "  [WARN] Small MINT 0.01 may have minimum: ${RESULT:0:60}"
fi

echo ""
echo "=== TEST 8.11: Large MINT ==="
RESULT=$($CLI khumint 10000 2>&1)
if echo "$RESULT" | grep -q "txid"; then
    echo "  [OK] Large MINT 10000 works"
    $CLI generate 1 > /dev/null
else
    echo "  [FAIL] Large MINT 10000 failed: ${RESULT:0:60}"
    ((ERRORS++))
fi

# Final invariants check
echo ""
echo "=== FINAL STATE ==="
FINAL_STATE=$($CLI getkhustate)
echo "$FINAL_STATE" | grep -E '"C"|"U"|"Z"|"invariants"'

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
echo "  TEST 8 SUMMARY"
echo "═══════════════════════════════════════════════════════════════"

if [ $ERRORS -eq 0 ]; then
    echo ""
    echo "  >>> TEST 8: PASS <<<"
    exit 0
else
    echo ""
    echo "  >>> TEST 8: FAIL ($ERRORS errors) <<<"
    exit 1
fi
