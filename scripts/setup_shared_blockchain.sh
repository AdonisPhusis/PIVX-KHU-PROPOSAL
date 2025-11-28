#!/bin/bash
#
# PIVX-KHU Testnet: 1 blockchain partagee, 3 wallets
# Economise ~80% d'espace disque
#

set -e

PIVX_DIR="/opt/pivx-khu"
DATA_DIR="$PIVX_DIR/data"

echo "=== Setup: 1 Blockchain + 3 Wallets ==="

# Creer structure
sudo mkdir -p $PIVX_DIR/bin
sudo mkdir -p $DATA_DIR

# Copier binaires
echo "Copie binaires..."
sudo cp /home/ubuntu/PIVX-V6-KHU/PIVX/src/pivxd $PIVX_DIR/bin/
sudo cp /home/ubuntu/PIVX-V6-KHU/PIVX/src/pivx-cli $PIVX_DIR/bin/

# Configuration unique (1 seul daemon)
cat << 'EOF' | sudo tee $DATA_DIR/pivx.conf
# KHU Testnet - Shared Blockchain
testnet=1
listen=1
server=1
daemon=1

# RPC
rpcuser=khutestnet
rpcpassword=KhuTestNet2024Secure!
rpcallowip=127.0.0.1

# Multi-wallet support
wallet=wallet_genesis.dat
wallet=wallet_dev1.dat
wallet=wallet_dev2.dat

# Debug
debug=khu
logips=1
txindex=1
EOF

sudo chmod 600 $DATA_DIR/pivx.conf

echo ""
echo "=== Configuration terminee ==="
echo ""
echo "Structure:"
echo "  $DATA_DIR/"
echo "  ├── pivx.conf           (config unique)"
echo "  ├── testnet3/           (blockchain partagee)"
echo "  │   ├── blocks/"
echo "  │   ├── chainstate/"
echo "  │   ├── wallet_genesis.dat"
echo "  │   ├── wallet_dev1.dat"
echo "  │   └── wallet_dev2.dat"
echo ""
echo "Usage:"
echo "  # Demarrer le daemon (1 seul)"
echo "  $PIVX_DIR/bin/pivxd -datadir=$DATA_DIR"
echo ""
echo "  # CLI avec wallet specifique"
echo "  $PIVX_DIR/bin/pivx-cli -datadir=$DATA_DIR -rpcwallet=wallet_genesis.dat getbalance"
echo "  $PIVX_DIR/bin/pivx-cli -datadir=$DATA_DIR -rpcwallet=wallet_dev1.dat getbalance"
echo "  $PIVX_DIR/bin/pivx-cli -datadir=$DATA_DIR -rpcwallet=wallet_dev2.dat getbalance"
echo ""
echo "Avantages:"
echo "  - 1 seul daemon (moins de RAM)"
echo "  - 1 seule blockchain (80% moins de disque)"
echo "  - 3 wallets independants pour tests"
