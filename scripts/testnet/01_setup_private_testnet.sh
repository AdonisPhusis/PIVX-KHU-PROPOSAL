#!/bin/bash
# =============================================================================
# KHU V6 Private Testnet - Setup Script
# =============================================================================
# Usage: ./01_setup_private_testnet.sh [datadir]
# Creates a fresh private testnet environment for KHU V6 testing
# =============================================================================

set -e

# Configuration
DATADIR="${1:-/tmp/khu_private_testnet}"
PIVX_BIN="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
CLI="$PIVX_BIN/pivx-cli -regtest -datadir=$DATADIR"
DAEMON="$PIVX_BIN/pivxd -regtest -datadir=$DATADIR"

echo "═══════════════════════════════════════════════════════════════"
echo "  KHU V6 PRIVATE TESTNET SETUP"
echo "═══════════════════════════════════════════════════════════════"
echo "Data directory: $DATADIR"
echo ""

# Step 1: Clean and create datadir
echo "[1/6] Creating data directory..."
rm -rf "$DATADIR"
mkdir -p "$DATADIR"

# Step 2: Create config
echo "[2/6] Creating configuration..."
cat > "$DATADIR/pivx.conf" << EOF
# KHU V6 Private Testnet Configuration
regtest=1
server=1
daemon=1

# RPC
rpcuser=khutest
rpcpassword=khutest123
rpcallowip=127.0.0.1
rpcport=18443

# Network
listen=1
port=18444
listenonion=0

# Logging
debug=khu
debug=rpc
printtoconsole=0
logtimestamps=1

# Performance
dbcache=256
maxconnections=10

# Mining (for regtest)
gen=0
EOF

echo "Config written to $DATADIR/pivx.conf"

# Step 3: Start daemon
echo "[3/6] Starting pivxd..."
$DAEMON

# Wait for startup
echo "Waiting for daemon to start..."
sleep 5

# Check if running
for i in {1..30}; do
    if $CLI getblockchaininfo > /dev/null 2>&1; then
        echo "Daemon started successfully!"
        break
    fi
    sleep 1
    echo -n "."
done

# Step 4: Generate initial blocks for V6 activation
echo ""
echo "[4/6] Generating blocks for V6 activation..."

# Get V6 activation height (typically 200 in regtest)
V6_HEIGHT=$($CLI getblockchaininfo | grep -o '"v6_0"[^}]*' | grep -o '"activationheight":[0-9]*' | grep -o '[0-9]*' || echo "200")
echo "V6 activation height: $V6_HEIGHT"

# Generate blocks to activate V6
CURRENT=$($CLI getblockcount)
NEEDED=$((V6_HEIGHT + 10 - CURRENT))

if [ $NEEDED -gt 0 ]; then
    echo "Generating $NEEDED blocks..."
    $CLI generate $NEEDED > /dev/null
fi

echo "Current block: $($CLI getblockcount)"

# Step 5: Create test wallet with funds
echo ""
echo "[5/6] Setting up test wallet..."

# Get a new address
ADDR=$($CLI getnewaddress "testnet_main")
echo "Test address: $ADDR"

# Generate more blocks to get mining rewards
$CLI generatetoaddress 101 $ADDR > /dev/null
echo "Generated 101 blocks to address"

# Check balance
BALANCE=$($CLI getbalance)
echo "Wallet balance: $BALANCE PIV"

# Step 6: Verify KHU state
echo ""
echo "[6/6] Verifying KHU state..."

KHU_STATE=$($CLI getkhustate 2>/dev/null || echo '{"error": "getkhustate not available"}')
echo "KHU State:"
echo "$KHU_STATE" | head -20

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  SETUP COMPLETE"
echo "═══════════════════════════════════════════════════════════════"
echo ""
echo "Testnet is ready for KHU V6 testing!"
echo ""
echo "Useful commands:"
echo "  CLI alias:  CLI=\"$CLI\""
echo "  Stop:       \$CLI stop"
echo "  Balance:    \$CLI getbalance"
echo "  KHU State:  \$CLI getkhustate"
echo "  MINT:       \$CLI khumint <amount>"
echo "  REDEEM:     \$CLI khuredeem <amount>"
echo ""
echo "Log file: $DATADIR/regtest/debug.log"
echo ""

# Export CLI for easy use
echo "export CLI=\"$CLI\"" > "$DATADIR/cli_alias.sh"
echo "Source this for easy CLI access: source $DATADIR/cli_alias.sh"
