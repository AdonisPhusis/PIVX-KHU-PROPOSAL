#!/bin/bash
#
# PIVX-V6-KHU Private Testnet Setup v2
# Corrected config format with [regtest] section for port settings
#

set -e

PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"
PIVX_CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"
TESTNET_BASE="/tmp/khu_private_testnet"

echo "═══════════════════════════════════════════════════════════════"
echo "  PIVX-V6-KHU Private Testnet Setup v2"
echo "  5 nodes: 1 miner, 1 wallet, 3 masternodes"
echo "═══════════════════════════════════════════════════════════════"

# Cleanup
echo ""
echo "=== Cleanup ==="
pkill -9 pivxd 2>/dev/null || true
sleep 2
rm -rf "$TESTNET_BASE"
mkdir -p "$TESTNET_BASE"/{miner,wallet,mn1,mn2,mn3}
echo "Directories created"

# Create configs with CORRECT [regtest] section format
echo ""
echo "=== Creating configs (with [regtest] section) ==="

# Miner config
cat > "$TESTNET_BASE/miner/pivx.conf" << 'EOF'
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

[regtest]
listen=1
port=19899
rpcport=19898
addnode=127.0.0.1:19999
addnode=127.0.0.1:20099
addnode=127.0.0.1:20199
addnode=127.0.0.1:20299
EOF
echo "  - miner: P2P=19899, RPC=19898"

# Wallet config
cat > "$TESTNET_BASE/wallet/pivx.conf" << 'EOF'
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

[regtest]
listen=1
port=19999
rpcport=19998
addnode=127.0.0.1:19899
addnode=127.0.0.1:20099
addnode=127.0.0.1:20199
addnode=127.0.0.1:20299
EOF
echo "  - wallet: P2P=19999, RPC=19998"

# MN1 config
cat > "$TESTNET_BASE/mn1/pivx.conf" << 'EOF'
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

[regtest]
listen=1
port=20099
rpcport=20098
addnode=127.0.0.1:19899
addnode=127.0.0.1:19999
addnode=127.0.0.1:20199
addnode=127.0.0.1:20299
EOF
echo "  - mn1: P2P=20099, RPC=20098"

# MN2 config
cat > "$TESTNET_BASE/mn2/pivx.conf" << 'EOF'
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

[regtest]
listen=1
port=20199
rpcport=20198
addnode=127.0.0.1:19899
addnode=127.0.0.1:19999
addnode=127.0.0.1:20099
addnode=127.0.0.1:20299
EOF
echo "  - mn2: P2P=20199, RPC=20198"

# MN3 config
cat > "$TESTNET_BASE/mn3/pivx.conf" << 'EOF'
server=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu

[regtest]
listen=1
port=20299
rpcport=20298
addnode=127.0.0.1:19899
addnode=127.0.0.1:19999
addnode=127.0.0.1:20099
addnode=127.0.0.1:20199
EOF
echo "  - mn3: P2P=20299, RPC=20298"

# Start nodes sequentially
echo ""
echo "=== Starting nodes ==="

# Start miner first
echo "Starting miner..."
$PIVXD -regtest -datadir="$TESTNET_BASE/miner" -daemon
sleep 8

# Verify miner is running
CLI_MINER="$PIVX_CLI -regtest -datadir=$TESTNET_BASE/miner -rpcport=19898"
if $CLI_MINER getblockcount > /dev/null 2>&1; then
    echo "  miner: OK (RPC responsive)"
else
    echo "  miner: FAILED - checking log..."
    grep -iE "error|binding|rpc" "$TESTNET_BASE/miner/regtest/debug.log" | tail -5
    exit 1
fi

# Start wallet
echo "Starting wallet..."
$PIVXD -regtest -datadir="$TESTNET_BASE/wallet" -daemon
sleep 5

CLI_WALLET="$PIVX_CLI -regtest -datadir=$TESTNET_BASE/wallet -rpcport=19998"
if $CLI_WALLET getblockcount > /dev/null 2>&1; then
    echo "  wallet: OK"
else
    echo "  wallet: FAILED"
fi

# Start masternodes
for mn in mn1 mn2 mn3; do
    case $mn in
        mn1) RPC=20098 ;;
        mn2) RPC=20198 ;;
        mn3) RPC=20298 ;;
    esac

    echo "Starting $mn..."
    $PIVXD -regtest -datadir="$TESTNET_BASE/$mn" -daemon
    sleep 4

    CLI="$PIVX_CLI -regtest -datadir=$TESTNET_BASE/$mn -rpcport=$RPC"
    if $CLI getblockcount > /dev/null 2>&1; then
        echo "  $mn: OK"
    else
        echo "  $mn: FAILED"
    fi
done

# Wait for peer connections
echo ""
echo "=== Waiting for peer connections (10s) ==="
sleep 10

# Generate initial blocks on miner
echo ""
echo "=== Generating initial blocks ==="
$CLI_MINER generate 1 > /dev/null
echo "Generated 1 block on miner"
sleep 3

# Check sync status
echo ""
echo "=== Final Status ==="
for node in miner wallet mn1 mn2 mn3; do
    case $node in
        miner)  RPC=19898 ;;
        wallet) RPC=19998 ;;
        mn1)    RPC=20098 ;;
        mn2)    RPC=20198 ;;
        mn3)    RPC=20298 ;;
    esac
    CLI="$PIVX_CLI -regtest -datadir=$TESTNET_BASE/$node -rpcport=$RPC"

    HEIGHT=$($CLI getblockcount 2>/dev/null || echo "offline")
    PEERS=$($CLI getconnectioncount 2>/dev/null || echo "?")
    echo "  $node: height=$HEIGHT, peers=$PEERS"
done

# Create helper CLI script
cat > "$TESTNET_BASE/cli.sh" << 'EOFCLI'
#!/bin/bash
NODE="${1:-miner}"
shift

case $NODE in
    miner)  RPC=19898 ;;
    wallet) RPC=19998 ;;
    mn1)    RPC=20098 ;;
    mn2)    RPC=20198 ;;
    mn3)    RPC=20298 ;;
    *)
        echo "Usage: cli.sh <node> <command>"
        echo "Nodes: miner, wallet, mn1, mn2, mn3"
        exit 1
        ;;
esac

DATADIR="/tmp/khu_private_testnet/$NODE"
/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli -regtest -datadir="$DATADIR" -rpcport=$RPC "$@"
EOFCLI
chmod +x "$TESTNET_BASE/cli.sh"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  Testnet Ready!"
echo ""
echo "  Helper CLI: $TESTNET_BASE/cli.sh <node> <command>"
echo "  Example:    $TESTNET_BASE/cli.sh miner getblockcount"
echo "              $TESTNET_BASE/cli.sh wallet khumint 1000"
echo "              $TESTNET_BASE/cli.sh mn1 getkhustate"
echo "═══════════════════════════════════════════════════════════════"
