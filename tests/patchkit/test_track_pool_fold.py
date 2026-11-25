"""Standalone DTII helper map and pre-send stereo bus proof."""

import struct

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc

BUS = 0x252DF8
RING = 0x200A0000
SUFFIX = 0x854
SALT = 0x22DCA71B
DATA = 0x20090000


def words(routes):
    header = 0x544B0000
    packed = 0
    for helper, owner in routes.items():
        header |= 1 << helper
        packed |= owner << (4 * helper)
    low, high = packed & 0xFFFFFFFF, packed >> 32
    return header, low, high, header ^ low ^ high ^ SALT


def sample(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


@pytest.fixture(scope="module")
def fold():
    source = (
        REPO_ROOT / "image-modules/adapters/track-keyboard/src/dsp/track-pool.s"
    ).read_text()
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_fold;
_test_fold:
    CALL _tk_pool_receive;
    I3 = R8;
    CALL _tk_pool_receive;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    RTS;
.GLOBAL _test_quiet;
_test_quiet:
    CALL _tk_pool_fold;
    CALL _tk_pool_quiet_reply;
    RTS;
"""
    obj = pk_sharc.assemble(source)
    symbols = {name: DATA + offset for name, (_, offset, _) in obj.data_symbols.items()}
    return pk_sharc.link(obj, 0xB90000, symbols), symbols


def run(fold, first, second):
    linked, symbols = fold
    payload = {symbols["_tk_pool_quiet_phase"] // 4: 0}
    for i, v in enumerate(first):
        payload[(RING + SUFFIX) // 4 + i] = v
    for i, v in enumerate(second):
        payload[(RING + 0x1000 + SUFFIX) // 4 + i] = v
    original = [
        sample((track + 1) * (1 if i < 32 else -2) / 64)
        for track in range(16)
        for i in range(64)
    ]
    payload.update({BUS // 4 + i: w for i, w in enumerate(original)})
    context = {"I1": 0x200C0010, "L1": 16, "M5": 0x10, "M6": 9, "R7": 0x1EA7}
    result = pk_sharc.run_call(
        linked,
        "_test_fold",
        regs={"I3": RING, "R8": RING + 0x1000, **context},
        dm_words=payload,
        reads={"buses": (BUS // 4, len(original))},
        read_regs=tuple(context),
        max_steps=8000,
    )
    assert result["halt"]["reason"] == "breakpoint", result["halt"]
    assert {reg: result["regs"][reg] for reg in context} == context
    return original, result["buses"]


@pytest.mark.parametrize(
    "malformed",
    [words({1: 2, 2: 1}), (0x544B0002, 0, 0, 0)],
    ids=["checksum-valid-cycle", "bad-checksum"],
)
def test_invalid_map_cannot_unroute_existing_helpers(fold, malformed):
    original, processed = run(fold, words({1: 0, 9: 0, 15: 3}), malformed)
    expected = original.copy()
    for owner, source_tracks in [(0, (0, 1, 9)), (3, (3, 15))]:
        value = sum(track + 1 for track in source_tracks) / 64
        expected[64 * owner : 64 * (owner + 1)] = [sample(value)] * 32 + [
            sample(-2 * value)
        ] * 32
    for helper in (1, 9, 15):
        expected[64 * helper : 64 * (helper + 1)] = [0] * 64
    assert processed == expected


def test_valid_empty_map_restores_unmodified_buses(fold):
    original, processed = run(fold, words({9: 0}), words({}))
    assert processed == original


def test_quiet_reply_excludes_pending_cursors_and_audible_filter_tails(fold):
    linked, symbols = fold
    dm = {symbols[name] // 4: 0 for name in symbols}
    dm[symbols["_tk_pool_quiet_phase"] // 4] = 7
    dm.update({BUS // 4 + i: 0 for i in range(16 * 64)})
    dm[BUS // 4 + 4 * 64 + 37] = sample(0.001)  # Right channel only.
    dm[BUS // 4 + 5 * 64 + 1] = 0x80000000  # Negative zero is silence.
    for voice in range(32):
        dm[(0x241484 + 0x1D8 * voice) // 4] = 0
    dm[(0x241484 + 0x1D8 * 6) // 4] = 1
    dm[(0x241484 + 0x1D8 * 31) // 4] = 0x10000  # Odd-channel pending cursor.
    reply = DATA + 0x1000
    output = pk_sharc.run_call(
        linked,
        "_test_quiet",
        regs={"I4": reply},
        dm_words=dm,
        reads={"status": ((reply + 0x60) // 4, 2)},
        max_steps=5000,
    )
    assert output["halt"]["reason"] == "breakpoint", output["halt"]
    mask = 0xFFFF ^ ((1 << 3) | (1 << 4) | (1 << 15))
    assert output["status"] == [(mask << 16) | 0x4B51, ((mask ^ 0x32F7 ^ 1) << 16) | 1]
