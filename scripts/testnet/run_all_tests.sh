#!/bin/bash
# =============================================================================
# KHU V6 Private Testnet - Run All Tests
# =============================================================================
# Runs all test phases sequentially with logging
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DATADIR="${1:-/tmp/khu_private_testnet}"
LOG_DIR="$SCRIPT_DIR/logs/$(date +%Y%m%d_%H%M%S)"

mkdir -p "$LOG_DIR"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo "═══════════════════════════════════════════════════════════════"
echo "  KHU V6 PRIVATE TESTNET - FULL TEST SUITE"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Data directory: $DATADIR"
echo "Log directory:  $LOG_DIR"
echo ""

run_phase() {
    PHASE=$1
    SCRIPT=$2
    DESC=$3

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${CYAN}PHASE $PHASE: $DESC${NC}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    LOG_FILE="$LOG_DIR/phase_${PHASE}.log"

    if bash "$SCRIPT_DIR/$SCRIPT" "$DATADIR" > "$LOG_FILE" 2>&1; then
        echo -e "${GREEN}✓ PHASE $PHASE PASSED${NC}"
        echo "  Log: $LOG_FILE"
        return 0
    else
        echo -e "${RED}✗ PHASE $PHASE FAILED${NC}"
        echo "  Log: $LOG_FILE"
        echo ""
        echo "Last 20 lines of log:"
        tail -20 "$LOG_FILE"
        return 1
    fi
}

# Track results
PASSED=0
FAILED=0
TOTAL=0

# =============================================================================
# RUN TESTS
# =============================================================================

echo ""
echo "Starting test suite at $(date)"
echo ""

# Phase A: Setup (implicitly done by Phase B if needed)
TOTAL=$((TOTAL + 1))
if run_phase "A" "01_setup_private_testnet.sh" "Infrastructure Setup"; then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1))
    echo ""
    echo -e "${RED}Setup failed. Cannot continue.${NC}"
    exit 1
fi

# Phase B: Basic Operations
TOTAL=$((TOTAL + 1))
if run_phase "B" "02_test_phase_b_basic_ops.sh" "Basic Operations (MINT/REDEEM)"; then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1))
fi

# Phase C: STAKE/UNSTAKE + Yield
TOTAL=$((TOTAL + 1))
if run_phase "C" "03_test_phase_c_stake_yield.sh" "STAKE/UNSTAKE + Yield"; then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1))
fi

# Phase D: DAO Treasury
TOTAL=$((TOTAL + 1))
if run_phase "D" "04_test_phase_d_dao_treasury.sh" "DAO Treasury"; then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1))
fi

# Phase E: DOMC R% Voting
TOTAL=$((TOTAL + 1))
if run_phase "E" "05_test_phase_e_domc.sh" "DOMC R% Voting"; then
    PASSED=$((PASSED + 1))
else
    FAILED=$((FAILED + 1))
fi

# =============================================================================
# SUMMARY
# =============================================================================

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TEST SUITE SUMMARY"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Completed at: $(date)"
echo ""
echo "Results:"
echo -e "  ${GREEN}Passed: $PASSED${NC}"
echo -e "  ${RED}Failed: $FAILED${NC}"
echo "  Total:  $TOTAL"
echo ""
echo "Logs: $LOG_DIR"
echo ""

if [ $FAILED -eq 0 ]; then
    echo "═══════════════════════════════════════════════════════════════"
    echo -e "${GREEN}  ALL TESTS PASSED - READY FOR NEXT PHASE${NC}"
    echo "═══════════════════════════════════════════════════════════════"
    exit 0
else
    echo "═══════════════════════════════════════════════════════════════"
    echo -e "${RED}  $FAILED TEST(S) FAILED - REVIEW LOGS${NC}"
    echo "═══════════════════════════════════════════════════════════════"
    exit 1
fi
