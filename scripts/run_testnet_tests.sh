#!/bin/bash
#
# KHU V6 Private Testnet Tests
# Usage: ./run_testnet_tests.sh [phase]
# Phases: 1-mint 2-stake 3-yield 4-unstake 5-utxo 6-invariants all
#

set -e

TESTNET_BASE="/tmp/khu_private_testnet"
CLI="$TESTNET_BASE/cli.sh"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_ok() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_section() { echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; echo -e "${YELLOW}  $1${NC}"; echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; }

check_invariants() {
    local node=${1:-miner}
    local STATE=$($CLI $node getkhustate 2>/dev/null)

    local C=$(echo "$STATE" | jq -r '.C // 0')
    local U=$(echo "$STATE" | jq -r '.U // 0')
    local Z=$(echo "$STATE" | jq -r '.Z // 0')
    local Cr=$(echo "$STATE" | jq -r '.Cr // 0')
    local Ur=$(echo "$STATE" | jq -r '.Ur // 0')
    local T=$(echo "$STATE" | jq -r '.T // 0')

    local sum=$((U + Z))

    echo "  C=$C U=$U Z=$Z Cr=$Cr Ur=$Ur T=$T"

    if [ "$C" -eq "$sum" ] && [ "$Cr" -eq "$Ur" ]; then
        log_ok "Invariants OK: C==U+Z ($C==$sum), Cr==Ur ($Cr==$Ur)"
        return 0
    else
        log_fail "Invariant VIOLATION: C=$C vs U+Z=$sum, Cr=$Cr vs Ur=$Ur"
        return 1
    fi
}

generate() {
    local count=${1:-1}
    $CLI miner generate $count > /dev/null
    log_info "Generated $count blocks. Height: $($CLI miner getblockcount)"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 1: MINT/REDEEM TESTS
# ═══════════════════════════════════════════════════════════════
test_phase1_mint() {
    log_section "PHASE 1: MINT/REDEEM HAUTE CHARGE"

    # Ensure V6 is active
    local height=$($CLI miner getblockcount)
    if [ "$height" -lt 200 ]; then
        log_info "Generating blocks for V6 activation..."
        generate $((210 - height))
    fi

    # TEST 1.1: Rapid MINTs
    log_info "TEST 1.1: Rapid MINT sequence (20x)"
    local success=0
    for i in {1..20}; do
        if $CLI wallet khumint 100 > /dev/null 2>&1; then
            ((success++))
        fi
    done
    generate 1
    log_info "Successful MINTs: $success/20"
    check_invariants wallet

    # TEST 1.2: Rapid REDEEMs
    log_info "TEST 1.2: Rapid REDEEM sequence (10x)"
    success=0
    for i in {1..10}; do
        if $CLI wallet khuredeem 100 > /dev/null 2>&1; then
            ((success++))
        fi
    done
    generate 1
    log_info "Successful REDEEMs: $success/10"
    check_invariants wallet

    # TEST 1.3: Check balances
    log_info "TEST 1.3: Final balances"
    echo "  KHU Balance: $($CLI wallet khubalance 2>&1)"
    echo "  PIV Balance: $($CLI wallet getbalance)"

    log_ok "PHASE 1 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 2: MULTI-STAKE TESTS
# ═══════════════════════════════════════════════════════════════
test_phase2_stake() {
    log_section "PHASE 2: MULTI-STAKE OPERATIONS"

    # Prepare: Ensure enough KHU
    log_info "Preparing: MINT 5000 KHU"
    $CLI wallet khumint 5000 > /dev/null 2>&1 || true
    generate 1

    # TEST 2.1: Multi-stake
    log_info "TEST 2.1: Creating multiple ZKHU notes"
    $CLI wallet khustake 1000 > /dev/null 2>&1 || log_fail "STAKE 1000 failed"
    generate 1
    $CLI wallet khustake 1500 > /dev/null 2>&1 || log_fail "STAKE 1500 failed"
    generate 1
    $CLI wallet khustake 2000 > /dev/null 2>&1 || log_fail "STAKE 2000 failed"
    generate 1

    # List staked notes
    log_info "TEST 2.2: Staked notes listing"
    $CLI wallet khuliststaked

    # Check state
    log_info "TEST 2.3: State after multi-stake"
    check_invariants wallet

    log_ok "PHASE 2 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 3: YIELD ACCUMULATION
# ═══════════════════════════════════════════════════════════════
test_phase3_yield() {
    log_section "PHASE 3: YIELD ACCUMULATION"

    # Get initial state
    log_info "Initial Cr/Ur state:"
    $CLI wallet getkhustate | jq '{Cr, Ur, R_annual}'

    # Generate 1 day (1440 blocks)
    log_info "Generating 1440 blocks (1 day)..."
    local batches=$((1440 / 100))
    for i in $(seq 1 $batches); do
        $CLI miner generate 100 > /dev/null
        echo -n "."
    done
    $CLI miner generate $((1440 - batches * 100)) > /dev/null
    echo ""

    # Check yield
    log_info "Cr/Ur after 1 day:"
    $CLI wallet getkhustate | jq '{Cr, Ur, R_annual}'
    check_invariants wallet

    log_ok "PHASE 3 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 4: UNSTAKE WITH YIELD
# ═══════════════════════════════════════════════════════════════
test_phase4_unstake() {
    log_section "PHASE 4: ATOMIC UNSTAKE WITH YIELD"

    # Check maturity
    log_info "Checking note maturity..."
    $CLI wallet khuliststaked

    # Get state before
    log_info "State BEFORE unstake:"
    local BEFORE=$($CLI wallet getkhustate)
    echo "$BEFORE" | jq '{C, U, Z, Cr, Ur}'

    # Unstake
    log_info "Executing UNSTAKE..."
    RESULT=$($CLI wallet khuunstake 2>&1)
    echo "Result: $RESULT"
    generate 1

    # Get state after
    log_info "State AFTER unstake:"
    local AFTER=$($CLI wallet getkhustate)
    echo "$AFTER" | jq '{C, U, Z, Cr, Ur}'

    # Verify atomic changes
    local Z_before=$(echo "$BEFORE" | jq -r '.Z')
    local Z_after=$(echo "$AFTER" | jq -r '.Z')
    local Cr_before=$(echo "$BEFORE" | jq -r '.Cr')
    local Cr_after=$(echo "$AFTER" | jq -r '.Cr')

    log_info "Z change: $Z_before -> $Z_after"
    log_info "Cr change: $Cr_before -> $Cr_after"

    check_invariants wallet

    log_ok "PHASE 4 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 5: UTXO PIPELINE
# ═══════════════════════════════════════════════════════════════
test_phase5_utxo() {
    log_section "PHASE 5: COMPLETE UTXO PIPELINE"

    # Fresh start
    local PIV_BEFORE=$($CLI wallet getbalance)
    log_info "PIV before cycle: $PIV_BEFORE"

    # MINT -> STAKE -> (wait maturity) -> UNSTAKE -> REDEEM
    log_info "Step 1: MINT 500 KHU"
    $CLI wallet khumint 500 > /dev/null 2>&1
    generate 1

    log_info "Step 2: STAKE 500 KHU"
    $CLI wallet khustake 500 > /dev/null 2>&1
    generate 1

    log_info "Step 3: Wait maturity (4320 blocks)..."
    local current=$($CLI miner getblockcount)
    local needed=4320
    while [ $needed -gt 0 ]; do
        local batch=$((needed > 500 ? 500 : needed))
        $CLI miner generate $batch > /dev/null
        needed=$((needed - batch))
        echo -n "."
    done
    echo ""

    log_info "Step 4: UNSTAKE with yield"
    $CLI wallet khuunstake > /dev/null 2>&1 || log_fail "UNSTAKE failed"
    generate 1

    log_info "Step 5: REDEEM all KHU"
    local khu_bal=$($CLI wallet khubalance 2>&1 | grep -oP '\d+' | head -1)
    if [ -n "$khu_bal" ] && [ "$khu_bal" -gt 0 ]; then
        $CLI wallet khuredeem $khu_bal > /dev/null 2>&1 || log_fail "REDEEM failed"
        generate 1
    fi

    local PIV_AFTER=$($CLI wallet getbalance)
    log_info "PIV after cycle: $PIV_AFTER"

    if (( $(echo "$PIV_AFTER > $PIV_BEFORE" | bc -l) )); then
        log_ok "Yield earned! ($PIV_BEFORE -> $PIV_AFTER)"
    else
        log_fail "No yield earned"
    fi

    check_invariants wallet

    log_ok "PHASE 5 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# PHASE 6: GLOBAL INVARIANTS
# ═══════════════════════════════════════════════════════════════
test_phase6_invariants() {
    log_section "PHASE 6: GLOBAL INVARIANT VERIFICATION"

    log_info "Checking all nodes..."
    for node in miner wallet mn1 mn2 mn3; do
        echo -n "  $node: "
        if check_invariants $node > /dev/null 2>&1; then
            echo -e "${GREEN}OK${NC}"
        else
            echo -e "${RED}FAIL${NC}"
        fi
    done

    # Detailed state from miner
    log_info "Detailed consensus state:"
    $CLI miner getkhustate | jq '.'

    log_ok "PHASE 6 COMPLETE"
}

# ═══════════════════════════════════════════════════════════════
# MAIN
# ═══════════════════════════════════════════════════════════════

# Check testnet is running
if ! $CLI miner getblockcount > /dev/null 2>&1; then
    log_fail "Testnet not running. Start with: ./setup_private_testnet_v2.sh"
    exit 1
fi

case "${1:-all}" in
    1|mint) test_phase1_mint ;;
    2|stake) test_phase2_stake ;;
    3|yield) test_phase3_yield ;;
    4|unstake) test_phase4_unstake ;;
    5|utxo) test_phase5_utxo ;;
    6|invariants) test_phase6_invariants ;;
    all)
        test_phase1_mint
        test_phase2_stake
        test_phase3_yield
        test_phase4_unstake
        test_phase5_utxo
        test_phase6_invariants
        log_section "ALL TESTS COMPLETE"
        ;;
    *)
        echo "Usage: $0 [phase]"
        echo "Phases: 1-mint 2-stake 3-yield 4-unstake 5-utxo 6-invariants all"
        ;;
esac
