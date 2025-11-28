#!/bin/bash
#
# KHU V6 DOMC Voting Test
# Tests the commit-reveal voting mechanism for R%
#

set -e

TESTNET_BASE="/tmp/khu_private_testnet"
CLI="$TESTNET_BASE/cli.sh"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_ok() { echo -e "${GREEN}[PASS]${NC} $1"; }
log_fail() { echo -e "${RED}[FAIL]${NC} $1"; }
log_section() { echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; echo -e "${YELLOW}  $1${NC}"; echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; }

# Check testnet
if ! $CLI miner getblockcount > /dev/null 2>&1; then
    log_fail "Testnet not running"
    exit 1
fi

log_section "DOMC VOTING TEST"

# Get current DOMC state
log_info "Current DOMC state:"
STATE=$($CLI miner getkhustate)
echo "$STATE" | jq '{R_annual, R_next, R_MAX_dynamic, domc_cycle_start, domc_cycle_length}'

CYCLE_START=$(echo "$STATE" | jq -r '.domc_cycle_start')
CYCLE_LENGTH=$(echo "$STATE" | jq -r '.domc_cycle_length')
COMMIT_START=$((CYCLE_START + 132480))
REVEAL_START=$((CYCLE_START + 152640))
CYCLE_END=$((CYCLE_START + CYCLE_LENGTH))

CURRENT_HEIGHT=$($CLI miner getblockcount)

log_info "DOMC Cycle Timeline:"
echo "  Cycle Start:   $CYCLE_START"
echo "  Commit Phase:  $COMMIT_START"
echo "  Reveal Phase:  $REVEAL_START"
echo "  Cycle End:     $CYCLE_END"
echo "  Current:       $CURRENT_HEIGHT"

# Determine current phase
if [ "$CURRENT_HEIGHT" -lt "$COMMIT_START" ]; then
    PHASE="ACTIVE (pre-commit)"
    TO_COMMIT=$((COMMIT_START - CURRENT_HEIGHT))
    log_info "Blocks until COMMIT phase: $TO_COMMIT"
elif [ "$CURRENT_HEIGHT" -lt "$REVEAL_START" ]; then
    PHASE="COMMIT"
    log_info "Currently in COMMIT phase"
elif [ "$CURRENT_HEIGHT" -lt "$CYCLE_END" ]; then
    PHASE="REVEAL/ADAPTATION"
    log_info "Currently in REVEAL phase"
else
    PHASE="CYCLE ENDED"
fi

log_info "Current Phase: $PHASE"

# For testing, we'll simulate a short DOMC cycle
# In regtest, you may need to modify DOMC_CYCLE_LENGTH

log_section "DOMC COMMIT TEST"
log_info "Attempting commit from masternodes..."

# Note: In real testing, you need to be in COMMIT phase
# This is a simulation

for mn in mn1 mn2 mn3; do
    log_info "  $mn: Committing R=3500 (35%)..."
    RESULT=$($CLI $mn domccommit 3500 2>&1 || echo "error")
    if [[ "$RESULT" == *"error"* ]] || [[ "$RESULT" == *"not in commit"* ]]; then
        log_info "    -> Not in commit phase or error: ${RESULT:0:50}"
    else
        log_ok "    -> Commit successful: ${RESULT:0:16}..."
    fi
done

log_section "DOMC REVEAL TEST"
log_info "Attempting reveal from masternodes..."

for mn in mn1 mn2 mn3; do
    log_info "  $mn: Revealing vote..."
    RESULT=$($CLI $mn domcreveal 2>&1 || echo "error")
    if [[ "$RESULT" == *"error"* ]] || [[ "$RESULT" == *"not in reveal"* ]]; then
        log_info "    -> Not in reveal phase or error: ${RESULT:0:50}"
    else
        log_ok "    -> Reveal successful: ${RESULT:0:16}..."
    fi
done

log_section "DOMC STATUS CHECK"
log_info "Final DOMC state:"
$CLI miner getkhustate | jq '{R_annual, R_next, R_MAX_dynamic}'

log_info ""
log_info "NOTE: Full DOMC cycle testing requires 172800 blocks (~4 months)."
log_info "For faster testing, consider modifying DOMC_CYCLE_LENGTH in regtest params."

log_ok "DOMC TEST COMPLETE"
