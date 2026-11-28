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
        REPO_ROOT / "runtime/platform/track-keyboard/src/dsp/track-pool.s"
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
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_fold;
    CALL _tk_pool_quiet_reply;
    RTS;
"""
    obj = pk_sharc.assemble(source)
    symbols = {name: DATA + offset for name, (_, offset, _) in obj.data_symbols.items()}
    # core/dsp-voice's stock-chord receiver is optional (module.toml): 0, as patchkit links it without that module
    return pk_sharc.link(obj, 0xB90000, {**symbols, "_cs_receive": 0}), symbols


def run(fold, first, second):
    linked, symbols = fold
    # every block inspects two tracks for the quiet mask: the module starts with zeroed data, and the cursors are idle
    payload = {symbols[name] // 4: 0 for name in symbols}
    payload.update({(0x241484 + 0x1D8 * voice) // 4: 0 for voice in range(32)})
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
    # a round of eight blocks, two tracks a block: the eighth publishes
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
        max_steps=8000,
    )
    assert output["halt"]["reason"] == "breakpoint", output["halt"]
    mask = 0xFFFF ^ ((1 << 3) | (1 << 4) | (1 << 15))
    assert output["status"] == [(mask << 16) | 0x4B51, ((mask ^ 0x32F7 ^ 1) << 16) | 1]


def quiet_session(fold):
    """A core that keeps its memory between calls: silent buses, idle cursors, no helper map."""
    linked, symbols = fold
    session = pk_sharc.CallSession([linked], 0xB9F000)
    for name in symbols:
        session.poke(symbols[name] // 4, 0)
    for i in range(16 * 64):
        session.poke(BUS // 4 + i, 0)
    for voice in range(32):
        session.poke((0x241484 + 0x1D8 * voice) // 4, 0)
        session.poke((0x241484 + 0x1D8 * voice) // 4 + 0x76, 0)
    return session, symbols


def test_quiet_scan_is_spread_two_tracks_a_block(fold):
    session, symbols = quiet_session(fold)
    session.poke(symbols["_tk_pool_quiet_phase"] // 4, 7)
    mask_at = symbols["_tk_pool_quiet_mask"] // 4
    serial_at = symbols["_tk_pool_quiet_serial"] // 4
    costs = []
    published = []
    # Three rounds. Track t sounds only in the block of round 1 that inspects it (phase t // 2), on one side; track 9's
    # cursor is active through round 2.
    for block in range(24):
        phase = block % 8
        for track in (2 * phase, 2 * phase + 1):
            loud = 8 <= block < 16 and track in (2, 7, 12)
            session.poke(
                BUS // 4 + 64 * track + 5 + 32 * (track % 2),
                sample(0.01) if loud else 0,
            )
        session.poke((0x241484 + 0x1D8 * 18) // 4, 1 if 8 <= block < 16 else 0)
        result = session.call("_tk_pool_fold", {}, max_steps=5000)
        assert result["halt"]["reason"] == "breakpoint", result["halt"]
        costs.append(result["instructions"])
        published.append((session.peek(serial_at), session.peek(mask_at)))
    # The mask and its serial move in the last block of a round and nowhere else.
    assert [p for i, p in enumerate(published) if i % 8 == 7] == [
        (1, 0xFFFF),
        (2, 0xFFFF ^ ((1 << 2) | (1 << 7) | (1 << 12) | (1 << 9))),
        (3, 0xFFFF),
    ]
    for i, p in enumerate(published[1:], 1):
        assert p == (published[i - 1] if i % 8 != 7 else p)
    assert published[0] == (0, 0)
    # No block carries more than two tracks' share: each of the two is its 64 bus words at four instructions and under 20
    # around them, plus the save and restore of the routine. One block inspecting all sixteen took over 4,300.
    share = 2 * (64 * 4 + 20) + 110
    assert max(costs) <= share, costs
    assert max(costs) - min(costs) <= 2 * (64 * 4 + 20), costs


def test_a_round_that_starts_late_marks_the_tracks_it_missed_as_not_quiet(fold):
    session, symbols = quiet_session(fold)
    # the module starts with phase 0: its first block is phase 1, so tracks 0 and 1 are not inspected in the first round
    for _ in range(7):
        session.call("_tk_pool_fold", {}, max_steps=5000)
    assert session.peek(symbols["_tk_pool_quiet_serial"] // 4) == 1
    assert session.peek(symbols["_tk_pool_quiet_mask"] // 4) == 0xFFFC
    for _ in range(8):
        session.call("_tk_pool_fold", {}, max_steps=5000)
    assert session.peek(symbols["_tk_pool_quiet_serial"] // 4) == 2
    assert session.peek(symbols["_tk_pool_quiet_mask"] // 4) == 0xFFFF
