# 06 — YIELD R% BLUEPRINT

**Date:** 2025-11-27
**Version:** 2.0
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
  Yield :       50 KHU_T
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
 *
 * IMPORTANT: Cette fonction NE MODIFIE PAS Cr/Ur.
 * L'émission PIVX est distribuée (staker, MN, DAO) mais Cr/Ur restent inchangés.
 */
void ApplyBlockReward(KhuGlobalState& state, int64_t reward_year) {
    // Émission PIVX distribuée (staker, MN, DAO)
    // (voir blueprint 01-PIVX-INFLATION-DIMINUTION)

    // ✅ AXIOME: Aucune injection Cr/Ur depuis émission PIVX
    // KHUPoolInjection = 0

    // Cr et Ur NE SONT PAS modifiés ici
    // (voir section 3.5.6 pour règle canonique)
}
```

**Source du pool — AXIOME CANONIQUE :**

```
❌ FAUX: Pool Cr/Ur alimenté par émission PIVX
✅ VRAI: Pool Cr/Ur = système FERMÉ (endogène)

Cr et Ur évoluent EXCLUSIVEMENT via:

1. YIELD quotidien (ApplyDailyYield):
   Cr += Δ
   Ur += Δ
   où Δ = (stake_total × R_annual / 10000) / 365

2. UNSTAKE (ProcessUNSTAKE):
   Z  -= P       (principal retiré du shielded)
   U  += P + Y   (principal + yield vers transparent)
   C  += Y       (yield ajoute au collateral)
   Cr -= Y       (yield consommé du pool)
   Ur -= Y       (yield consommé des droits)
   où P = principal, Y = note.Ur_accumulated (0 si immature)

AUCUNE autre source (ni émission PIV, ni fees, ni injection externe).

KHUPoolInjection = 0  (axiome immuable)
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

Quand Alice UNSTAKE (après maturity de 4320 blocs = 3 jours) :
  Alice avait P = 1000 KHU staké
  Y = Ur_accumulated = 50 KHU (1 an de yield)

  state.Z  -= 1000 (principal retiré du shielded)
  state.U  += 1050 (principal + yield vers transparent)
  state.C  += 50   (yield = inflation contrôlée)
  state.Cr -= 50   (yield consommé du pool)
  state.Ur -= 50   (yield consommé des droits)

  Alice reçoit : 1000 KHU_T (principal P) + 50 KHU_T (yield Y)
               = 1050 KHU_T total

MATURITY: AUCUN yield si note < 4320 blocs (3 jours)
```

**Cr/Ur = système de création monétaire différée (deferred minting).**

Les KHU_T de yield sont **créés lors de l'UNSTAKE**, pas lors du yield quotidien.
Le yield quotidien augmente seulement les compteurs Cr/Ur (promesses virtuelles).
L'UNSTAKE matérialise ces promesses en KHU_T réels (inflation contrôlée).

### 2.4 Axiome Injection Pool — RÈGLE CANONIQUE IMMUABLE

**Il n'existe AUCUNE injection externe ou interne vers Cr ou Ur.**

```cpp
// ✅ AXIOME CANONIQUE (IMMUABLE)
const int64_t KHUPoolInjection = 0;

// ❌ INTERDIT: Toute source externe vers Cr/Ur
// • Émission PIVX (reward_year) → N'alimente PAS Cr/Ur
// • Fees PIV burned → N'alimentent PAS Cr/Ur
// • Masternode rewards → N'alimentent PAS Cr/Ur
// • DAO budget → N'alimente PAS Cr/Ur
// • Inflation externe → INTERDITE

// ✅ SEULES opérations légales sur Cr/Ur:

// 1. YIELD quotidien (ApplyDailyYield)
Cr += Δ;  // Δ = (stake_total × R_annual / 10000) / 365
Ur += Δ;

// 2. UNSTAKE (ProcessUNSTAKE)
Cr -= B;  // B = note.Ur_accumulated
Ur -= B;
```

**Conséquence architecturale:**

Le système Cr/Ur est **complètement fermé**, **endogène**, et **auto-déterminé**.

- Cr/Ur commencent à 0 à l'activation (nActivationHeight)
- Cr/Ur croissent via YIELD quotidien (création promesses)
- Cr/Ur décroissent via UNSTAKE (matérialisation promesses)
- Cr/Ur oscillent selon comportement stakers (stake vs unstake)

**Toute mutation Cr/Ur autre que YIELD/UNSTAKE doit provoquer un rejet de bloc.**

```cpp
// Validation ConnectBlock
if (state_new.Cr != state_old.Cr + Δ_yield - Σ_unstake_yields) {
    return state.Invalid(REJECT_INVALID, "khu-invalid-cr-mutation");
}
if (state_new.Ur != state_old.Ur + Δ_yield - Σ_unstake_yields) {
    return state.Invalid(REJECT_INVALID, "khu-invalid-ur-mutation");
}
```

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

**ORDRE CRITIQUE :** Yield AVANT transactions (voir blueprint SPEC.md section 11.1).

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
    // note.Ur_accumulated utilisé pour yield (Y)
}
```

---

## 5. GOUVERNANCE R% (DOMC)

### 5.1 Principe de Gouvernance R%

```
┌─────────────────────────────────────────────────────────────────┐
│  R%     = Taux de yield, VOTÉ par les masternodes               │
│  R_MAX  = Plafond du vote, DÉCROÎT AUTOMATIQUEMENT (-1%/an)    │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  1. Les MN votent pour R% entre 0% et R_MAX                    │
│  2. Après le vote → R% MIS À JOUR AUTOMATIQUEMENT              │
│  3. R_MAX décroît: 40% → 7% sur 33 ans (plancher)              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

| Élément | Qui décide? | Comment? |
|---------|-------------|----------|
| **R%** (taux réel) | Masternodes | Vote tous les 4 mois |
| **R_MAX** (plafond) | Protocole | Décroît automatiquement |
| **Mise à jour R%** | Automatique | Appliqué à la fin du cycle |
```

### 5.2 R_MAX_dynamic (Plafond Décroissant)

**R_MAX = plafond du vote qui décroît automatiquement avec le temps.**

```cpp
/**
 * Calculer R_MAX_dynamic pour une hauteur donnée
 *
 * Année 0-33 : Décroît de 40% à 7% (-1%/an)
 * Année 34+ : Plancher 7% (jamais en dessous)
 */
uint16_t GetRMaxDynamic(uint32_t nHeight, uint32_t nActivationHeight) {
    uint32_t year = (nHeight - nActivationHeight) / BLOCKS_PER_YEAR;

    // Formule : max(700, 4000 - year × 100)
    uint16_t r_max = std::max(700, 4000 - year * 100);

    return r_max;
}
```

**Schedule R_MAX_dynamic :**

| Année | R_MAX_dynamic | Pourcentage |
|-------|---------------|-------------|
| 0     | 4000 bp       | 40.00%      |
| 10    | 3000 bp       | 30.00%      |
| 20    | 2000 bp       | 20.00%      |
| 33    | 700 bp        | 7.00%       |
| 34+   | 700 bp        | 7.00%       |

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

### 5.2 Vote R% via Commit-Reveal (Calendrier Fixe, Privacy Voting)

**Masternodes votent R% avec privacy (commit-reveal) + dates fixes + préavis 2 semaines.**

**ARCHITECTURE: Extension Ping MN + Commit-Reveal + Auto-Proposal**

Design optimal combinant:
- ✅ **Privacy**: Votes cachés pendant commit (2 semaines)
- ✅ **Dates fixes**: Calendrier prévisible (blocs fixes)
- ✅ **Garantie 4 mois**: R% verrouillé après activation
- ✅ **Préavis LP**: R_next visible 2 semaines avant activation
- ✅ **Simple**: Extension ping MN + validation automatique

**CYCLE COMPLET: 172800 blocs (4 mois exacts)**

**IMPORTANT:** Toutes les dates sont calculées depuis **nActivationHeight** (fork V6 PIVX)

```cpp
/**
 * Extension CMasternodePing avec commit-reveal
 * Fichier: src/masternode/masternode.h
 */
class CMasternodePing {
    CTxIn vin;
    uint256 blockHash;
    int64_t sigTime;
    std::vector<unsigned char> vchSig;

    // ← PHASE COMMIT: Vote caché
    uint256 nRCommitment;       // SHA256(R_proposal || secret)
                                // Non-zero = commitment actif

    // ← PHASE REVEAL: Dévoilement au bloc fixe
    uint16_t nRProposal;        // R% proposé (0-9999 = 0.00%-99.99%)
    uint256 nRSecret;           // Secret 256-bit (révélé)

    ADD_SERIALIZE_METHODS;
    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action) {
        READWRITE(vin);
        READWRITE(blockHash);
        READWRITE(sigTime);
        READWRITE(vchSig);

        if (s.GetVersion() >= PROTOCOL_VERSION_KHU) {
            READWRITE(nRCommitment);
            READWRITE(nRProposal);
            READWRITE(nRSecret);
        }
    }

    /**
     * Valider reveal (au bloc deadline)
     */
    bool ValidateReveal() const {
        if (nRCommitment.IsNull()) return false;

        // Vérifier: SHA256(R_proposal || secret) == commitment
        CHashWriter ss(SER_GETHASH, 0);
        ss << nRProposal << nRSecret;
        return (ss.GetHash() == nRCommitment);
    }
};
```

**SPÉCIFICATIONS R% :**
- **Vote caché** : Commitment SHA256 (invisible pendant 2 semaines)
- **Format** : XX.XX% (2 decimals) — Ex: 25.55%, 20.20%
- **Agrégation** : Moyenne arithmétique (reveals valides uniquement)
- **Durée application** : **4 MOIS COMPLETS** (R% TOUJOURS ACTIF)
- **Cycle total** : 172800 blocs (4 mois exacts)
- **Gouvernance parallèle** : Dernier mois (commit + préavis EN PARALLÈLE avec R% actif)

**TIMELINE COMPLÈTE (Cycle 4 mois) :**

```
┌─────────────────────────────────────────────────────────────────┐
│ ⚠️ IMPORTANT: R% ACTIF PENDANT LES 4 MOIS COMPLETS              │
│ Les processus de gouvernance se déroulent EN PARALLÈLE          │
│ REVEAL = INSTANTANÉ au bloc 152640                              │
└─────────────────────────────────────────────────────────────────┘

R% ACTIF PENDANT 4 MOIS COMPLETS (jamais interrompu)

0────────────132480────────152640────────172800
│              │              │              │
│              │    VOTE      │  ADAPTATION  │
│              │  (2 sem)     │   (2 sem)    │
│              │   commits    │              │
│              │   secrets    │  REVEAL      │
│              │              │  instantané  │
│              │              │  ↓           │
│              │              │  Futur R%    │
│              │              │  visible     │
│                                            │
├────────────────────────────────────────────┤
│     R% ACTIF PENDANT TOUT LE CYCLE         │
│              (4 mois)                      │
└────────────────────────────────────────────┴─────►
                                             │
                                   Nouveau R% activé

┌─────────────────────────────────────────────────────────────────┐
│ PHASE 1 : R% ACTIF UNIQUEMENT — 3 mois + 2 jours               │
│           Position: 0 → 132480                                  │
├─────────────────────────────────────────────────────────────────┤
│ • R% ACTIF (yield distribué chaque jour)                       │
│ • AUCUNE gouvernance (période stable)                          │
│ • LP planifient avec certitude totale                          │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ PHASE 2 : VOTE (2 SEMAINES) — R% TOUJOURS ACTIF                │
│           Position: 132480 → 152640                             │
├─────────────────────────────────────────────────────────────────┤
│ ✅ R% CONTINUE d'être distribué (yield quotidien)              │
│ 🔄 EN PARALLÈLE: MN soumettent commits secrets                 │
│                                                                 │
│ Processus:                                                     │
│ 1. MN choisit R_proposal (ex: 2250 = 22.50%)                   │
│ 2. MN génère secret aléatoire (32 bytes)                       │
│ 3. MN calcule commitment = SHA256(R_proposal || secret)        │
│ 4. MN broadcast commitment via ping                            │
│                                                                 │
│ 🔒 VOTES CACHÉS (commitment SHA256 uniquement)                 │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ BLOC 152640 : REVEAL INSTANTANÉ                                │
├─────────────────────────────────────────────────────────────────┤
│ ⚡ INSTANTANÉ: Tous les votes sont dévoilés                    │
│                                                                 │
│   1. Validation: SHA256(R || secret) == commitment             │
│   2. Consensus: R_next = moyenne(reveals_valides)              │
│   3. R_next VISIBLE immédiatement                              │
│   4. R% actuel TOUJOURS ACTIF                                  │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ PHASE 3 : ADAPTATION (2 SEMAINES) — R% TOUJOURS ACTIF          │
│           Position: 152640 → 172800                             │
├─────────────────────────────────────────────────────────────────┤
│ ✅ R% actuel CONTINUE d'être distribué (jusqu'à la fin)        │
│ 👁️ Futur R% VISIBLE (tout le monde peut s'adapter)             │
│                                                                 │
│ • LP voient R_next 2 SEMAINES AVANT activation                 │
│ • Adaptation stratégies / rééquilibrage pools                  │
│ • Calendrier prévisible (bloc activation connu)                │
└─────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────┐
│ BLOC 172800 : NOUVEAU R% ACTIVÉ                                │
├─────────────────────────────────────────────────────────────────┤
│ • Nouveau R% ACTIVÉ (remplace l'ancien)                        │
│ • Nouveau cycle commence                                       │
│ • R% actif pour 4 MOIS COMPLETS                                │
│ • Prochain vote dans 132480 blocs (3 mois + 2 jours)           │
└─────────────────────────────────────────────────────────────────┘

FORMULE UNIVERSELLE:
Position dans cycle = (nHeight - nActivationHeight) % 172800
Cycle actuel = (nHeight - nActivationHeight) / 172800
Reveal height = nActivationHeight + (cycle × 172800) + 152640
Activation height = nActivationHeight + ((cycle+1) × 172800)
```

### 5.3 Implémentation C++ — Commit-Reveal + Auto-Proposal

**Fichiers à modifier:**
- `src/masternode/masternode.h` — Extension CMasternodePing (commit/reveal)
- `src/rpc/masternode.cpp` — RPC `masternode commitkhu`
- `src/masternode/masternodemanager.cpp` — Validation reveal + consensus
- `src/budget/budgetmanager.cpp` — Création auto-proposal
- `src/validation.cpp` — Réveal bloc fixe + activation
- `src/consensus/params.h` — Constantes cycle

**CONSTANTES CYCLE (consensus/params.h):**

```cpp
/**
 * Constantes cycle DOMC R%
 * CRITIQUE: R% actif pendant 172800 blocs COMPLETS
 *           Gouvernance = processus parallèle (dernier mois)
 */
const int KHU_R_CYCLE_BLOCKS = 172800;      // 4 mois exacts (R% actif complet)
const int KHU_R_PURE_BLOCKS  = 132480;      // 3 mois + 2 jours (R% seul)
const int KHU_R_COMMIT_BLOCKS = 20160;      // 2 semaines commit (parallèle)
const int KHU_R_NOTICE_BLOCKS = 20160;      // 2 semaines préavis (parallèle)

/**
 * IMPORTANT: Toutes les fonctions utilisent nActivationHeight comme référence
 *            nActivationHeight = fork V6 PIVX (tous nodes doivent upgrade)
 */

/**
 * Calculer position dans cycle actuel
 * @return Position [0..172799] relative au début du cycle
 */
int GetKHUCyclePosition(int nHeight, int nActivationHeight) {
    if (nHeight < nActivationHeight) return -1;
    return (nHeight - nActivationHeight) % KHU_R_CYCLE_BLOCKS;
}

/**
 * Calculer numéro cycle actuel
 * @return Cycle 0 = premier cycle après fork V6
 */
int GetKHUCycleNumber(int nHeight, int nActivationHeight) {
    if (nHeight < nActivationHeight) return -1;
    return (nHeight - nActivationHeight) / KHU_R_CYCLE_BLOCKS;
}

/**
 * Vérifier période commit (votes cachés)
 * Pendant commit: R% ACTUEL reste actif (processus parallèle)
 */
bool IsKHUCommitPeriod(int nHeight, int nActivationHeight) {
    int pos = GetKHUCyclePosition(nHeight, nActivationHeight);
    if (pos < 0) return false;
    return (pos >= KHU_R_PURE_BLOCKS &&
            pos < KHU_R_PURE_BLOCKS + KHU_R_COMMIT_BLOCKS);
}

/**
 * Calculer hauteur reveal (bloc fixe) pour cycle actuel
 * Formula: nActivationHeight + (cycle × 172800) + 152640
 */
int GetKHURevealHeight(int nHeight, int nActivationHeight) {
    if (nHeight < nActivationHeight) return -1;
    int cycle = GetKHUCycleNumber(nHeight, nActivationHeight);
    return nActivationHeight + (cycle * KHU_R_CYCLE_BLOCKS) +
           KHU_R_PURE_BLOCKS + KHU_R_COMMIT_BLOCKS;
}

/**
 * Calculer hauteur activation PROCHAIN R%
 * Formula: nActivationHeight + ((cycle+1) × 172800)
 */
int GetKHUActivationHeight(int nHeight, int nActivationHeight) {
    if (nHeight < nActivationHeight) return -1;
    int cycle = GetKHUCycleNumber(nHeight, nActivationHeight);
    return nActivationHeight + ((cycle + 1) * KHU_R_CYCLE_BLOCKS);
}
```

#### 5.3.1 RPC Commit (Phase 2 — Vote Caché)

```cpp
/**
 * RPC: masternode commitkhu <R_percent>
 * Fichier: src/rpc/masternode.cpp
 *
 * PHASE COMMIT uniquement (2 semaines)
 * Crée commitment SHA256, broadcast dans prochain ping
 */
UniValue masternode_commitkhu(const JSONRPCRequest& request)
{
    if (request.fHelp || request.params.size() != 2)
        throw std::runtime_error(
            "masternode commitkhu <R_percent>\n"
            "\nCommit vote for KHU annual yield rate (masternode only).\n"
            "Vote is HIDDEN via SHA256 commitment during 2-week commit period.\n"
            "\nArguments:\n"
            "1. R_percent    (numeric, required) Annual yield rate (0.00-99.99)\n"
            "                Example: 25.55 for 25.55%\n"
            "\nResult:\n"
            "{\n"
            "  \"status\": \"committed\",\n"
            "  \"R_proposal\": 2555,\n"
            "  \"commitment\": \"a3f5...\",\n"
            "  \"reveal_height\": 195360\n"
            "}\n"
            "\nExamples:\n"
            + HelpExampleCli("masternode commitkhu", "25.55")
            + HelpExampleRpc("masternode", "\"commitkhu\", 25.55")
        );

    // Vérifier période commit
    int nHeight = chainActive.Height();
    int nActivationHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight;

    if (!IsKHUCommitPeriod(nHeight, nActivationHeight))
        throw JSONRPCError(RPC_MISC_ERROR,
            "Not in commit period (must be during 2-week commit phase)");

    // Vérifier masternode actif
    if (!fMasterNode || !activeMasternode.vin.prevout.IsNull())
        throw JSONRPCError(RPC_MISC_ERROR, "This is not an active masternode");

    // Parser R% (format XX.XX)
    double R_percent = request.params[1].get_real();
    uint16_t R_proposal = static_cast<uint16_t>(R_percent * 100.0);

    // Valider bornes
    if (R_proposal > 9999)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            "R% must be between 0.00 and 99.99");

    // Vérifier R_MAX_dynamic
    uint16_t R_MAX = GetRMaxDynamic(nHeight, nActivationHeight);
    if (R_proposal > R_MAX)
        throw JSONRPCError(RPC_INVALID_PARAMETER,
            strprintf("R% exceeds current R_MAX (%.2f%%)", R_MAX / 100.0));

    // Générer secret aléatoire (256 bits)
    uint256 secret = GetRandHash();

    // Calculer commitment = SHA256(R_proposal || secret)
    CHashWriter ss(SER_GETHASH, 0);
    ss << R_proposal << secret;
    uint256 commitment = ss.GetHash();

    // Stocker localement pour reveal ultérieur
    activeMasternode.nRProposal = R_proposal;
    activeMasternode.nRSecret = secret;
    activeMasternode.nRCommitment = commitment;

    LogPrintf("KHU R% commitment created: %.2f%% (commitment=%s)\n",
              R_percent, commitment.GetHex().substr(0, 8));

    // Broadcast dans prochain ping (automatique)

    int revealHeight = GetKHURevealHeight(nHeight, nActivationHeight);

    UniValue result(UniValue::VOBJ);
    result.pushKV("status", "committed");
    result.pushKV("R_proposal", R_proposal);
    result.pushKV("commitment", commitment.GetHex().substr(0, 16) + "...");
    result.pushKV("reveal_height", revealHeight);

    return result;
}
```

#### 5.3.2 Reveal Automatique (Phase 3 — Bloc 195360 Fixe)

```cpp
/**
 * Validation reveals au bloc deadline (195360)
 * Fichier: src/validation.cpp (dans ConnectBlock)
 */
bool ProcessKHUReveal(int nHeight, int nActivationHeight)
{
    // Vérifier si c'est le bloc reveal
    int revealHeight = GetKHURevealHeight(nHeight, nActivationHeight);
    if (nHeight != revealHeight)
        return true;  // Pas le bloc reveal

    LogPrintf("KHU REVEAL HEIGHT %d: Processing masternode reveals...\n", nHeight);

    std::vector<uint16_t> valid_reveals;

    LOCK(mnodeman.cs);
    for (const auto& mnpair : mnodeman.mapMasternodes) {
        const CMasternode& mn = mnpair.second;

        if (!mn.IsEnabled())
            continue;

        const CMasternodePing& ping = mn.lastPing;

        // Skip si pas de commitment
        if (ping.nRCommitment.IsNull())
            continue;

        // Valider reveal: SHA256(R_proposal || secret) == commitment
        if (!ping.ValidateReveal()) {
            LogPrint(BCLog::MASTERNODE,
                "KHU Reveal INVALID for MN %s (commitment mismatch)\n",
                mn.vin.prevout.ToStringShort());
            continue;  // Rejeté
        }

        // Vérifier R_MAX_dynamic
        uint16_t R_MAX = GetRMaxDynamic(nHeight, nActivationHeight);
        if (ping.nRProposal > R_MAX) {
            LogPrint(BCLog::MASTERNODE,
                "KHU Reveal rejected for MN %s (R=%.2f%% > R_MAX=%.2f%%)\n",
                mn.vin.prevout.ToStringShort(),
                ping.nRProposal / 100.0,
                R_MAX / 100.0);
            continue;  // Rejeté
        }

        // ✅ Reveal valide
        valid_reveals.push_back(ping.nRProposal);

        LogPrint(BCLog::MASTERNODE,
            "KHU Reveal VALID for MN %s: R=%.2f%%\n",
            mn.vin.prevout.ToStringShort(),
            ping.nRProposal / 100.0);
    }

    // Calculer consensus (moyenne arithmétique)
    if (valid_reveals.empty()) {
        LogPrintf("KHU Reveal: No valid reveals, keeping current R%\n");
        return true;  // Pas de changement
    }

    uint64_t sum = 0;
    for (uint16_t r : valid_reveals)
        sum += r;

    uint16_t R_consensus = static_cast<uint16_t>(sum / valid_reveals.size());

    LogPrintf("KHU Reveal consensus: %d reveals, average = %.2f%%\n",
             valid_reveals.size(), R_consensus / 100.0);

    // Créer auto-proposal avec R_consensus
    if (!CreateKHUAutoProposal(R_consensus, nHeight, nActivationHeight))
        return error("ProcessKHUReveal: Failed to create auto-proposal");

    return true;
}
```

#### 5.3.3 Auto-Proposal Création (Phase 4 — Préavis 2 Semaines)

```cpp
/**
 * Créer auto-proposal budget DAO avec R_next
 * Fichier: src/budget/budgetmanager.cpp
 */
bool CreateKHUAutoProposal(uint16_t R_consensus, int nHeight, int nActivationHeight)
{
    // Calculer activation height (bloc 215520)
    int activationHeight = GetKHUActivationHeight(nHeight, nActivationHeight);

    // Nom proposal: "KHU_R_22.50_NEXT"
    std::string proposalName = strprintf("KHU_R_%.2f_NEXT", R_consensus / 100.0);

    // URL info (optionnel)
    std::string url = "https://pivx.org/khu/governance";

    // Montant = R% encodé (centièmes)
    // Ex: 2250 centièmes = 22.50 PIVX symbolique
    CAmount amount = R_consensus * COIN / 100;

    // Paiement address (burn address, pas utilisé)
    CTxDestination dest = DecodeDestination("D...");  // Burn address

    // Créer proposal
    CBudgetProposal proposal;
    proposal.strProposalName = proposalName;
    proposal.strURL = url;
    proposal.nBlockStart = activationHeight;
    proposal.nBlockEnd = activationHeight + KHU_R_ACTIVE_BLOCKS;  // 4 mois
    proposal.address = dest;
    proposal.nAmount = amount;
    proposal.nTime = GetTime();

    // Soumettre au réseau
    std::string strError;
    if (!budget.AddProposal(proposal, strError)) {
        return error("CreateKHUAutoProposal: %s", strError);
    }

    LogPrintf("KHU Auto-Proposal created: %s (R=%.2f%%, activation=%d)\n",
             proposalName, R_consensus / 100.0, activationHeight);

    // Broadcast au réseau
    proposal.Relay();

    return true;
}
```

#### 5.3.4 Activation Automatique (Phase 1 — Bloc 215520)

```cpp
/**
 * Activation R_consensus au bloc fixe 215520
 * Fichier: src/validation.cpp (dans ConnectBlock)
 */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, ...)
{
    int nHeight = pindex->nHeight;
    int nActivationHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight;

    // ... code existant ...

    // KHU R% Reveal processing (bloc 195360)
    if (nHeight >= nActivationHeight) {
        if (!ProcessKHUReveal(nHeight, nActivationHeight))
            return state.Invalid(false, REJECT_INVALID, "khu-reveal-failed");
    }

    // KHU R% Activation (bloc 215520, puis tous les 215520 blocs)
    if (nHeight >= nActivationHeight) {
        int activationHeight = GetKHUActivationHeight(nHeight, nActivationHeight);

        if (nHeight == activationHeight) {
            // Lire R_consensus depuis auto-proposal
            uint16_t R_next = GetKHURNextFromProposal();

            if (R_next == 0) {
                LogPrintf("KHU Activation: No auto-proposal found, keeping current R%\n");
                // Pas de changement
            } else {
                // Activer nouveau R%
                SetKhuAnnualRate(R_next);

                LogPrintf("KHU R% ACTIVATED at height %d: %.2f%% (locked for 4 months)\n",
                         nHeight, R_next / 100.0);
            }
        }
    }

    // ... suite code existant ...
}

/**
 * Lire R_next depuis auto-proposal
 */
uint16_t GetKHURNextFromProposal()
{
    LOCK(budget.cs);

    // Chercher proposal "KHU_R_*_NEXT"
    for (const auto& proposal : budget.GetBudget()) {
        if (proposal.strProposalName.find("KHU_R_") == 0 &&
            proposal.strProposalName.find("_NEXT") != std::string::npos)
        {
            // Extraire R% du nom
            // Ex: "KHU_R_22.50_NEXT" → 2250
            std::string r_str = proposal.strProposalName.substr(6);  // Skip "KHU_R_"
            r_str = r_str.substr(0, r_str.find("_"));  // Avant "_NEXT"

            double r_double = std::stod(r_str);
            return static_cast<uint16_t>(r_double * 100.0);
        }
    }

    return 0;  // Pas trouvé
}
```

#### 5.3.5 RPC Interrogation État

```cpp
/**
 * RPC: getkhugovernance
 * Fichier: src/rpc/blockchain.cpp
 */
UniValue getkhugovernance(const JSONRPCRequest& request)
{
    if (request.fHelp)
        throw std::runtime_error(
            "getkhugovernance\n"
            "\nGet current KHU R% governance cycle status.\n"
            "\nResult:\n"
            "{\n"
            "  \"cycle_position\": n,      (numeric) Position dans cycle (0-215519)\n"
            "  \"phase\": \"active|commit|reveal|notice\",\n"
            "  \"R_current\": xx.xx,       (numeric) R% actuellement actif\n"
            "  \"R_next\": xx.xx,          (numeric) R% prochain (si visible)\n"
            "  \"R_max\": xx.xx,           (numeric) R_MAX_dynamic actuel\n"
            "  \"commit_height\": n,       (numeric) Hauteur début commit\n"
            "  \"reveal_height\": n,       (numeric) Hauteur reveal (deadline)\n"
            "  \"activation_height\": n,   (numeric) Hauteur activation R_next\n"
            "  \"valid_commits\": n        (numeric) Nombre commitments valides\n"
            "}\n"
        );

    int nHeight = chainActive.Height();
    int nActivationHeight = Params().GetConsensus().vUpgrades[Consensus::UPGRADE_KHU].nActivationHeight;

    int cyclePos = GetKHUCyclePosition(nHeight, nActivationHeight);
    int revealHeight = GetKHURevealHeight(nHeight, nActivationHeight);
    int activationHeight = GetKHUActivationHeight(nHeight, nActivationHeight);
    int commitStart = activationHeight - KHU_R_CYCLE_BLOCKS + KHU_R_ACTIVE_BLOCKS;

    // Déterminer phase
    std::string phase;
    if (cyclePos < KHU_R_ACTIVE_BLOCKS)
        phase = "active";
    else if (cyclePos < KHU_R_ACTIVE_BLOCKS + KHU_R_COMMIT_BLOCKS)
        phase = "commit";
    else if (nHeight == revealHeight)
        phase = "reveal";
    else
        phase = "notice";

    uint16_t R_current = GetCurrentRAnnual();
    uint16_t R_next = GetKHURNextFromProposal();
    uint16_t R_MAX = GetRMaxDynamic(nHeight, nActivationHeight);

    // Compter commitments valides
    int valid_commits = 0;
    if (phase == "commit" || phase == "notice") {
        LOCK(mnodeman.cs);
        for (const auto& mnpair : mnodeman.mapMasternodes) {
            if (mnpair.second.IsEnabled() &&
                !mnpair.second.lastPing.nRCommitment.IsNull())
            {
                valid_commits++;
            }
        }
    }

    UniValue result(UniValue::VOBJ);
    result.pushKV("cycle_position", cyclePos);
    result.pushKV("phase", phase);
    result.pushKV("R_current", R_current / 100.0);

    if (R_next > 0)
        result.pushKV("R_next", R_next / 100.0);

    result.pushKV("R_max", R_MAX / 100.0);
    result.pushKV("commit_height", commitStart);
    result.pushKV("reveal_height", revealHeight);
    result.pushKV("activation_height", activationHeight);
    result.pushKV("valid_commits", valid_commits);

    return result;
}
```

#### 5.3.6 Workflow Exemple Complet

```cpp
/**
 * EXEMPLE COMPLET: Cycle DOMC Commit-Reveal
 * Toutes positions relatives à nActivationHeight (fork V6 PIVX)
 *
 * Cycle #0: Blocs nActivationHeight → nActivationHeight+172800
 * =============================================================
 *
 * PHASE 1: R% ACTIF (0 → 132480)
 * -------------------------------
 * Bloc nActivationHeight:
 *   R% = 25.00% ACTIVÉ (actif pendant 4 mois COMPLETS)
 *
 * Blocs nActivationHeight+1 → nActivationHeight+132479:
 *   R% = 25.00% distribué QUOTIDIENNEMENT (yield)
 *   Aucune gouvernance (période stable)
 *   LP planifient avec certitude absolue
 *
 * PHASE 2: COMMIT (132480 → 152640) — R% TOUJOURS 25.00%
 * --------------------------------------------------------
 * Bloc nActivationHeight+132480:
 *   ✅ R% = 25.00% CONTINUE d'être distribué (yield quotidien)
 *   🔄 Période commit commence (gouvernance parallèle)
 *
 * MN1 exécute:
 *   $ masternode commitkhu 22.50
 *   → R_proposal = 2250
 *   → secret = a3f5b2...
 *   → commitment = SHA256(2250 || a3f5b2...)
 *                = 7d3e9c...
 *   → Broadcast commitment via ping
 *
 * MN2 exécute:
 *   $ masternode commitkhu 23.00
 *   → commitment = 9f2a1b...
 *
 * ... (tous MN votent pendant 2 semaines)
 *
 * Blocs nActivationHeight+132481 → nActivationHeight+152639:
 *   ✅ R% = 25.00% ACTIF (yield distribué chaque jour)
 *   🔒 Votes CACHÉS (commitments SHA256 uniquement)
 *   🔒 Personne ne peut voir les R% proposés
 *
 * PHASE 3: REVEAL (Bloc nActivationHeight+152640)
 * ------------------------------------------------
 * Bloc nActivationHeight+152640 ATTEINT:
 *   ✅ R% = 25.00% CONTINUE d'être distribué
 *   🔄 ProcessKHUReveal() exécuté automatiquement
 *
 *   MN1 ping contient:
 *     nRCommitment = 7d3e9c...
 *     nRProposal = 2250
 *     nRSecret = a3f5b2...
 *
 *   Validation:
 *     SHA256(2250 || a3f5b2...) == 7d3e9c... ✅ VALIDE
 *
 *   MN2 ping contient:
 *     nRCommitment = 9f2a1b...
 *     nRProposal = 2300
 *     nRSecret = c7d1e9...
 *
 *   Validation:
 *     SHA256(2300 || c7d1e9...) == 9f2a1b... ✅ VALIDE
 *
 *   ... (400 MN révèlent)
 *
 *   Reveals valides:
 *     MN1: 2250 (22.50%)
 *     MN2: 2300 (23.00%)
 *     MN3: 2200 (22.00%)
 *     ... (400 votes)
 *
 *   Consensus:
 *     R_consensus = moyenne(2250, 2300, 2200, ...)
 *                 = 2270 (22.70%)
 *
 *   Auto-Proposal créée:
 *     Nom: "KHU_R_22.70_NEXT"
 *     Montant: 22.70 PIVX (symbolique)
 *     Activation: Bloc nActivationHeight+172800
 *
 * PHASE 4: PRÉAVIS (152641 → 172800) — R% TOUJOURS 25.00%
 * --------------------------------------------------------
 * Bloc nActivationHeight+152641:
 *   ✅ R% = 25.00% CONTINUE d'être distribué (jusqu'à la fin)
 *   👁️ R_next = 22.70% VISIBLE (auto-proposal réseau)
 *
 * Blocs nActivationHeight+152642 → nActivationHeight+172799:
 *   ✅ R% = 25.00% ACTIF (yield quotidien continue)
 *   👁️ R_next visible 2 semaines avant activation
 *   👁️ LP adaptent stratégies / rééquilibrage pools
 *   📅 Activation nActivationHeight+172800 (prévisible)
 *
 * ACTIVATION (Bloc nActivationHeight+172800)
 * -------------------------------------------
 * Bloc nActivationHeight+172800 ATTEINT:
 *   ❌ R% = 25.00% DÉSACTIVÉ (fin du cycle)
 *   ✅ R% = 22.70% ACTIVÉ (début cycle #1)
 *   Nouveau cycle commence (position = 0)
 *   R% = 22.70% actif pour 4 MOIS COMPLETS
 *
 * CYCLE #1 COMMENCE
 * =================
 * Blocs nActivationHeight+172800 → nActivationHeight+345600
 *
 * TIMELINE VISUELLE (positions relatives à nActivationHeight):
 *
 * nActivationHeight
 *   ↓
 *   0────────132480────152640────172800────────────────►
 *   │   R% ACTIF  │ COMMIT │PRÉAVIS│  Cycle 1
 *   │   25.00%    │+25.00% │+25.00%│  R=22.70% (4m)
 *   │  (3m+2j)    │ 2 sem  │ 2 sem │
 *   └─────────────┴────────┴───────┴─────────────────────►
 *                          ▲
 *                       REVEAL
 *                 (bloc fixe calculé)
 *
 * FORMULES HEIGHTS (relatives à nActivationHeight):
 *   Cycle #0 start:  nActivationHeight + 0
 *   Commit start:    nActivationHeight + 132480
 *   Reveal:          nActivationHeight + 152640
 *   Activation #1:   nActivationHeight + 172800
 *   Cycle #1 start:  nActivationHeight + 172800
 *   Commit #1:       nActivationHeight + 172800 + 132480 = nActivationHeight + 305280
 *   Reveal #1:       nActivationHeight + 172800 + 152640 = nActivationHeight + 325440
 *   Activation #2:   nActivationHeight + 172800 × 2 = nActivationHeight + 345600
 *
 * GÉNÉRIQUE:
 *   Cycle N start:  nActivationHeight + (N × 172800)
 *   Commit N:       nActivationHeight + (N × 172800) + 132480
 *   Reveal N:       nActivationHeight + (N × 172800) + 152640
 *   Activation N+1: nActivationHeight + ((N+1) × 172800)
 */
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
  Yield :       50 KHU_T (Ur_accumulated)
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
    // Année 0 : 40%
    BOOST_CHECK_EQUAL(GetRMaxDynamic(0, 0), 4000);

    // Année 10 : 30%
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 10, 0), 3000);

    // Année 33 : 7% (plancher)
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 33, 0), 700);

    // Année 100 : 7% (plancher)
    BOOST_CHECK_EQUAL(GetRMaxDynamic(525600 * 100, 0), 700);
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
- `SPEC.md.md` — Section 8 (Yield Mechanism)
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

**Version:** 2.0
**Date:** 2025-11-27
**Status:** CANONIQUE
