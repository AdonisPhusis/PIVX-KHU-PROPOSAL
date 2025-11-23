# 05 — PIVX-V6-KHU ROADMAP (FINAL)

Roadmap claire, simple, technique, sans dates, uniquement en PHASES NUMÉROTÉES et DURÉES EN BLOCS.
Aucune notion inutile. Aucun audit. Juste : SI TESTNET OK → MAINNET OK.

---------------------------------------

## 1. PHASE 1 — CONSENSUS DE BASE

**STATUT : ✅ COMPLETED**
*Référence : voir docs/reports/phase1/ pour les rapports d'implémentation détaillés*

### Objectifs
- Activer l'infrastructure KHU dans PIVX-V6.
- Ajouter l'état global : C, U, Cr, Ur.
- Invariants :
  - CD = C/U = 1 (strict)
  - CDr = Cr/Ur = 1 (strict)
- Activer :
  - MINT (PIV brûlés → KHU créés)
  - REDEEM (KHU brûlés → PIV créés)
  - Aucun BURN KHU (seul REDEEM détruit du KHU)
- Créer le tracker UTXO KHU_T.
- Tous les frais PIVX sont BRÛLÉS.
- Implémenter l'émission PIVX déflationnaire :
  - year = (height – activation_height) / 525600
  - reward_year = max(6 – year, 0)
  - staker = mn = dao = reward_year.
- RPC : getkhuglobalstate.

### Résultat
Socle économique stable prêt pour SAPLING et DOMC.

---------------------------------------

## 2. PHASE 2 — PIPELINE KHU (MODE TRANSPARENT)

**STATUT : ✅ COMPLETED**
*Référence : voir docs/reports/phase2/ pour les rapports d'implémentation et tests*

### Objectifs
Pipeline minimal garanti :
PIV → MINT → KHU_T → REDEEM → PIV.
Pas de privacy.
Pas de rendement.

### Résultat
KHU fonctionne comme actif collatéralisé 1:1.

---------------------------------------

## 3. PHASE 3 — FINALITÉ MASTERNODE

**STATUT : ✅ COMPLETED & VALIDATED**
*Référence : voir docs/reports/phase3/RAPPORT_FINAL_PHASE3_V6_ACTIVATION.md*
*Tests : 52/52 PASS (100% success) | Sécurité : 20/20 vecteurs d'attaque bloqués*

### Objectifs
- Finalité BLS via masternodes.
- Finalisation ≤ 12 blocs :
  - protège C/U/Cr/Ur
  - élimine reorgs profonds
  - sécurise DOMC
  - simplifie consensus.
- Rotation quorum toutes les 240 blocs :
  Un quorum = groupe déterministe de masternodes signant les blocs.
  Rotation = empêche capture longue.
- **KhuStateCommitment** signé par LLMQ :
  - Chaque état KHU (C, U, Cr, Ur) reçoit une signature BLS du quorum actif
  - Après 12 confirmations avec signatures, l'état devient **irréversible**
  - Structure : `struct KhuStateCommitment { uint256 stateHash; vector<CBLSSignature> sigs; }`
  - Stockage : LevelDB clé `'K' + 'C' + height`

### Résultat
✅ **IMPLÉMENTÉ ET VALIDÉ**
- Invariants CD et CDr impossibles à briser (garantie mathématique)
- Finality opérationnelle : état KHU irréversible après 12 blocs signés
- Structure KhuStateCommitment avec BLS signatures fonctionnelle
- Database LevelDB opérationnelle (préfixe 'K'+'C')
- Seuil quorum ≥60% implémenté
- Protection double couche : limite 12 blocs + finalité cryptographique
- RPC getkhustatecommitment fonctionnel
- Tous les CVE critiques résolus (CVE-KHU-2025-002, VULN-KHU-2025-001)

---------------------------------------

## 4. PHASE 4 — SAPLING (STAKE / UNSTAKE)

**STATUT : ⏳ PLANNED**
*Durée estimée : ~8 jours d'implémentation (complexité LOW-MEDIUM)*
*Approche : Wrapper autour du Sapling PIVX existant (pas de nouvelle crypto)*

### Objectifs
- STAKE : KHU_T → ZKHU.
- UNSTAKE : ZKHU → KHU_T.
- Sapling minimal : 1 note par stake.
- Pas de Z→Z KHU.
- Rolling Frontier Tree.
- Maturité staking : 3 jours = 4320 blocs.

### Résultat
Staking privé ZK opérationnel.

---------------------------------------

## 5. PHASE 5 — YIELD Cr/Ur (MOTEUR DOMC)

**STATUT : ⏳ PLANNED**
*Durée estimée : ~7 jours d'implémentation*
*Axiome confirmé : KHUPoolInjection = 0 (aucune injection externe)*

### Fonctionnement
- R% voté annuellement par DOMC.
- R_MAX_DYNAMIC = max(400, 3000 – year(height)*100).  // basis points

**Valeurs initiales (activation):**
- R_annual = 500 (5.00%)
- R_MAX_dynamic = 3000 (30.00%)
- Premier cycle DOMC: activation_height + 525600 (1 an)

Chaque jour = 1440 blocs :
Ur_daily = (stake_amount * R_annual/10000) / 365
Ur += Ur_daily
Cr += Ur_daily

UNSTAKE :
bonus = Ur_accumulé
U += bonus
C += bonus
Cr -= bonus
Ur -= bonus

Invariants préservés automatiquement.

### Résultat
Rendement généré uniquement par DOMC.

---------------------------------------

## 6. PHASE 6 — DOMC (GOUVERNANCE DE R%)

**STATUT : ⏳ PLANNED**

### Objectifs
- Vote commit/reveal.
- Cycle exprimé en blocs.
- Activation automatique.
- R% ∈ [0, R_MAX_DYNAMIC].

### Résultat
Création monétaire programmable & décentralisée.

---------------------------------------

## 7. PHASE 7 — HTLC CROSS-CHAIN (GATEWAY COMPATIBLE)

**STATUT : ⏳ PLANNED**
*Note importante : Utilise les scripts HTLC Bitcoin standards (pas d'implémentation KHU spéciale)*
*Gateway : Off-chain, KHU comme unité de compte uniquement*

### Objectifs
- Implémentation HTLC compatibles Gateway :
  - **Hashlock:** SHA256(secret) uniquement (32 bytes)
  - **Timelock:** block height (CLTV) - pas de timestamp
  - **Script:** Template Bitcoin standard
  - **RPC:** khu_listhtlcs, khu_gethtlc, khu_htlccreate, khu_htlcclaim, khu_htlcrefund
- HTLC_CREATE / CLAIM / REFUND
- Compatible BTC / DASH / ETH
- Pattern matching automatique (watchers)

### Spécifications
- **Hashlock:** `hashlock = SHA256(secret)` où `secret = 32 bytes random`
- **Timelock:** `timelock_height: uint32_t` (CHECKLOCKTIMEVERIFY)
- **Script template:**
  ```
  OP_IF
      OP_SHA256 <hashlock> OP_EQUALVERIFY
      <recipient_pubkey> OP_CHECKSIG
  OP_ELSE
      <timelock_height> OP_CHECKLOCKTIMEVERIFY OP_DROP
      <sender_pubkey> OP_CHECKSIG
  OP_ENDIF
  ```
- **Status:** pending / claimed / refunded
- **Invariants:** C=U, Cr=Ur préservés (HTLC = ownership transfer only)

### Tests Requis
- HTLC create/claim/refund
- Pattern matching compatible watchers
- Secret propagation validée dans scriptSig
- Aucun impact sur C/U/Cr/Ur
- Cross-chain atomic swap simulation

### Interdictions Absolues
- ❌ Z→Z HTLC (ZKHU → ZKHU)
- ❌ Timelock timestamp
- ❌ SHA3/blake2/keccak hashlock
- ❌ KHU burn via HTLC
- ❌ Metadata on-chain
- ❌ KHU→PIV direct (use REDEEM first)
- ❌ Oracle price feeds on-chain

### Résultat
Interopérabilité UTXO-chain avec Gateway off-chain compatible.

---------------------------------------

## 8. PHASE 8 — WALLET / RPC

**STATUT : ⏳ PLANNED**

### Objectifs
- RPC complet : MINT, REDEEM, STAKE, UNSTAKE, DOMC, finalité.
- UI complète.
- Affichage C/U/Cr/Ur et finalité.

---------------------------------------

## 9. PHASE 9 — TESTNET LONG

**STATUT : ⏳ PLANNED**

### Objectifs
Tester :
- finalité
- staking
- Cr/Ur
- DOMC
- HTLC
- invariants CD & CDr
Jusqu'à stabilité complète.

SI TESTNET OK → MAINNET.

---------------------------------------

## 10. PHASE 10 — MAINNET

**STATUT : ⏳ PLANNED**

Activation du système complet :
- Emission PIVX 6→0 active.
- DOMC actif.
- Finalité masternode.
- Cr/Ur actifs.
- Sapling staking-only.
- HTLC cross-chain.
- Fees brûlés.
- CD et CDr garantis.

---------------------------------------

## FIN
