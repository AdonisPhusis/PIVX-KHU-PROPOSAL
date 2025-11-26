// Copyright (c) 2025 The PIVX Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * TEST DÉMONSTRATION PIPELINE KHUGLOBALSTATE
 *
 * Ce test simule le pipeline complet avec logs détaillés pour visualiser
 * comment KhuGlobalState circule à travers le système.
 */

#include "khu/khu_state.h"
#include "khu/khu_yield.h"
#include "khu/khu_dao.h"
#include "khu/khu_domc.h"
#include "amount.h"
#include "test/test_pivx.h"

#include <boost/test/unit_test.hpp>

BOOST_FIXTURE_TEST_SUITE(khu_pipeline_demo_tests, BasicTestingSetup)

/**
 * Fonction helper pour afficher l'état KHU
 */
void PrintState(const std::string& label, const KhuGlobalState& state, int indent = 0)
{
    std::string ind(indent, ' ');
    std::cout << ind << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << ind << label << std::endl;
    std::cout << ind << "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━" << std::endl;
    std::cout << ind << "Height:    " << state.nHeight << std::endl;
    std::cout << ind << "C:         " << state.C / COIN << " KHU" << std::endl;
    std::cout << ind << "U:         " << state.U / COIN << " KHU" << std::endl;
    std::cout << ind << "Cr:        " << state.Cr / COIN << " KHU" << std::endl;
    std::cout << ind << "Ur:        " << state.Ur / COIN << " KHU" << std::endl;
    std::cout << ind << "T:         " << state.T / COIN << " KHU" << std::endl;
    std::cout << ind << "R_annual:  " << state.R_annual << " (" << (state.R_annual / 100.0) << "%)" << std::endl;
    std::cout << ind << "Invariants: C==U? " << (state.C == state.U ? "✅" : "❌")
              << " | Cr==Ur? " << (state.Cr == state.Ur ? "✅" : "❌")
              << " | T>=0? " << (state.T >= 0 ? "✅" : "❌") << std::endl;
    std::cout << std::endl;
}

/**
 * TEST 1: Pipeline complet sur 1 bloc simple
 */
BOOST_AUTO_TEST_CASE(pipeline_demo_simple_block)
{
    std::cout << "\n\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TEST 1: PIPELINE SIMPLE - 1 BLOC AVEC MINT             ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 1: État initial (bloc 1000)
    // ═══════════════════════════════════════════════════════════
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 1000;
    state.C = 1000 * COIN;
    state.U = 1000 * COIN;
    state.Cr = 50 * COIN;
    state.Ur = 50 * COIN;
    state.T = 10 * COIN;
    state.R_annual = 1500; // 15%

    PrintState("📥 ÉTAT INITIAL (bloc 1000)", state);

    // Vérifier invariants initiaux
    BOOST_CHECK(state.CheckInvariants());

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 2: Copie vers nouvel état (bloc 1001)
    // ═══════════════════════════════════════════════════════════
    std::cout << "🔄 TRAITEMENT BLOC 1001" << std::endl;
    std::cout << std::endl;

    KhuGlobalState newState = state;
    newState.nHeight = 1001;

    std::cout << "   ✅ État copié pour bloc 1001" << std::endl;
    std::cout << std::endl;

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 3: Transaction MINT (+500 KHU)
    // ═══════════════════════════════════════════════════════════
    std::cout << "   📦 TRANSACTION 1: MINT" << std::endl;
    std::cout << "      ┌─────────────────────────────────────" << std::endl;
    std::cout << "      │ Type: MINT (PIV → KHU_T)" << std::endl;
    std::cout << "      │ Amount: 500 KHU" << std::endl;
    std::cout << "      │" << std::endl;

    int64_t mint_amount = 500 * COIN;
    int64_t C_before = newState.C;
    int64_t U_before = newState.U;

    newState.C += mint_amount;
    newState.U += mint_amount;

    std::cout << "      │ C: " << (C_before / COIN) << " → " << (newState.C / COIN)
              << " (+" << (mint_amount / COIN) << ")" << std::endl;
    std::cout << "      │ U: " << (U_before / COIN) << " → " << (newState.U / COIN)
              << " (+" << (mint_amount / COIN) << ")" << std::endl;
    std::cout << "      └─────────────────────────────────────" << std::endl;
    std::cout << std::endl;

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 4: Vérification invariants
    // ═══════════════════════════════════════════════════════════
    std::cout << "   🔍 VÉRIFICATION INVARIANTS" << std::endl;
    std::cout << "      C == U?    " << newState.C / COIN << " == " << newState.U / COIN
              << " → " << (newState.C == newState.U ? "✅ OUI" : "❌ NON") << std::endl;
    std::cout << "      Cr == Ur?  " << newState.Cr / COIN << " == " << newState.Ur / COIN
              << " → " << (newState.Cr == newState.Ur ? "✅ OUI" : "❌ NON") << std::endl;
    std::cout << "      T >= 0?    " << newState.T / COIN << " >= 0"
              << " → " << (newState.T >= 0 ? "✅ OUI" : "❌ NON") << std::endl;
    std::cout << std::endl;

    BOOST_CHECK(newState.CheckInvariants());
    BOOST_CHECK_EQUAL(newState.C, 1500 * COIN);
    BOOST_CHECK_EQUAL(newState.U, 1500 * COIN);

    // ═══════════════════════════════════════════════════════════
    // ÉTAPE 5: État final
    // ═══════════════════════════════════════════════════════════
    PrintState("📊 ÉTAT FINAL (bloc 1001)", newState);

    std::cout << "   💾 État persisté dans LevelDB (simulation)" << std::endl;
    std::cout << std::endl;
    std::cout << "✅ BLOC 1001 CONNECTÉ AVEC SUCCÈS" << std::endl;
    std::cout << std::endl;
}

/**
 * TEST 2: Pipeline avec UNSTAKE (double flux)
 */
BOOST_AUTO_TEST_CASE(pipeline_demo_unstake_double_flux)
{
    std::cout << "\n\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TEST 2: PIPELINE UNSTAKE - DOUBLE FLUX ATOMIQUE        ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    // État initial
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = 5000;
    state.C = 2000 * COIN;
    state.U = 2000 * COIN;
    state.Cr = 100 * COIN;
    state.Ur = 100 * COIN;
    state.T = 50 * COIN;
    state.R_annual = 1200;

    PrintState("📥 ÉTAT INITIAL (bloc 5000)", state);

    // Nouveau bloc
    std::cout << "🔄 TRAITEMENT BLOC 5001" << std::endl;
    std::cout << std::endl;

    KhuGlobalState newState = state;
    newState.nHeight = 5001;

    // Transaction UNSTAKE
    std::cout << "   📦 TRANSACTION 1: UNSTAKE" << std::endl;
    std::cout << "      ┌─────────────────────────────────────" << std::endl;
    std::cout << "      │ Type: UNSTAKE (ZKHU → KHU_T)" << std::endl;
    std::cout << "      │ Principal: 1000 KHU" << std::endl;
    std::cout << "      │ Bonus (Ur_accumulated): 50 KHU" << std::endl;
    std::cout << "      │" << std::endl;

    int64_t principal = 1000 * COIN;
    int64_t bonus = 50 * COIN;

    int64_t C_before = newState.C;
    int64_t U_before = newState.U;
    int64_t Cr_before = newState.Cr;
    int64_t Ur_before = newState.Ur;

    std::cout << "      │ ⚡ DOUBLE FLUX ATOMIQUE:" << std::endl;
    std::cout << "      │" << std::endl;

    // (1) Créer KHU_T pour le bonus
    newState.U += bonus;
    std::cout << "      │   (1) U  += " << (bonus / COIN) << " KHU  →  U = "
              << (newState.U / COIN) << " KHU" << std::endl;

    // (2) Augmenter collateral
    newState.C += bonus;
    std::cout << "      │   (2) C  += " << (bonus / COIN) << " KHU  →  C = "
              << (newState.C / COIN) << " KHU" << std::endl;

    // (3) Consommer reward pool
    newState.Cr -= bonus;
    std::cout << "      │   (3) Cr -= " << (bonus / COIN) << " KHU  →  Cr = "
              << (newState.Cr / COIN) << " KHU" << std::endl;

    // (4) Consommer reward rights
    newState.Ur -= bonus;
    std::cout << "      │   (4) Ur -= " << (bonus / COIN) << " KHU  →  Ur = "
              << (newState.Ur / COIN) << " KHU" << std::endl;

    std::cout << "      │" << std::endl;
    std::cout << "      │ 📊 BILAN:" << std::endl;
    std::cout << "      │   C:  " << (C_before / COIN) << " → " << (newState.C / COIN)
              << " (+" << (bonus / COIN) << ")" << std::endl;
    std::cout << "      │   U:  " << (U_before / COIN) << " → " << (newState.U / COIN)
              << " (+" << (bonus / COIN) << ")" << std::endl;
    std::cout << "      │   Cr: " << (Cr_before / COIN) << " → " << (newState.Cr / COIN)
              << " (-" << (bonus / COIN) << ")" << std::endl;
    std::cout << "      │   Ur: " << (Ur_before / COIN) << " → " << (newState.Ur / COIN)
              << " (-" << (bonus / COIN) << ")" << std::endl;
    std::cout << "      │" << std::endl;
    std::cout << "      │   Total KHU pour staker: " << ((principal + bonus) / COIN) << " KHU" << std::endl;
    std::cout << "      └─────────────────────────────────────" << std::endl;
    std::cout << std::endl;

    // Vérification invariants
    std::cout << "   🔍 VÉRIFICATION INVARIANTS" << std::endl;
    std::cout << "      C == U?    " << newState.C / COIN << " == " << newState.U / COIN
              << " → " << (newState.C == newState.U ? "✅ OUI" : "❌ NON") << std::endl;
    std::cout << "      Cr == Ur?  " << newState.Cr / COIN << " == " << newState.Ur / COIN
              << " → " << (newState.Cr == newState.Ur ? "✅ OUI" : "❌ NON") << std::endl;
    std::cout << std::endl;

    BOOST_CHECK(newState.CheckInvariants());
    BOOST_CHECK_EQUAL(newState.C, 2050 * COIN);
    BOOST_CHECK_EQUAL(newState.U, 2050 * COIN);
    BOOST_CHECK_EQUAL(newState.Cr, 50 * COIN);
    BOOST_CHECK_EQUAL(newState.Ur, 50 * COIN);

    PrintState("📊 ÉTAT FINAL (bloc 5001)", newState);
    std::cout << "✅ BLOC 5001 CONNECTÉ AVEC SUCCÈS" << std::endl;
    std::cout << std::endl;
}

/**
 * TEST 3: Pipeline complet multi-phases sur plusieurs blocs
 */
BOOST_AUTO_TEST_CASE(pipeline_demo_multi_blocks)
{
    std::cout << "\n\n";
    std::cout << "╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  TEST 3: PIPELINE MULTI-BLOCS - TOUTES OPÉRATIONS       ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;

    const uint32_t V6_ACTIVATION = 1000;

    // État genesis
    KhuGlobalState state;
    state.SetNull();
    state.nHeight = V6_ACTIVATION;
    state.R_annual = 1500;
    state.R_MAX_dynamic = 3000;

    PrintState("📥 GENESIS STATE (bloc 1000)", state);

    // ═══════════════════════════════════════════════════════════
    // BLOC 1001: MINT 1000 KHU
    // ═══════════════════════════════════════════════════════════
    std::cout << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    std::cout << "┃ BLOC 1001: MINT 1000 KHU                    ┃" << std::endl;
    std::cout << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl;

    state.nHeight = 1001;
    state.C += 1000 * COIN;
    state.U += 1000 * COIN;

    std::cout << "   🪙 MINT: +1000 KHU" << std::endl;
    std::cout << "      C: 0 → 1000 | U: 0 → 1000" << std::endl;
    BOOST_CHECK(state.CheckInvariants());
    PrintState("   📊 État après MINT", state, 3);

    // ═══════════════════════════════════════════════════════════
    // BLOC 2441: DAILY YIELD (après 1440 blocs)
    // ═══════════════════════════════════════════════════════════
    std::cout << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    std::cout << "┃ BLOC 2441: DAILY YIELD                      ┃" << std::endl;
    std::cout << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl;

    state.nHeight = 2441;

    // Simuler yield: 1000 KHU à 15% = 150/365 = 0.41 KHU/jour
    int64_t daily_yield = (1000 * COIN * 1500) / (10000LL * 365LL);
    state.Cr += daily_yield;
    state.Ur += daily_yield;

    std::cout << "   💰 DAILY YIELD: +" << (daily_yield / COIN) << " KHU" << std::endl;
    std::cout << "      Cr: 0 → " << (state.Cr / COIN) << " | Ur: 0 → " << (state.Ur / COIN) << std::endl;
    BOOST_CHECK(state.CheckInvariants());
    PrintState("   📊 État après YIELD", state, 3);

    // ═══════════════════════════════════════════════════════════
    // BLOC 5000: UNSTAKE avec bonus
    // ═══════════════════════════════════════════════════════════
    std::cout << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    std::cout << "┃ BLOC 5000: UNSTAKE (principal 500 + bonus)  ┃" << std::endl;
    std::cout << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl;

    state.nHeight = 5000;

    // Ajouter plus de yield accumulé
    int64_t accumulated_yield = 10 * COIN;
    state.Cr += accumulated_yield;
    state.Ur += accumulated_yield;

    int64_t bonus = 5 * COIN;
    state.U += bonus;
    state.C += bonus;
    state.Cr -= bonus;
    state.Ur -= bonus;

    std::cout << "   🔓 UNSTAKE: bonus +" << (bonus / COIN) << " KHU" << std::endl;
    std::cout << "      C: " << ((state.C - bonus) / COIN) << " → " << (state.C / COIN) << std::endl;
    std::cout << "      U: " << ((state.U - bonus) / COIN) << " → " << (state.U / COIN) << std::endl;
    std::cout << "      Cr: " << ((state.Cr + bonus) / COIN) << " → " << (state.Cr / COIN) << std::endl;
    std::cout << "      Ur: " << ((state.Ur + bonus) / COIN) << " → " << (state.Ur / COIN) << std::endl;
    BOOST_CHECK(state.CheckInvariants());
    PrintState("   📊 État après UNSTAKE", state, 3);

    // ═══════════════════════════════════════════════════════════
    // BLOC 173800: DAO TREASURY BOUNDARY
    // ═══════════════════════════════════════════════════════════
    std::cout << "┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┓" << std::endl;
    std::cout << "┃ BLOC 173800: DAO TREASURY BOUNDARY          ┃" << std::endl;
    std::cout << "┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┛" << std::endl;

    state.nHeight = 173800;

    // Delta = (U + Ur) / 182500 (2% annual = ~0.00548% daily)
    // Per CLAUDE.md §12.3: T_daily = (U + Ur) / 182500
    int64_t delta_T = (state.U + state.Ur) / 182500;
    state.T += delta_T;

    std::cout << "   🏦 DAO TREASURY: +" << (delta_T / COIN) << " KHU" << std::endl;
    std::cout << "      Formula: (U + Ur) / 182500 = (" << (state.U / COIN)
              << " + " << (state.Ur / COIN) << ") / 182500 = " << (delta_T / COIN) << std::endl;
    std::cout << "      (2% annual rate)" << std::endl;
    std::cout << "      T: " << ((state.T - delta_T) / COIN) << " → " << (state.T / COIN) << std::endl;
    BOOST_CHECK(state.CheckInvariants());
    PrintState("   📊 État après DAO TREASURY", state, 3);

    // ═══════════════════════════════════════════════════════════
    // RÉSUMÉ FINAL
    // ═══════════════════════════════════════════════════════════
    std::cout << "╔═══════════════════════════════════════════════════════════╗" << std::endl;
    std::cout << "║  RÉSUMÉ: ÉVOLUTION SUR " << (state.nHeight - V6_ACTIVATION) << " BLOCS                    ║" << std::endl;
    std::cout << "╚═══════════════════════════════════════════════════════════╝" << std::endl;
    std::cout << std::endl;
    std::cout << "   Bloc 1000  → Genesis:        C=0    U=0    Cr=0   Ur=0   T=0" << std::endl;
    std::cout << "   Bloc 1001  → MINT:           C=1000 U=1000 Cr=0   Ur=0   T=0" << std::endl;
    std::cout << "   Bloc 2441  → YIELD:          C=1000 U=1000 Cr=" << (daily_yield/COIN) << "  Ur=" << (daily_yield/COIN) << "  T=0" << std::endl;
    std::cout << "   Bloc 5000  → UNSTAKE:        C=" << (state.C/COIN) << " U=" << (state.U/COIN) << " Cr=" << (state.Cr/COIN) << " Ur=" << (state.Ur/COIN) << " T=0" << std::endl;
    std::cout << "   Bloc 173800→ DAO TREASURY:   C=" << (state.C/COIN) << " U=" << (state.U/COIN) << " Cr=" << (state.Cr/COIN) << " Ur=" << (state.Ur/COIN) << " T=" << (state.T/COIN) << std::endl;
    std::cout << std::endl;
    std::cout << "   ✅ Invariants respectés à CHAQUE bloc!" << std::endl;
    std::cout << std::endl;
}

BOOST_AUTO_TEST_SUITE_END()
