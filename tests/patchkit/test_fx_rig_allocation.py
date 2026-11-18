"""FX state isolation and bounds without requiring an installed assembler.

Only assembly/linking and the memory transport are substituted: FxRig chooses its
layout, allocates memory and initializes the chain table through its normal path.
"""

import struct
import unittest
from pathlib import Path
from types import SimpleNamespace
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
        # the rig fills the slots of core/fx with the entries of the dispatch object
        empty.symbols = {"_fx_dispatch": (0, True), "_fxs_dispatch": (0, True)}
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

    def test_named_and_free_rows_keep_fallback_but_absent_rows_reserve_nothing(self):
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
            [rig.AUTO_STATE_BYTES, rig.AUTO_STATE_BYTES, 0],
        )
        self.assertEqual(fx.row_word(1, rig.STATE), fx.built.data["_named_state"])
        self.assertIs(fx.built, rig.build((SOURCE,), auto_descs=(None, None)))

    def test_large_owned_bss_fits_without_allocating_absent_rows(self):
        self.obj = object_with_states(64)
        self.obj.data["audio"] = (bytes(384000), 4, True)
        fx = self.make("_large_desc")
        state = fx.row_word(0, rig.STATE)
        self.assertLessEqual(state + 64, rig.DATA_END)
        self.assertIn((state, 64), fx.built.module_data)
        for index in range(1, 4):
            self.assertEqual(fx.built.data[f"_fx_auto_state{index}"], state + 64)

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

    def phrase_object(self):
        self.obj = object_with_states(64)
        self.obj.relocs = [
            pk_sharc.SharcReloc(0, 0, name, 0)
            for name in (
                "_phrase_control_info",
                "_phrase_storage_info",
                "_phrase_status",
            )
        ]

    def test_phrase_capability_supplies_coherent_capture_clock_without_module_identity(
        self,
    ):
        self.phrase_object()
        fx = self.make("_large_desc")
        self.assertTrue(fx.built.phrase_provider)
        fx._phrase_frame()
        fields = rig.PH
        start = rig.nw(fx.built.data["_phrase_control_info"])
        first = [fx.sess.peek(start + i) for i in range(fields["PH_CONTROL_WORDS"])]
        self.assertEqual(
            first[fields["PH_C_SEQ"] // 4], first[fields["PH_C_SEQ_END"] // 4]
        )
        self.assertEqual(first[fields["PH_C_ACTION"] // 4], fields["PH_ACTION_ARM"])
        self.assertTrue(
            first[fields["PH_C_TIMING_FLAGS"] // 4] & fields["PH_TIMING_VALID"]
        )
        checksum = fields["PH_CHECK_SALT"]
        for word in first[: fields["PH_C_CHECK"] // 4]:
            checksum ^= word
        self.assertEqual(first[fields["PH_C_CHECK"] // 4], checksum)
        for _ in range(4):
            fx._phrase_frame()
        later = [fx.sess.peek(start + i) for i in range(fields["PH_CONTROL_WORDS"])]
        self.assertEqual(later[fields["PH_C_MASTER_PHASE"] // 4], 65536)
        self.assertEqual(later[fields["PH_C_BEAT_PHASE"] // 4], 16384)
        self.assertEqual(
            later[fields["PH_C_CMD_SEQ"] // 4], first[fields["PH_C_CMD_SEQ"] // 4]
        )
        self.assertGreater(
            later[fields["PH_C_SEQ"] // 4], first[fields["PH_C_SEQ"] // 4]
        )
        fx.reset()
        fx._phrase_frame()
        self.assertEqual(fx.sess.peek(start + fields["PH_C_SEQ"] // 4), 1)

    def test_phrase_provider_grants_only_lease_and_engine_status_writes(self):
        self.phrase_object()
        fx = self.make("_large_desc")
        fx.built.data.update(
            _fx_scratch=0x2009D000, _fx_save=0x2009D100, _fx_in=0x2009D200
        )
        allowed = fx.allowed(0)

        def writable(at):
            return any(lo <= at and at + 4 <= lo + length for lo, length, _ in allowed)

        self.assertFalse(writable(fx.built.data["_phrase_control_info"]))
        self.assertFalse(writable(fx.built.data["_phrase_storage_info"]))
        status = fx.built.data["_phrase_status"]
        self.assertTrue(writable(status + rig.PH["PH_S_ERROR"]))
        self.assertFalse(writable(status + rig.PH["PH_S_FADE"]))
        self.assertFalse(writable(status + rig.PH["PH_S_ROUTE_FAULT"]))
        for bank in rig.PHRASE_BANKS:
            self.assertTrue(writable(bank))
            self.assertTrue(writable(bank + rig.PHRASE_BANK_BYTES - 4))
            self.assertFalse(writable(bank + rig.PHRASE_BANK_BYTES))
            word = rig.byte_to_normal_word(bank)
            self.assertIsNotNone(word)
            self.assertEqual(fx.sess.peek(word), rig.POISON)
        # External addresses are already byte addresses; they must not be
        # mistaken for the on-chip short-word alias in write-set validation.
        store = SimpleNamespace(
            address=rig.PHRASE_BANKS[0], width=4, pc_sw=fx.built.module_code[0][0]
        )
        fx.sess.stores = [store]
        self.assertEqual(fx.foreign_stores(0), [])
        store.address += rig.PHRASE_BANK_BYTES
        self.assertEqual(len(fx.foreign_stores(0)), 1)

    def test_explicit_phrase_provider_remains_authoritative(self):
        self.phrase_object()
        for name, size in (
            ("_phrase_control_info", rig.PH["PH_CONTROL_WORDS"] * 4),
            ("_phrase_storage_info", rig.PH["PH_STORAGE_BYTES"]),
            ("_phrase_status", rig.PH["PH_STATUS_WORDS"] * 4),
        ):
            self.obj.data[name] = (bytes(size), 4, False)
            self.obj.data_symbols[name] = (name, 0, True)
        fx = self.make("_large_desc")
        self.assertFalse(fx.built.phrase_provider)
        control = rig.nw(fx.built.data["_phrase_control_info"])
        fx.sess.poke(control, 12345)
        fx._phrase_frame()
        self.assertEqual(fx.sess.peek(control), 12345)

    def test_partial_explicit_phrase_provider_is_rejected(self):
        self.phrase_object()
        self.obj.data["status"] = (bytes(128), 4, False)
        self.obj.data_symbols["_phrase_status"] = ("status", 0, True)
        with self.assertRaisesRegex(ValueError, "all three ABI symbols"):
            self.make("_large_desc")


if __name__ == "__main__":
    unittest.main()
