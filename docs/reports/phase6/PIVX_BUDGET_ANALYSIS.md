# ANALYSE SYSTÈME BUDGET PIVX — Réutilisation Phase 6

**Date**: 2025-11-24
**Objectif**: Identifier éléments réutilisables du système budget PIVX pour Phase 6 KHU DAO

---

## 📊 ARCHITECTURE BUDGET PIVX EXISTANT

### Fichiers Principaux

```
src/budget/
├── budgetproposal.{h,cpp}        # Propositions budget
├── budgetvote.{h,cpp}            # Votes MN sur propositions
├── budgetmanager.{h,cpp}         # Gestionnaire central
├── finalizedbudget.{h,cpp}       # Budget finalisé (payements)
├── finalizedbudgetvote.{h,cpp}   # Votes sur budget finalisé
├── budgetdb.{h,cpp}              # Database persistence
└── budgetutil.{h,cpp}            # Utilitaires

src/rpc/budget.cpp                # RPC commands
test/functional/rpc_budget.py     # Tests fonctionnels
```

---

## 🔍 STRUCTURES CLÉS PIVX BUDGET

### 1. **CBudgetProposal** — Proposition Individuelle

```cpp
class CBudgetProposal {
private:
    CAmount nAllotted;              // Montant alloué
    bool fValid;                    // Validité
    std::string strInvalid;         // Raison invalidité
    std::map<COutPoint, CBudgetVote> mapVotes;  // Votes MN

protected:
    std::string strProposalName;    // Nom (max 20 chars)
    std::string strURL;             // URL info (max 64 chars)
    int nBlockStart;                // Début paiement
    int nBlockEnd;                  // Fin paiement
    CScript address;                // Adresse bénéficiaire
    CAmount nAmount;                // Montant demandé
    uint256 nFeeTXHash;             // Collateral TX (50 PIV)

public:
    int64_t nTime;                  // Timestamp création

    // Fonctions vote
    bool AddOrUpdateVote(const CBudgetVote& vote, std::string& strError);
    int GetYeas() const;            // Votes YES
    int GetNays() const;            // Votes NO
    int GetAbstains() const;        // Votes ABSTAIN

    // Validation
    bool UpdateValid(int nHeight, int mnCount);
    bool IsValid() const;
    bool IsPassing(int nBlockStartBudget, int nBlockEndBudget, int mnCount) const;
    bool IsExpired(int nCurrentHeight) const;

    // Utilitaires
    uint256 GetHash() const;
    double GetRatio() const;        // Ratio YES/(YES+NO)
};
```

**CARACTÉRISTIQUES:**
- ✅ Nom unique (20 chars max)
- ✅ URL metadata (64 chars max)
- ✅ Période paiement (nBlockStart → nBlockEnd)
- ✅ Collateral fee (50 PIV anti-spam)
- ✅ Vote MN (YES/NO/ABSTAIN)
- ✅ Validation automatique (seuils quorum)

---

### 2. **CBudgetVote** — Vote Masternode

```cpp
class CBudgetVote : public CSignedMessage {
public:
    enum VoteDirection : uint32_t {
        VOTE_ABSTAIN = 0,
        VOTE_YES = 1,
        VOTE_NO = 2
    };

private:
    bool fValid;                    // Vote valide
    bool fSynced;                   // Vote synced réseau
    uint256 nProposalHash;          // Hash proposition
    VoteDirection nVote;            // Direction vote
    int64_t nTime;                  // Timestamp
    CTxIn vin;                      // MN identifier

public:
    uint256 GetHash() const;
    std::string GetVoteString() const;
    void Relay() const;             // Broadcast réseau
};
```

**CARACTÉRISTIQUES:**
- ✅ Signature MN (via CSignedMessage)
- ✅ 3 directions: YES/NO/ABSTAIN
- ✅ Broadcast P2P (relay)
- ✅ Validation signature

---

### 3. **CBudgetManager** — Gestionnaire Central

```cpp
class CBudgetManager : public CValidationInterface {
protected:
    std::map<uint256, CBudgetProposal> mapProposals;
    std::map<uint256, CFinalizedBudget> mapFinalizedBudgets;
    std::map<uint256, CBudgetVote> mapSeenProposalVotes;
    std::map<uint256, CFinalizedBudgetVote> mapSeenFinalizedBudgetVotes;

    // Orphan votes (en attente MN sync)
    std::map<uint256, PropVotesAndLastVoteReceivedTime> mapOrphanProposalVotes;

    // Collateral tracking (anti-reorg)
    std::map<uint256, uint256> mapFeeTxToProposal;
    std::map<uint256, uint256> mapFeeTxToBudget;

public:
    // Locks (ordre critique)
    mutable RecursiveMutex cs_budgets;
    mutable RecursiveMutex cs_proposals;
    mutable RecursiveMutex cs_finalizedvotes;
    mutable RecursiveMutex cs_votes;

    // Gestion propositions
    bool AddProposal(CBudgetProposal& budgetProposal);
    bool UpdateProposal(const CBudgetVote& vote, CNode* pfrom, std::string& strError);
    CBudgetProposal* FindProposal(const uint256& nHash);
    std::vector<CBudgetProposal*> GetAllProposalsOrdered();  // Tri par votes

    // Finalisation
    bool AddFinalizedBudget(CFinalizedBudget& finalizedBudget, CNode* pfrom = nullptr);
    bool FillBlockPayee(CMutableTransaction& txCoinbase, CMutableTransaction& txCoinstake,
                       const int nHeight, bool fProofOfStake) const;

    // Validation
    TrxValidationStatus IsTransactionValid(const CTransaction& txNew,
                                          const uint256& nBlockHash, int nBlockHeight) const;

    // P2P sync
    bool ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, int& banScore);
    void Sync(CNode* node, bool fPartial);

    // Total budget disponible
    static CAmount GetTotalBudget(int nHeight);
};
```

**CARACTÉRISTIQUES:**
- ✅ Maps thread-safe (RecursiveMutex)
- ✅ Orphan votes cache (10000 limite)
- ✅ Collateral tracking (reorg safety)
- ✅ P2P sync protocol
- ✅ Validation consensus (IsTransactionValid)
- ✅ FillBlockPayee (intégration coinstake)

---

### 4. **CFinalizedBudget** — Budget Finalisé

```cpp
class CFinalizedBudget {
private:
    bool fAutoChecked;              // Auto-vote MN si match
    bool fValid;
    std::string strInvalid;
    std::map<COutPoint, CFinalizedBudgetVote> mapVotes;

protected:
    std::string strBudgetName;
    int nBlockStart;
    std::vector<CTxBudgetPayment> vecBudgetPayments;  // Liste paiements
    uint256 nFeeTXHash;
    std::string strProposals;       // Hash propositions incluses

public:
    static constexpr unsigned int MAX_PROPOSALS_PER_CYCLE = 100;

    bool AddOrUpdateVote(const CFinalizedBudgetVote& vote, std::string& strError);
    bool UpdateValid(int nHeight);
    bool IsWellFormed(const CAmount& nTotalBudget);

    bool GetPayeeAndAmount(int64_t nBlockHeight, CScript& payee, CAmount& nAmount) const;
    bool GetBudgetPaymentByBlock(int64_t nBlockHeight, CTxBudgetPayment& payment) const;

    TrxValidationStatus IsTransactionValid(const CTransaction& txNew,
                                          const uint256& nBlockHash, int nBlockHeight) const;

    bool CheckProposals(const std::map<uint256, CBudgetProposal>& mapWinningProposals) const;
    CAmount GetTotalPayout() const;
};
```

---

## ✅ ÉLÉMENTS RÉUTILISABLES POUR PHASE 6

### 🔄 **1. Structure Vote (RÉUTILISABLE)**

```cpp
// ✅ GARDER: Enum VoteDirection
enum VoteDirection : uint32_t {
    VOTE_ABSTAIN = 0,
    VOTE_YES = 1,
    VOTE_NO = 2
};

// ✅ GARDER: Base vote signature
class CKhuDAOVote : public CSignedMessage {
    CTxIn vin;              // MN identifier
    uint256 nProposalHash;  // Proposition KHU DAO
    VoteDirection nVote;
    int64_t nTime;

    // Hérité CSignedMessage
    std::vector<unsigned char> vchSig;

    uint256 GetHash() const;
    std::string GetVoteString() const;
    void Relay() const;
};
```

**POURQUOI:**
- ✅ Structure éprouvée (années production PIVX)
- ✅ Signature MN intégrée
- ✅ P2P relay fonctionnel
- ✅ Sérialisation consensus

---

### 🔄 **2. Gestion Collateral (ADAPTER)**

```cpp
// PIVX Budget: 50 PIV collateral anti-spam
static const CAmount PROPOSAL_FEE_TX = (50 * COIN);

// KHU DAO: Adapter montant (5-10 PIV?)
static const CAmount KHU_DAO_PROPOSAL_FEE = (10 * COIN);
```

**MÉCANISME PIVX:**
1. User crée TX avec fee collateral (50 PIV)
2. TX confirmé → Proposal valide
3. Proposal rejeté → Collateral **RETOURNÉ** (pas brûlé)
4. Reorg → Tracking via `mapFeeTxToProposal`

**ADAPTATION KHU DAO:**
1. User crée TX avec fee collateral (10 PIV?)
2. TX confirmé → Proposal valide
3. Proposal rejeté → Collateral **BRÛLÉ** 🔥 (déflationniste!)
4. Reorg → Tracking identique

---

### 🔄 **3. Manager Thread-Safe (RÉUTILISABLE)**

```cpp
// ✅ GARDER: Pattern locks
class CKhuDAOManager {
    mutable RecursiveMutex cs_proposals;
    mutable RecursiveMutex cs_votes;

    std::map<uint256, CKhuDAOProposal> mapProposals;
    std::map<uint256, CKhuDAOVote> mapSeenVotes;
    std::map<uint256, OrphanVotes> mapOrphanVotes;  // Cache 10000 limite

    // Collateral tracking (reorg safety)
    std::map<uint256, uint256> mapFeeTxToProposal;
};
```

**AVANTAGES:**
- ✅ Thread-safety prouvée
- ✅ Orphan votes cache (sync retardé)
- ✅ Reorg safety (collateral tracking)

---

### 🔄 **4. Validation Consensus (ADAPTER)**

```cpp
// PIVX: TrxValidationStatus pour validation coinstake
enum class TrxValidationStatus {
    InValid,         // TX invalide
    Valid,           // TX valide
    DoublePayment,   // Double paiement budget
    VoteThreshold    // Pas assez votes MN
};

// KHU DAO: Identique
TrxValidationStatus IsKhuDAOTransactionValid(
    const CTransaction& txNew,
    const uint256& nBlockHash,
    int nBlockHeight
) const;
```

**LOGIQUE PIVX RÉUTILISABLE:**
1. Vérifier height = bloc paiement budget
2. Vérifier output payee matches proposal
3. Vérifier montant matches proposal
4. Vérifier quorum MN (seuil votes)
5. Vérifier pas double-paiement (mapPayment_History)

**ADAPTATION KHU DAO:**
- ✅ Même logique validation
- ✅ Vérifier budget cycle (172800 blocs)
- ✅ Vérifier montant = (U+Ur)×0.5%
- ✅ Si rejeté → output = OP_RETURN (burn)

---

### 🔄 **5. FillBlockPayee (ADAPTER)**

```cpp
// PIVX Budget: Injection paiement dans coinstake
bool CBudgetManager::FillBlockPayee(
    CMutableTransaction& txCoinbase,
    CMutableTransaction& txCoinstake,
    const int nHeight,
    bool fProofOfStake
) const {
    // Si budget payment height:
    CScript payee;
    CAmount nAmount;

    if (GetPayeeAndAmount(nHeight, payee, nAmount)) {
        // Ajouter output coinstake
        txCoinstake.vout.emplace_back(nAmount, payee);
        return true;
    }

    return false;
}

// KHU DAO: Adaptation
bool CKhuDAOManager::FillDAOBudgetPayment(
    CMutableTransaction& txCoinstake,
    const KhuGlobalState& state,
    int nHeight
) const {
    if (!IsDAOBudgetBlock(state, nHeight)) {
        return false;
    }

    // Calculer budget
    CAmount dao_budget = (state.U + state.Ur) * 5 / 1000;  // 0.5%

    // Récupérer proposition gagnante (highest votes)
    CScript payee;
    if (GetWinningProposal(nHeight, payee)) {
        // Accepté → Payer
        txCoinstake.vout.emplace_back(dao_budget, payee);
    } else {
        // Rejeté → Brûler
        CScript burnScript;
        burnScript << OP_RETURN;
        txCoinstake.vout.emplace_back(dao_budget, burnScript);
    }

    return true;
}
```

**AVANTAGES:**
- ✅ Intégration coinstake éprouvée
- ✅ Validation consensus (IsTransactionValid)
- ✅ Reorg safety

---

### ❌ **6. NON RÉUTILISABLE**

#### **CFinalizedBudget** → Trop complexe pour KHU DAO
- ❌ PIVX: Budget multi-propositions (100 max)
- ❌ PIVX: Paiements étalés (nBlockStart → nBlockEnd)
- ❌ PIVX: Double vote (proposals + finalized budget)

**KHU DAO PLUS SIMPLE:**
- ✅ 1 gagnant par cycle (highest votes)
- ✅ Paiement ponctuel (1 bloc)
- ✅ Vote unique (propositions seulement)

---

## 🎯 PLAN RÉUTILISATION

### ✅ **GARDER TEL QUEL**

1. **Enum VoteDirection** (YES/NO/ABSTAIN)
2. **CSignedMessage pattern** (vote signature MN)
3. **RecursiveMutex locks** (cs_proposals, cs_votes)
4. **Orphan votes cache** (mapOrphanVotes, 10000 limite)
5. **Collateral tracking** (mapFeeTxToProposal)
6. **P2P relay protocol** (Sync, ProcessMessage)

### 🔄 **ADAPTER**

1. **CBudgetProposal → CKhuDAOProposal**
   - Garder: name, URL, address, amount, nFeeTXHash, votes
   - Supprimer: nBlockStart/End (1 paiement ponctuel)
   - Ajouter: dao_cycle (172800 blocs)

2. **CBudgetVote → CKhuDAOVote**
   - Identique (aucun changement)

3. **CBudgetManager → CKhuDAOManager**
   - Garder: maps, locks, orphan cache, collateral tracking
   - Supprimer: FinalizedBudget logic
   - Simplifier: 1 gagnant par cycle (pas multi-propositions)

4. **FillBlockPayee → FillDAOBudgetPayment**
   - Garder: injection coinstake
   - Ajouter: burn logic (OP_RETURN si rejeté)

### ❌ **NE PAS UTILISER**

1. **CFinalizedBudget** (trop complexe)
2. **Multi-proposals per cycle** (KHU = 1 gagnant)
3. **Paiements étalés** (KHU = paiement ponctuel)

---

## 📝 STRUCTURE FINALE KHU DAO

```cpp
// src/khu/khu_dao_proposal.h
class CKhuDAOProposal {
private:
    bool fValid;
    std::string strInvalid;
    std::map<COutPoint, CKhuDAOVote> mapVotes;

protected:
    std::string strProposalName;    // Max 20 chars
    std::string strURL;             // Max 64 chars
    CScript address;                // Bénéficiaire
    CAmount nAmount;                // Montant demandé
    uint256 nFeeTXHash;             // Collateral (10 PIV)
    uint32_t dao_cycle;             // Cycle DAO (height / 172800)

public:
    int64_t nTime;

    bool AddOrUpdateVote(const CKhuDAOVote& vote, std::string& strError);
    int GetYeas() const;
    int GetNays() const;
    int GetAbstains() const;
    double GetRatio() const;

    bool UpdateValid(int nHeight, int mnCount);
    bool IsValid() const;
    bool IsPassing(int mnCount) const;

    uint256 GetHash() const;
};

// src/khu/khu_dao_vote.h
class CKhuDAOVote : public CSignedMessage {
public:
    enum VoteDirection : uint32_t {
        VOTE_ABSTAIN = 0,
        VOTE_YES = 1,
        VOTE_NO = 2
    };

private:
    bool fValid;
    bool fSynced;
    uint256 nProposalHash;
    VoteDirection nVote;
    int64_t nTime;
    CTxIn vin;

public:
    uint256 GetHash() const;
    std::string GetVoteString() const;
    void Relay() const;
};

// src/khu/khu_dao_manager.h
class CKhuDAOManager {
    mutable RecursiveMutex cs_proposals;
    mutable RecursiveMutex cs_votes;

    std::map<uint256, CKhuDAOProposal> mapProposals;
    std::map<uint256, CKhuDAOVote> mapSeenVotes;
    std::map<uint256, OrphanVotes> mapOrphanVotes;
    std::map<uint256, uint256> mapFeeTxToProposal;

public:
    bool AddProposal(CKhuDAOProposal& proposal);
    bool UpdateProposal(const CKhuDAOVote& vote, CNode* pfrom, std::string& strError);

    // Récupérer proposition gagnante (highest votes)
    bool GetWinningProposal(uint32_t dao_cycle, CScript& payeeRet, CAmount& nAmountRet) const;

    // Fill coinstake avec paiement DAO
    bool FillDAOBudgetPayment(CMutableTransaction& txCoinstake,
                             const KhuGlobalState& state,
                             int nHeight) const;

    // Validation consensus
    TrxValidationStatus IsKhuDAOTransactionValid(const CTransaction& txNew,
                                                 const uint256& nBlockHash,
                                                 int nBlockHeight) const;

    // P2P sync
    bool ProcessMessage(CNode* pfrom, std::string& strCommand, CDataStream& vRecv, int& banScore);
    void Sync(CNode* node, bool fPartial);
};
```

---

## 🚀 PROCHAINES ÉTAPES

1. ✅ **Copier structures de base** (vote, proposal)
2. ✅ **Simplifier** (supprimer finalized budget)
3. ✅ **Adapter FillBlockPayee** → burn logic
4. ✅ **Tester** avec mêmes tests fonctionnels PIVX

---

**FIN ANALYSE**
