#!/usr/bin/env python3
# Copyright (c) 2025 The PIVX Core developers
# Distributed under the MIT software license, see the accompanying
# file COPYING or http://www.opensource.org/licenses/mit-license.php.
"""Test KHU wallet RPC commands.

This test verifies:
1. khugetinfo - returns wallet and network KHU status
2. khubalance - returns KHU balance
3. khulistunspent - returns KHU UTXOs
4. khurescan - rescans blockchain for KHU coins
5. getkhustate - returns global KHU state (non-wallet)
6. getkhustatecommitment - returns KHU state commitment hash

Note: Full transaction tests (khumint, khuredeem, khusend) require
KHU consensus activation which is not yet enabled on testnet.
"""

from test_framework.test_framework import PivxTestFramework
from test_framework.util import (
    assert_equal,
    assert_greater_than_or_equal,
    assert_raises_rpc_error,
    Decimal,
)


class KHURPCTest(PivxTestFramework):
    def set_test_params(self):
        self.num_nodes = 2
        self.setup_clean_chain = True

    def setup_network(self):
        self.add_nodes(2)
        self.start_node(0)
        self.start_node(1)
        self.connect_nodes(0, 1)
        self.sync_all()

    def run_test(self):
        self.log.info("Testing KHU RPC commands...")

        # Mine some blocks to have a working chain
        self.log.info("Mining initial blocks...")
        self.nodes[0].generate(110)
        self.sync_all()

        # Test khugetinfo
        self.test_khugetinfo()

        # Test khubalance
        self.test_khubalance()

        # Test khulistunspent
        self.test_khulistunspent()

        # Test khurescan
        self.test_khurescan()

        # Test getkhustate (non-wallet RPC)
        self.test_getkhustate()

        # Test getkhustatecommitment
        self.test_getkhustatecommitment()

        self.log.info("All KHU RPC tests passed!")

    def test_khugetinfo(self):
        """Test khugetinfo RPC command."""
        self.log.info("Testing khugetinfo...")

        info = self.nodes[0].khugetinfo()

        # Verify wallet section exists
        assert "wallet" in info, "Missing 'wallet' section in khugetinfo"
        wallet = info["wallet"]
        assert "khu_transparent" in wallet, "Missing khu_transparent"
        assert "khu_staked" in wallet, "Missing khu_staked"
        assert "khu_total" in wallet, "Missing khu_total"
        assert "utxo_count" in wallet, "Missing utxo_count"
        assert "note_count" in wallet, "Missing note_count"

        # Verify network section exists
        assert "network" in info, "Missing 'network' section in khugetinfo"
        network = info["network"]
        assert "height" in network, "Missing height"
        assert "C" in network, "Missing C (collateral)"
        assert "U" in network, "Missing U (supply)"
        assert "Cr" in network, "Missing Cr (reward pool)"
        assert "Ur" in network, "Missing Ur (reward rights)"
        assert "T" in network, "Missing T (DAO treasury)"
        assert "R_annual_pct" in network, "Missing R_annual_pct"
        assert "invariants_ok" in network, "Missing invariants_ok"

        # Verify activation section exists
        assert "activation" in info, "Missing 'activation' section in khugetinfo"
        activation = info["activation"]
        assert "khu_active" in activation, "Missing khu_active"
        assert "activation_height" in activation, "Missing activation_height"

        # Verify values are sensible
        assert_greater_than_or_equal(wallet["khu_transparent"], Decimal("0"))
        assert_greater_than_or_equal(wallet["khu_staked"], Decimal("0"))
        assert_greater_than_or_equal(wallet["utxo_count"], 0)
        assert_greater_than_or_equal(network["height"], 110)

        # Invariants should be OK
        assert_equal(network["invariants_ok"], True)

        self.log.info("khugetinfo OK")

    def test_khubalance(self):
        """Test khubalance RPC command."""
        self.log.info("Testing khubalance...")

        balance = self.nodes[0].khubalance()

        # Verify structure
        assert "transparent" in balance, "Missing transparent"
        assert "staked" in balance, "Missing staked"
        assert "total" in balance, "Missing total"
        assert "pending_yield_estimated" in balance, "Missing pending_yield_estimated"
        assert "utxo_count" in balance, "Missing utxo_count"
        assert "note_count" in balance, "Missing note_count"

        # Initial balance should be 0 (no KHU minted yet)
        assert_equal(balance["transparent"], Decimal("0"))
        assert_equal(balance["staked"], Decimal("0"))
        assert_equal(balance["total"], Decimal("0"))
        assert_equal(balance["utxo_count"], 0)

        self.log.info("khubalance OK")

    def test_khulistunspent(self):
        """Test khulistunspent RPC command."""
        self.log.info("Testing khulistunspent...")

        # Default call
        unspent = self.nodes[0].khulistunspent()
        assert isinstance(unspent, list), "khulistunspent should return a list"
        assert_equal(len(unspent), 0)  # No KHU coins yet

        # Call with minconf/maxconf
        unspent = self.nodes[0].khulistunspent(1, 9999999)
        assert isinstance(unspent, list), "khulistunspent should return a list"
        assert_equal(len(unspent), 0)

        # Edge case: negative minconf (returns empty list, not an error)
        unspent = self.nodes[0].khulistunspent(-1, 100)
        assert isinstance(unspent, list), "khulistunspent should return a list"

        self.log.info("khulistunspent OK")

    def test_khurescan(self):
        """Test khurescan RPC command."""
        self.log.info("Testing khurescan...")

        # Full rescan from genesis
        result = self.nodes[0].khurescan(0)

        assert "scanned_blocks" in result, "Missing scanned_blocks"
        assert "khu_coins_found" in result, "Missing khu_coins_found"
        assert "khu_balance" in result, "Missing khu_balance"
        assert "khu_staked" in result, "Missing khu_staked"

        # Should have scanned at least 110 blocks
        assert_greater_than_or_equal(result["scanned_blocks"], 110)
        assert_equal(result["khu_coins_found"], 0)  # No KHU coins
        assert_equal(result["khu_balance"], Decimal("0"))

        # Partial rescan from block 50
        result = self.nodes[0].khurescan(50)
        assert_greater_than_or_equal(result["scanned_blocks"], 60)

        # Invalid start height should fail
        assert_raises_rpc_error(-8, None, self.nodes[0].khurescan, 999999999)
        assert_raises_rpc_error(-8, None, self.nodes[0].khurescan, -1)

        self.log.info("khurescan OK")

    def test_getkhustate(self):
        """Test getkhustate RPC command (non-wallet).

        Note: getkhustate requires KHU state database to be initialized.
        On a fresh chain without KHU activation, this will fail gracefully.
        """
        self.log.info("Testing getkhustate...")

        try:
            state = self.nodes[0].getkhustate()

            # Verify essential fields if state is available
            assert "height" in state, "Missing height"
            assert "C" in state, "Missing C (collateral)"
            assert "U" in state, "Missing U (supply)"
            assert "Cr" in state, "Missing Cr (reward pool)"
            assert "Ur" in state, "Missing Ur (reward rights)"
            assert "T" in state, "Missing T (DAO treasury)"
            assert "R_annual" in state, "Missing R_annual"
            assert "R_MAX_dynamic" in state, "Missing R_MAX_dynamic"

            # Initial state should have 0 collateral/supply
            assert_equal(state["C"], 0)
            assert_equal(state["U"], 0)
            assert_equal(state["Cr"], 0)
            assert_equal(state["Ur"], 0)
            assert_equal(state["T"], 0)

            self.log.info("getkhustate OK (KHU state available)")
        except Exception as e:
            # KHU state not initialized is expected on fresh test chains
            if "Unable to load KHU state" in str(e):
                self.log.info("getkhustate OK (KHU state not yet initialized - expected)")
            else:
                raise

    def test_getkhustatecommitment(self):
        """Test getkhustatecommitment RPC command.

        Note: getkhustatecommitment requires KHU state database to be initialized
        and a height argument. On a fresh chain without KHU activation, this
        will fail gracefully.
        """
        self.log.info("Testing getkhustatecommitment...")

        # Get current height
        current_height = self.nodes[0].getblockcount()

        try:
            commitment = self.nodes[0].getkhustatecommitment(current_height)

            assert "height" in commitment, "Missing height"
            assert "hashState" in commitment, "Missing hashState"

            # State hash should be a 64-character hex string
            assert len(commitment["hashState"]) == 64, "Invalid hashState length"
            # Verify it's valid hex
            int(commitment["hashState"], 16)

            self.log.info("getkhustatecommitment OK (KHU state available)")
        except Exception as e:
            # KHU state/commitment not initialized is expected on fresh test chains
            error_msg = str(e)
            if any(x in error_msg for x in ["Unable to load KHU state", "KHU state not found", "No commitment found"]):
                self.log.info("getkhustatecommitment OK (KHU state not yet initialized - expected)")
            else:
                raise


if __name__ == '__main__':
    KHURPCTest().main()
