#!/bin/bash
# KHU Testnet - Masternode Setup Script
# Requires: 3 masternodes minimum for LLMQ

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"
CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"
TESTNET_DIR="/tmp/khu_testnet_mn"

# Masternode collateral (10,000 PIV for testnet)
MN_COLLATERAL=10000

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║       KHU TESTNET - MASTERNODE SETUP (3 MN minimum)          ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""

# Cleanup
pkill -9 pivxd 2>/dev/null || true
sleep 3
rm -rf $TESTNET_DIR
mkdir -p $TESTNET_DIR/{controller,mn1,mn2,mn3}

echo "=== Step 1: Create configurations ==="

# Controller node (mines blocks, creates collateral)
cat > $TESTNET_DIR/controller/pivx.conf << 'EOF'
regtest=1
server=1
listen=1
port=19000
rpcport=19001
rpcuser=controller
rpcpassword=controller123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
debug=masternode
# Connect to MNs
addnode=127.0.0.1:19100
addnode=127.0.0.1:19200
addnode=127.0.0.1:19300
EOF

# Masternode 1
cat > $TESTNET_DIR/mn1/pivx.conf << 'EOF'
regtest=1
server=1
listen=1
port=19100
rpcport=19101
rpcuser=mn1
rpcpassword=mn1pass123
rpcallowip=127.0.0.1
masternode=1
masternodeprivkey=TO_BE_GENERATED
externalip=127.0.0.1:19100
fallbackfee=0.0001
debug=khu
debug=masternode
addnode=127.0.0.1:19000
EOF

# Masternode 2
cat > $TESTNET_DIR/mn2/pivx.conf << 'EOF'
regtest=1
server=1
listen=1
port=19200
rpcport=19201
rpcuser=mn2
rpcpassword=mn2pass123
rpcallowip=127.0.0.1
masternode=1
masternodeprivkey=TO_BE_GENERATED
externalip=127.0.0.1:19200
fallbackfee=0.0001
debug=khu
debug=masternode
addnode=127.0.0.1:19000
EOF

# Masternode 3
cat > $TESTNET_DIR/mn3/pivx.conf << 'EOF'
regtest=1
server=1
listen=1
port=19300
rpcport=19301
rpcuser=mn3
rpcpassword=mn3pass123
rpcallowip=127.0.0.1
masternode=1
masternodeprivkey=TO_BE_GENERATED
externalip=127.0.0.1:19300
fallbackfee=0.0001
debug=khu
debug=masternode
addnode=127.0.0.1:19000
EOF

echo "   Configurations created"

echo ""
echo "=== Step 2: Start controller node ==="
$PIVXD -regtest -datadir=$TESTNET_DIR/controller -daemon
sleep 5

CTRL="$CLI -regtest -datadir=$TESTNET_DIR/controller -rpcport=19001"

# Generate blocks to have coins
echo "   Mining initial blocks..."
$CTRL generate 200 > /dev/null
echo "   Balance: $($CTRL getbalance) PIV"

echo ""
echo "=== Step 3: Generate masternode keys ==="

# Generate 3 masternode keys
MN1_KEY=$($CTRL masternode genkey)
MN2_KEY=$($CTRL masternode genkey)
MN3_KEY=$($CTRL masternode genkey)

echo "   MN1 key: $MN1_KEY"
echo "   MN2 key: $MN2_KEY"
echo "   MN3 key: $MN3_KEY"

# Update configs with keys
sed -i "s/masternodeprivkey=TO_BE_GENERATED/masternodeprivkey=$MN1_KEY/" $TESTNET_DIR/mn1/pivx.conf
sed -i "s/masternodeprivkey=TO_BE_GENERATED/masternodeprivkey=$MN2_KEY/" $TESTNET_DIR/mn2/pivx.conf
sed -i "s/masternodeprivkey=TO_BE_GENERATED/masternodeprivkey=$MN3_KEY/" $TESTNET_DIR/mn3/pivx.conf

echo ""
echo "=== Step 4: Create collateral transactions ==="

# Get addresses for collateral
MN1_ADDR=$($CTRL getnewaddress "mn1_collateral")
MN2_ADDR=$($CTRL getnewaddress "mn2_collateral")
MN3_ADDR=$($CTRL getnewaddress "mn3_collateral")

echo "   MN1 collateral address: $MN1_ADDR"
echo "   MN2 collateral address: $MN2_ADDR"
echo "   MN3 collateral address: $MN3_ADDR"

# Send collateral (10,000 PIV each)
echo "   Sending collateral..."
MN1_TXID=$($CTRL sendtoaddress "$MN1_ADDR" $MN_COLLATERAL)
MN2_TXID=$($CTRL sendtoaddress "$MN2_ADDR" $MN_COLLATERAL)
MN3_TXID=$($CTRL sendtoaddress "$MN3_ADDR" $MN_COLLATERAL)

echo "   MN1 collateral txid: $MN1_TXID"
echo "   MN2 collateral txid: $MN2_TXID"
echo "   MN3 collateral txid: $MN3_TXID"

# Confirm collateral
$CTRL generate 15 > /dev/null
echo "   Collateral confirmed (15 blocks)"

echo ""
echo "=== Step 5: Get collateral output indices ==="

# Find output index for each collateral tx
get_vout() {
    local txid=$1
    local amount=$2
    $CTRL getrawtransaction "$txid" 1 | grep -A5 "\"value\": $amount" | grep -oP '"n": \K[0-9]+' | head -1
}

MN1_VOUT=$(get_vout $MN1_TXID $MN_COLLATERAL)
MN2_VOUT=$(get_vout $MN2_TXID $MN_COLLATERAL)
MN3_VOUT=$(get_vout $MN3_TXID $MN_COLLATERAL)

echo "   MN1 output: $MN1_TXID-$MN1_VOUT"
echo "   MN2 output: $MN2_TXID-$MN2_VOUT"
echo "   MN3 output: $MN3_TXID-$MN3_VOUT"

echo ""
echo "=== Step 6: Create masternode.conf ==="

cat > $TESTNET_DIR/controller/masternode.conf << EOF
# Masternode config file
# Format: alias IP:port masternodeprivkey collateral_txid collateral_output_index
mn1 127.0.0.1:19100 $MN1_KEY $MN1_TXID $MN1_VOUT
mn2 127.0.0.1:19200 $MN2_KEY $MN2_TXID $MN2_VOUT
mn3 127.0.0.1:19300 $MN3_KEY $MN3_TXID $MN3_VOUT
EOF

echo "   masternode.conf created"
cat $TESTNET_DIR/controller/masternode.conf

echo ""
echo "=== Step 7: Start masternode daemons ==="

# Restart controller to load masternode.conf
$CTRL stop 2>/dev/null || true
sleep 3
$PIVXD -regtest -datadir=$TESTNET_DIR/controller -daemon
sleep 5

# Start MN nodes
$PIVXD -regtest -datadir=$TESTNET_DIR/mn1 -daemon
$PIVXD -regtest -datadir=$TESTNET_DIR/mn2 -daemon
$PIVXD -regtest -datadir=$TESTNET_DIR/mn3 -daemon
sleep 8

echo "   All nodes started"

echo ""
echo "=== Step 8: Start masternodes ==="

CTRL="$CLI -regtest -datadir=$TESTNET_DIR/controller -rpcport=19001"

# Wait for sync
echo "   Waiting for nodes to sync..."
sleep 5
$CTRL generate 10 > /dev/null

# Start masternodes
echo "   Starting mn1..."
$CTRL masternode start-alias mn1 || echo "   (may need manual start)"
echo "   Starting mn2..."
$CTRL masternode start-alias mn2 || echo "   (may need manual start)"
echo "   Starting mn3..."
$CTRL masternode start-alias mn3 || echo "   (may need manual start)"

# Generate more blocks for MN activation
$CTRL generate 20 > /dev/null

echo ""
echo "=== Step 9: Check masternode status ==="
sleep 5
echo ""
echo "Masternode list:"
$CTRL masternode list

echo ""
echo "Masternode count:"
$CTRL masternode count

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║                  MASTERNODE SETUP COMPLETE                    ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Controller: $CLI -regtest -datadir=$TESTNET_DIR/controller -rpcport=19001"
echo "  MN1:        $CLI -regtest -datadir=$TESTNET_DIR/mn1 -rpcport=19101"
echo "  MN2:        $CLI -regtest -datadir=$TESTNET_DIR/mn2 -rpcport=19201"
echo "  MN3:        $CLI -regtest -datadir=$TESTNET_DIR/mn3 -rpcport=19301"
echo ""
echo "  To check status: \$CTRL masternode list"
echo ""

# Save helper script
cat > $TESTNET_DIR/cli.sh << 'EOF'
#!/bin/bash
CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"
TESTNET_DIR="/tmp/khu_testnet_mn"

case "$1" in
    ctrl|controller)
        shift
        $CLI -regtest -datadir=$TESTNET_DIR/controller -rpcport=19001 "$@"
        ;;
    mn1)
        shift
        $CLI -regtest -datadir=$TESTNET_DIR/mn1 -rpcport=19101 "$@"
        ;;
    mn2)
        shift
        $CLI -regtest -datadir=$TESTNET_DIR/mn2 -rpcport=19201 "$@"
        ;;
    mn3)
        shift
        $CLI -regtest -datadir=$TESTNET_DIR/mn3 -rpcport=19301 "$@"
        ;;
    *)
        echo "Usage: $0 {ctrl|mn1|mn2|mn3} <command>"
        echo "Example: $0 ctrl getblockcount"
        ;;
esac
EOF
chmod +x $TESTNET_DIR/cli.sh

echo "  Helper script: $TESTNET_DIR/cli.sh"
echo "  Example: $TESTNET_DIR/cli.sh ctrl masternode list"
echo ""
