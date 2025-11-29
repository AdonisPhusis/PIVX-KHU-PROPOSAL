#!/bin/bash
# =============================================================================
# KHU V6 TESTNET WITH MASTERNODES
# 1 Controller + 3 Masternodes with LLMQ for proper finality
# =============================================================================
set -e

SRC="/home/ubuntu/PIVX-V6-KHU/PIVX/src"
DAEMON="$SRC/pivxd"
CLI="$SRC/pivx-cli"
TESTDIR="/tmp/khu_mn_testnet"

echo "============================================================"
echo "  KHU V6 TESTNET WITH MASTERNODES"
echo "  1 Controller + 3 Masternodes"
echo "============================================================"
echo ""

# Cleanup
echo "=== Step 1: Cleanup ==="
pkill -9 pivxd 2>/dev/null || true
sleep 3
rm -rf $TESTDIR
mkdir -p $TESTDIR/{ctrl,mn1,mn2,mn3}

# CLI function
rpc_ctrl() {
    $CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 "$@"
}

# Start Controller ONLY first
echo "=== Step 2: Start Controller Node ==="
$DAEMON -regtest \
    -datadir=$TESTDIR/ctrl \
    -port=19000 \
    -rpcport=19001 \
    -rpcbind=127.0.0.1 \
    -rpcuser=ctrl \
    -rpcpassword=ctrlpass \
    -rpcallowip=127.0.0.1 \
    -listen=1 \
    -server=1 \
    -fallbackfee=0.0001 \
    -debug=khu \
    -debug=llmq \
    -daemon

echo "Waiting for controller..."
for i in {1..30}; do
    if rpc_ctrl getblockcount >/dev/null 2>&1; then
        echo "Controller OK"
        break
    fi
    sleep 1
done

# Generate initial blocks for mining reward
echo "=== Step 3: Generate 250 blocks for funds ==="
rpc_ctrl generate 250 > /dev/null
echo "Balance: $(rpc_ctrl getbalance) PIV"
echo "Block: $(rpc_ctrl getblockcount)"

# Generate BLS keys FIRST
echo "=== Step 4: Generate BLS keys ==="
BLS1=$(rpc_ctrl generateblskeypair)
BLS2=$(rpc_ctrl generateblskeypair)
BLS3=$(rpc_ctrl generateblskeypair)

BLS1_SECRET=$(echo $BLS1 | python3 -c "import sys,json; print(json.load(sys.stdin)['secret'])")
BLS1_PUBLIC=$(echo $BLS1 | python3 -c "import sys,json; print(json.load(sys.stdin)['public'])")
BLS2_SECRET=$(echo $BLS2 | python3 -c "import sys,json; print(json.load(sys.stdin)['secret'])")
BLS2_PUBLIC=$(echo $BLS2 | python3 -c "import sys,json; print(json.load(sys.stdin)['public'])")
BLS3_SECRET=$(echo $BLS3 | python3 -c "import sys,json; print(json.load(sys.stdin)['secret'])")
BLS3_PUBLIC=$(echo $BLS3 | python3 -c "import sys,json; print(json.load(sys.stdin)['public'])")

echo "BLS keys generated"
echo "  MN1 Public: ${BLS1_PUBLIC:0:20}..."
echo "  MN2 Public: ${BLS2_PUBLIC:0:20}..."
echo "  MN3 Public: ${BLS3_PUBLIC:0:20}..."
echo "  MN1 Secret: ${BLS1_SECRET:0:30}..."
echo "  MN2 Secret: ${BLS2_SECRET:0:30}..."
echo "  MN3 Secret: ${BLS3_SECRET:0:30}..."

# Create MN configs WITHOUT masternode options (will add via command line later)
echo "=== Step 5: Create MN configs (basic, without MN options) ==="
for node in mn1 mn2 mn3; do
    cat > $TESTDIR/$node/pivx.conf << EOF
regtest=1
server=1
listen=1
rpcuser=$node
rpcpassword=${node}pass
rpcallowip=127.0.0.1
fallbackfee=0.0001
EOF
done
echo "Configs created (MN options will be passed via command line)"

# Get addresses for MN registration
echo "=== Step 6: Get addresses ==="
COLLATERAL1=$(rpc_ctrl getnewaddress)
COLLATERAL2=$(rpc_ctrl getnewaddress)
COLLATERAL3=$(rpc_ctrl getnewaddress)
OWNER1=$(rpc_ctrl getnewaddress)
OWNER2=$(rpc_ctrl getnewaddress)
OWNER3=$(rpc_ctrl getnewaddress)
PAYOUT1=$(rpc_ctrl getnewaddress)
PAYOUT2=$(rpc_ctrl getnewaddress)
PAYOUT3=$(rpc_ctrl getnewaddress)

echo "Addresses created"

# Register masternodes (before starting MN nodes)
echo "=== Step 7: Register Masternodes ==="
echo "Registering MN1..."
PROTX1=$(rpc_ctrl protx_register_fund "$COLLATERAL1" "127.0.0.1:19010" "$OWNER1" "$BLS1_PUBLIC" "" "$PAYOUT1")
echo "  ProTx: ${PROTX1:0:20}..."

echo "Registering MN2..."
PROTX2=$(rpc_ctrl protx_register_fund "$COLLATERAL2" "127.0.0.1:19020" "$OWNER2" "$BLS2_PUBLIC" "" "$PAYOUT2")
echo "  ProTx: ${PROTX2:0:20}..."

echo "Registering MN3..."
PROTX3=$(rpc_ctrl protx_register_fund "$COLLATERAL3" "127.0.0.1:19030" "$OWNER3" "$BLS3_PUBLIC" "" "$PAYOUT3")
echo "  ProTx: ${PROTX3:0:20}..."

# Confirm registrations
echo "=== Step 8: Confirm registrations ==="
rpc_ctrl generate 10 > /dev/null
echo "Block: $(rpc_ctrl getblockcount)"

# First start MN nodes as regular nodes to sync the blockchain
echo "=== Step 9a: Start MN nodes for initial sync ==="
$DAEMON -regtest -datadir="$TESTDIR/mn1" -port=19010 -rpcport=19011 -rpcbind=127.0.0.1 \
    -rpcuser=mn1 -rpcpassword=mn1pass -rpcallowip=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 -daemon

$DAEMON -regtest -datadir="$TESTDIR/mn2" -port=19020 -rpcport=19021 -rpcbind=127.0.0.1 \
    -rpcuser=mn2 -rpcpassword=mn2pass -rpcallowip=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 -daemon

$DAEMON -regtest -datadir="$TESTDIR/mn3" -port=19030 -rpcport=19031 -rpcbind=127.0.0.1 \
    -rpcuser=mn3 -rpcpassword=mn3pass -rpcallowip=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 -daemon

echo "Waiting for sync..."
sleep 15

# Wait for sync
CTRL_HEIGHT=$(rpc_ctrl getblockcount)
for i in {1..30}; do
    MN1_HEIGHT=$($CLI -regtest -datadir=$TESTDIR/mn1 -rpcport=19011 -rpcuser=mn1 -rpcpassword=mn1pass getblockcount 2>/dev/null || echo "0")
    MN2_HEIGHT=$($CLI -regtest -datadir=$TESTDIR/mn2 -rpcport=19021 -rpcuser=mn2 -rpcpassword=mn2pass getblockcount 2>/dev/null || echo "0")
    MN3_HEIGHT=$($CLI -regtest -datadir=$TESTDIR/mn3 -rpcport=19031 -rpcuser=mn3 -rpcpassword=mn3pass getblockcount 2>/dev/null || echo "0")
    echo "  Sync: CTRL=$CTRL_HEIGHT MN1=$MN1_HEIGHT MN2=$MN2_HEIGHT MN3=$MN3_HEIGHT"
    if [ "$MN1_HEIGHT" = "$CTRL_HEIGHT" ] && [ "$MN2_HEIGHT" = "$CTRL_HEIGHT" ] && [ "$MN3_HEIGHT" = "$CTRL_HEIGHT" ]; then
        echo "  All nodes synced!"
        break
    fi
    sleep 2
done

# Stop MN nodes
echo "=== Step 9b: Stop MN nodes for restart with BLS keys ==="
$CLI -regtest -datadir=$TESTDIR/mn1 -rpcport=19011 -rpcuser=mn1 -rpcpassword=mn1pass stop 2>/dev/null || true
$CLI -regtest -datadir=$TESTDIR/mn2 -rpcport=19021 -rpcuser=mn2 -rpcpassword=mn2pass stop 2>/dev/null || true
$CLI -regtest -datadir=$TESTDIR/mn3 -rpcport=19031 -rpcuser=mn3 -rpcpassword=mn3pass stop 2>/dev/null || true
sleep 5

# Restart MN nodes with BLS keys
echo "=== Step 9c: Restart MN nodes as masternodes ==="
$DAEMON -regtest -datadir="$TESTDIR/mn1" -port=19010 -rpcport=19011 -rpcbind=127.0.0.1 \
    -rpcuser=mn1 -rpcpassword=mn1pass -rpcallowip=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS1_SECRET" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 \
    -debug=khu -debug=llmq -daemon

$DAEMON -regtest -datadir="$TESTDIR/mn2" -port=19020 -rpcport=19021 -rpcbind=127.0.0.1 \
    -rpcuser=mn2 -rpcpassword=mn2pass -rpcallowip=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS2_SECRET" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 \
    -debug=khu -debug=llmq -daemon

$DAEMON -regtest -datadir="$TESTDIR/mn3" -port=19030 -rpcport=19031 -rpcbind=127.0.0.1 \
    -rpcuser=mn3 -rpcpassword=mn3pass -rpcallowip=127.0.0.1 \
    -masternode=1 -mnoperatorprivatekey="$BLS3_SECRET" \
    -externalip=127.0.0.1 -bind=127.0.0.1 \
    -addnode=127.0.0.1:19000 -listen=1 -server=1 -fallbackfee=0.0001 \
    -debug=khu -debug=llmq -daemon

echo "Waiting for MN nodes to start as masternodes..."
sleep 10

# CLI shortcuts for MN nodes
rpc_mn1() {
    $CLI -regtest -datadir=$TESTDIR/mn1 -rpcport=19011 -rpcuser=mn1 -rpcpassword=mn1pass -rpcconnect=127.0.0.1 "$@"
}
rpc_mn2() {
    $CLI -regtest -datadir=$TESTDIR/mn2 -rpcport=19021 -rpcuser=mn2 -rpcpassword=mn2pass -rpcconnect=127.0.0.1 "$@"
}
rpc_mn3() {
    $CLI -regtest -datadir=$TESTDIR/mn3 -rpcport=19031 -rpcuser=mn3 -rpcpassword=mn3pass -rpcconnect=127.0.0.1 "$@"
}

# Check connections
echo "=== Step 10: Check connections ==="
echo "Controller peers: $(rpc_ctrl getconnectioncount)"
echo "MN1 peers: $(rpc_mn1 getconnectioncount 2>/dev/null || echo 'starting...')"
echo "MN2 peers: $(rpc_mn2 getconnectioncount 2>/dev/null || echo 'starting...')"
echo "MN3 peers: $(rpc_mn3 getconnectioncount 2>/dev/null || echo 'starting...')"

# Check masternode list
echo "=== Step 11: Check Masternode List ==="
MN_COUNT=$(rpc_ctrl protx_list 2>/dev/null | grep -c proTxHash)
echo "Active Masternodes: $MN_COUNT"

# Generate more blocks for LLMQ
echo "=== Step 12: Generate blocks for LLMQ ==="
rpc_ctrl generate 50 > /dev/null
echo "Block: $(rpc_ctrl getblockcount)"

# Wait for sync
echo "=== Step 13: Wait for sync ==="
sleep 10
echo "Controller: $(rpc_ctrl getblockcount)"
echo "MN1: $(rpc_mn1 getblockcount 2>/dev/null || echo 'syncing...')"
echo "MN2: $(rpc_mn2 getblockcount 2>/dev/null || echo 'syncing...')"
echo "MN3: $(rpc_mn3 getblockcount 2>/dev/null || echo 'syncing...')"

# Check LLMQ quorums
echo "=== Step 14: Check LLMQ Quorums ==="
rpc_ctrl listquorums

# Check KHU state
echo "=== Step 15: Check KHU State (V6 activated) ==="
rpc_ctrl getkhustate | head -15

echo ""
echo "============================================================"
echo "  TESTNET WITH MASTERNODES READY"
echo "============================================================"
echo ""
echo "Block: $(rpc_ctrl getblockcount)"
echo "V6 Active: block >= 200"
echo "Masternodes: $MN_COUNT"
echo ""
echo "CLI Helper: /tmp/khu_mn_testnet/cli.sh"
echo ""
echo "Usage:"
echo "  cli.sh ctrl khumint 1000"
echo "  cli.sh ctrl balance"
echo "  cli.sh mn1 getblockcount"
echo ""

# Create helper script
cat > $TESTDIR/cli.sh << 'HELPER'
#!/bin/bash
CLI="/home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli"
TESTDIR="/tmp/khu_mn_testnet"

case "$1" in
    ctrl) shift; $CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 "$@" ;;
    mn1)  shift; $CLI -regtest -datadir=$TESTDIR/mn1 -rpcport=19011 -rpcuser=mn1 -rpcpassword=mn1pass -rpcconnect=127.0.0.1 "$@" ;;
    mn2)  shift; $CLI -regtest -datadir=$TESTDIR/mn2 -rpcport=19021 -rpcuser=mn2 -rpcpassword=mn2pass -rpcconnect=127.0.0.1 "$@" ;;
    mn3)  shift; $CLI -regtest -datadir=$TESTDIR/mn3 -rpcport=19031 -rpcuser=mn3 -rpcpassword=mn3pass -rpcconnect=127.0.0.1 "$@" ;;
    balance)
        echo "=== CONTROLLER BALANCE ==="
        $CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 getbalance
        $CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 khubalance 2>/dev/null || echo "No KHU"
        echo ""
        echo "=== KHU GLOBAL STATE ==="
        $CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 getkhustate | head -15
        ;;
    sync)
        echo "=== SYNC STATUS ==="
        echo "Controller: $($CLI -regtest -datadir=$TESTDIR/ctrl -rpcport=19001 -rpcuser=ctrl -rpcpassword=ctrlpass -rpcconnect=127.0.0.1 getblockcount)"
        echo "MN1: $($CLI -regtest -datadir=$TESTDIR/mn1 -rpcport=19011 -rpcuser=mn1 -rpcpassword=mn1pass -rpcconnect=127.0.0.1 getblockcount 2>/dev/null || echo 'offline')"
        echo "MN2: $($CLI -regtest -datadir=$TESTDIR/mn2 -rpcport=19021 -rpcuser=mn2 -rpcpassword=mn2pass -rpcconnect=127.0.0.1 getblockcount 2>/dev/null || echo 'offline')"
        echo "MN3: $($CLI -regtest -datadir=$TESTDIR/mn3 -rpcport=19031 -rpcuser=mn3 -rpcpassword=mn3pass -rpcconnect=127.0.0.1 getblockcount 2>/dev/null || echo 'offline')"
        ;;
    *) echo "Usage: $0 {ctrl|mn1|mn2|mn3|balance|sync} [command]" ;;
esac
HELPER
chmod +x $TESTDIR/cli.sh

echo "Stop: pkill -f pivxd"
echo ""
