#!/bin/bash
# =============================================================================
# KHU V6 LOCAL TESTNET - Script robuste
# =============================================================================
set -e

SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATADIR="/tmp/khu_testnet"
DAEMON="$SRC/pivxd"
CLI="$SRC/pivx-cli"

echo "=== Nettoyage complet ==="
pkill -9 pivxd 2>/dev/null || true
sleep 3
rm -rf "$DATADIR"
mkdir -p "$DATADIR"/{node1,node2,node3}

# CLI function
rpc() {
    local port=$1
    shift
    $CLI -rpcconnect=127.0.0.1 -rpcport=$port -rpcuser=user -rpcpassword=pass "$@"
}

# Start Node 1
echo "=== Demarrage Node1 ==="
$DAEMON -regtest \
    -datadir="$DATADIR/node1" \
    -port=18444 \
    -rpcport=18441 \
    -rpcbind=127.0.0.1 \
    -rpcuser=user \
    -rpcpassword=pass \
    -rpcallowip=127.0.0.1 \
    -listen=1 \
    -server=1 \
    -fallbackfee=0.0001 \
    -debug=khu \
    -daemon

echo "Attente Node1..."
for i in {1..30}; do
    if rpc 18441 getblockcount >/dev/null 2>&1; then
        echo "Node1 OK"
        break
    fi
    sleep 1
done

# Start Node 2
echo "=== Demarrage Node2 ==="
$DAEMON -regtest \
    -datadir="$DATADIR/node2" \
    -port=18445 \
    -rpcport=18442 \
    -rpcbind=127.0.0.1 \
    -rpcuser=user \
    -rpcpassword=pass \
    -rpcallowip=127.0.0.1 \
    -listen=1 \
    -server=1 \
    -fallbackfee=0.0001 \
    -debug=khu \
    -addnode=127.0.0.1:18444 \
    -daemon

echo "Attente Node2..."
for i in {1..30}; do
    if rpc 18442 getblockcount >/dev/null 2>&1; then
        echo "Node2 OK"
        break
    fi
    sleep 1
done

# Start Node 3
echo "=== Demarrage Node3 ==="
$DAEMON -regtest \
    -datadir="$DATADIR/node3" \
    -port=18446 \
    -rpcport=18443 \
    -rpcbind=127.0.0.1 \
    -rpcuser=user \
    -rpcpassword=pass \
    -rpcallowip=127.0.0.1 \
    -listen=1 \
    -server=1 \
    -fallbackfee=0.0001 \
    -debug=khu \
    -addnode=127.0.0.1:18444 \
    -daemon

echo "Attente Node3..."
for i in {1..30}; do
    if rpc 18443 getblockcount >/dev/null 2>&1; then
        echo "Node3 OK"
        break
    fi
    sleep 1
done

# Force peer connection
echo "=== Connexion peers ==="
rpc 18442 addnode "127.0.0.1:18444" "onetry" 2>/dev/null || true
rpc 18443 addnode "127.0.0.1:18444" "onetry" 2>/dev/null || true
sleep 2

# Generate 800 blocks for V6 + mature coins for PoS (600+ confirmations)
echo "=== Generation 800 blocs (V6 + PoS maturity) ==="
rpc 18441 generate 800 > /dev/null

# Sync
echo "=== Synchronisation ==="
for i in {1..20}; do
    B1=$(rpc 18441 getblockcount)
    B2=$(rpc 18442 getblockcount)
    B3=$(rpc 18443 getblockcount)
    if [ "$B1" = "$B2" ] && [ "$B2" = "$B3" ]; then
        echo "Sync OK: $B1 blocs"
        break
    fi
    echo "Sync... $B1/$B2/$B3"
    sleep 1
done

# Distribute PIV
echo "=== Distribution PIV ==="
ADDR2=$(rpc 18442 getnewaddress)
ADDR3=$(rpc 18443 getnewaddress)
rpc 18441 sendtoaddress "$ADDR2" 10000 > /dev/null
rpc 18441 sendtoaddress "$ADDR3" 5000 > /dev/null
rpc 18441 generate 1 > /dev/null
sleep 2

# Status
echo ""
echo "============================================================"
echo "  TESTNET KHU V6 PRET"
echo "============================================================"
echo "Blocs: $(rpc 18441 getblockcount)"
echo ""
echo "Balances PIV:"
echo "  Node1: $(rpc 18441 getbalance)"
echo "  Node2: $(rpc 18442 getbalance)"
echo "  Node3: $(rpc 18443 getbalance)"
echo ""
echo "============================================================"
echo "  UTILISATION"
echo "============================================================"
echo ""
echo "CLI: /tmp/khu_testnet/cli.sh <node> <commande>"
echo ""
echo "  cli.sh 1 generate 10       # Miner 10 blocs"
echo "  cli.sh 2 khumint 1000      # Mint 1000 KHU"
echo "  cli.sh 2 khustake 500      # Stake 500 KHU"
echo "  cli.sh 2 balance           # Afficher balance complete"
echo ""
echo "Stop: pkill -f pivxd"
echo ""

# Helper script with balance command
cat > "$DATADIR/cli.sh" << 'EOFCLI'
#!/bin/bash
SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$SRC/pivx-cli"

NODE=${1:-1}
CMD=${2:-help}
shift 2 2>/dev/null || shift 1 2>/dev/null || true

PORT=$((18440 + NODE))
RPC="$CLI -rpcconnect=127.0.0.1 -rpcport=$PORT -rpcuser=user -rpcpassword=pass"

# Special command: balance
if [ "$CMD" = "balance" ]; then
    echo "============================================================"
    echo "  NODE $NODE - BALANCE COMPLETE"
    echo "============================================================"
    echo ""

    # PIV libre
    PIV_FREE=$($RPC getbalance)
    echo "PIV Libre:        $PIV_FREE"
    echo ""

    # KHU Balance (from khubalance RPC)
    KHU_BAL=$($RPC khubalance 2>/dev/null)
    if [ -n "$KHU_BAL" ]; then
        KHU_TRANS=$(echo "$KHU_BAL" | grep '"transparent"' | grep -o '[0-9.]*')
        KHU_STAKED=$(echo "$KHU_BAL" | grep '"staked"' | head -1 | grep -o '[0-9.]*')
        KHU_TOTAL=$(echo "$KHU_BAL" | grep '"total"' | grep -o '[0-9.]*')
        echo "KHU transparent:  ${KHU_TRANS:-0}"
        echo "KHU staked:       ${KHU_STAKED:-0}"
        echo "KHU total:        ${KHU_TOTAL:-0}"
    else
        echo "KHU: 0"
    fi
    echo ""

    # Staked notes (ZKHU)
    echo "--- ZKHU Notes Staked ---"
    STAKED=$($RPC khuliststaked 2>/dev/null)
    if echo "$STAKED" | grep -q '"amount"'; then
        echo "$STAKED" | python3 -c "
import sys, json
try:
    data = json.load(sys.stdin)
    notes = data if isinstance(data, list) else data.get('staked_notes', [])
    total = 0
    mature = 0
    immature = 0
    for note in notes:
        amt = note.get('amount', 0)
        total += amt
        if note.get('is_mature', False):
            mature += amt
            status = 'MATURE'
        else:
            immature += amt
            blocks = note.get('blocks_to_maturity', note.get('blocks_staked', '?'))
            status = f'immature ({blocks} blocs)'
        print(f'  {amt} ZKHU - {status}')
    print(f'')
    print(f'Total ZKHU:       {total}')
    print(f'  Mature:         {mature}')
    print(f'  Immature:       {immature}')
except Exception as e:
    print(f'  (erreur: {e})')
" 2>/dev/null || echo "  (aucune note)"
    else
        echo "  (aucune note)"
    fi
    echo ""

    # Global KHU State
    echo "--- ETAT GLOBAL KHU ---"
    STATE=$($RPC getkhustate 2>/dev/null)
    if [ -n "$STATE" ]; then
        C=$(echo "$STATE" | grep '"C"' | grep -o '[0-9.]*')
        U=$(echo "$STATE" | grep '"U"' | grep -o '[0-9.]*')
        Z=$(echo "$STATE" | grep '"Z"' | grep -o '[0-9.]*')
        Cr=$(echo "$STATE" | grep '"Cr"' | grep -o '[0-9.]*')
        Ur=$(echo "$STATE" | grep '"Ur"' | grep -o '[0-9.]*')
        T=$(echo "$STATE" | grep '"T"' | grep -o '[0-9.]*')
        R=$(echo "$STATE" | grep '"R_annual"' | grep -o '[0-9]*')
        HEIGHT=$(echo "$STATE" | grep '"height"' | grep -o '[0-9]*')

        echo "Height:           $HEIGHT"
        echo ""
        echo "C (Collateral):   $C PIV"
        echo "U (KHU supply):   $U KHU"
        echo "Z (ZKHU shielded):$Z ZKHU"
        echo ""
        echo "Cr (Reward pool): $Cr"
        echo "Ur (Reward rights):$Ur"
        echo ""
        echo "T (DAO Treasury): $T PIV"
        echo "R% (Yield rate):  $((R / 100)).$((R % 100))%"
        echo ""

        # Invariants check
        if echo "$STATE" | grep -q '"invariants_ok": true'; then
            echo "Invariants:       OK (C = U + Z)"
        else
            echo "Invariants:       ERREUR!"
        fi
    else
        echo "  V6 non active"
    fi
    echo ""
    echo "============================================================"
    exit 0
fi

# Normal RPC command
$RPC "$CMD" "$@"
EOFCLI
chmod +x "$DATADIR/cli.sh"
