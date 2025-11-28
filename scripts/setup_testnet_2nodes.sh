#!/bin/bash
# Setup 2-node private testnet for KHU V6 testing
# Usage: ./setup_testnet_2nodes.sh

set -e

BASEDIR="/tmp/khu_testnet"
PIVXD="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd"
CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"

echo "═══════════════════════════════════════════════════════════════"
echo "        KHU V6 PRIVATE TESTNET - 2 NODE SETUP"
echo "═══════════════════════════════════════════════════════════════"

# Kill existing
pkill -9 pivxd 2>/dev/null || true
sleep 2

# Clean and create directories
rm -rf "$BASEDIR"
mkdir -p "$BASEDIR/miner" "$BASEDIR/wallet"

# Miner config
cat > "$BASEDIR/miner/pivx.conf" << 'EOF'
regtest=1
server=1
listen=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
EOF

# Wallet config
cat > "$BASEDIR/wallet/pivx.conf" << 'EOF'
regtest=1
server=1
listen=1
rpcuser=test
rpcpassword=test123
rpcallowip=127.0.0.1
fallbackfee=0.0001
debug=khu
EOF

# Create CLI wrapper scripts
cat > "$BASEDIR/miner.sh" << 'EOF'
#!/bin/bash
/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli -regtest -datadir=/tmp/khu_testnet/miner -rpcport=19801 "$@"
EOF
chmod +x "$BASEDIR/miner.sh"

cat > "$BASEDIR/wallet.sh" << 'EOF'
#!/bin/bash
/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli -regtest -datadir=/tmp/khu_testnet/wallet -rpcport=19901 "$@"
EOF
chmod +x "$BASEDIR/wallet.sh"

# Relay helper
cat > "$BASEDIR/relay.sh" << 'EOF'
#!/bin/bash
# Relay transactions from wallet mempool to miner
MINER="/tmp/khu_testnet/miner.sh"
WALLET="/tmp/khu_testnet/wallet.sh"

for TXID in $($WALLET getrawmempool 2>/dev/null | grep '"' | tr -d '[]", '); do
    RAWTX=$($WALLET getrawtransaction "$TXID" 2>/dev/null)
    if [ -n "$RAWTX" ]; then
        $MINER sendrawtransaction "$RAWTX" 2>/dev/null && echo "Relayed: ${TXID:0:16}..."
    fi
done
EOF
chmod +x "$BASEDIR/relay.sh"

echo ""
echo "Starting miner node (P2P:19800, RPC:19801)..."
$PIVXD -regtest -datadir="$BASEDIR/miner" -port=19800 -rpcport=19801 -daemon
sleep 5

echo "Starting wallet node (P2P:19900, RPC:19901)..."
$PIVXD -regtest -datadir="$BASEDIR/wallet" -port=19900 -rpcport=19901 -addnode=127.0.0.1:19800 -daemon
sleep 5

MINER="$BASEDIR/miner.sh"
WALLET="$BASEDIR/wallet.sh"

echo ""
echo "Checking nodes..."
echo "  Miner block: $($MINER getblockcount)"
echo "  Wallet block: $($WALLET getblockcount)"

echo ""
echo "Generating 250 blocks for V6 activation..."
$MINER generate 250 > /dev/null
sleep 2

echo ""
echo "Syncing wallet..."
sleep 3
echo "  Miner block: $($MINER getblockcount)"
echo "  Wallet block: $($WALLET getblockcount)"

echo ""
echo "Sending 1000 PIV to wallet..."
WALLET_ADDR=$($WALLET getnewaddress)
$MINER sendtoaddress "$WALLET_ADDR" 1000 > /dev/null
$MINER generate 1 > /dev/null
sleep 2

echo ""
echo "Final state:"
echo "  Miner PIV: $($MINER getbalance)"
echo "  Wallet PIV: $($WALLET getbalance)"
echo "  KHU State: $($MINER getkhustate | grep -E '"C"|"U"|"Z"' | tr '\n' ' ')"

echo ""
echo "═══════════════════════════════════════════════════════════════"
echo "  TESTNET READY!"
echo ""
echo "  CLI Commands:"
echo "    $BASEDIR/miner.sh <command>"
echo "    $BASEDIR/wallet.sh <command>"
echo "    $BASEDIR/relay.sh  (relay wallet tx to miner)"
echo ""
echo "  Example:"
echo "    /tmp/khu_testnet/wallet.sh khumint 100"
echo "    /tmp/khu_testnet/relay.sh"
echo "    /tmp/khu_testnet/miner.sh generate 1"
echo "═══════════════════════════════════════════════════════════════"
