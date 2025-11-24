// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_domc_tx.h"

#include "consensus/params.h"
#include "consensus/validation.h"
#include "khu/khu_domcdb.h"
#include "khu/khu_state.h"
#include "logging.h"
#include "script/script.h"
#include "streams.h"

// ============================================================================
// EXTRACTION functions (parse DOMC data from transaction)
// ============================================================================

bool ExtractDomcCommitFromTx(const CTransaction& tx, khu_domc::DomcCommit& commit)
{
    // DOMC commit encoded in first vout with OP_RETURN
    // Format: OP_RETURN <serialized DomcCommit>

    if (tx.vout.empty()) {
        return false;
    }

    const CTxOut& txout = tx.vout[0];
    if (!txout.scriptPubKey.IsUnspendable()) {
        return false; // Must be OP_RETURN
    }

    // Extract data after OP_RETURN
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!txout.scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }

    if (!txout.scriptPubKey.GetOp(pc, opcode, data)) {
        return false;
    }

    // Deserialize DomcCommit
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> commit;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: ExtractDomcCommitFromTx: Deserialization failed: %s\n", e.what());
        return false;
    }
}

bool ExtractDomcRevealFromTx(const CTransaction& tx, khu_domc::DomcReveal& reveal)
{
    // DOMC reveal encoded in first vout with OP_RETURN
    // Format: OP_RETURN <serialized DomcReveal>

    if (tx.vout.empty()) {
        return false;
    }

    const CTxOut& txout = tx.vout[0];
    if (!txout.scriptPubKey.IsUnspendable()) {
        return false; // Must be OP_RETURN
    }

    // Extract data after OP_RETURN
    CScript::const_iterator pc = txout.scriptPubKey.begin();
    opcodetype opcode;
    std::vector<unsigned char> data;

    if (!txout.scriptPubKey.GetOp(pc, opcode) || opcode != OP_RETURN) {
        return false;
    }

    if (!txout.scriptPubKey.GetOp(pc, opcode, data)) {
        return false;
    }

    // Deserialize DomcReveal
    try {
        CDataStream ss(data, SER_NETWORK, PROTOCOL_VERSION);
        ss >> reveal;
        return true;
    } catch (const std::exception& e) {
        LogPrintf("ERROR: ExtractDomcRevealFromTx: Deserialization failed: %s\n", e.what());
        return false;
    }
}

// ============================================================================
// VALIDATION functions (consensus-critical)
// ============================================================================

bool ValidateDomcCommitTx(
    const CTransaction& tx,
    CValidationState& state,
    const KhuGlobalState& khuState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Extract commit from transaction
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        return state.Invalid(false, REJECT_INVALID, "bad-domc-commit-format",
                            "Failed to extract DOMC commit from transaction");
    }

    // RULE 1: Must be in commit phase
    if (!khu_domc::IsDomcCommitPhase(nHeight, khuState.domc_cycle_start)) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-phase",
                            strprintf("DOMC commit not allowed outside commit phase (height=%u, cycle_start=%u)",
                                    nHeight, khuState.domc_cycle_start));
    }

    // RULE 2: Verify cycle ID matches current cycle
    uint32_t currentCycleId = khu_domc::GetCurrentCycleId(nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    if (commit.nCycleId != currentCycleId) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-cycle",
                            strprintf("DOMC commit cycle ID mismatch (commit=%u, expected=%u)",
                                    commit.nCycleId, currentCycleId));
    }

    // RULE 3: Masternode must not have already committed in this cycle
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        return state.Invalid(false, REJECT_INVALID, "domc-db-not-initialized",
                            "DOMC database not initialized");
    }

    if (domcDB->HaveCommit(commit.mnOutpoint, commit.nCycleId)) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-duplicate",
                            strprintf("Masternode %s already committed in cycle %u",
                                    commit.mnOutpoint.ToString(), commit.nCycleId));
    }

    // RULE 4: Verify commit height is current height
    if (commit.nCommitHeight != nHeight) {
        return state.Invalid(false, REJECT_INVALID, "domc-commit-wrong-height",
                            strprintf("Commit height mismatch (commit=%u, expected=%u)",
                                    commit.nCommitHeight, nHeight));
    }

    // TODO: RULE 5: Verify masternode signature (requires masternode code integration)
    // For now, we accept all commits (signature verification will be added in integration phase)

    LogPrint(BCLog::KHU, "ValidateDomcCommitTx: Valid commit from MN %s for cycle %u at height %u\n",
             commit.mnOutpoint.ToString(), commit.nCycleId, nHeight);

    return true;
}

bool ValidateDomcRevealTx(
    const CTransaction& tx,
    CValidationState& state,
    const KhuGlobalState& khuState,
    uint32_t nHeight,
    const Consensus::Params& consensusParams
)
{
    // Extract reveal from transaction
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        return state.Invalid(false, REJECT_INVALID, "bad-domc-reveal-format",
                            "Failed to extract DOMC reveal from transaction");
    }

    // RULE 1: Must be in reveal phase
    if (!khu_domc::IsDomcRevealPhase(nHeight, khuState.domc_cycle_start)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-phase",
                            strprintf("DOMC reveal not allowed outside reveal phase (height=%u, cycle_start=%u)",
                                    nHeight, khuState.domc_cycle_start));
    }

    // RULE 2: Verify cycle ID matches current cycle
    uint32_t currentCycleId = khu_domc::GetCurrentCycleId(nHeight,
        consensusParams.vUpgrades[Consensus::UPGRADE_V6_0].nActivationHeight);

    if (reveal.nCycleId != currentCycleId) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-cycle",
                            strprintf("DOMC reveal cycle ID mismatch (reveal=%u, expected=%u)",
                                    reveal.nCycleId, currentCycleId));
    }

    // RULE 3: Must have matching commit from same masternode
    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        return state.Invalid(false, REJECT_INVALID, "domc-db-not-initialized",
                            "DOMC database not initialized");
    }

    khu_domc::DomcCommit commit;
    if (!domcDB->ReadCommit(reveal.mnOutpoint, reveal.nCycleId, commit)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-no-commit",
                            strprintf("No commit found for masternode %s in cycle %u",
                                    reveal.mnOutpoint.ToString(), reveal.nCycleId));
    }

    // RULE 4: Hash(R + salt) must match commit hash
    uint256 revealHash = reveal.GetCommitHash();
    if (revealHash != commit.hashCommit) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-hash-mismatch",
                            strprintf("Reveal hash does not match commit (expected=%s, got=%s)",
                                    commit.hashCommit.GetHex(), revealHash.GetHex()));
    }

    // RULE 5: R proposal must be ≤ R_MAX (absolute maximum)
    if (reveal.nRProposal > khu_domc::R_MAX) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-r-too-high",
                            strprintf("R proposal %u exceeds maximum %u",
                                    reveal.nRProposal, khu_domc::R_MAX));
    }

    // RULE 6: Verify reveal height is current height
    if (reveal.nRevealHeight != nHeight) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-wrong-height",
                            strprintf("Reveal height mismatch (reveal=%u, expected=%u)",
                                    reveal.nRevealHeight, nHeight));
    }

    // RULE 7: Masternode must not have already revealed in this cycle
    if (domcDB->HaveReveal(reveal.mnOutpoint, reveal.nCycleId)) {
        return state.Invalid(false, REJECT_INVALID, "domc-reveal-duplicate",
                            strprintf("Masternode %s already revealed in cycle %u",
                                    reveal.mnOutpoint.ToString(), reveal.nCycleId));
    }

    // TODO: RULE 8: Verify masternode signature (requires masternode code integration)
    // For now, we accept all reveals (signature verification will be added in integration phase)

    LogPrint(BCLog::KHU, "ValidateDomcRevealTx: Valid reveal from MN %s for cycle %u: R=%u (%.2f%%) at height %u\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId, reveal.nRProposal,
             reveal.nRProposal / 100.0, nHeight);

    return true;
}

// ============================================================================
// APPLY functions (store to database)
// ============================================================================

bool ApplyDomcCommitTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to extract commit from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: ApplyDomcCommitTx: DOMC DB not initialized\n");
        return false;
    }

    // Write commit to database
    if (!domcDB->WriteCommit(commit)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to write commit to DB (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    // Add masternode to cycle index (for GetRevealsForCycle)
    if (!domcDB->AddMasternodeToCycleIndex(commit.nCycleId, commit.mnOutpoint)) {
        LogPrintf("ERROR: ApplyDomcCommitTx: Failed to add MN to cycle index (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    LogPrint(BCLog::KHU, "ApplyDomcCommitTx: Stored commit from MN %s for cycle %u\n",
             commit.mnOutpoint.ToString(), commit.nCycleId);

    return true;
}

bool ApplyDomcRevealTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        LogPrintf("ERROR: ApplyDomcRevealTx: Failed to extract reveal from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: ApplyDomcRevealTx: DOMC DB not initialized\n");
        return false;
    }

    // Write reveal to database
    if (!domcDB->WriteReveal(reveal)) {
        LogPrintf("ERROR: ApplyDomcRevealTx: Failed to write reveal to DB (MN=%s, cycle=%u)\n",
                  reveal.mnOutpoint.ToString(), reveal.nCycleId);
        return false;
    }

    LogPrint(BCLog::KHU, "ApplyDomcRevealTx: Stored reveal from MN %s for cycle %u: R=%u (%.2f%%)\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId, reveal.nRProposal,
             reveal.nRProposal / 100.0);

    return true;
}

// ============================================================================
// UNDO functions (reorg support)
// ============================================================================

bool UndoDomcCommitTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcCommit commit;
    if (!ExtractDomcCommitFromTx(tx, commit)) {
        LogPrintf("ERROR: UndoDomcCommitTx: Failed to extract commit from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: UndoDomcCommitTx: DOMC DB not initialized\n");
        return false;
    }

    // Erase commit from database
    if (!domcDB->EraseCommit(commit.mnOutpoint, commit.nCycleId)) {
        LogPrintf("ERROR: UndoDomcCommitTx: Failed to erase commit from DB (MN=%s, cycle=%u)\n",
                  commit.mnOutpoint.ToString(), commit.nCycleId);
        return false;
    }

    LogPrint(BCLog::KHU, "UndoDomcCommitTx: Erased commit from MN %s for cycle %u\n",
             commit.mnOutpoint.ToString(), commit.nCycleId);

    return true;
}

bool UndoDomcRevealTx(const CTransaction& tx, uint32_t nHeight)
{
    khu_domc::DomcReveal reveal;
    if (!ExtractDomcRevealFromTx(tx, reveal)) {
        LogPrintf("ERROR: UndoDomcRevealTx: Failed to extract reveal from tx %s\n",
                  tx.GetHash().ToString());
        return false;
    }

    CKHUDomcDB* domcDB = GetKHUDomcDB();
    if (!domcDB) {
        LogPrintf("ERROR: UndoDomcRevealTx: DOMC DB not initialized\n");
        return false;
    }

    // Erase reveal from database
    if (!domcDB->EraseReveal(reveal.mnOutpoint, reveal.nCycleId)) {
        LogPrintf("ERROR: UndoDomcRevealTx: Failed to erase reveal from DB (MN=%s, cycle=%u)\n",
                  reveal.mnOutpoint.ToString(), reveal.nCycleId);
        return false;
    }

    LogPrint(BCLog::KHU, "UndoDomcRevealTx: Erased reveal from MN %s for cycle %u\n",
             reveal.mnOutpoint.ToString(), reveal.nCycleId);

    return true;
}
