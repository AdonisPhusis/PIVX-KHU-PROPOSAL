#!/bin/bash
# KHU Testnet Faucet
# Distributes test PIV to users

FAUCET_AMOUNT=${FAUCET_AMOUNT:-100}  # Default 100 PIV per request
DATADIR=${DATADIR:-"/home/ubuntu/.pivx-testnet"}
CLI="pivx-cli -testnet -datadir=$DATADIR"

echo "╔═══════════════════════════════════════════════════════════════╗"
echo "║              KHU TESTNET FAUCET                               ║"
echo "╚═══════════════════════════════════════════════════════════════╝"

if [ -z "$1" ]; then
    echo ""
    echo "Usage: $0 <pivx_address>"
    echo ""
    echo "  Sends $FAUCET_AMOUNT PIV to the specified address"
    echo ""
    echo "Environment variables:"
    echo "  FAUCET_AMOUNT - Amount to send (default: 100)"
    echo "  DATADIR       - Node data directory"
    echo ""
    exit 1
fi

ADDRESS="$1"

# Validate address format (basic check)
if [[ ! "$ADDRESS" =~ ^[yYdD][a-zA-Z0-9]{25,34}$ ]]; then
    echo "ERROR: Invalid PIVX address format"
    exit 1
fi

# Check node is running
if ! $CLI getblockcount > /dev/null 2>&1; then
    echo "ERROR: PIVX node not running or not accessible"
    exit 1
fi

# Check balance
BALANCE=$($CLI getbalance)
if (( $(echo "$BALANCE < $FAUCET_AMOUNT" | bc -l) )); then
    echo "ERROR: Faucet balance too low ($BALANCE PIV)"
    exit 1
fi

echo ""
echo "Sending $FAUCET_AMOUNT PIV to $ADDRESS..."

TXID=$($CLI sendtoaddress "$ADDRESS" $FAUCET_AMOUNT "" "" false)

if [ $? -eq 0 ]; then
    echo ""
    echo "SUCCESS!"
    echo "  Amount: $FAUCET_AMOUNT PIV"
    echo "  To:     $ADDRESS"
    echo "  TxID:   $TXID"
    echo ""
    echo "Transaction will be confirmed in the next block."
else
    echo ""
    echo "ERROR: Failed to send transaction"
    exit 1
fi
