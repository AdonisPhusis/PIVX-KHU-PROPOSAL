# 06 — YIELD R% BLUEPRINT

**Date:** 2025-11-22
**Version:** 1.0
**Status:** CANONIQUE (IMMUABLE)

---

## OBJECTIF

Ce sous-blueprint définit le système de **yield R%** pour le staking KHU.

**R% est le taux de rendement annuel versé aux stakers KHU, gouverné par DOMC.**

**RÈGLE ABSOLUE : R% est INDÉPENDANT de l'émission PIVX et fonctionne via le pool de reward (Cr/Ur).**

---

## 1. R% : CONCEPT ET NÉCESSITÉ

### 1.1 Qu'est-ce que R% ?

**R%** = Taux de rendement annuel versé aux utilisateurs qui stakent KHU_T en ZKHU.

```
Exemple :
  Alice stake 1000 KHU_T → ZKHU
  R% = 5.00% annuel

  Après 1 an (365 jours) :
  Yield = 1000 × 5% = 50 KHU

  Alice unstake → reçoit :
  Principal : 1000 KHU_T
  Bonus :       50 KHU_T
  Total :     1050 KHU_T
```

**Unité :** R% est exprimé en **pourcentage avec 2 décimales** (XX.XX%)
```cpp
// Format stockage: centièmes de % (2 decimals)
uint16_t R_annual = 2555;  // 25.55%
uint16_t R_annual = 2020;  // 20.20%
uint16_t R_annual = 500;   // 5.00%

// Conversion:
double R_percent = R_annual / 100.0;  // 2555 / 100 = 25.55%
```

### 1.2 Pourquoi R% est Nécessaire ?

**Après l'année 6, l'émission PIVX tombe à 0.**

```
┌──────────────────────────────────────────────────────┐
│ TIMELINE PIVX v6-KHU                                 │
├──────────────────────────────────────────────────────┤
│ Année 0-6 :                                          │
│   • Émission PIVX : 18→15→12→9→6→3→0 PIV/bloc       │
│   • Stakers PIV : reward_year PIV/bloc               │
│   • Stakers KHU : reward_year PIV + R% KHU           │
│                                                      │
│ Année 6+ :                                           │
│   • Émission PIVX : 0 PIV/bloc ❌                    │
│   • Stakers PIV : 0 récompense ❌                    │
│   • Stakers KHU : R% KHU uniquement ✅               │
│                                                      │
│ → R% devient la SEULE récompense pour stakers       │
└──────────────────────────────────────────────────────┘
```

**Sans R%, après année 6 :**
- Pas de récompense pour stakers
- Pas d'incitation à sécuriser le réseau
- Réseau devient vulnérable

**Avec R% :**
- Stakers continuent d'être récompensés
- Incitation perpétuelle à staker
- Sécurité réseau maintenue

### 1.3 R% vs Émission PIVX (Indépendance Stricte)

```
┌─────────────────────────────────────────┐
│ ÉMISSION PIVX (reward_year)             │
├─────────────────────────────────────────┤
│ • Source : Création monétaire PIVX      │
│ • Formule : max(6 - year, 0) × COIN     │
│ • Bénéficiaires : Staker + MN + DAO     │
│ • Gouvernance : AUCUNE (immuable)       │
│ • Fin : Année 6 (0 PIV/bloc)            │
└─────────────────────────────────────────┘

┌─────────────────────────────────────────┐
│ YIELD KHU (R%)                          │
├─────────────────────────────────────────┤
│ • Source : Pool de reward (Cr/Ur)       │
│ • Formule : (amount × R%) / 365 / 10000 │
│ • Bénéficiaires : Stakers KHU uniquement│
│ • Gouvernance : DOMC (vote MN)          │
│ • Fin : Jamais (perpétuel)              │
└─────────────────────────────────────────┘
```

**INTERDICTION ABSOLUE : R% et reward_year NE DOIVENT JAMAIS s'influencer.**

---

## 2. POOL DE REWARD (Cr/Ur)

### 2.1 Qu'est-ce que Cr/Ur ?

**Cr** (Collateral reward) = PIV verrouillé pour financer les rewards
**Ur** (Utility reward) = Droits de reward KHU accumulés

```cpp
struct KhuGlobalState {
    int64_t C;   // Collateral total PIV
    int64_t U;   // Supply totale KHU_T
    int64_t Cr;  // ← Collateral reward pool
    int64_t Ur;  // ← Reward rights accumulated

    // Invariant 2 : Cr == Ur (TOUJOURS)
};
```

**Invariant sacré :** `Cr == Ur` (à tout moment)

### 2.2 D'où Vient le Pool de Reward ?

**Le pool Cr/Ur est alimenté UNIQUEMENT par l'émission de blocs.**

```cpp
/**
 * Application block reward (après émission PIVX)
 */
void ApplyBlockReward(KhuGlobalState& state, int64_t reward_year) {
    // 1. Émission PIVX distribuée (staker, MN, DAO)
    //    (voir blueprint 01-PIVX-INFLATION-DIMINUTION)

    // 2. Injection dans pool reward KHU
    //    Une portion de l'émission est allouée au pool
    int64_t khu_pool_injection = CalculateKHUPoolInjection(reward_year);

    state.Cr += khu_pool_injection;  // Atomique
    state.Ur += khu_pool_injection;  // Atomique

    // Invariant : Cr == Ur préservé
}
```

**Source du pool :**
```
Bloc N miné :
  reward_year = 6 PIV (année 0)

  Distribution :
  • Staker : 6 PIV
  • MN :     6 PIV
  • DAO :    6 PIV

  MAIS AUSSI :
  • Pool KHU (Cr/Ur) : X PIV → convertis en KHU

  (X dépend de la politique d'injection - TBD)
```

### 2.3 Comment le Pool Finance le Yield ?

**Chaque jour, le yield est distribué depuis le pool.**

```
Jour 0 :
  Cr = 10,000 KHU
  Ur = 10,000 KHU

  Notes ZKHU stakées : 100,000 KHU total
  R% = 5% annuel

Jour 1 (1440 blocs plus tard) :
  Daily yield = (100,000 × 500) / 10000 / 365
              = 13.7 KHU

  Cr = 10,013.7 KHU  (ajout yield)
  Ur = 10,013.7 KHU  (ajout yield)

  → Pool AUGMENTE chaque jour
  → Notes ZKHU accumulent Ur

Quand Alice UNSTAKE :
  Alice avait 1000 KHU staké
  Ur_accumulated = 50 KHU (1 an)

  state.Cr -= 50  (consommation pool)
  state.Ur -= 50  (consommation droits)
  state.C  += 50  (création KHU pour Alice)
  state.U  += 50  (création KHU pour Alice)

  Alice reçoit : 1000 + 50 = 1050 KHU_T
```

**Le pool Cr/Ur est un buffer entre émission et distribution yield.**

---

## 3. CALCUL DU YIELD

### 3.1 Formule Canonique (Daily Yield)

```cpp
/**
 * Calcul yield quotidien pour une note ZKHU
 *
 * @param stake_amount Montant staké (principal)
 * @param R_annual Taux annuel en basis points (ex: 500 = 5%)
 * @return Yield quotidien en satoshis
 */
int64_t CalculateDailyYield(int64_t stake_amount, uint16_t R_annual) {
    // Yield annuel = amount × R% / 100
    int64_t annual_yield = (stake_amount * R_annual) / 10000;

    // Yield quotidien = annuel / 365
    int64_t daily_yield = annual_yield / 365;

    return daily_yield;
}
```

**Exemple :**
```cpp
stake_amount = 1000 * COIN;  // 1000 KHU
R_annual = 500;              // 5.00%

annual_yield = (1000 * COIN × 500) / 10000
             = 50 * COIN  // 50 KHU/an

daily_yield = 50 * COIN / 365
            = 0.1369863 * COIN  // ~0.137 KHU/jour
```

### 3.2 Yield Linéaire (Pas de Compounding)

**RÈGLE ABSOLUE : Le yield est LINÉAIRE uniquement.**

```cpp
// ✅ CORRECT : Yield linéaire
daily = (principal × R_annual) / 10000 / 365;

// ❌ INTERDIT : Compounding
daily = ((principal + accumulated_yield) × R_annual) / 10000 / 365;
```

**Exemple compounding interdit :**
```
Jour 0 : principal = 1000 KHU
Jour 1 : yield = 0.137 KHU
         accumulated = 0.137 KHU

Jour 2 (si compounding - INTERDIT) :
  daily = ((1000 + 0.137) × 500) / 10000 / 365
        = 0.137019 KHU  ❌ PAS ÇA !

Jour 2 (linéaire - CORRECT) :
  daily = (1000 × 500) / 10000 / 365
        = 0.137 KHU  ✅ TOUJOURS pareil
```

**Raison :** Yield linéaire = prévisible, auditable, pas d'explosion exponentielle.

### 3.3 Maturity (4320 Blocs = 3 Jours)

**Le yield ne commence QU'APRÈS maturity.**

```cpp
const uint32_t MATURITY_BLOCKS = 4320;  // 3 jours (1440 blocs/jour × 3)

/**
 * Vérifier si note est mature
 */
bool IsNoteMature(uint32_t stake_start_height, uint32_t current_height) {
    return (current_height - stake_start_height) >= MATURITY_BLOCKS;
}
```

**Timeline yield :**
```
Bloc 1000 : Alice stake 1000 KHU → ZKHU
Bloc 1001-5319 : Pas de yield (< 4320 blocs)
Bloc 5320 : MATURE ✅
  → Yield commence
  → Daily yield = 0.137 KHU/jour

Bloc 6760 (1440 blocs = 1 jour après maturity) :
  → Premier yield appliqué
  → note.Ur_accumulated += 0.137 KHU

Bloc 8200 (2 jours après maturity) :
  → note.Ur_accumulated += 0.137 KHU
  → Total = 0.274 KHU

...

Bloc 370320 (365 jours après stake) :
  → note.Ur_accumulated ≈ 50 KHU
```

**Raison maturity :** Éviter gaming (stake/unstake rapide pour yield).

---

## 4. APPLICATION DU YIELD (ConnectBlock)

### 4.1 Ordre Canonique ConnectBlock

```cpp
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex) {
    // 1. Charger état KHU précédent
    KhuGlobalState khuState;
    LoadKhuState(pindex->pprev->nHeight, khuState);

    // 2. ⚠️ CRITICAL : Appliquer yield AVANT transactions
    if ((pindex->nHeight - khuState.last_yield_update_height) >= 1440) {
        ApplyDailyYield(khuState, pindex->nHeight);
    }

    // 3. Traiter transactions KHU (MINT, REDEEM, STAKE, UNSTAKE)
    for (const auto& tx : block.vtx) {
        ApplyKHUTransaction(tx, khuState, state);
    }

    // 4. Appliquer block reward (émission PIVX)
    ApplyBlockReward(block, khuState);

    // 5. Vérifier invariants
    if (!khuState.CheckInvariants())
        return state.Invalid("khu-invariant-violation");

    // 6. Sauvegarder nouvel état
    SaveKhuState(pindex->nHeight, khuState);

    return true;
}
```

**ORDRE CRITIQUE :** Yield AVANT transactions (voir blueprint 02-canonical-specification section 11.1).

### 4.2 ApplyDailyYield Implementation

```cpp
/**
 * Appliquer yield quotidien à toutes les notes ZKHU matures
 */
bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight) {
    // ⚠️ LOCK: cs_khu MUST be held
    AssertLockHeld(cs_khu);

    int64_t total_daily_yield = 0;

    // Itérer sur toutes les notes ZKHU actives
    for (const auto& [nullifier, noteData] : activeZKHUNotes) {
        // Vérifier maturity
        if (!IsNoteMature(noteData.stakeStartHeight, nHeight))
            continue;  // Skip immature notes

        // Calculer daily yield pour cette note
        int64_t daily = CalculateDailyYield(noteData.amount, state.R_annual);

        // Accumuler dans note
        noteData.Ur_accumulated += daily;

        // Accumuler total
        total_daily_yield += daily;
    }

    // Mettre à jour pool global (Cr/Ur)
    state.Cr += total_daily_yield;  // Atomique
    state.Ur += total_daily_yield;  // Atomique

    // Mettre à jour hauteur dernière application
    state.last_yield_update_height = nHeight;

    // Vérifier invariant
    if (!state.CheckInvariants())
        return error("ApplyDailyYield: invariant violation");

    LogPrint(BCLog::KHU, "ApplyDailyYield: height=%d total=%d Cr=%d Ur=%d\n",
             nHeight, total_daily_yield, state.Cr, state.Ur);

    return true;
}
```

### 4.3 Tracking Notes ZKHU

```cpp
/**
 * Map des notes ZKHU actives (en mémoire)
 */
std::map<uint256, ZKHUNoteData> activeZKHUNotes;

/**
 * Quand note est stakée (STAKE transaction)
 */
void OnZKHUStake(const uint256& nullifier, const ZKHUNoteData& noteData) {
    activeZKHUNotes[nullifier] = noteData;
    // note.Ur_accumulated = 0 initialement
}

/**
 * Quand note est unstakée (UNSTAKE transaction)
 */
void OnZKHUUnstake(const uint256& nullifier) {
    activeZKHUNotes.erase(nullifier);
    // note.Ur_accumulated utilisé pour bonus
}
```

---

## 5. GOUVERNANCE R% (DOMC)

### 5.1 R_MAX_dynamic (Bornes Décroissantes)

**R% est borné par R_MAX_dynamic qui décroît avec le temps.**

```cpp
/**
 * Calculer R_MAX_dynamic pour une hauteur donnée
 *
 * Année 0-25 : Décroît de 30% à 4% (-1%/an)
 * Année 26+ : Plancher 4% (jamais en dessous)
 */
uint16_t GetRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight) {
    uint32_t year = (nHeight - nActivationHeight) / BLOCKS_PER_YEAR;

    // Formule : max(400, 3000 - year × 100)
    uint16_t r_max = std::max(400, 3000 - year * 100);

    return r_max;
}
```

**Schedule R_MAX_dynamic :**

| Année | R_MAX_dynamic | Pourcentage |
|-------|---------------|-------------|
| 0     | 3000 bp       | 30.00%      |
| 1     | 2900 bp       | 29.00%      |
| 2     | 2800 bp       | 28.00%      |
| ...   | ...           | ...         |
| 25    | 500 bp        | 5.00%       |
| 26    | 400 bp        | 4.00%       |
| 27+   | 400 bp        | 4.00%       |

**Graphique :**
```
R_MAX_dynamic (%)
    │
 30%│██████████████  Année 0
    │
 20%│█████████      Année 10
    │
 10%│████           Année 20
    │
  4%│█              Année 26+ (plancher)
    └────────────────────────────────> temps
```

### 5.2 Vote R% via Masternode Ping (Infrastructure DAO PIVX)

**Masternodes votent R% en étendant le mécanisme de ping existant.**

**ARCHITECTURE: Réutilisation Infrastructure DAO PIVX**

PIVX possède déjà un système de governance robuste (actif depuis 2015):
- Masternodes pingent le réseau régulièrement (toutes les ~15 minutes)
- Chaque ping est signé cryptographiquement
- Les pings sont relayés sur tout le réseau P2P
- Infrastructure éprouvée et mature

**MODIFICATION MINIMALE: Extension Ping avec R%**

On ajoute simplement un champ `nRProposal` au ping masternode existant:

```cpp
/**
 * Extension du Masternode Ping existant (masternode.h)
 */
class CMasternodePing {
    CTxIn vin;                  // Masternode identifier (existant)
    uint256 blockHash;          // Block hash (existant)
    int64_t sigTime;            // Signature timestamp (existant)
    std::vector<unsigned char> vchSig;  // Signature (existant)

    // ← NOUVEAU: R% préféré du masternode
    uint16_t nRProposal;        // R% en centièmes (0-9999 = 0.00%-99.99%)
                                // 0 = pas d'opinion / abstention
                                // Ex: 2555 = 25.55%

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);

        // Rétrocompatibilité: seulement si version >= PROTOCOL_VERSION_KHU
        if (s.GetVersion() >= PROTOCOL_VERSION_KHU)
            READWRITE(nRProposal);
    }
};
```

**SPÉCIFICATIONS R% :**
- **Vote public** (transparent) : Pas de commit-reveal nécessaire
- **Format** : XX.XX% (2 decimals) — Ex: 25.55%, 20.20%
- **Agrégation** : Moyenne arithmétique de tous les votes valides
- **Durée application** : 30 jours (cycle budget DAO = 43200 blocs mainnet)
- **Mise à jour** : Temps réel (MN peut changer vote à tout moment)
- **Coût** : GRATUIT (utilise ping existant)

**Processus vote (Simple, Temps Réel) :**

```
┌──────────────────────────────────────────────────────┐
│ VOTE CONTINU (Temps réel via pings masternodes)     │
├──────────────────────────────────────────────────────┤
│ 1. Masternode choisit R_proposal (ex: 2555 = 25.55%)│
│ 2. RPC: masternode votekhu 25.55                    │
│ 3. Stockage local: activeMasternode.nRProposal = 2555│
│ 4. Prochain ping inclut automatiquement nRProposal │
│ 5. Ping broadcast réseau (comme d'habitude)        │
│ 6. Tous les nodes collectent votes via pings       │
│ 7. Calcul consensus temps réel (moyenne arith.)    │
│                                                      │
│ ✅ SIMPLE: Pas de commitment, pas de reveal        │
│ ✅ GRATUIT: Aucun collateral requis                │
│ ✅ TEMPS RÉEL: MN peut changer vote instantanément │
└──────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────┐
│ ACTIVATION (Cycle Budget DAO = 30 jours)            │
├──────────────────────────────────────────────────────┤
│ • Tous les 43200 blocs (mainnet) = 30 jours         │
│ • Au superblock budget DAO:                         │
│   1. Collecter tous pings masternodes actifs       │
│   2. Filtrer MN avec nRProposal > 0                 │
│   3. Calculer moyenne: R_new = Σ(nRProposal) / count│
│   4. Si R_new > R_MAX_dynamic: clamp à R_MAX       │
│   5. Activer: SetKhuAnnualRate(R_new)              │
│   6. Application: 30 prochains jours                │
│                                                      │
│ ✅ DÉTERMINISTE: Hauteur de bloc uniquement        │
│ ✅ CONSENSUS: Tous nodes calculent même résultat   │
└──────────────────────────────────────────────────────┘
```

**Timeline complète (1 cycle = 30 jours) :**

```
Bloc 0          │◄────────────── Vote continu (30 jours) ──────────────►│
                │                                                        │
                │  MN votent à tout moment (temps réel)                  │
                │  Consensus calculé en continu                          │
                │                                                        │
Bloc 43200      │ ACTIVATION (superblock DAO)                           │
(Mainnet)       │   → Calcul moyenne finale                             │
                │   → R_annual = moyenne                                │
                │                                                        │
                │◄─────── R% appliqué (30 prochains jours) ────────────►│
```

### 5.3 Implémentation C++ — Infrastructure Simple

**Fichiers à modifier:**
- `src/masternode/masternode.h` — Extension CMasternodePing
- `src/rpc/masternode.cpp` — Nouveau RPC `masternode votekhu`
- `src/masternode/masternodemanager.cpp` — Calcul consensus
- `src/validation.cpp` — Activation au superblock

#### 5.3.1 RPC Vote Masternode

```cpp
/**
 * RPC: masternode votekhu <R_percent>
 * Fichier: src/rpc/masternode.cpp
 */
UniValue masternode_votekhu(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "masternode votekhu <R_percent>\n"
            "\nVote for KHU annual yield rate (masternode only).\n"
            "\nArguments:\n"
            "1. R_percent    (numeric, required) Annual yield rate (0.00-99.99)\n"
            "                Example: 25.55 for 25.55%\n"
            "\nResult:\n"
            "\"success\"      Vote preference saved, will broadcast in next ping\n"
            "\nExamples:\n"
            + HelpExampleCli("masternode votekhu", "25.55")
            + HelpExampleRpc("masternode", "\"votekhu\", 25.55")
        );

    // Vérifier que c'est un masternode actif
    if (!fMasterNode)
        throw JSONRPCError(RPC_MISC_ERROR, "This is not a masternode");

    // Parser R% (format XX.XX)
    double R_percent = request.params[1].get_real();

    // Convertir en centièmes
    uint16_t R_centimes = static_cast<uint16_t>(R_percent * 100.0);

    // Valider bornes
    if (R_centimes > 9999)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          "R% must be between 0.00 and 99.99");

    // Vérifier R_MAX_dynamic
    uint16_t R_MAX = GetKhuRMax();
    if (R_centimes > R_MAX)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
                          strprintf("R% exceeds current R_MAX (%.2f%%)",
                                   R_MAX / 100.0));

    // Stocker préférence localement
    activeMasternode.nRProposal = R_centimes;

    LogPrintf("Masternode R% vote: %.2f%% (will broadcast in next ping)\n",
              R_percent);

    return "success";
}
```

#### 5.3.2 Calcul Consensus Réseau

```cpp
/**
 * Calculer consensus R% depuis pings masternodes
 * Fichier: src/masternode/masternodemanager.cpp
 */
uint16_t CMasternodeMan::GetRConsensus() const
{
    LOCK(cs);

    std::vector<uint16_t> R_values;

    // Parcourir tous les masternodes
    for (const auto& mnpair : mapMasternodes) {
        const CMasternode& mn = mnpair.second;

        // Filtrer masternodes actifs avec vote R%
        if (mn.IsEnabled() && mn.lastPing.nRProposal > 0) {
            R_values.push_back(mn.lastPing.nRProposal);
        }
    }

    // Si aucun vote, garder R% actuel
    if (R_values.empty()) {
        LogPrint(BCLog::MASTERNODE,
                "GetRConsensus: No R% votes, keeping current rate\n");
        return GetCurrentRAnnual();
    }

    // Calcul moyenne arithmétique
    uint64_t sum = 0;
    for (uint16_t r : R_values)
        sum += r;

    uint16_t average = static_cast<uint16_t>(sum / R_values.size());

    // Clamping à R_MAX_dynamic
    uint16_t R_MAX = GetKhuRMax();
    if (average > R_MAX) {
        LogPrintf("R% consensus %.2f%% exceeds R_MAX %.2f%%, clamping\n",
                 average / 100.0, R_MAX / 100.0);
        average = R_MAX;
    }

    LogPrintf("R% consensus: %d votes, average = %.2f%%\n",
             R_values.size(), average / 100.0);

    return average;
}
```

#### 5.3.3 RPC Query Consensus

```cpp
/**
 * RPC: getkhurconsensus
 * Fichier: src/rpc/blockchain.cpp (ou khu_rpc.cpp)
 */
UniValue getkhurconsensus(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getkhurconsensus\n"
            "\nGet current KHU R% consensus from masternode votes.\n"
            "\nResult:\n"
            "{\n"
            "  \"R_annual\": xx.xx,        (numeric) Consensus R% annual\n"
            "  \"R_centimes\": xxxx,       (numeric) R% in centimes\n"
            "  \"votes_count\": n,         (numeric) Number of MN votes\n"
            "  \"total_masternodes\": n,   (numeric) Total enabled MN\n"
            "  \"R_current\": xx.xx,       (numeric) Currently active R%\n"
            "  \"R_max\": xx.xx           (numeric) Maximum allowed R%\n"
            "}\n"
        );

    uint16_t R_consensus = mnodeman.GetRConsensus();
    uint16_t R_current = GetCurrentRAnnual();
    uint16_t R_MAX = GetKhuRMax();

    int votes_count = 0;
    int total_mn = 0;

    LOCK(mnodeman.cs);
    for (const auto& mnpair : mnodeman.mapMasternodes) {
        const CMasternode& mn = mnpair.second;
        if (mn.IsEnabled()) {
            total_mn++;
            if (mn.lastPing.nRProposal > 0)
                votes_count++;
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("R_annual", R_consensus / 100.0);
    result.pushKV("R_centimes", R_consensus);
    result.pushKV("votes_count", votes_count);
    result.pushKV("total_masternodes", total_mn);
    result.pushKV("R_current", R_current / 100.0);
    result.pushKV("R_max", R_MAX / 100.0);

    return result;
}
```

#### 5.3.4 Activation au Superblock

```cpp
/**
 * Activation R% au superblock budget (30 jours)
 * Fichier: src/validation.cpp (dans ConnectBlock)
 */
bool ConnectBlock(const CBlock& block, CValidationState& state, ...)
{
    // ... code existant ...

    // Activation KHU R% au superblock budget DAO
    if (nHeight >= Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight)
    {
        // Tous les 43200 blocs (mainnet) = 30 jours
        if (nHeight % Params().GetConsensus().nBudgetCycleBlocks == 0)
        {
            // Calculer consensus R% depuis pings masternodes
            uint16_t R_consensus = mnodeman.GetRConsensus();

            // Activer nouveau R%
            SetKhuAnnualRate(R_consensus);

            LogPrintf("KHU R% activated at height %d: %.2f%% (applied for next 30 days)\n",
                     nHeight, R_consensus / 100.0);
        }
    }

    // ... suite code existant ...
}
```

---

## 6. INTERDICTIONS ABSOLUES

### 6.1 Code Interdit

```cpp
// ❌ INTERDIT : Compounding
int64_t daily = ((principal + Ur_accumulated) × R_annual) / 10000 / 365;

// ❌ INTERDIT : Yield avant maturity
if (current_height - stake_start < MATURITY_BLOCKS) {
    ApplyYield();  // ❌ JAMAIS !
}

// ❌ INTERDIT : R% au-delà de R_MAX_dynamic
state.R_annual = 5000;  // ❌ Si R_MAX = 3000

// ❌ INTERDIT : Modifier Cr/Ur séparément
state.Cr += yield;  // Sans state.Ur += yield

// ❌ INTERDIT : Float/double pour yield
double daily_yield = principal * 0.05 / 365;  // ❌ int64_t uniquement

// ❌ INTERDIT : R% influence émission PIVX
reward_year = 6 - year + (R_annual / 100);  // ❌ Indépendance stricte
```

### 6.2 Opérations Interdites

```
❌ Yield avant 4320 blocs (maturity)
❌ Compounding (yield sur yield)
❌ R% > R_MAX_dynamic
❌ R% < 0 (négatif)
❌ Modifier Cr sans Ur (ou inverse)
❌ Yield appliqué après transactions (ordre inversé)
❌ R% modifié sans vote DOMC
❌ Yield financé par autre source que Cr/Ur
❌ Float/double (CAmount = int64_t uniquement)
```

---

## 7. EXEMPLES COMPLETS

### 7.1 Exemple 1 : Stake 1 An

```
Alice stake 1000 KHU_T → ZKHU
R% = 5.00% (500 bp)

Bloc 1000 : STAKE
  note.amount = 1000 * COIN
  note.stakeStartHeight = 1000
  note.Ur_accumulated = 0

Bloc 5320 (maturity = 4320 blocs) :
  note mature ✅

Bloc 6760 (1er jour après maturity) :
  daily = (1000 × COIN × 500) / 10000 / 365
        = 0.1369863 * COIN
  note.Ur_accumulated = 0.1369863 * COIN

Bloc 8200 (2ème jour) :
  note.Ur_accumulated += 0.1369863 * COIN
  Total = 0.2739726 * COIN

...

Bloc 370320 (365 jours après stake) :
  note.Ur_accumulated ≈ 50 * COIN

Alice UNSTAKE :
  Principal : 1000 KHU_T
  Bonus :       50 KHU_T (Ur_accumulated)
  Total :     1050 KHU_T
```

### 7.2 Exemple 2 : Vote DOMC Change R%

```
Année 0 : R% = 5.00% (500 bp)

Cycle DOMC #1 (blocs 100000-143200) :
  COMMIT phase : MN votent (commitments)
  REVEAL phase : MN révèlent (R_proposal)

  Votes :
    MN1 : 450 bp (4.5%)
    MN2 : 500 bp (5.0%)
    MN3 : 550 bp (5.5%)
    ...
    MN400 : 475 bp (4.75%)

  Médiane = 480 bp (4.8%)
  R_MAX_dynamic = 3000 bp (année 0)

  Vérification : 480 < 3000 ✅

  Nouveau R_annual = 480 bp

Bloc 143201 : R% actif = 4.80%
  Tous nouveaux yields calculés avec 4.8%
```

---

## 8. TESTS

### 8.1 Tests Unitaires : src/test/khu_yield_tests.cpp

```cpp
#include <boost/test/unit_test.hpp>
#include "khu/khu_yield.h"

BOOST_AUTO_TEST_SUITE(khu_yield_tests)

BOOST_AUTO_TEST_CASE(test_daily_yield_calculation)
{
    int64_t stake = 1000 * COIN;
    uint16_t R = 500;  // 5%

    int64_t daily = CalculateDailyYield(stake, R);

    // 1000 × 5% / 365 = 0.1369863 KHU/jour
    BOOST_CHECK_EQUAL(daily, 136986301);  // satoshis
}

BOOST_AUTO_TEST_CASE(test_no_compounding)
{
    int64_t principal = 1000 * COIN;
    int64_t accumulated = 50 * COIN;
    uint16_t R = 500;

    // Yield calculé sur principal uniquement (pas accumulated)
    int64_t daily = CalculateDailyYield(principal, R);

    // PAS : CalculateDailyYield(principal + accumulated, R)
    BOOST_CHECK_EQUAL(daily, 136986301);
}

BOOST_AUTO_TEST_CASE(test_r_max_dynamic)
{
    // Année 0 : 30%
    BOOST_CHECK_EQUAL(GetRMaxDynamic(0, 0), 3000);

    // Année 10 : 20%
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 10, 0), 2000);

    // Année 26 : 4% (plancher)
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 26, 0), 400);

    // Année 100 : 4% (plancher)
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 100, 0), 400);
}

BOOST_AUTO_TEST_CASE(test_maturity)
{
    uint32_t stake_start = 1000;

    // Bloc 4319 : immature
    BOOST_CHECK(!IsNoteMature(stake_start, 5319));

    // Bloc 5320 : mature ✅
    BOOST_CHECK(IsNoteMature(stake_start, 5320));
}

BOOST_AUTO_TEST_SUITE_END()
```

---

## 9. RÉFÉRENCES

**Blueprints liés :**
- `01-PIVX-INFLATION-DIMINUTION.md` — Séparation émission/yield
- `05-ZKHU-STAKE-UNSTAKE.md` — Application yield dans UNSTAKE

**Documents liés :**
- `02-canonical-specification.md` — Section 8 (Yield Mechanism)
- `06-protocol-reference.md` — Section 15 (Yield code C++)

---

## 10. VALIDATION FINALE

**Ce blueprint est CANONIQUE et IMMUABLE.**

**Règles fondamentales (NON-NÉGOCIABLES) :**

1. **Yield linéaire** : Pas de compounding
2. **Maturity 4320 blocs** : 3 jours minimum
3. **Invariant Cr==Ur** : Toujours respecté
4. **R% ≤ R_MAX_dynamic** : Borné dynamiquement
5. **Gouvernance DOMC** : Vote masternodes uniquement
6. **Indépendance émission** : R% ≠ reward_year

**Statut :** ACTIF pour implémentation Phase 3

---

**FIN DU BLUEPRINT YIELD R%**

**Version:** 1.0
**Date:** 2025-11-22
**Status:** CANONIQUE
