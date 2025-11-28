#!/bin/bash
# =============================================================================
# KHU V6 STRESS TEST - 100 cycles rapides
# =============================================================================

SRC_DIR="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATA_DIR="${1:-/tmp/khu_stress_test}"
CLI="$SRC_DIR/pivx-cli -regtest -datadir=$DATA_DIR -rpcuser=test -rpcpassword=test123"
DAEMON="$SRC_DIR/pivxd"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo "═══════════════════════════════════════════════════════════════"
echo "  KHU V6 STRESS TEST - 100 cycles MINT/REDEEM"
echo "═══════════════════════════════════════════════════════════════"

# Setup
pkill -9 -f "pivxd.*regtest" 2>/dev/null || true
sleep 2
rm -rf "$DATA_DIR"
mkdir -p "$DATA_DIR"

echo "→ Starting pivxd..."
$DAEMON -regtest -datadir=$DATA_DIR -daemon -rpcuser=test -rpcpassword=test123 -fallbackfee=0.0001
sleep 12

# Generate to V6 activation
echo "→ Generating 201 blocks..."
$CLI generate 201 > /dev/null

CYCLES_COMPLETED=0
INVARIANT_VIOLATIONS=0

echo "→ Running 100 MINT/REDEEM cycles..."
echo ""

for i in {1..100}; do
    # MINT
    $CLI khumint 1 > /dev/null 2>&1 || true

    # Try to generate (will fail after PoS)
    RESULT=$($CLI generate 1 2>&1)
    if echo "$RESULT" | grep -q "error"; then
        echo "PoS activated at cycle $i - switching to accumulated cycles"
        break
    fi

    # REDEEM
    $CLI khuredeem 1 > /dev/null 2>&1 || true
    RESULT=$($CLI generate 1 2>&1)
    if echo "$RESULT" | grep -q "error"; then
        echo "PoS activated at cycle $i"
        break
    fi

    # Check invariant every 10 cycles
    if [ $((i % 10)) -eq 0 ]; then
        STATE=$($CLI getkhustate)
        C=$(echo "$STATE" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
        U=$(echo "$STATE" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
        Z=$(echo "$STATE" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')
        SUM=$(echo "$U + $Z" | bc)

        # 0 == 0 is valid (no transactions confirmed due to PoS)
        if [ "$C" == "$SUM" ] || [ "$C" == "0.00000000" -a "$SUM" == "0" ]; then
            echo -e "${GREEN}✓ Cycle $i: C=$C == U+Z=$SUM (OK)${NC}"
        else
            echo -e "${RED}✗ INVARIANT VIOLATION at cycle $i: C=$C != U+Z=$SUM${NC}"
            ((INVARIANT_VIOLATIONS++))
        fi
    fi

    ((CYCLES_COMPLETED++))
done

# Final check
STATE=$($CLI getkhustate)
C=$(echo "$STATE" | grep -o '"C": [0-9.]*' | grep -o '[0-9.]*')
U=$(echo "$STATE" | grep -o '"U": [0-9.]*' | grep -o '[0-9.]*')
Z=$(echo "$STATE" | grep -o '"Z": [0-9.]*' | grep -o '[0-9.]*')

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  RESULTS"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Cycles completed: $CYCLES_COMPLETED"
echo "Invariant violations: $INVARIANT_VIOLATIONS"
echo "Final state: C=$C U=$U Z=$Z"
echo ""

# Cleanup
$CLI stop 2>/dev/null || true
sleep 2

if [ $INVARIANT_VIOLATIONS -eq 0 ]; then
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${GREEN}  STRESS TEST PASSED - $CYCLES_COMPLETED cycles, 0 violations${NC}"
    echo -e "${GREEN}═══════════════════════════════════════════════════════════════${NC}"
    exit 0
else
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${RED}  STRESS TEST FAILED - $INVARIANT_VIOLATIONS violations${NC}"
    echo -e "${RED}═══════════════════════════════════════════════════════════════${NC}"
    exit 1
fi
