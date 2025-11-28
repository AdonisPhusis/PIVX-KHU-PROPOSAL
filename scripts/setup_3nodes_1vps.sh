#!/bin/bash
#
# PIVX-KHU Testnet: 3 nodes sur 1 VPS
# Usage: ./setup_3nodes_1vps.sh
#

set -e

PIVX_DIR="/opt/pivx-khu"
DATA_BASE="/opt/pivx-khu/data"

# Creer structure
echo "=== Creation structure ==="
sudo mkdir -p $PIVX_DIR/{bin,data}
sudo mkdir -p $DATA_BASE/{node1,node2,node3}

# Copier binaires (supposant build deja fait)
echo "=== Copie binaires ==="
sudo cp /home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd $PIVX_DIR/bin/
sudo cp /home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli $PIVX_DIR/bin/

# ============================================
# NODE 1: Seed Genesis (port 51470)
# ============================================
cat << 'EOF' | sudo tee $DATA_BASE/node1/pivx.conf
# Node 1 - Seed Genesis
testnet=1
listen=1
server=1
port=51470
rpcport=51471
rpcuser=khutest1
rpcpassword=KhuTestNet2024Node1!
rpcallowip=127.0.0.1
debug=khu
logips=1
# Peers
connect=127.0.0.1:51472
connect=127.0.0.1:51474
EOF

# ============================================
# NODE 2: Seed (port 51472)
# ============================================
cat << 'EOF' | sudo tee $DATA_BASE/node2/pivx.conf
# Node 2 - Seed
testnet=1
listen=1
server=1
port=51472
rpcport=51473
rpcuser=khutest2
rpcpassword=KhuTestNet2024Node2!
rpcallowip=127.0.0.1
debug=khu
logips=1
# Peers
connect=127.0.0.1:51470
connect=127.0.0.1:51474
EOF

# ============================================
# NODE 3: Explorer/Test (port 51474)
# ============================================
cat << 'EOF' | sudo tee $DATA_BASE/node3/pivx.conf
# Node 3 - Explorer
testnet=1
listen=1
server=1
port=51474
rpcport=51475
rpcuser=khutest3
rpcpassword=KhuTestNet2024Node3!
rpcallowip=127.0.0.1
debug=khu
logips=1
txindex=1
# Peers
connect=127.0.0.1:51470
connect=127.0.0.1:51472
EOF

# Permissions
sudo chmod 600 $DATA_BASE/*/pivx.conf

echo "=== Configuration terminee ==="
echo ""
echo "Pour demarrer les 3 nodes:"
echo "  $PIVX_DIR/bin/pivxd -datadir=$DATA_BASE/node1 -daemon"
echo "  $PIVX_DIR/bin/pivxd -datadir=$DATA_BASE/node2 -daemon"
echo "  $PIVX_DIR/bin/pivxd -datadir=$DATA_BASE/node3 -daemon"
echo ""
echo "CLI shortcuts:"
echo "  alias cli1='$PIVX_DIR/bin/pivx-cli -datadir=$DATA_BASE/node1'"
echo "  alias cli2='$PIVX_DIR/bin/pivx-cli -datadir=$DATA_BASE/node2'"
echo "  alias cli3='$PIVX_DIR/bin/pivx-cli -datadir=$DATA_BASE/node3'"
