"""FX state isolation and bounds without requiring an installed assembler.

Only assembly/linking and the memory transport are substituted: FxRig chooses its
layout, allocates memory and initializes the chain table through its normal path.
"""

import struct
import unittest
from pathlib import Path
from unittest.mock import patch

from support import fx_rig as rig

from tools.patchkit import pk_sharc

SOURCE = Path("allocation-fixture.s")
LARGE_STATE = 96064


def object_with_states(first=LARGE_STATE, second=4):
    obj = pk_sharc.SharcObject("", b"\0\0", {}, [], [])

    # Descriptors need not be the first symbol or start at section offset zero.
    def desc(size):
        return struct.pack("<7I", 0x444B4658, 1, 0, size, 0, 0, 16)

    obj.data = {
        "descriptors": (b"\0" * 8 + desc(first) + desc(second), 4, False),
        "named": (bytes(4), 4, True),
    }
    obj.data_symbols = {
        "_large_desc": ("descriptors", 8, True),
        "_small_desc": ("descriptors", 36, True),
        "_named_state": ("named", 0, True),
    }
    obj.symbols = {"_process": (0, True)}
    return obj


class MemorySession:
    def __init__(self, *_args, **_kwargs):
        self.words = {}

    def poke(self, address, value):
        self.words[address] = value

    def peek(self, address):
        return self.words.get(address)


class FxRigAllocationTests(unittest.TestCase):
    def setUp(self):
        self.obj = object_with_states()
        empty = pk_sharc.SharcObject("", b"\0\0", {}, [], [])
        patches = [
            patch.object(rig, "_BUILT", {}),
            patch.object(
                rig,
                "_assemble",
                side_effect=lambda path: self.obj if path == SOURCE else empty,
            ),
            patch.object(pk_sharc, "link", return_value=object()),
            patch.object(pk_sharc, "CallSession", MemorySession),
        ]
        for p in patches:
            p.start()
            self.addCleanup(p.stop)

    def make(self, *descriptors):
        return rig.FxRig(
            [rig.Row("_process", "", desc=desc) for desc in descriptors],
            sources=(SOURCE,),
        )

    def test_large_state_and_next_row_remain_separate_through_initialization(self):
        fx = self.make("_large_desc", "_small_desc")
        first = fx.row_word(0, rig.STATE)
        second = fx.row_word(1, rig.STATE)
        self.assertEqual(second - first, LARGE_STATE)
        self.assertEqual(fx.state_word(0, LARGE_STATE // 4 - 1), rig.POISON)
        self.assertEqual(fx.state_word(1, 0), rig.POISON)
        # Initializing all state promised by the descriptor cannot erase row 1.
        for k in range(fx.desc_words("_large_desc")[3] // 4):
            fx.sess.poke(rig.nw(first) + k, 0)
        self.assertEqual(fx.state_word(1, 0), rig.POISON)
        self.assertIn((first, LARGE_STATE), fx.built.module_data)

    def test_cache_keeps_distinct_descriptor_orders_and_reuses_equal_layout(self):
        first = self.make("_large_desc", "_small_desc")
        other = self.make("_small_desc", "_large_desc")
        again = self.make("_large_desc", "_small_desc")
        self.assertIs(first.built, again.built)
        self.assertIsNot(first.built, other.built)
        self.assertEqual(other.row_word(1, rig.STATE) - other.row_word(0, rig.STATE), 4)
        self.assertIsNot(first.sess, again.sess)

    def test_named_and_free_rows_keep_the_legacy_fallback(self):
        fx = rig.FxRig(
            [
                rig.Row("", "", desc="unused_missing_descriptor"),
                rig.Row("_process", "_named_state", desc="_small_desc"),
            ],
            sources=(SOURCE,),
        )
        addresses = [fx.built.data[f"_fx_auto_state{i}"] for i in range(4)]
        self.assertEqual(
            [b - a for a, b in zip(addresses, addresses[1:], strict=False)],
            [rig.AUTO_STATE_BYTES] * 3,
        )
        self.assertEqual(fx.row_word(1, rig.STATE), fx.built.data["_named_state"])
        self.assertIs(fx.built, rig.build((SOURCE,)))

    def test_invalid_or_unavailable_descriptor_fails_before_linking(self):
        for size in (3, 0xFFFFFFFF, rig.DATA_END - rig.DATA_BASE + 4):
            with self.subTest(size=size):
                self.obj = object_with_states(size)
                with self.assertRaisesRegex(ValueError, "state_bytes"):
                    self.make("_large_desc")
        with self.assertRaisesRegex(ValueError, "not found"):
            self.make("_missing")
        pk_sharc.link.assert_not_called()

    def test_combined_state_limit_includes_module_data_and_fallback_rows(self):
        # Each state fits alone, but the pair must fail instead of overlapping L2.
        self.obj = object_with_states(200000, 200000)
        with self.assertRaisesRegex(RuntimeError, "bytes of L2"):
            self.make("_large_desc", "_small_desc")
        # Even a single state equal to the region leaves no room for its descriptor.
        self.obj = object_with_states(rig.DATA_END - rig.DATA_BASE)
        with self.assertRaisesRegex(RuntimeError, "bytes of L2"):
            self.make("_large_desc")
        pk_sharc.link.assert_not_called()

    def test_zero_state_is_valid_and_reserves_no_poisoned_words(self):
        self.obj = object_with_states(0)
        fx = self.make("_large_desc", "_small_desc")
        self.assertEqual(fx.row_word(0, rig.STATE), fx.row_word(1, rig.STATE))
        self.assertEqual(fx.state_word(1, 0), rig.POISON)


if __name__ == "__main__":
    unittest.main()
