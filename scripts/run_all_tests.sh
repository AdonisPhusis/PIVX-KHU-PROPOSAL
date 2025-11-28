#!/bin/bash
# KHU V6 - Run All Tests
# Prerequisites: pivxd binary must be compiled

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RESULTS_DIR="/tmp/khu_test_results"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║          KHU V6 - COMPREHENSIVE TEST SUITE                    ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Script directory: $SCRIPT_DIR"
echo "  Results directory: $RESULTS_DIR"
echo ""

mkdir -p $RESULTS_DIR

# Track results
PASSED=0
FAILED=0
SKIPPED=0

run_test() {
    local TEST_NUM=$1
    local TEST_NAME=$2
    local TEST_SCRIPT=$3

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Running Test $TEST_NUM: $TEST_NAME"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    if [ ! -f "$SCRIPT_DIR/$TEST_SCRIPT" ]; then
        echo "  SKIP: Script not found"
        ((SKIPPED++))
        return
    fi

    # Setup fresh testnet for each test
    echo "  Setting up fresh testnet..."
    bash "$SCRIPT_DIR/setup_testnet_2nodes.sh" > /dev/null 2>&1 || {
        echo "  FAIL: Could not setup testnet"
        ((FAILED++))
        return
    }

    # Run test
    if bash "$SCRIPT_DIR/$TEST_SCRIPT" 2>&1 | tee "$RESULTS_DIR/test_${TEST_NUM}.log"; then
        echo ""
        echo "  Result: PASS"
        ((PASSED++))
    else
        echo ""
        echo "  Result: FAIL"
        ((FAILED++))
    fi

    # Cleanup
    pkill -9 pivxd 2>/dev/null || true
    sleep 2
}

# Run all tests
run_test "01" "MINT/REDEEM High Load" "test_01_high_load.sh"
run_test "02" "Multi-STAKE Operations" "test_02_multi_stake.sh"
run_test "03" "Invariants Verification" "test_03_invariants.sh"
run_test "04" "Daily Yield" "test_04_daily_yield.sh"
run_test "05" "Atomic UNSTAKE Flux" "test_05_atomic_unstake.sh"
run_test "06" "UTXO Fragmentation" "test_06_utxo_fragmented.sh"

# Tests 07 and 08 use their own single-node setup
run_standalone_test() {
    local TEST_NUM=$1
    local TEST_NAME=$2
    local TEST_SCRIPT=$3

    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  Running Test $TEST_NUM: $TEST_NAME (standalone)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    if [ ! -f "$SCRIPT_DIR/$TEST_SCRIPT" ]; then
        echo "  SKIP: Script not found"
        ((SKIPPED++))
        return
    fi

    # Cleanup before test
    pkill -9 pivxd 2>/dev/null || true
    sleep 2

    # Run test (it handles its own setup)
    if bash "$SCRIPT_DIR/$TEST_SCRIPT" 2>&1 | tee "$RESULTS_DIR/test_${TEST_NUM}.log"; then
        echo ""
        echo "  Result: PASS"
        ((PASSED++))
    else
        echo ""
        echo "  Result: FAIL"
        ((FAILED++))
    fi

    # Cleanup
    pkill -9 pivxd 2>/dev/null || true
    sleep 2
}

run_standalone_test "07" "Persistence (Restart)" "test_07_persistence.sh"
run_standalone_test "08" "Edge Cases" "test_08_edge_cases.sh"

# Summary
echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║                    TEST RESULTS SUMMARY                       ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""
echo "  PASSED:  $PASSED"
echo "  FAILED:  $FAILED"
echo "  SKIPPED: $SKIPPED"
echo ""
echo "  Detailed logs: $RESULTS_DIR/"
echo ""

# Final cleanup
pkill -9 pivxd 2>/dev/null || true

if [ $FAILED -eq 0 ]; then
    echo "  ╔═════════════════════════════════════════╗"
    echo "  ║  ALL TESTS PASSED - READY FOR TESTNET   ║"
    echo "  ╚═════════════════════════════════════════╝"
    exit 0
else
    echo "  ╔═════════════════════════════════════════╗"
    echo "  ║  SOME TESTS FAILED - REVIEW REQUIRED    ║"
    echo "  ╚═════════════════════════════════════════╝"
    exit 1
fi
