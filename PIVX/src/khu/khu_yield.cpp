// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "khu/khu_yield.h"

#include "khu/khu_state.h"
#include "khu/khu_validation.h"
#include "khu/zkhu_db.h"
#include "khu/zkhu_note.h"
#include "logging.h"

#include <boost/multiprecision/cpp_int.hpp>

namespace khu_yield {

// ============================================================================
// Internal Functions
// ============================================================================

/**
 * IterateStakedNotes - Stream all ZKHU notes from LevelDB and apply functor
 *
 * RÈGLE CONSENSUS-CRITICAL:
 * - LevelDB cursors provide deterministic lexicographic order
 * - All nodes will process notes in the same order
 * - No in-memory sorting required
 *
 * @param func Functor to apply to each note: bool(uint256 noteId, ZKHUNoteData& data)
 * @return true if iteration completed successfully
 */
template<typename Func>
static bool IterateStakedNotes(Func func)
{
    CZKHUTreeDB* zkhuDB = GetZKHUDB();
    if (!zkhuDB) {
        // DB not initialized (e.g., test environment or before ZKHU activation)
        // This is valid - treat as empty note set, iteration succeeds with zero notes
        LogPrint(BCLog::KHU, "IterateStakedNotes: ZKHU DB not initialized (empty note set)\n");
        return true;
    }

    // Create LevelDB cursor starting at ZKHU note prefix
    // Key format: 'K' + 'T' + note_id
    std::unique_ptr<CDBIterator> pcursor(const_cast<CDBWrapper*>(static_cast<const CDBWrapper*>(zkhuDB))->NewIterator());

    // Seek to first note entry
    // Using namespace 'K' (ZKHU) and prefix 'T' (notes)
    pcursor->Seek(std::make_pair('K', std::make_pair('T', uint256())));

    // Iterate all notes in deterministic order
    while (pcursor->Valid()) {
        // Read key
        std::pair<char, std::pair<char, uint256>> key;
        if (!pcursor->GetKey(key)) {
            break;
        }

        // Check if still in ZKHU note namespace
        if (key.first != 'K' || key.second.first != 'T') {
            break; // End of notes
        }

        const uint256& noteId = key.second.second;

        // Read note data
        ZKHUNoteData noteData;
        if (!pcursor->GetValue(noteData)) {
            LogPrintf("ERROR: IterateStakedNotes: Failed to read note data for %s\n",
                      noteId.GetHex());
            return false;
        }

        // Apply functor
        if (!func(noteId, noteData)) {
            return false;
        }

        // Move to next note
        pcursor->Next();
    }

    return true;
}

/**
 * CalculateTotalYield - Calculate total yield for all mature staked notes
 *
 * ALGORITHME CONSENSUS-CRITICAL:
 * 1. Iterate all ZKHU notes via LevelDB cursor (deterministic order)
 * 2. For each mature note: add daily yield to total
 * 3. Protect against overflow using int128_t
 *
 * @param nHeight Current block height
 * @param R_annual Annual yield rate (basis points)
 * @param totalYield Output: total yield calculated (satoshis)
 * @return true on success, false on overflow or error
 */
static bool CalculateTotalYield(uint32_t nHeight, uint16_t R_annual, CAmount& totalYield)
{
    totalYield = 0;

    // Use int128_t for overflow-safe accumulation
    using int128_t = boost::multiprecision::int128_t;
    int128_t totalYield128 = 0;

    bool success = IterateStakedNotes([&](const uint256& noteId, const ZKHUNoteData& note) {
        // Check note maturity
        if (!IsNoteMature(note.nStakeStartHeight, nHeight)) {
            return true; // Skip immature note, continue iteration
        }

        // Calculate daily yield for this note
        CAmount dailyYield = CalculateDailyYieldForNote(note.amount, R_annual);

        // Accumulate with overflow protection
        totalYield128 += dailyYield;

        // Check for overflow (should never happen with realistic values)
        if (totalYield128 > std::numeric_limits<CAmount>::max()) {
            LogPrintf("ERROR: CalculateTotalYield: Overflow detected at note %s\n",
                      noteId.GetHex());
            return false; // Stop iteration
        }

        LogPrint(BCLog::KHU, "CalculateTotalYield: Note %s amount=%d dailyYield=%d\n",
                 noteId.GetHex().substr(0, 16), note.amount, dailyYield);

        return true; // Continue iteration
    });

    if (!success) {
        return false;
    }

    // Safe cast to CAmount (overflow already checked)
    totalYield = static_cast<CAmount>(totalYield128);

    return true;
}

// ============================================================================
// Public Functions
// ============================================================================

bool ShouldApplyDailyYield(uint32_t nHeight, uint32_t nV6ActivationHeight, uint32_t nLastYieldHeight)
{
    // Before V6 activation: no yield
    if (nHeight < nV6ActivationHeight) {
        return false;
    }

    // First yield at activation height
    if (nLastYieldHeight == 0 && nHeight == nV6ActivationHeight) {
        return true;
    }

    // Subsequent yields every YIELD_INTERVAL blocks (exact boundary only)
    if (nLastYieldHeight > 0 && (nHeight - nLastYieldHeight) == YIELD_INTERVAL) {
        return true;
    }

    return false;
}

bool ApplyDailyYield(KhuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight)
{
    // Sanity check: height must be at yield boundary
    if (!ShouldApplyDailyYield(nHeight, nV6ActivationHeight, state.last_yield_update_height)) {
        LogPrintf("ERROR: ApplyDailyYield: Not at yield boundary (height=%u, last=%u)\n",
                  nHeight, state.last_yield_update_height);
        return false;
    }

    // Calculate total yield from all mature staked notes
    CAmount totalYield = 0;
    if (!CalculateTotalYield(nHeight, state.R_annual, totalYield)) {
        LogPrintf("ERROR: ApplyDailyYield: Failed to calculate total yield at height %u\n", nHeight);
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE — Cr ET Ur ensemble
    // Invariant Cr == Ur doit être préservé
    // ═══════════════════════════════════════════════════════════
    state.Cr += totalYield;
    state.Ur += totalYield;

    // Store yield for exact undo (P1 fix: avoid recalculation on reorg)
    state.last_yield_amount = totalYield;

    // Update last yield height
    state.last_yield_update_height = nHeight;

    LogPrint(BCLog::KHU, "ApplyDailyYield: height=%u R_annual=%u (%.2f%%) totalYield=%d Cr=%d Ur=%d\n",
             nHeight, state.R_annual, state.R_annual / 100.0, totalYield, state.Cr, state.Ur);

    return true;
}

bool UndoDailyYield(KhuGlobalState& state, uint32_t nHeight, uint32_t nV6ActivationHeight)
{
    // P1 FIX: Use stored yield amount instead of recalculating
    // This ensures exact undo even if note set changed during reorg
    CAmount totalYield = state.last_yield_amount;

    // Sanity check: yield must have been stored
    if (totalYield < 0) {
        LogPrintf("ERROR: UndoDailyYield: Invalid stored yield %d at height %u\n", totalYield, nHeight);
        return false;
    }

    // ═══════════════════════════════════════════════════════════
    // DOUBLE MUTATION ATOMIQUE REVERSE — Cr ET Ur ensemble
    // ═══════════════════════════════════════════════════════════
    if (state.Cr < totalYield) {
        LogPrintf("ERROR: UndoDailyYield: Underflow Cr=%d < totalYield=%d at height %u\n",
                  state.Cr, totalYield, nHeight);
        return false;
    }
    if (state.Ur < totalYield) {
        LogPrintf("ERROR: UndoDailyYield: Underflow Ur=%d < totalYield=%d at height %u\n",
                  state.Ur, totalYield, nHeight);
        return false;
    }
    state.Cr -= totalYield;
    state.Ur -= totalYield;

    // Clear stored yield amount
    state.last_yield_amount = 0;

    // Restore previous last_yield_update_height
    if (nHeight == nV6ActivationHeight) {
        state.last_yield_update_height = 0;
    } else if (nHeight > nV6ActivationHeight + YIELD_INTERVAL) {
        state.last_yield_update_height = nHeight - YIELD_INTERVAL;
    } else {
        state.last_yield_update_height = nV6ActivationHeight;
    }

    LogPrint(BCLog::KHU, "UndoDailyYield: height=%u totalYield=%d (stored) Cr=%d Ur=%d\n",
             nHeight, totalYield, state.Cr, state.Ur);

    return true;
}

CAmount CalculateDailyYieldForNote(CAmount amount, uint16_t R_annual)
{
    // FORMULE CONSENSUS (basis points):
    // daily = (amount × R_annual / 10000) / 365
    //
    // Use int128_t to avoid overflow

    if (amount <= 0 || R_annual == 0) {
        return 0;
    }

    using int128_t = boost::multiprecision::int128_t;

    // annual_yield = amount × R_annual / 10000
    int128_t annual128 = static_cast<int128_t>(amount) * R_annual / 10000;

    // daily_yield = annual_yield / 365
    int128_t daily128 = annual128 / DAYS_PER_YEAR;

    // Check overflow (should never happen with realistic values)
    if (daily128 > std::numeric_limits<CAmount>::max()) {
        LogPrintf("ERROR: CalculateDailyYieldForNote: Overflow (amount=%d, R=%u)\n",
                  amount, R_annual);
        return 0;
    }

    return static_cast<CAmount>(daily128);
}

bool IsNoteMature(uint32_t noteHeight, uint32_t currentHeight)
{
    // RÈGLE CONSENSUS: Note must be staked for at least MATURITY_BLOCKS (4320 blocks = 3 days)
    if (currentHeight < noteHeight) {
        return false; // Invalid state
    }

    return (currentHeight - noteHeight) >= MATURITY_BLOCKS;
}

} // namespace khu_yield
