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

**STATUT : ✅ COMPLETED**
*Référence : voir docs/reports/phase4/ pour implémentation et tests*
*Tests : 7/7 PASS (DisconnectKHUBlock + unit tests)*

### Objectifs
- STAKE : KHU_T → ZKHU.
- UNSTAKE : ZKHU → KHU_T (avec bonus Ur_accumulated).
- Sapling minimal : 1 note par stake.
- Pas de Z→Z KHU.
- Rolling Frontier Tree.
- Maturité staking : 3 jours = 4320 blocs.

### Résultat
✅ **IMPLÉMENTÉ ET VALIDÉ**
- Staking privé ZK opérationnel
- Notes ZKHU avec nullifiers (anti-double-spend)
- DisconnectKHUBlock fonctionnel (reorg safety)
- Database ZKHU avec note tracking

---------------------------------------

## 5. PHASE 5 — ZKHU SAPLING & DB INTEGRATION

**STATUT : ✅ COMPLETED & TESTED**
*Référence : voir docs/reports/phase5/ pour rapports d'implémentation et audit*
*Tests : 38/38 PASS (33 C++ unit tests + 5 Python stress tests)*

### Objectifs
- Intégration complète Sapling dans système KHU
- Database ZKHU avec note tracking et nullifiers
- ZKHU note commitment tree (Merkle tree Sapling)
- Vérification ZKHU proofs (Groth16)
- DisconnectKHUBlock (reorg safety)

**Tests exhaustifs:**
- **Regression (6/6)**: Non-régression Phases 1-4
- **Red Team (12/12)**: Attaques économiques (overflow, double-spend, pool drain)
- **Yield (15/15)**: Formules R% avec BLOCKS_PER_DAY=1440 canonique
- **Python Stress (5/5)**: Long sequences, reorgs (shallow & deep), cascade

### Résultat
✅ **IMPLÉMENTÉ ET AUDITÉ**
- ZKHU Sapling opérationnel (privacy complète)
- Database LevelDB avec préfixes ZKHU
- Nullifier tracking (anti-double-spend)
- Invariants C==U, Cr==Ur vérifiés avec égalité EXACTE
- Reorg safety testée (jusqu'à 20 blocs)
- Formules yield consensus-accurate (pas d'approximations float)
- Protection overflow avec __int128

**Axiome confirmé:** KHUPoolInjection = 0 (système fermé, aucune injection externe)

---------------------------------------

## 6. PHASE 6 — DOMC (GOUVERNANCE R% + DAO BUDGET)

**STATUT : ⏳ PLANNED**
*Référence : voir docs/blueprints/06-YIELD-R-PERCENT.md et 08-DAO-BUDGET.md*

### Objectifs

**6.1 — Gouvernance R% (Yield Stakers)**
- Vote commit/reveal par masternodes (privacy)
- Cycle 4 mois = 172800 blocs
- Timeline:
  ```
  0────────132480────152640────172800
  │   R% ACTIF  │ COMMIT │PRÉAVIS│
  │  (3m+2j)    │ 2 sem  │ 2 sem │
  ```
- R% ∈ [0, R_MAX_DYNAMIC]
- R_MAX_DYNAMIC = max(400, 3000 – year×100) // Décroit 30%→4% sur 25 ans
- Activation automatique tous les 172800 blocs

**Formule yield (quotidien):**
```
Ur_daily = floor(stake_amount × R_annual / 10000 / 365)
Cr += Ur_daily
Ur += Ur_daily
```

**UNSTAKE (bonus matérialisé):**
```
bonus = Ur_accumulated
C += bonus  (MINT nouveaux KHU_T)
U += bonus
Cr -= bonus (consommation pool)
Ur -= bonus
```

**6.2 — DAO Budget Automatique (NOUVEAU)**
- **Budget créé automatiquement** tous les 4 mois (aligné cycle DOMC):
  ```
  DAO_budget = (U + Ur) × 0.5%  // 0,5% de la supply KHU
  ```
- **Distribution contrôlée par vote MN:**
  - Propositions DAO soumises (projets dev, marketing, infra)
  - Masternodes votent approve/reject
  - Proposition acceptée → PIV payé au projet
  - Proposition rejetée → **PIV BRÛLÉ** 🔥 (déflationniste!)

**Inflation annuelle:** ~1,5%/an (0,5% × 3 cycles)

**Gouvernance:**
- Masternodes = gouvernants (vote R% + propositions DAO)
- Stakers KHU = économie (votent avec leurs pieds: stake/unstake si R% insatisfaisant)

### Résultat
- ✅ Création monétaire programmable & décentralisée
- ✅ Financement DAO perpétuel (post année-6 où émission PIVX = 0)
- ✅ Mécanisme déflationniste (burn si propositions rejetées)
- ✅ Équilibre des pouvoirs (MN votent, stakers sanctionnent économiquement)

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
