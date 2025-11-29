# Blueprint V2-01: HU Chain Cleanup Plan

**Version:** 1.0
**Date:** 2025-11-29
**Status:** PLANNING
**Target:** HEDGE-HUNIT repository

---

## 1. OBJECTIF

Nettoyer le code PIVX pour créer **HU Chain** - une blockchain légère avec:
- HU comme coin natif (remplace PIV)
- KHU colored coin 1:1
- ZKHU staking privé (sans Z→Z)
- HU SHIELD transfers privés (garde Z→Z)
- LLMQ/Chainlocks dès genesis
- Block reward = 0 (économie R%)

---

## 2. ANALYSE ACTUELLE

```
PIVX Source Total: ~352,000 lignes

À supprimer:        ~21,300 lignes (confirmé)
Qt (optionnel):     ~30,360 lignes
Total potentiel:    ~51,660 lignes (-15%)
```

---

## 3. SUPPRESSIONS OBLIGATOIRES

### 3.1 Zerocoin (MORT)

| Dossier/Fichier | Lignes | Action |
|-----------------|--------|--------|
| `src/libzerocoin/` | ~2,377 | `rm -rf` |
| `src/zpiv/zpivmodule.cpp` | ~470 | `rm -rf src/zpiv/` |
| `src/zpiv/zpos.cpp` | inclus | idem |
| `src/legacy/validation_zerocoin_legacy.cpp` | ~3,121 | supprimer |

**Commande:**
```bash
rm -rf src/libzerocoin/
rm -rf src/zpiv/
rm src/legacy/validation_zerocoin_legacy.*
```

**Fichiers à nettoyer (références):**
```
src/validation.cpp       → retirer conditions zerocoin
src/init.cpp             → retirer init zerocoin
src/rpc/misc.cpp         → retirer RPC zerocoin
src/wallet/wallet.cpp    → retirer wallet zerocoin
```

---

### 3.2 Legacy Staking (PoS PIVX)

**⚠️ DÉCISION FINALE: GARDER LE MOTEUR PoS**

| Fichier | Lignes | Action |
|---------|--------|--------|
| `src/stakeinput.cpp` | ~200 | **GARDER** |
| `src/stakeinput.h` | ~100 | **GARDER** |
| `src/kernel.cpp` | ~80 | **GARDER** |
| `src/kernel.h` | ~46 | **GARDER** |
| `src/legacy/stakemodifier.cpp` | ~9,421 | **GARDER** |
| `src/legacy/stakemodifier.h` | ~726 | **GARDER** |

**Pourquoi garder?**
- Le moteur PoS reste nécessaire pour la production de blocs
- On met juste `GetBlockValue() = 0` pour supprimer les rewards
- ZKHU-PoS = upgrade v2 future

**À modifier uniquement:**
```cpp
// src/validation.cpp
CAmount GetBlockValue(int nHeight) {
    return 0;  // HU Chain: émission = 0
}
```

---

### 3.3 Cold Staking

| Référence | Occurrences | Action |
|-----------|-------------|--------|
| `coldstaking` | ~305 | grep & remove |
| `ColdStaking` | inclus | idem |
| `cold_staking` | inclus | idem |

**Fichiers principaux:**
```
src/script/standard.cpp  → retirer TX_COLDSTAKE
src/script/standard.h    → retirer enum
src/wallet/wallet.cpp    → retirer cold staking logic
src/rpc/misc.cpp         → retirer RPC
```

**Sporks à supprimer:**
```cpp
// src/sporkid.h
SPORK_17_COLDSTAKING_ENFORCEMENT  // Deprecated
SPORK_19_COLDSTAKING_MAINTENANCE  // Remove
```

---

### 3.4 Budget Legacy → DAO T-direct

**DÉCISION FINALE: Proposals tapent directement dans T**

| Fichier | Lignes | Action |
|---------|--------|--------|
| `src/budget/budgetdb.cpp` | ~200 | simplifier |
| `src/budget/budgetmanager.cpp` | ~2,500 | **REFACTOR** pour T-direct |
| `src/budget/budgetproposal.cpp` | ~400 | adapter pour proposals simples |
| `src/budget/budgetutil.cpp` | ~400 | simplifier |
| `src/budget/finalizedbudget.cpp` | ~600 | **SUPPRIMER** |
| `src/budget/finalizedbudgetvote.cpp` | ~150 | **SUPPRIMER** |
| **Total** | ~3,719 | refactor |

**Nouveau système DAO:**
```cpp
// TX type nouveau
TxType::DAO_PAYOUT = 12

// Structure proposal simplifiée
struct DAOProposal {
    uint256 id;
    CScript payeeScript;    // address bénéficiaire
    CAmount amount;         // montant HU
    int payoutHeight;       // bloc d'exécution
    int yesVotes;
    int noVotes;
    bool executed;
};

// Exécution dans ConnectBlock
if (proposal.approved && nHeight == proposal.payoutHeight) {
    if (state.T >= proposal.amount) {
        state.T -= proposal.amount;
        // Créer TX DAO_PAYOUT vers payeeScript
    }
}
```

**Ce qu'on SUPPRIME:**
- Superblocks
- Cycles d'élection complexes
- Payouts depuis block reward

---

### 3.5 Sporks Deprecated

```cpp
// À SUPPRIMER de src/sporkid.h:
SPORK_2_SWIFTTX                    = 10001,  // Deprecated v4.3.99
SPORK_3_SWIFTTX_BLOCK_FILTERING    = 10002,  // Deprecated v4.3.99
SPORK_5_MAX_VALUE                  = 10004,  // Deprecated v5.2.99
SPORK_16_ZEROCOIN_MAINTENANCE_MODE = 10015,  // Deprecated v5.2.99
SPORK_17_COLDSTAKING_ENFORCEMENT   = 10017,  // Deprecated v4.3.99
SPORK_18_ZEROCOIN_PUBLICSPEND_V4   = 10018,  // Deprecated v5.2.99
SPORK_19_COLDSTAKING_MAINTENANCE   = 10019,  // Remove

// À GARDER:
SPORK_8_MASTERNODE_PAYMENT_ENFORCEMENT   // MN
SPORK_9_MASTERNODE_BUDGET_ENFORCEMENT    // Budget/DAO
SPORK_13_ENABLE_SUPERBLOCKS              // Superblocks
SPORK_14_NEW_PROTOCOL_ENFORCEMENT        // Protocol
SPORK_15_NEW_PROTOCOL_ENFORCEMENT_2      // Protocol
SPORK_20_SAPLING_MAINTENANCE             // HU SHIELD
SPORK_21_LEGACY_MNS_MAX_HEIGHT           // DMN transition
SPORK_22_LLMQ_DKG_MAINTENANCE            // LLMQ
SPORK_23_CHAINLOCKS_ENFORCEMENT          // Chainlocks
```

---

## 4. COMPOSANTS À GARDER

### 4.1 Essentiels (INTOUCHABLES)

| Composant | Lignes | Usage |
|-----------|--------|-------|
| `src/sapling/` | ~7,133 | HU SHIELD + ZKHU |
| `src/llmq/` | ~8,540 | Chainlocks, Finality |
| `src/evo/` | ~3,178 | DMN/ProTx |
| `src/tiertwo/` | ~1,356 | Masternodes |
| `src/crypto/` | ~10k+ | Cryptographie |
| `src/khu/` | existant | Pipeline KHU |
| `src/chiabls/` | externe | BLS signatures |

### 4.2 À Adapter

| Composant | Action |
|-----------|--------|
| `src/chainparams.cpp` | Nouveau genesis HU |
| `src/consensus/params.h` | Params HU Chain |
| `src/amount.h` | HU denomination |
| `src/validation.cpp` | Block reward = 0 |
| `src/spork*` | Nettoyer deprecated |
| `src/budget/` | Adapter pour T |

---

## 5. OPTIONNEL

### 5.1 Qt GUI (~30,360 lignes)

**Options:**
1. **Garder** - Interface utilisateur complète
2. **Supprimer** - CLI only (daemon + cli)
3. **Différer** - Garder pour v1, nettoyer v2

**Recommandation:** Garder pour v1 (UX importante)

### 5.2 ZMQ (~562 lignes)

**Usage:** Notifications real-time pour apps externes
**Recommandation:** Garder (utile pour monitoring)

### 5.3 Tests

**Action:** Supprimer tests zerocoin/legacy, garder tests KHU/LLMQ

---

## 6. PLAN D'EXÉCUTION

### Phase 1: Nettoyage Sécurisé (Jour 1-2)

```bash
# 1. Backup
git checkout -b hu-cleanup
git tag pre-cleanup-backup

# 2. Supprimer dossiers morts
rm -rf src/libzerocoin/
rm -rf src/zpiv/

# 3. Supprimer fichiers legacy
rm src/stakeinput.*
rm src/kernel.*
rm src/legacy/validation_zerocoin_legacy.*
rm src/legacy/stakemodifier.*

# 4. Commit checkpoint
git add -A && git commit -m "chore: remove zerocoin and legacy staking"
```

### Phase 2: Nettoyage Références (Jour 2-3)

```bash
# 1. Trouver références zerocoin
grep -r "zerocoin\|zPIV\|libzerocoin" src/ --include="*.cpp" --include="*.h"

# 2. Trouver références cold staking
grep -r "coldstaking\|ColdStaking\|cold_staking" src/ --include="*.cpp" --include="*.h"

# 3. Nettoyer fichier par fichier
# - validation.cpp
# - init.cpp
# - wallet/wallet.cpp
# - rpc/*.cpp

# 4. Commit
git commit -am "chore: clean zerocoin and coldstaking references"
```

### Phase 3: Sporks (Jour 3)

```cpp
// Modifier src/sporkid.h
// Retirer les 7 sporks deprecated
// Garder les 9 sporks essentiels
```

### Phase 4: Build & Test (Jour 3-4)

```bash
# 1. Clean build
make clean
./autogen.sh
./configure --disable-tests
make -j$(nproc)

# 2. Fix compilation errors
# Itérer jusqu'à clean build

# 3. Test basique
./src/hud -regtest -daemon
./src/hu-cli -regtest getblockcount
```

### Phase 5: Renommage (Jour 4-5)

```bash
# 1. PIV → HU
find src/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's/PIV/HU/g' {} \;

# 2. PIVX → HU
find src/ -type f \( -name "*.cpp" -o -name "*.h" \) -exec sed -i 's/PIVX/HU/g' {} \;

# 3. pivxd → hud
mv src/pivxd.cpp src/hud.cpp
sed -i 's/pivxd/hud/g' src/*.cpp src/*.h

# 4. pivx-cli → hu-cli
mv src/pivx-cli.cpp src/hu-cli.cpp
sed -i 's/pivx-cli/hu-cli/g' src/*.cpp src/*.h

# 5. Makefile.am updates
```

---

## 7. VÉRIFICATION

### Checklist Post-Cleanup

- [ ] `make clean && make -j$(nproc)` compile sans erreur
- [ ] Aucune référence `zerocoin` dans le code
- [ ] Aucune référence `coldstaking` dans le code
- [ ] Sporks deprecated supprimés
- [ ] `hud -regtest` démarre
- [ ] `hu-cli -regtest getblockcount` fonctionne
- [ ] Tests KHU passent
- [ ] LLMQ fonctionne

### Comptage Final

```bash
# Avant
find src/ -name "*.cpp" -o -name "*.h" | xargs wc -l | tail -1

# Après
find src/ -name "*.cpp" -o -name "*.h" | xargs wc -l | tail -1

# Delta
echo "Réduction: $((AVANT - APRES)) lignes"
```

---

## 8. RISQUES ET MITIGATIONS

| Risque | Mitigation |
|--------|------------|
| Dépendances cachées | Compiler souvent, fix au fur et à mesure |
| Casser consensus | Tests regtest intensifs |
| Références manquées | grep exhaustif avant commit |
| Build breaks | Commits atomiques, facile à rollback |

---

## 9. FICHIERS MODIFIÉS (Estimé)

```
SUPPRESSIONS DIRECTES:
- src/libzerocoin/*           (2,377 lignes)
- src/zpiv/*                  (471 lignes)
- src/legacy/zerocoin*        (3,121 lignes)
- src/legacy/stakemodifier*   (10,147 lignes)
- src/stakeinput.*            (300 lignes)
- src/kernel.*                (126 lignes)
                              ─────────────
                              ~16,500 lignes

MODIFICATIONS:
- src/validation.cpp          (~500 lignes retirées)
- src/init.cpp                (~200 lignes retirées)
- src/wallet/wallet.cpp       (~300 lignes retirées)
- src/rpc/*.cpp               (~200 lignes retirées)
- src/sporkid.h               (~50 lignes retirées)
- src/script/standard.*       (~100 lignes retirées)
                              ─────────────
                              ~1,350 lignes

TOTAL ESTIMÉ: ~17,850 - 21,300 lignes
```

---

## 10. TIMELINE

```
Jour 1:   Backup + Suppression dossiers
Jour 2:   Nettoyage références zerocoin
Jour 3:   Nettoyage cold staking + sporks
Jour 4:   Build & fix errors
Jour 5:   Renommage PIV→HU
Jour 6:   Tests complets
Jour 7:   Documentation + commit final

TOTAL: ~1 semaine
```

---

## 11. COMMIT MESSAGES

```
chore: remove libzerocoin and zpiv folders
chore: remove legacy staking (stakeinput, kernel, stakemodifier)
chore: clean zerocoin references from validation
chore: clean zerocoin references from wallet
chore: remove cold staking code
chore: remove deprecated sporks (2,3,5,16,17,18,19)
refactor: rename PIV to HU globally
refactor: rename pivxd to hud, pivx-cli to hu-cli
feat(genesis): create HU Chain genesis block
test: verify KHU pipeline on HU Chain
```

---

## CHANGELOG

- **v1.0** (2025-11-29): Initial cleanup plan for HU Chain
