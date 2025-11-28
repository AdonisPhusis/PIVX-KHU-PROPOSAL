#!/bin/bash
#═══════════════════════════════════════════════════════════════════════════════
# PIVX-KHU Private Testnet Setup
#
# Creates a private testnet with:
# - 1 Mining node (generates blocks continuously)
# - 1 Wallet node (for testing KHU operations)
# - 3 Masternode nodes (for DOMC voting and consensus)
#
# All nodes run on localhost with different ports
#═══════════════════════════════════════════════════════════════════════════════

set -e

# Configuration
PIVX_SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
TESTNET_BASE="/tmp/khu_private_testnet"
NETWORK_ID="khutest"

# Node ports (P2P / RPC)
MINER_P2P=19899
MINER_RPC=19898
WALLET_P2P=19999
WALLET_RPC=19998
MN1_P2P=20099
MN1_RPC=20098
MN2_P2P=20199
MN2_RPC=20198
MN3_P2P=20299
MN3_RPC=20298

# Masternode collateral (10000 PIV for PIVX)
MN_COLLATERAL=10000

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_header() {
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}═══════════════════════════════════════════════════════════════${NC}"
}

print_step() {
    echo -e "${GREEN}[✓] $1${NC}"
}

print_warning() {
    echo -e "${YELLOW}[!] $1${NC}"
}

print_error() {
    echo -e "${RED}[✗] $1${NC}"
}

# Cleanup function
cleanup() {
    print_header "Cleaning up previous testnet..."
    pkill -9 -f "pivxd.*${TESTNET_BASE}" 2>/dev/null || true
    sleep 2
    rm -rf "$TESTNET_BASE"
    mkdir -p "$TESTNET_BASE"
    print_step "Cleanup complete"
}

# Create node configuration
create_node_config() {
    local node_name=$1
    local p2p_port=$2
    local rpc_port=$3
    local is_masternode=$4
    local datadir="${TESTNET_BASE}/${node_name}"

    mkdir -p "$datadir"

    cat > "${datadir}/pivx.conf" << EOF
# Network
regtest=1
listen=1
port=${p2p_port}

# RPC
server=1
rpcuser=test
rpcpassword=test123
rpcport=${rpc_port}
rpcallowip=127.0.0.1

# Peers (connect to all other nodes)
connect=127.0.0.1:${MINER_P2P}
connect=127.0.0.1:${WALLET_P2P}
connect=127.0.0.1:${MN1_P2P}
connect=127.0.0.1:${MN2_P2P}
connect=127.0.0.1:${MN3_P2P}

# General
fallbackfee=0.0001
debug=khu
debug=masternode
debug=net

# Masternode specific
EOF

    if [ "$is_masternode" = "true" ]; then
        echo "masternode=1" >> "${datadir}/pivx.conf"
    fi

    print_step "Created config for ${node_name} (P2P:${p2p_port} RPC:${rpc_port})"
}

# Start a node
start_node() {
    local node_name=$1
    local datadir="${TESTNET_BASE}/${node_name}"

    ${PIVX_SRC}/pivxd -regtest -datadir="$datadir" -daemon
    print_step "Started ${node_name}"
}

# Get CLI for a node
get_cli() {
    local node_name=$1
    local rpc_port=$2
    echo "${PIVX_SRC}/pivx-cli -regtest -datadir=${TESTNET_BASE}/${node_name} -rpcuser=test -rpcpassword=test123 -rpcport=${rpc_port}"
}

# Wait for node to be ready
wait_for_node() {
    local node_name=$1
    local rpc_port=$2
    local cli=$(get_cli "$node_name" "$rpc_port")

    for i in {1..30}; do
        if $cli getblockcount >/dev/null 2>&1; then
            print_step "${node_name} is ready"
            return 0
        fi
        sleep 1
    done
    print_error "${node_name} failed to start!"
    return 1
}

#═══════════════════════════════════════════════════════════════════════════════
# MAIN SETUP
#═══════════════════════════════════════════════════════════════════════════════

print_header "PIVX-KHU Private Testnet Setup"
echo ""

# Step 1: Cleanup
cleanup

# Step 2: Create configurations
print_header "Creating Node Configurations..."
create_node_config "miner"  $MINER_P2P  $MINER_RPC  "false"
create_node_config "wallet" $WALLET_P2P $WALLET_RPC "false"
create_node_config "mn1"    $MN1_P2P    $MN1_RPC    "true"
create_node_config "mn2"    $MN2_P2P    $MN2_RPC    "true"
create_node_config "mn3"    $MN3_P2P    $MN3_RPC    "true"

# Step 3: Start nodes
print_header "Starting Nodes..."
start_node "miner"
sleep 3
start_node "wallet"
start_node "mn1"
start_node "mn2"
start_node "mn3"

# Step 4: Wait for nodes
print_header "Waiting for Nodes to Initialize..."
sleep 10
wait_for_node "miner"  $MINER_RPC
wait_for_node "wallet" $WALLET_RPC
wait_for_node "mn1"    $MN1_RPC
wait_for_node "mn2"    $MN2_RPC
wait_for_node "mn3"    $MN3_RPC

# Define CLIs
CLI_MINER=$(get_cli "miner" $MINER_RPC)
CLI_WALLET=$(get_cli "wallet" $WALLET_RPC)
CLI_MN1=$(get_cli "mn1" $MN1_RPC)
CLI_MN2=$(get_cli "mn2" $MN2_RPC)
CLI_MN3=$(get_cli "mn3" $MN3_RPC)

# Step 5: Generate initial blocks on miner
print_header "Generating Initial Blocks (V6 Activation)..."
echo "This may take a moment..."

# Generate 200 blocks for V6 activation
$CLI_MINER generate 200 > /dev/null
HEIGHT=$($CLI_MINER getblockcount)
print_step "Generated to height ${HEIGHT} (V6 activated)"

# Step 6: Check sync
print_header "Checking Network Sync..."
sleep 5

for node in "miner:$MINER_RPC" "wallet:$WALLET_RPC" "mn1:$MN1_RPC" "mn2:$MN2_RPC" "mn3:$MN3_RPC"; do
    name=$(echo $node | cut -d: -f1)
    port=$(echo $node | cut -d: -f2)
    cli=$(get_cli "$name" "$port")
    height=$($cli getblockcount 2>/dev/null || echo "N/A")
    echo -e "  ${name}: height=${height}"
done

# Step 7: Fund wallet for KHU testing
print_header "Funding Wallet Node..."

# Get wallet address
WALLET_ADDR=$($CLI_WALLET getnewaddress)
print_step "Wallet address: ${WALLET_ADDR}"

# Generate 101 more blocks to miner, then send to wallet
$CLI_MINER generate 101 > /dev/null  # Need 100 confirmations for coinbase
MINER_BALANCE=$($CLI_MINER getbalance)
print_step "Miner balance: ${MINER_BALANCE} PIV"

# Send PIV to wallet node
$CLI_MINER sendtoaddress "$WALLET_ADDR" 50000 > /dev/null
$CLI_MINER generate 1 > /dev/null
sleep 3

WALLET_BALANCE=$($CLI_WALLET getbalance)
print_step "Wallet balance: ${WALLET_BALANCE} PIV"

# Step 8: Summary
print_header "Private Testnet Ready!"
echo ""
echo "Nodes running:"
echo "  - Miner:  P2P=${MINER_P2P}  RPC=${MINER_RPC}"
echo "  - Wallet: P2P=${WALLET_P2P} RPC=${WALLET_RPC}"
echo "  - MN1:    P2P=${MN1_P2P}    RPC=${MN1_RPC}"
echo "  - MN2:    P2P=${MN2_P2P}    RPC=${MN2_RPC}"
echo "  - MN3:    P2P=${MN3_P2P}    RPC=${MN3_RPC}"
echo ""
echo "CLI Commands:"
echo "  MINER:  ${CLI_MINER}"
echo "  WALLET: ${CLI_WALLET}"
echo ""
echo "Quick test:"
echo "  ${CLI_WALLET} getkhustate"
echo "  ${CLI_WALLET} khumint 1000"
echo ""

# Create helper script
cat > "${TESTNET_BASE}/cli.sh" << 'HELPER'
#!/bin/bash
BASE="/tmp/khu_private_testnet"
SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"

case "$1" in
    miner)  $SRC/pivx-cli -regtest -datadir=$BASE/miner  -rpcuser=test -rpcpassword=test123 -rpcport=19898 "${@:2}" ;;
    wallet) $SRC/pivx-cli -regtest -datadir=$BASE/wallet -rpcuser=test -rpcpassword=test123 -rpcport=19998 "${@:2}" ;;
    mn1)    $SRC/pivx-cli -regtest -datadir=$BASE/mn1    -rpcuser=test -rpcpassword=test123 -rpcport=20098 "${@:2}" ;;
    mn2)    $SRC/pivx-cli -regtest -datadir=$BASE/mn2    -rpcuser=test -rpcpassword=test123 -rpcport=20198 "${@:2}" ;;
    mn3)    $SRC/pivx-cli -regtest -datadir=$BASE/mn3    -rpcuser=test -rpcpassword=test123 -rpcport=20298 "${@:2}" ;;
    mine)   $SRC/pivx-cli -regtest -datadir=$BASE/miner  -rpcuser=test -rpcpassword=test123 -rpcport=19898 generate ${2:-1} ;;
    status)
        for node in miner wallet mn1 mn2 mn3; do
            echo -n "$node: "
            $0 $node getblockcount 2>/dev/null || echo "offline"
        done
        ;;
    stop)
        pkill -9 -f "pivxd.*khu_private_testnet" 2>/dev/null
        echo "All nodes stopped"
        ;;
    *)
        echo "Usage: $0 {miner|wallet|mn1|mn2|mn3|mine|status|stop} [command]"
        echo ""
        echo "Examples:"
        echo "  $0 wallet getkhustate"
        echo "  $0 wallet khumint 1000"
        echo "  $0 mine 10           # Generate 10 blocks"
        echo "  $0 status            # Check all nodes"
        ;;
esac
HELPER
chmod +x "${TESTNET_BASE}/cli.sh"

print_step "Helper script created: ${TESTNET_BASE}/cli.sh"
echo ""
print_header "Setup Complete!"
