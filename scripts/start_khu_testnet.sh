#!/bin/bash
# =============================================================================
# KHU V6 PRIVATE TESTNET - Internet Ready
# =============================================================================
# This script creates a private testnet with:
# - 1 Seed/Controller node
# - 3 Masternodes with BLS/DMN
# - V6/KHU active from block 201
# - LLMQ_TEST quorum (3 MNs)
# =============================================================================

set -e

# Configuration
SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DATADIR="/tmp/khu_public_testnet"
DAEMON="$SRC/pivxd"
CLI="$SRC/pivx-cli"

# Network mode: use -testnet for KHU-native params
# (V6 active from block 201, LLMQ_TEST for chainlocks)
NETWORK="-testnet"

# Ports for testnet mode
SEED_PORT=51474
SEED_RPC=51475
MN1_PORT=51476
MN1_RPC=51477
MN2_PORT=51478
MN2_RPC=51479
MN3_PORT=51480
MN3_RPC=51481

# Get external IP (for internet access)
EXTERNAL_IP=$(curl -s https://api.ipify.org 2>/dev/null || echo "127.0.0.1")
echo "External IP: $EXTERNAL_IP"

# RPC helper
rpc() {
    local port=$1
    shift
    $CLI $NETWORK -rpcconnect=127.0.0.1 -rpcport=$port -rpcuser=khu -rpcpassword=khupass "$@"
}

echo "============================================================"
echo "  KHU V6 PRIVATE TESTNET (Internet Ready)"
echo "============================================================"
echo ""
echo "Network: testnet (V6 active from block 201)"
echo "External IP: $EXTERNAL_IP"
echo ""

# Cleanup
echo "=== Step 1: Cleanup ==="
pkill -9 pivxd 2>/dev/null || true
sleep 3
rm -rf "$DATADIR"
mkdir -p "$DATADIR"/{seed,mn1,mn2,mn3}

# Create configs
echo "=== Step 2: Create configs ==="

# Seed node config
cat > "$DATADIR/seed/pivx.conf" << EOF
testnet=1
server=1
listen=1
rpcuser=khu
rpcpassword=khupass
rpcallowip=0.0.0.0/0
rpcbind=0.0.0.0
fallbackfee=0.0001
debug=khu
debug=llmq
externalip=$EXTERNAL_IP
EOF

# MN configs (basic, MN options added via command line)
for node in mn1 mn2 mn3; do
    cat > "$DATADIR/$node/pivx.conf" << EOF
testnet=1
server=1
listen=1
rpcuser=khu
rpcpassword=khupass
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
debug=llmq
EOF
done

# Start seed node
echo "=== Step 3: Start Seed Node ==="
$DAEMON $NETWORK \
    -datadir="$DATADIR/seed" \
    -port=$SEED_PORT \
    -rpcport=$SEED_RPC \
    -rpcbind=0.0.0.0 \
    -daemon

echo "Waiting for seed node..."
for i in {1..30}; do
    if rpc $SEED_RPC getblockcount >/dev/null 2>&1; then
        echo "Seed node OK"
        break
    fi
    sleep 1
done

# Generate initial blocks
echo "=== Step 4: Generate 250 blocks (V6 activation + funds) ==="
rpc $SEED_RPC generate 250 > /dev/null
BALANCE=$(rpc $SEED_RPC getbalance)
echo "Balance: $BALANCE PIV"
echo "Block: $(rpc $SEED_RPC getblockcount)"

# Generate BLS keys
echo "=== Step 5: Generate BLS keys ==="
BLS1=$(rpc $SEED_RPC generateblskeypair)
BLS2=$(rpc $SEED_RPC generateblskeypair)
BLS3=$(rpc $SEED_RPC generateblskeypair)

BLS1_PK=$(echo "$BLS1" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS1_SK=$(echo "$BLS1" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)
BLS2_PK=$(echo "$BLS2" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS2_SK=$(echo "$BLS2" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)
BLS3_PK=$(echo "$BLS3" | grep -o '"public": "[^"]*' | cut -d'"' -f4)
BLS3_SK=$(echo "$BLS3" | grep -o '"secret": "[^"]*' | cut -d'"' -f4)

echo "BLS keys generated"

# Get addresses
echo "=== Step 6: Get addresses ==="
ADDR1=$(rpc $SEED_RPC getnewaddress)
ADDR2=$(rpc $SEED_RPC getnewaddress)
ADDR3=$(rpc $SEED_RPC getnewaddress)
PAYOUT1=$(rpc $SEED_RPC getnewaddress)
PAYOUT2=$(rpc $SEED_RPC getnewaddress)
PAYOUT3=$(rpc $SEED_RPC getnewaddress)

# Register MNs
echo "=== Step 7: Register Masternodes ==="
echo "Registering MN1..."
TX1=$(rpc $SEED_RPC protx_register_fund "$ADDR1" "127.0.0.1:$MN1_PORT" "$ADDR1" "$BLS1_PK" "$ADDR1" "$PAYOUT1" 2>&1)
echo "  ProTx: ${TX1:0:20}..."

echo "Registering MN2..."
TX2=$(rpc $SEED_RPC protx_register_fund "$ADDR2" "127.0.0.1:$MN2_PORT" "$ADDR2" "$BLS2_PK" "$ADDR2" "$PAYOUT2" 2>&1)
echo "  ProTx: ${TX2:0:20}..."

echo "Registering MN3..."
TX3=$(rpc $SEED_RPC protx_register_fund "$ADDR3" "127.0.0.1:$MN3_PORT" "$ADDR3" "$BLS3_PK" "$ADDR3" "$PAYOUT3" 2>&1)
echo "  ProTx: ${TX3:0:20}..."

# Confirm registrations
echo "=== Step 8: Confirm registrations ==="
rpc $SEED_RPC generate 10 > /dev/null
echo "Block: $(rpc $SEED_RPC getblockcount)"

# Start MN nodes for sync
echo "=== Step 9a: Start MN nodes for sync ==="
$DAEMON $NETWORK -datadir="$DATADIR/mn1" -port=$MN1_PORT -rpcport=$MN1_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon
$DAEMON $NETWORK -datadir="$DATADIR/mn2" -port=$MN2_PORT -rpcport=$MN2_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon
$DAEMON $NETWORK -datadir="$DATADIR/mn3" -port=$MN3_PORT -rpcport=$MN3_RPC -rpcbind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon

echo "Waiting for sync..."
SEED_HEIGHT=$(rpc $SEED_RPC getblockcount)
for i in {1..60}; do
    H1=$(rpc $MN1_RPC getblockcount 2>/dev/null || echo "0")
    H2=$(rpc $MN2_RPC getblockcount 2>/dev/null || echo "0")
    H3=$(rpc $MN3_RPC getblockcount 2>/dev/null || echo "0")
    if [ "$H1" = "$SEED_HEIGHT" ] && [ "$H2" = "$SEED_HEIGHT" ] && [ "$H3" = "$SEED_HEIGHT" ]; then
        echo "  All nodes synced at block $SEED_HEIGHT"
        break
    fi
    echo "  Sync: SEED=$SEED_HEIGHT MN1=$H1 MN2=$H2 MN3=$H3"
    sleep 2
done

# Stop MN nodes
echo "=== Step 9b: Stop MN nodes ==="
rpc $MN1_RPC stop 2>/dev/null || true
rpc $MN2_RPC stop 2>/dev/null || true
rpc $MN3_RPC stop 2>/dev/null || true
sleep 5

# Restart as masternodes
echo "=== Step 9c: Restart as masternodes ==="
$DAEMON $NETWORK -datadir="$DATADIR/mn1" -port=$MN1_PORT -rpcport=$MN1_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS1_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon

$DAEMON $NETWORK -datadir="$DATADIR/mn2" -port=$MN2_PORT -rpcport=$MN2_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS2_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon

$DAEMON $NETWORK -datadir="$DATADIR/mn3" -port=$MN3_PORT -rpcport=$MN3_RPC -rpcbind=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS3_SK" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:$SEED_PORT -listen=1 -server=1 -daemon

echo "Waiting for MNs to start..."
sleep 10

# Check MN status
echo "=== Step 10: Check Masternode Status ==="
MN_COUNT=$(rpc $SEED_RPC getmasternodecount | grep '"enabled"' | grep -o '[0-9]*')
echo "Active Masternodes: $MN_COUNT"

# Generate more blocks for LLMQ
echo "=== Step 11: Generate blocks for LLMQ ==="
rpc $SEED_RPC generate 50 > /dev/null
echo "Block: $(rpc $SEED_RPC getblockcount)"

# Check KHU State
echo "=== Step 12: Check KHU State ==="
rpc $SEED_RPC getkhustate 2>&1 | head -15

# Create CLI helper
echo "=== Step 13: Create CLI helper ==="
cat > "$DATADIR/cli.sh" << 'EOFCLI'
#!/bin/bash
SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$SRC/pivx-cli"

NODE=${1:-seed}
CMD=${2:-help}
shift 2 2>/dev/null || shift 1 2>/dev/null || true

case $NODE in
    seed) PORT=51475 ;;
    mn1)  PORT=51477 ;;
    mn2)  PORT=51479 ;;
    mn3)  PORT=51481 ;;
    *)    echo "Unknown node: $NODE"; exit 1 ;;
esac

$CLI -testnet -rpcconnect=127.0.0.1 -rpcport=$PORT -rpcuser=khu -rpcpassword=khupass "$CMD" "$@"
EOFCLI
chmod +x "$DATADIR/cli.sh"

# Summary
echo ""
echo "============================================================"
echo "  KHU V6 PRIVATE TESTNET READY"
echo "============================================================"
echo ""
echo "Network:     testnet (V6 active from block 201)"
echo "Block:       $(rpc $SEED_RPC getblockcount)"
echo "Masternodes: $MN_COUNT"
echo ""
echo "External IP: $EXTERNAL_IP"
echo "Seed Port:   $SEED_PORT"
echo ""
echo "CLI: $DATADIR/cli.sh <node> <command>"
echo ""
echo "Examples:"
echo "  cli.sh seed khumint 1000"
echo "  cli.sh seed khustake 500"
echo "  cli.sh seed balance"
echo "  cli.sh seed getkhustate"
echo "  cli.sh mn1 getmasternodestatus"
echo ""
echo "Stop: pkill -f pivxd"
echo ""
