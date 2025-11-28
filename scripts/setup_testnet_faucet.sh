#!/bin/bash
# KHU Testnet Faucet Setup
# Creates a funded faucet wallet for the testnet

set -e

FAUCET_DATADIR="/home/ubuntu/.pivx-faucet"
FAUCET_PORT=51470
FAUCET_RPCPORT=51471
PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"
CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║              KHU TESTNET FAUCET SETUP                         ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

# Stop any existing faucet
pkill -f "pivxd.*pivx-faucet" 2>/dev/null || true
sleep 2

# Create faucet directory
rm -rf $FAUCET_DATADIR
mkdir -p $FAUCET_DATADIR

# Create faucet config
cat > $FAUCET_DATADIR/pivx.conf << EOF
# KHU Testnet Faucet Node
testnet=1
server=1
listen=1
port=$FAUCET_PORT
rpcport=$FAUCET_RPCPORT
rpcuser=faucet
rpcpassword=faucet_$(openssl rand -hex 16)
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

# Connect to seed nodes
# addnode=seed1.khu-testnet.io
# addnode=seed2.khu-testnet.io
EOF

echo ""
echo "Starting faucet node..."
$PIVXD -testnet -datadir=$FAUCET_DATADIR -daemon
sleep 10

FAUCET_CLI="$CLI -testnet -datadir=$FAUCET_DATADIR"

# Wait for node to be ready
echo "Waiting for node to sync..."
for i in {1..30}; do
    if $FAUCET_CLI getblockcount > /dev/null 2>&1; then
        break
    fi
    sleep 2
done

# Get faucet address
FAUCET_ADDRESS=$($FAUCET_CLI getnewaddress "faucet")

echo ""
echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║              FAUCET SETUP COMPLETE                            ║"
echo "╚═══════════════════════════════════════════════════════════════╝"
echo ""
echo "  Faucet Address: $FAUCET_ADDRESS"
echo "  Data Directory: $FAUCET_DATADIR"
echo "  RPC Port:       $FAUCET_RPCPORT"
echo ""
echo "  To fund the faucet, send PIV to:"
echo "  $FAUCET_ADDRESS"
echo ""
echo "  To use the faucet:"
echo "  ./faucet.sh <destination_address>"
echo ""
echo "  Faucet CLI:"
echo "  $FAUCET_CLI <command>"
echo ""

# Save faucet info
cat > $FAUCET_DATADIR/faucet_info.txt << EOF
FAUCET_ADDRESS=$FAUCET_ADDRESS
FAUCET_CLI=$FAUCET_CLI
FAUCET_DATADIR=$FAUCET_DATADIR
EOF

echo "Faucet info saved to: $FAUCET_DATADIR/faucet_info.txt"
