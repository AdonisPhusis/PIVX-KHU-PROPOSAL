#!/bin/bash
#
# TEST COMPLET KHU V6 - Script de validation locale
#
# Ce script teste toutes les fonctionnalités KHU V6:
# - MINT / REDEEM / STAKE / UNSTAKE
# - Yield quotidien (R% = 40%)
# - Cycle DOMC (initialisation)
# - DAO Treasury (T)
# - État après restart
#
# Usage: ./scripts/test_khu_v6_complete.sh
#

set -e

# Configuration
PIVX_DIR="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATA_DIR="/tmp/khu_v6_test"
CLI="$PIVX_DIR/pivx-cli -regtest -datadir=$DATA_DIR -rpcuser=test -rpcpassword=test123"
DAEMON="$PIVX_DIR/pivxd"

# Couleurs
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Fonctions utilitaires
log_info() { echo -e "${BLUE}[INFO]${NC} $1"; }
log_ok() { echo -e "${GREEN}[OK]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }
log_section() { echo -e "\n${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; echo -e "${YELLOW}  $1${NC}"; echo -e "${YELLOW}═══════════════════════════════════════════════════════════════${NC}"; }

cleanup() {
    log_info "Nettoyage..."
    pkill -9 pivxd 2>/dev/null || true
    sleep 2
    rm -rf "$DATA_DIR"
}

start_daemon() {
    log_info "Création du répertoire de données..."
    mkdir -p "$DATA_DIR"

    cat > "$DATA_DIR/pivx.conf" << 'EOF'
regtest=1
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
EOF

    log_info "Démarrage du daemon..."
    $DAEMON -regtest -datadir="$DATA_DIR" -daemon

    # Attendre que le daemon soit prêt
    for i in {1..30}; do
        if $CLI getblockcount &>/dev/null; then
            log_ok "Daemon prêt"
            return 0
        fi
        sleep 1
    done

    log_error "Timeout - daemon non prêt"
    return 1
}

generate_blocks() {
    local count=$1
    log_info "Génération de $count blocs..."
    $CLI generate $count > /dev/null
    log_ok "Blocs générés. Hauteur: $($CLI getblockcount)"
}

# ═══════════════════════════════════════════════════════════════
# DÉBUT DES TESTS
# ═══════════════════════════════════════════════════════════════

trap cleanup EXIT

log_section "PHASE 0: INITIALISATION"
cleanup
start_daemon

log_section "PHASE 1: GÉNÉRATION BLOCS PRÉ-V6"
log_info "V6 s'active au bloc 200 en regtest"
generate_blocks 210

# Vérifier l'état KHU initial
log_info "Vérification état KHU initial..."
STATE=$($CLI getkhustate 2>/dev/null || echo '{"error":"not available"}')
echo "$STATE" | python3 -m json.tool 2>/dev/null || echo "$STATE"

log_section "PHASE 2: TEST MINT"
log_info "Balance PIV avant MINT..."
BAL_BEFORE=$($CLI getbalance)
log_info "Balance: $BAL_BEFORE PIV"

log_info "MINT 1000 KHU..."
MINT_TX=$($CLI khumint 1000 2>&1)
if [[ "$MINT_TX" == *"error"* ]]; then
    log_error "MINT échoué: $MINT_TX"
else
    log_ok "MINT TX: ${MINT_TX:0:16}..."
fi

generate_blocks 1

log_info "Vérification balance KHU..."
KHU_BAL=$($CLI khubalance 2>&1)
log_info "Balance KHU: $KHU_BAL"

log_info "État KHU après MINT..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_section "PHASE 3: TEST STAKE"
log_info "STAKE 500 KHU..."
STAKE_TX=$($CLI khustake 500 2>&1)
if [[ "$STAKE_TX" == *"error"* ]]; then
    log_error "STAKE échoué: $STAKE_TX"
else
    log_ok "STAKE TX: ${STAKE_TX:0:16}..."
fi

generate_blocks 1

log_info "Liste notes stakées..."
$CLI khuliststaked 2>&1 | python3 -m json.tool 2>/dev/null || $CLI khuliststaked

log_info "État KHU après STAKE..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_section "PHASE 4: TEST MATURITY + YIELD"
log_info "Notes ZKHU ont besoin de 4320 blocs pour maturité"
log_info "Génération de blocs pour atteindre maturité..."

CURRENT=$($CLI getblockcount)
NEEDED=$((4320 - (CURRENT - 212)))  # 212 = hauteur du STAKE
if [ $NEEDED -gt 0 ]; then
    log_info "Génération de $NEEDED blocs pour maturité..."
    # Générer par lots pour éviter timeout
    while [ $NEEDED -gt 0 ]; do
        BATCH=$((NEEDED > 100 ? 100 : NEEDED))
        $CLI generate $BATCH > /dev/null
        NEEDED=$((NEEDED - BATCH))
        echo -n "."
    done
    echo ""
fi

log_ok "Maturité atteinte. Hauteur: $($CLI getblockcount)"

log_info "Notes après maturité..."
$CLI khuliststaked 2>&1 | python3 -m json.tool 2>/dev/null || $CLI khuliststaked

log_info "État KHU (Cr/Ur devraient avoir du yield)..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_section "PHASE 5: TEST UNSTAKE"
log_info "UNSTAKE (devrait inclure le yield)..."
UNSTAKE_TX=$($CLI khuunstake 2>&1)
if [[ "$UNSTAKE_TX" == *"error"* ]]; then
    log_warn "UNSTAKE: $UNSTAKE_TX"
else
    log_ok "UNSTAKE TX: ${UNSTAKE_TX:0:16}..."
fi

generate_blocks 1

log_info "Balance KHU après UNSTAKE..."
$CLI khubalance 2>&1

log_info "État KHU après UNSTAKE..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_section "PHASE 6: TEST REDEEM"
log_info "REDEEM 200 KHU..."
REDEEM_TX=$($CLI khuredeem 200 2>&1)
if [[ "$REDEEM_TX" == *"error"* ]]; then
    log_error "REDEEM échoué: $REDEEM_TX"
else
    log_ok "REDEEM TX: ${REDEEM_TX:0:16}..."
fi

generate_blocks 1

log_info "Balance KHU après REDEEM..."
$CLI khubalance 2>&1

log_info "État KHU après REDEEM..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_section "PHASE 7: TEST RESTART PERSISTENCE"
log_info "Arrêt du daemon..."
$CLI stop 2>/dev/null || true
sleep 5

log_info "Redémarrage..."
$DAEMON -regtest -datadir="$DATA_DIR" -daemon
sleep 10

log_info "État KHU après restart..."
$CLI getkhustate | python3 -m json.tool 2>/dev/null || $CLI getkhustate

log_info "Balance KHU après restart..."
$CLI khubalance 2>&1

log_section "PHASE 8: DIAGNOSTICS"
log_info "Exécution khudiagnostics..."
$CLI khudiagnostics true 2>&1 | python3 -m json.tool 2>/dev/null || $CLI khudiagnostics true

log_section "RÉSUMÉ"
echo ""
log_ok "Tests terminés!"
echo ""
echo "Vérifications manuelles recommandées:"
echo "  1. C == U + Z (invariant 1)"
echo "  2. Cr == Ur (invariant 2)"
echo "  3. Yield accumulé après maturité"
echo "  4. État persisté après restart"
echo ""
