#!/bin/bash
# =============================================================================
# KHU V6 LOCAL TESTNET - avec LLMQ fonctionnel
# =============================================================================
# - 1 Controller + 3 Masternodes DMN/BLS
# - Regtest avec V6 actif
# - Generation lente pour formation LLMQ
# =============================================================================

set -e

SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATADIR="/tmp/khu_mn_llmq"
DAEMON="$SRC/pivxd"
CLI="$SRC/pivx-cli"

# Ports
CTRL_PORT=19100
CTRL_RPC=19101
MN1_PORT=19110
MN1_RPC=19111
MN2_PORT=19120
MN2_RPC=19121
MN3_PORT=19130
MN3_RPC=19131

# RPC helper
rpc() {
    local port=$1
    shift
    $CLI -regtest -rpcconnect=127.0.0.1 -rpcport=$port -rpcuser=khu -rpcpassword=khupass "$@"
}

# Slow block generation for LLMQ
slow_generate() {
    local port=$1
    local count=$2
    local delay=${3:-2}
    echo "  Generating $count blocks slowly (${delay}s interval)..."
    for i in $(seq 1 $count); do
        rpc $port generate 1 > /dev/null
        printf "\r  Block %d/%d" $i $count
        sleep $delay
    done
    echo ""
}

echo "============================================================"
echo "  KHU V6 TESTNET LOCAL - LLMQ READY"
echo "============================================================"
echo ""

# Cleanup
echo "=== Step 1: Cleanup ==="
pkill -9 pivxd 2>/dev/null || true
sleep 3
rm -rf "$DATADIR"
mkdir -p "$DATADIR"/{ctrl,mn1,mn2,mn3}

# Create configs
echo "=== Step 2: Create configs ==="
for node in ctrl mn1 mn2 mn3; do
    cat > "$DATADIR/$node/pivx.conf" << EOF
regtest=1
server=1
listen=1
rpcuser=khu
rpcpassword=khupass
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=llmq
debug=llmq-dkg
debug=khu
EOF
done

# Start Controller
echo "=== Step 3: Start Controller ==="
$DAEMON -regtest \
    -datadir="$DATADIR/ctrl" \
    -port=$CTRL_PORT \
    -rpcport=$CTRL_RPC \
    -rpcbind=127.0.0.1 \
    -daemon

echo "Waiting for controller..."
for i in {1..30}; do
    if rpc $CTRL_RPC getblockcount >/dev/null 2>&1; then
        echo "Controller OK"
        break
    fi
    sleep 1
done

# Generate initial blocks for V6 + funds
echo "=== Step 4: Generate 250 blocks (V6 activation + funds) ==="
rpc $CTRL_RPC generate 250 > /dev/null
BALANCE=$(rpc $CTRL_RPC getbalance)
echo "Balance: $BALANCE PIV"
echo "Block: $(rpc $CTRL_RPC getblockcount)"

# Generate BLS keys
echo "=== Step 5: Generate BLS keys ==="
BLS1=$(rpc $CTRL_RPC generateblskeypair)
BLS2=$(rpc $CTRL_RPC generateblskeypair)
BLS3=$(rpc $CTRL_RPC generateblskeypair)

BLS1_PK=$(echo "$BLS1" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS1_SK=$(echo "$BLS1" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)
BLS2_PK=$(echo "$BLS2" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS2_SK=$(echo "$BLS2" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)
BLS3_PK=$(echo "$BLS3" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS3_SK=$(echo "$BLS3" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)

echo "BLS keys generated"

# Get addresses - IMPORTANT: collateral, owner, voting, and payout must be DIFFERENT
echo "=== Step 6: Get addresses ==="
# MN1 addresses
COLLAT1=$(rpc $CTRL_RPC getnewaddress)
OWNER1=$(rpc $CTRL_RPC getnewaddress)
VOTING1=$(rpc $CTRL_RPC getnewaddress)
PAYOUT1=$(rpc $CTRL_RPC getnewaddress)

# MN2 addresses
COLLAT2=$(rpc $CTRL_RPC getnewaddress)
OWNER2=$(rpc $CTRL_RPC getnewaddress)
VOTING2=$(rpc $CTRL_RPC getnewaddress)
PAYOUT2=$(rpc $CTRL_RPC getnewaddress)

# MN3 addresses
COLLAT3=$(rpc $CTRL_RPC getnewaddress)
OWNER3=$(rpc $CTRL_RPC getnewaddress)
VOTING3=$(rpc $CTRL_RPC getnewaddress)
PAYOUT3=$(rpc $CTRL_RPC getnewaddress)

echo "Addresses generated (12 unique addresses)"

# Register MNs
echo "=== Step 7: Register Masternodes ==="
echo "Registering MN1..."
TX1=$(rpc $CTRL_RPC protx_register_fund "$COLLAT1" "127.0.0.1:$MN1_PORT" "$OWNER1" "$BLS1_PK" "$VOTING1" "$PAYOUT1" 2>&1)
echo "  ProTx: ${TX1:0:20}..."

echo "Registering MN2..."
TX2=$(rpc $CTRL_RPC protx_register_fund "$COLLAT2" "127.0.0.1:$MN2_PORT" "$OWNER2" "$BLS2_PK" "$VOTING2" "$PAYOUT2" 2>&1)
echo "  ProTx: ${TX2:0:20}..."

echo "Registering MN3..."
TX3=$(rpc $CTRL_RPC protx_register_fund "$COLLAT3" "127.0.0.1:$MN3_PORT" "$OWNER3" "$BLS3_PK" "$VOTING3" "$PAYOUT3" 2>&1)
echo "  ProTx: ${TX3:0:20}..."

# Confirm registrations
rpc $CTRL_RPC generate 10 > /dev/null
echo "Block: $(rpc $CTRL_RPC getblockcount)"

# Start MN nodes for sync
echo "=== Step 8a: Start MN nodes for sync ==="
$DAEMON -regtest -datadir="$DATADIR/mn1" -port=$MN1_PORT -rpcport=$MN1_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon
$DAEMON -regtest -datadir="$DATADIR/mn2" -port=$MN2_PORT -rpcport=$MN2_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon
$DAEMON -regtest -datadir="$DATADIR/mn3" -port=$MN3_PORT -rpcport=$MN3_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon

echo "Waiting for sync..."
CTRL_HEIGHT=$(rpc $CTRL_RPC getblockcount)
for i in {1..60}; do
    H1=$(rpc $MN1_RPC getblockcount 2>/dev/null || echo "0")
    H2=$(rpc $MN2_RPC getblockcount 2>/dev/null || echo "0")
    H3=$(rpc $MN3_RPC getblockcount 2>/dev/null || echo "0")
    if [ "$H1" = "$CTRL_HEIGHT" ] && [ "$H2" = "$CTRL_HEIGHT" ] && [ "$H3" = "$CTRL_HEIGHT" ]; then
        echo "  All nodes synced at block $CTRL_HEIGHT"
        break
    fi
    echo "  Sync: CTRL=$CTRL_HEIGHT MN1=$H1 MN2=$H2 MN3=$H3"
    sleep 2
done

# Stop MN nodes
echo "=== Step 8b: Stop MN nodes ==="
rpc $MN1_RPC stop 2>/dev/null || true
rpc $MN2_RPC stop 2>/dev/null || true
rpc $MN3_RPC stop 2>/dev/null || true
sleep 5

# Restart as masternodes
echo "=== Step 8c: Restart as masternodes ==="
$DAEMON -regtest -datadir="$DATADIR/mn1" -port=$MN1_PORT -rpcport=$MN1_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS1_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon

$DAEMON -regtest -datadir="$DATADIR/mn2" -port=$MN2_PORT -rpcport=$MN2_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS2_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon

$DAEMON -regtest -datadir="$DATADIR/mn3" -port=$MN3_PORT -rpcport=$MN3_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS3_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$CTRL_PORT -listen=1 -server=1 -daemon

echo "Waiting for MNs to start..."
sleep 10

# Connect all nodes together for LLMQ
echo "=== Step 9: Connect nodes for LLMQ ==="
rpc $MN1_RPC addnode "127.0.0.1:$MN2_PORT" "onetry" 2>/dev/null || true
rpc $MN1_RPC addnode "127.0.0.1:$MN3_PORT" "onetry" 2>/dev/null || true
rpc $MN2_RPC addnode "127.0.0.1:$MN1_PORT" "onetry" 2>/dev/null || true
rpc $MN2_RPC addnode "127.0.0.1:$MN3_PORT" "onetry" 2>/dev/null || true
rpc $MN3_RPC addnode "127.0.0.1:$MN1_PORT" "onetry" 2>/dev/null || true
rpc $MN3_RPC addnode "127.0.0.1:$MN2_PORT" "onetry" 2>/dev/null || true
sleep 5

# Check MN status
echo "=== Step 10: Check Masternode Status ==="
MN_COUNT=$(rpc $CTRL_RPC getmasternodecount | grep '"enabled"' | grep -o '[0-9]*')
echo "Active Masternodes: $MN_COUNT"

# Check MN statuses
echo ""
echo "MN1 status:"
rpc $MN1_RPC getmasternodestatus 2>&1 | grep -E "status|state" | head -3
echo ""
echo "MN2 status:"
rpc $MN2_RPC getmasternodestatus 2>&1 | grep -E "status|state" | head -3
echo ""
echo "MN3 status:"
rpc $MN3_RPC getmasternodestatus 2>&1 | grep -E "status|state" | head -3

# Generate blocks SLOWLY for LLMQ formation
echo ""
echo "=== Step 11: Generate blocks for LLMQ (SLOW - 2s interval) ==="
echo "LLMQ needs time between blocks for DKG communication..."
slow_generate $CTRL_RPC 30 2

# Check LLMQ status
echo ""
echo "=== Step 12: Check LLMQ Status ==="
LLMQ_STATUS=$(rpc $CTRL_RPC quorum list 2>&1)
echo "$LLMQ_STATUS" | head -20

# Check KHU State
echo ""
echo "=== Step 13: Check KHU State ==="
rpc $CTRL_RPC getkhustate 2>&1 | head -15

# Create CLI helper
echo "=== Step 14: Create CLI helper ==="
cat > "$DATADIR/cli.sh" << 'EOFCLI'
#!/bin/bash
SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$SRC/pivx-cli"

NODE=${1:-ctrl}
CMD=${2:-help}
shift 2 2>/dev/null || shift 1 2>/dev/null || true

case $NODE in
    ctrl) PORT=19101 ;;
    mn1)  PORT=19111 ;;
    mn2)  PORT=19121 ;;
    mn3)  PORT=19131 ;;
    *)    echo "Unknown node: $NODE (use: ctrl, mn1, mn2, mn3)"; exit 1 ;;
esac

$CLI -regtest -rpcconnect=127.0.0.1 -rpcport=$PORT -rpcuser=khu -rpcpassword=khupass "$CMD" "$@"
EOFCLI
chmod +x "$DATADIR/cli.sh"

# Summary
echo ""
echo "============================================================"
echo "  KHU V6 TESTNET LOCAL READY"
echo "============================================================"
echo ""
echo "Network:     regtest (V6 active from block 201)"
echo "Block:       $(rpc $CTRL_RPC getblockcount)"
echo "Masternodes: $MN_COUNT ENABLED"
echo ""
echo "CLI: $DATADIR/cli.sh <node> <command>"
echo ""
echo "Examples:"
echo "  cli.sh ctrl khumint 1000"
echo "  cli.sh ctrl khustake 500"
echo "  cli.sh ctrl getkhustate"
echo "  cli.sh ctrl quorum list"
echo "  cli.sh mn1 getmasternodestatus"
echo ""
echo "For LLMQ formation, generate more blocks slowly:"
echo "  for i in {1..20}; do cli.sh ctrl generate 1; sleep 2; done"
echo ""
echo "Stop: pkill -f pivxd"
echo ""
