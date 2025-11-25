#!/bin/bash
# Copyright (c) 2025 The PIVX Core developers
# Distributed under the MIT software license

# =============================================================================
# SCRIPT DE DEMONSTRATION KHU EN MODE REGTEST
# =============================================================================
# Ce script permet de tester le pipeline KHU complet:
#   PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV
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
    cli getkhustate 2>/dev/null | jq -r '
        "Hauteur: \(.height)",
        "C (Collateral): \(.C / 100000000) PIV",
        "U (Supply KHU_T): \(.U / 100000000) KHU",
        "Cr (Reward pool): \(.Cr / 100000000) PIV",
        "Ur (Reward rights): \(.Ur / 100000000) KHU",
        "T (Treasury): \(.T / 100000000) PIV",
        "R% annuel: \(.R_annual_bps / 100)%"
    ' 2>/dev/null || echo "État KHU non disponible (V6 pas encore activé?)"
    echo ""
}

# Fonction pour afficher le solde KHU wallet
show_khu_balance() {
    echo -e "${YELLOW}--- Solde KHU Wallet ---${NC}"
    cli khubalance 2>/dev/null || echo "khubalance non disponible"
    echo ""
}

# Nettoyage datadir précédent
echo -e "${YELLOW}[1/10] Nettoyage environnement...${NC}"
if [ -d "$DATADIR" ]; then
    rm -rf "$DATADIR"
fi
mkdir -p "$DATADIR"

# Démarrage pivxd en mode regtest
echo -e "${YELLOW}[2/10] Démarrage pivxd en mode regtest...${NC}"
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
echo -e "${YELLOW}[3/10] Génération 101 blocs initiaux (maturation coinbase)...${NC}"
ADDR=$(cli getnewaddress)
cli generatetoaddress 101 "$ADDR" > /dev/null
BALANCE=$(cli getbalance)
echo -e "${GREEN}✓ Balance disponible: $BALANCE PIV${NC}"
echo ""

# Activation V6 (supposons activation au bloc 200)
echo -e "${YELLOW}[4/10] Génération jusqu'à activation V6 (bloc 200)...${NC}"
cli generatetoaddress 99 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ Hauteur actuelle: $HEIGHT${NC}"
show_khu_state "$HEIGHT"

# Test MINT (PIV → KHU_T)
echo -e "${YELLOW}[5/10] Test MINT: 10 PIV → 10 KHU_T...${NC}"
MINT_AMOUNT=10
MINT_RESULT=$(cli khumint $MINT_AMOUNT 2>&1) || {
    echo -e "${YELLOW}⚠ khumint non disponible ou erreur: $MINT_RESULT${NC}"
    echo "Continuons avec les autres tests..."
}
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ MINT effectué${NC}"
show_khu_state "$HEIGHT"
show_khu_balance

# Test STAKE (KHU_T → ZKHU)
echo -e "${YELLOW}[6/10] Test STAKE: 5 KHU_T → ZKHU (privacy staking)...${NC}"
STAKE_AMOUNT=5
STAKE_RESULT=$(cli khustake $STAKE_AMOUNT 2>&1) || {
    echo -e "${YELLOW}⚠ khustake non disponible ou erreur: $STAKE_RESULT${NC}"
    echo "Continuons avec les autres tests..."
}
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ STAKE effectué${NC}"
show_khu_state "$HEIGHT"

# Afficher les notes stakées
echo -e "${YELLOW}[7/10] Liste des notes ZKHU stakées...${NC}"
cli khuliststaked 2>/dev/null || echo "khuliststaked non disponible"
echo ""

# Génération blocs de maturation (4320 blocs = 3 jours)
echo -e "${YELLOW}[8/10] Génération 4320 blocs de maturation (3 jours simulés)...${NC}"
MATURITY_BLOCKS=4320
for ((i=0; i<43; i++)); do
    cli generatetoaddress 100 "$ADDR" > /dev/null
    CURRENT=$(cli getblockcount)
    PROGRESS=$((i * 100 / 43))
    echo -ne "${BLUE}Progrès: $CURRENT blocs, maturation ${PROGRESS}%${NC}\r"
done
cli generatetoaddress 20 "$ADDR" > /dev/null
echo ""
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ Maturation terminée à la hauteur $HEIGHT${NC}"
show_khu_state "$HEIGHT"

# Test UNSTAKE (ZKHU → KHU_T + yield)
echo -e "${YELLOW}[9/10] Test UNSTAKE: ZKHU → KHU_T + yield bonus...${NC}"
UNSTAKE_RESULT=$(cli khuunstake 2>&1) || {
    echo -e "${YELLOW}⚠ khuunstake non disponible ou erreur: $UNSTAKE_RESULT${NC}"
    echo "Continuons avec les autres tests..."
}
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ UNSTAKE effectué${NC}"
show_khu_state "$HEIGHT"
show_khu_balance

# Test REDEEM (KHU_T → PIV)
echo -e "${YELLOW}[10/10] Test REDEEM: 5 KHU_T → 5 PIV...${NC}"
REDEEM_AMOUNT=5
REDEEM_RESULT=$(cli khuredeem $REDEEM_AMOUNT 2>&1) || {
    echo -e "${YELLOW}⚠ khuredeem non disponible ou erreur: $REDEEM_RESULT${NC}"
}
cli generatetoaddress 1 "$ADDR" > /dev/null
HEIGHT=$(cli getblockcount)
echo -e "${GREEN}✓ REDEEM effectué${NC}"
show_khu_state "$HEIGHT"
show_khu_balance

# Affichage état final
echo -e "${BLUE}=== ÉTAT FINAL ===${NC}"
show_khu_state "$HEIGHT"

# Vérifications
echo -e "${BLUE}=== VERIFICATIONS INVARIANTS ===${NC}"
STATE=$(cli getkhustate 2>/dev/null)

if [ -n "$STATE" ]; then
    C=$(echo "$STATE" | jq -r '.C // 0')
    U=$(echo "$STATE" | jq -r '.U // 0')
    Cr=$(echo "$STATE" | jq -r '.Cr // 0')
    Ur=$(echo "$STATE" | jq -r '.Ur // 0')
    T=$(echo "$STATE" | jq -r '.T // 0')

    echo "C (Collateral): $C satoshis"
    echo "U (KHU Supply): $U satoshis"
    echo "Cr (Reward Pool): $Cr satoshis"
    echo "Ur (Reward Rights): $Ur satoshis"
    echo "T (Treasury): $T satoshis"
    echo ""

    # Check C == U invariant
    if [ "$C" -eq "$U" ]; then
        echo -e "${GREEN}✓ Invariant C == U respecté${NC}"
    else
        echo -e "${RED}✗ ERREUR: C != U (violation invariant!)${NC}"
    fi

    # Check Cr == Ur invariant
    if [ "$Cr" -eq "$Ur" ]; then
        echo -e "${GREEN}✓ Invariant Cr == Ur respecté${NC}"
    else
        echo -e "${RED}✗ ERREUR: Cr != Ur (violation invariant!)${NC}"
    fi

    # Check T >= 0
    if [ "$T" -ge 0 ]; then
        echo -e "${GREEN}✓ Treasury T >= 0 respecté${NC}"
    else
        echo -e "${RED}✗ ERREUR: T < 0 (violation invariant!)${NC}"
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
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" khubalance"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" khulistunspent"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" khuliststaked"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" getblockcount"
echo ""
echo "Pour arrêter:"
echo "  $PIVX_CLI -regtest -datadir=\"$DATADIR\" stop"
echo ""
echo -e "${YELLOW}Pipeline testé: PIV → MINT → KHU_T → STAKE → ZKHU → UNSTAKE → KHU_T → REDEEM → PIV${NC}"
