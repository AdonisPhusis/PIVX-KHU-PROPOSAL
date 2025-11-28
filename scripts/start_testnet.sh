#!/bin/bash
#
# Demarrer/Arreter les 3 nodes testnet KHU
# Usage: ./start_testnet.sh [start|stop|status|init]
#

PIVX_DIR="/opt/pivx-khu"
CLI="$PIVX_DIR/bin/pivx-cli"
DAEMON="$PIVX_DIR/bin/pivxd"

# Aliases pour faciliter
cli1() { $CLI -datadir=$PIVX_DIR/data/node1 "$@"; }
cli2() { $CLI -datadir=$PIVX_DIR/data/node2 "$@"; }
cli3() { $CLI -datadir=$PIVX_DIR/data/node3 "$@"; }

start_nodes() {
    echo "=== Demarrage Node 1 (Genesis) ==="
    $DAEMON -datadir=$PIVX_DIR/data/node1 -daemon
    sleep 3

    echo "=== Demarrage Node 2 ==="
    $DAEMON -datadir=$PIVX_DIR/data/node2 -daemon
    sleep 3

    echo "=== Demarrage Node 3 (Explorer) ==="
    $DAEMON -datadir=$PIVX_DIR/data/node3 -daemon
    sleep 5

    echo ""
    echo "=== Status ==="
    status_nodes
}

stop_nodes() {
    echo "=== Arret des nodes ==="
    cli1 stop 2>/dev/null || true
    cli2 stop 2>/dev/null || true
    cli3 stop 2>/dev/null || true
    sleep 5
    echo "Nodes arretes."
}

status_nodes() {
    echo "Node 1: $(cli1 getblockcount 2>/dev/null || echo 'OFFLINE')"
    echo "Node 2: $(cli2 getblockcount 2>/dev/null || echo 'OFFLINE')"
    echo "Node 3: $(cli3 getblockcount 2>/dev/null || echo 'OFFLINE')"
    echo ""
    echo "Connections:"
    echo "  Node 1: $(cli1 getconnectioncount 2>/dev/null || echo '0')"
    echo "  Node 2: $(cli2 getconnectioncount 2>/dev/null || echo '0')"
    echo "  Node 3: $(cli3 getconnectioncount 2>/dev/null || echo '0')"
}

init_genesis() {
    echo "=== Initialisation Genesis (Node 1) ==="

    # Verifier node1 actif
    if ! cli1 getblockcount > /dev/null 2>&1; then
        echo "Erreur: Node 1 non demarre. Lancez d'abord: $0 start"
        exit 1
    fi

    echo "Generation 250 blocs pour activer V6..."
    cli1 generate 250 > /dev/null

    sleep 5
    echo ""
    echo "=== V6 Status ==="
    echo "Block height: $(cli1 getblockcount)"
    echo ""
    echo "KHU State:"
    cli1 getkhustate

    echo ""
    echo "=== Attente sync autres nodes ==="
    sleep 10
    status_nodes
}

test_khu() {
    echo "=== Test KHU sur Node 1 ==="

    echo "1. Balance PIV:"
    cli1 getbalance

    echo ""
    echo "2. MINT 100 KHU:"
    cli1 khumint 100
    cli1 generate 1 > /dev/null

    echo ""
    echo "3. KHU State:"
    cli1 getkhustate | grep -E '"C"|"U"|invariants'

    echo ""
    echo "4. KHU Balance:"
    cli1 khubalance

    echo ""
    echo "=== Verification sync Node 2 ==="
    sleep 5
    cli2 getkhustate | grep -E '"C"|"U"|invariants'
}

case "$1" in
    start)
        start_nodes
        ;;
    stop)
        stop_nodes
        ;;
    status)
        status_nodes
        ;;
    init)
        init_genesis
        ;;
    test)
        test_khu
        ;;
    *)
        echo "Usage: $0 {start|stop|status|init|test}"
        echo ""
        echo "  start  - Demarrer les 3 nodes"
        echo "  stop   - Arreter les 3 nodes"
        echo "  status - Afficher l'etat"
        echo "  init   - Generer genesis (250 blocs, activer V6)"
        echo "  test   - Tester KHU (MINT)"
        exit 1
        ;;
esac
