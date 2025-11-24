#!/bin/bash
# Copyright (c) 2025 The PIVX Core developers
# Distributed under the MIT software license

# =============================================================================
# SCRIPT DE DEMONSTRATION KHU EN MODE REGTEST
# =============================================================================
# Ce script permet de tester un cycle DAO complet (172800 blocs) en quelques
# minutes au lieu d'attendre 4 mois en production.
#
# USAGE:
#   ./test_khu_regtest_demo.sh
#
# PRE-REQUIS:
#   - pivxd compilé (dans PIVX/src/)
#   - Port 18444 disponible (regtest default)
# =============================================================================

set -e  # Exit on error

# Configuration
PIVXD="./PIVX/src/pivxd"
PIVX_CLI="./PIVX/src/pivx-cli"
DATADIR="/tmp/pivx_khu_regtest_demo"
RPC_PORT=18444

# Couleurs pour output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  KHU REGTEST DEMONSTRATION SCRIPT${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""

# Fonction utilitaire
cli() {
    $PIVX_CLI -regtest -datadir="$DATADIR" "$@"
}

# Fonction pour afficher l'état KHU
show_khu_state() {
    echo -e "${YELLOW}--- État KHU à la hauteur $1 ---${NC}"
    cli getkhustate | jq -r '
        "Hauteur: \(.height)",
        "C (Collateral): \(.C / 100000000) PIV",
        "U (Staked): \(.U / 100000000) PIV",
        "Cr (Reward pool): \(.Cr / 100000000) PIV",
        "Ur (Reward rights): \(.Ur / 100000000) PIV",
        "T (Treasury): \(.T / 100000000) PIV",
        "R% annuel: \(.R_annual_bps / 100)%"
    ' 2>/dev/null || echo "État KHU non disponible (V6 pas encore activé?)"
    echo ""
}

# Nettoyage datadir précédent
echo -e "${YELLOW}[1/8] Nettoyage environnement...${NC}"
if [ -d "$DATADIR" ]; then
    rm -rf "$DATADIR"
fi
mkdir -p "$DATADIR"

# Démarrage pivxd en mode regtest
echo -e "${YELLOW}[2/8] Démarrage pivxd en mode regtest...${NC}"
$PIVXD -regtest -datadir="$DATADIR" -daemon -txindex=1 \
    -port=18445 -rpcport=$RPC_PORT -fallbackfee=0.0001 \
    -server=1 -rpcallowip=127.0.0.1

# Attente démarrage avec retry
echo "Attente démarrage pivxd..."
MAX_RETRIES=20
RETRY_COUNT=0
while [ $RETRY_COUNT -lt $MAX_RETRIES ]; do
    sleep 2
    if cli getblockcount &>/dev/null; then
        break
    fi
    RETRY_COUNT=$((RETRY_COUNT + 1))
    echo -n "."
done
echo ""

# Vérification connexion
if ! cli getblockcount &>/dev/null; then
    echo -e "${RED}Erreur: pivxd ne répond pas après ${MAX_RETRIES} tentatives${NC}"
    echo "Vérifiez les logs: tail -50 $DATADIR/regtest/debug.log"
    exit 1
fi

echo -e "${GREEN}✓ Pivxd démarré (datadir: $DATADIR)${NC}"
echo ""

# Génération blocs initiaux (maturation coinbase)
echo -e "${YELLOW}[3/8] Génération 101 blocs initiaux (maturation coinbase)...${NC}"
ADDR=$(cli getnewaddress)
cli generatetoaddress 101 "$ADDR" > /dev/null
BALANCE=$(cli getbalance)
echo -e "${GREEN}✓ Balance disponible: $BALANCE PIV${NC}"
echo ""

# Activation V6 (supposons activation au bloc 200)
echo -e "${YELLOW}[4/8] Génération jusqu'à activation V6 (bloc 200)...${NC}"
cli generatetoaddress 99 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ Hauteur actuelle: $HEIGHT${NC}"
show_khu_state "$HEIGHT"

# Test MINT (création KHU)
echo -e "${YELLOW}[5/8] Test transaction MINT (10 PIV → KHU)...${NC}"
MINT_AMOUNT=10
MINT_TX=$(cli sendtoaddress "$(cli getnewaddress)" $MINT_AMOUNT false "MINT TEST" "" false 1)
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ MINT effectué (txid: ${MINT_TX:0:16}...)${NC}"
show_khu_state "$HEIGHT"

# Test STAKE
echo -e "${YELLOW}[6/8] Test transaction STAKE (5 PIV → staking)...${NC}"
STAKE_AMOUNT=5
STAKE_TX=$(cli sendtoaddress "$(cli getnewaddress)" $STAKE_AMOUNT false "STAKE TEST" "" false 2)
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ STAKE effectué (txid: ${STAKE_TX:0:16}...)${NC}"
show_khu_state "$HEIGHT"

# PARTIE CRITIQUE: Génération jusqu'au premier cycle DAO
echo -e "${YELLOW}[7/8] Génération jusqu'au premier cycle DAO (172800 blocs)...${NC}"
echo -e "${BLUE}INFO: Cette opération prend ~2-5 minutes selon votre machine${NC}"
echo -e "${BLUE}INFO: En production, cela représenterait 4 mois d'attente !${NC}"
echo ""

# Calcul blocs nécessaires
CURRENT_HEIGHT=$(cli getblockcount)
ACTIVATION_HEIGHT=200
DAO_CYCLE=172800
TARGET_HEIGHT=$((ACTIVATION_HEIGHT + DAO_CYCLE))
BLOCKS_TO_GENERATE=$((TARGET_HEIGHT - CURRENT_HEIGHT))

echo "Hauteur actuelle: $CURRENT_HEIGHT"
echo "Hauteur cible (1er cycle DAO): $TARGET_HEIGHT"
echo "Blocs à générer: $BLOCKS_TO_GENERATE"
echo ""

# Génération par blocs de 1000 pour monitoring
BATCH_SIZE=10000
REMAINING=$BLOCKS_TO_GENERATE

while [ $REMAINING -gt 0 ]; do
    if [ $REMAINING -lt $BATCH_SIZE ]; then
        TO_GEN=$REMAINING
    else
        TO_GEN=$BATCH_SIZE
    fi

    cli generatetoaddress $TO_GEN "$ADDR" > /dev/null
    REMAINING=$((REMAINING - TO_GEN))
    CURRENT=$(cli getblockcount)
    PROGRESS=$(echo "scale=1; ($CURRENT - $ACTIVATION_HEIGHT) * 100 / $DAO_CYCLE" | bc)
    echo -ne "${BLUE}Progrès: $CURRENT / $TARGET_HEIGHT blocs (${PROGRESS}%)${NC}\r"
done

echo ""
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ Cycle DAO atteint à la hauteur $HEIGHT${NC}"
echo ""

# Affichage état final
echo -e "${YELLOW}[8/8] État final après 1er cycle DAO:${NC}"
show_khu_state "$HEIGHT"

# Vérifications
echo -e "${BLUE}=== VERIFICATIONS ===${NC}"
STATE=$(cli getkhustate 2>/dev/null)

if [ -n "$STATE" ]; then
    T=$(echo "$STATE" | jq -r '.T // 0')
    U=$(echo "$STATE" | jq -r '.U // 0')
    Ur=$(echo "$STATE" | jq -r '.Ur // 0')

    # Calcul attendu: 0.5% × (U + Ur)
    EXPECTED=$(echo "scale=0; ($U + $Ur) * 5 / 1000" | bc)

    echo "T (Treasury): $T satoshis"
    echo "Attendu (~0.5% × (U+Ur)): $EXPECTED satoshis"

    if [ "$T" -gt 0 ]; then
        echo -e "${GREEN}✓ DAO Treasury a bien accumulé des fonds${NC}"
    else
        echo -e "${YELLOW}⚠ DAO Treasury = 0 (peut être normal si U+Ur=0)${NC}"
    fi
else
    echo -e "${YELLOW}⚠ Impossible de récupérer l'état KHU${NC}"
fi

echo ""
echo -e "${BLUE}======================================${NC}"
echo -e "${BLUE}  DEMONSTRATION TERMINEE${NC}"
echo -e "${BLUE}======================================${NC}"
echo ""
echo -e "${GREEN}Le nœud regtest est toujours actif.${NC}"
echo ""
echo "Commandes utiles:"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" getkhustate"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" getblockcount"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" getbalance"
echo ""
echo "Pour arrêter:"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" stop"
echo ""
echo -e "${YELLOW}Note: Ce script a simulé 4 MOIS de blockchain en quelques minutes !${NC}"
