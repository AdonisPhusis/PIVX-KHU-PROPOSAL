#!/bin/bash
# =============================================================================
# KHU V6 LOCAL TESTNET - 3 Nodes sur localhost
# =============================================================================
# Lance 3 nodes en regtest qui communiquent entre eux
# Node 1: Mining/Genesis (port 18444)
# Node 2: Staker (port 18445)
# Node 3: Client (port 18446)
# =============================================================================

set -e

SRC_DIR="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
BASE_DIR="/tmp/khu_local_testnet"
DAEMON="$SRC_DIR/pivxd"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

echo -e "${CYAN}"
echo "═══════════════════════════════════════════════════════════════"
echo "  KHU V6 LOCAL TESTNET - 3 Nodes"
echo "═══════════════════════════════════════════════════════════════"
echo -e "${NC}"

# Cleanup any existing
echo -e "${YELLOW}→ Cleaning up previous instances...${NC}"
pkill -9 -f "pivxd.*khu_local_testnet" 2>/dev/null || true
sleep 2

# Create directories
rm -rf "$BASE_DIR"
mkdir -p "$BASE_DIR"/{node1,node2,node3}

# =============================================================================
# NODE 1 CONFIG - Mining Node
# =============================================================================
cat > "$BASE_DIR/node1/pivx.conf" << 'EOF'
regtest=1
server=1
listen=1
port=18444
rpcport=18441
rpcbind=127.0.0.1
rpcuser=node1
rpcpassword=testpass1
rpcallowip=127.0.0.1
debug=khu
fallbackfee=0.0001
# Connect to other nodes
addnode=127.0.0.1:18445
addnode=127.0.0.1:18446
EOF

# =============================================================================
# NODE 2 CONFIG - Staker Node
# =============================================================================
cat > "$BASE_DIR/node2/pivx.conf" << 'EOF'
regtest=1
server=1
listen=1
port=18445
rpcport=18442
rpcbind=127.0.0.1
rpcuser=node2
rpcpassword=testpass2
rpcallowip=127.0.0.1
debug=khu
fallbackfee=0.0001
# Connect to other nodes
addnode=127.0.0.1:18444
addnode=127.0.0.1:18446
EOF

# =============================================================================
# NODE 3 CONFIG - Client Node
# =============================================================================
cat > "$BASE_DIR/node3/pivx.conf" << 'EOF'
regtest=1
server=1
listen=1
port=18446
rpcport=18443
rpcbind=127.0.0.1
rpcuser=node3
rpcpassword=testpass3
rpcallowip=127.0.0.1
debug=khu
txindex=1
fallbackfee=0.0001
# Connect to other nodes
addnode=127.0.0.1:18444
addnode=127.0.0.1:18445
EOF

# CLI shortcuts (with explicit rpcport)
CLI1="$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node1 -rpcport=18441 -rpcuser=node1 -rpcpassword=testpass1"
CLI2="$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node2 -rpcport=18442 -rpcuser=node2 -rpcpassword=testpass2"
CLI3="$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node3 -rpcport=18443 -rpcuser=node3 -rpcpassword=testpass3"

# =============================================================================
# START NODES
# =============================================================================
echo -e "${YELLOW}→ Starting Node 1 (Mining)...${NC}"
$DAEMON -regtest -datadir="$BASE_DIR/node1" -rpcport=18441 -rpcbind=127.0.0.1 -port=18444 -daemon
sleep 3

echo -e "${YELLOW}→ Starting Node 2 (Staker)...${NC}"
$DAEMON -regtest -datadir="$BASE_DIR/node2" -rpcport=18442 -rpcbind=127.0.0.1 -port=18445 -daemon
sleep 3

echo -e "${YELLOW}→ Starting Node 3 (Client)...${NC}"
$DAEMON -regtest -datadir="$BASE_DIR/node3" -rpcport=18443 -rpcbind=127.0.0.1 -port=18446 -daemon
sleep 8

# Verify all running
echo ""
echo -e "${YELLOW}→ Verifying nodes...${NC}"

BLOCK1=$($CLI1 getblockcount 2>&1)
BLOCK2=$($CLI2 getblockcount 2>&1)
BLOCK3=$($CLI3 getblockcount 2>&1)

if [[ "$BLOCK1" =~ ^[0-9]+$ ]] && [[ "$BLOCK2" =~ ^[0-9]+$ ]] && [[ "$BLOCK3" =~ ^[0-9]+$ ]]; then
    echo -e "${GREEN}✓ All 3 nodes running${NC}"
else
    echo -e "${RED}✗ Some nodes failed to start${NC}"
    echo "Node1: $BLOCK1"
    echo "Node2: $BLOCK2"
    echo "Node3: $BLOCK3"
    exit 1
fi

# =============================================================================
# GENERATE BLOCKS FOR V6 ACTIVATION
# =============================================================================
echo ""
echo -e "${YELLOW}→ Generating 201 blocks for V6 activation...${NC}"
$CLI1 generate 201 > /dev/null

# Wait for sync
sleep 3

# Verify sync
BLOCK1=$($CLI1 getblockcount)
BLOCK2=$($CLI2 getblockcount)
BLOCK3=$($CLI3 getblockcount)

echo -e "${GREEN}✓ Block heights: Node1=$BLOCK1 Node2=$BLOCK2 Node3=$BLOCK3${NC}"

# Verify V6 active
STATE=$($CLI1 getkhustate 2>&1)
if echo "$STATE" | grep -q '"C"'; then
    echo -e "${GREEN}✓ V6 activated - KHU state available${NC}"
else
    echo -e "${RED}✗ V6 not activated${NC}"
fi

# =============================================================================
# SETUP WALLETS WITH PIV
# =============================================================================
echo ""
echo -e "${YELLOW}→ Setting up wallets...${NC}"

# Get addresses
ADDR1=$($CLI1 getnewaddress)
ADDR2=$($CLI2 getnewaddress)
ADDR3=$($CLI3 getnewaddress)

echo "Node1 address: $ADDR1"
echo "Node2 address: $ADDR2"
echo "Node3 address: $ADDR3"

# Send PIV to Node2 and Node3
echo ""
echo -e "${YELLOW}→ Distributing PIV...${NC}"
$CLI1 sendtoaddress "$ADDR2" 10000
$CLI1 sendtoaddress "$ADDR3" 5000
$CLI1 generate 1 > /dev/null

sleep 2

BAL1=$($CLI1 getbalance)
BAL2=$($CLI2 getbalance)
BAL3=$($CLI3 getbalance)

echo -e "${GREEN}✓ Balances: Node1=$BAL1 PIV, Node2=$BAL2 PIV, Node3=$BAL3 PIV${NC}"

# =============================================================================
# DEMO: MINT KHU on Node2
# =============================================================================
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  DEMO: MINT KHU on Node2${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"

echo -e "${YELLOW}→ Node2: Minting 1000 KHU...${NC}"
MINT_RESULT=$($CLI2 khumint 1000 2>&1)
echo "$MINT_RESULT"

$CLI1 generate 1 > /dev/null
sleep 2

echo ""
echo -e "${YELLOW}→ Node2: KHU Balance${NC}"
$CLI2 khubalance

echo ""
echo -e "${YELLOW}→ Global KHU State (from Node1)${NC}"
$CLI1 getkhustate | grep -E '"C"|"U"|"Z"|"R_annual"'

# =============================================================================
# DEMO: STAKE on Node2
# =============================================================================
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  DEMO: STAKE KHU on Node2${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"

echo -e "${YELLOW}→ Node2: Staking 500 KHU...${NC}"
STAKE_RESULT=$($CLI2 khustake 500 2>&1)
echo "$STAKE_RESULT"

$CLI1 generate 1 > /dev/null
sleep 2

echo ""
echo -e "${YELLOW}→ Node2: Staked Notes${NC}"
$CLI2 khuliststaked

echo ""
echo -e "${YELLOW}→ Global KHU State (Z should be 500)${NC}"
$CLI1 getkhustate | grep -E '"C"|"U"|"Z"'

# =============================================================================
# DEMO: Node3 also mints
# =============================================================================
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  DEMO: Node3 also mints KHU${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"

echo -e "${YELLOW}→ Node3: Minting 200 KHU...${NC}"
$CLI3 khumint 200 2>&1

$CLI1 generate 1 > /dev/null
sleep 2

echo ""
echo -e "${YELLOW}→ Global KHU State (C should be 1200)${NC}"
$CLI1 getkhustate | grep -E '"C"|"U"|"Z"'

# Verify all nodes see same state
echo ""
echo -e "${YELLOW}→ Verifying consensus across nodes...${NC}"
STATE1=$($CLI1 getkhustate | grep '"C"' | grep -o '[0-9.]*')
STATE2=$($CLI2 getkhustate | grep '"C"' | grep -o '[0-9.]*')
STATE3=$($CLI3 getkhustate | grep '"C"' | grep -o '[0-9.]*')

if [ "$STATE1" == "$STATE2" ] && [ "$STATE2" == "$STATE3" ]; then
    echo -e "${GREEN}✓ All nodes agree: C=$STATE1${NC}"
else
    echo -e "${RED}✗ Consensus mismatch: Node1=$STATE1, Node2=$STATE2, Node3=$STATE3${NC}"
fi

# =============================================================================
# SUMMARY
# =============================================================================
echo ""
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo -e "${CYAN}  LOCAL TESTNET RUNNING${NC}"
echo -e "${CYAN}═══════════════════════════════════════════════════════════════${NC}"
echo ""
echo "Data directory: $BASE_DIR"
echo ""
echo "CLI Commands:"
echo "  Node1: $SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node1 -rpcuser=node1 -rpcpassword=testpass1"
echo "  Node2: $SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node2 -rpcuser=node2 -rpcpassword=testpass2"
echo "  Node3: $SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node3 -rpcuser=node3 -rpcpassword=testpass3"
echo ""
echo "Quick aliases (copy/paste):"
echo "  alias cli1='$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node1 -rpcuser=node1 -rpcpassword=testpass1'"
echo "  alias cli2='$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node2 -rpcuser=node2 -rpcpassword=testpass2'"
echo "  alias cli3='$SRC_DIR/pivx-cli -regtest -datadir=$BASE_DIR/node3 -rpcuser=node3 -rpcpassword=testpass3'"
echo ""
echo "To stop all nodes:"
echo "  pkill -f 'pivxd.*khu_local_testnet'"
echo ""
echo -e "${GREEN}Testnet is ready! Use the CLI commands above to interact.${NC}"
