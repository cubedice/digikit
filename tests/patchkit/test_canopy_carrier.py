"""CANOPY through the real voice adapter; stock AMP is deliberately not simulated."""

from dataclasses import replace
from pathlib import Path

import pytest
from support import dsp_voice_rig as dv
from support.machine_rig import MachineRig

from tools.patchkit import pk_conform

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "modules/machines/canopy/src/dsp"


@pytest.fixture(scope="module")
def spec():
    base = pk_conform.spec("digikit/pluck", "dt2")
    assert base is not None
    return replace(
        base,
        module="canopy-carrier-probe",
        moddir=SOURCE,
        sources=(ROOT / "tests/support/canopy_voice_probe.s",),
        entry="_cp_voice_probe",
        note_on=None,
        desc=None,
        params=(),
    )


def make_rig(spec, track=0):
    rig = MachineRig(spec.module, "dt2", spec=spec, track=track, record_stores=True)
    # Runtime initialization is still absent: seed only the module's declared BSS.
    for address, blob, is_bss in rig.built.inits:
        if is_bss:
            for i in range(len(blob) // 4):
                rig.sess.poke(address // 4 + i, 0)
    return rig


def event(rig, name, token, note=60, velocity=100):
    result = rig.sess.call(
        "_cp_" + name,
        {
            "R4": rig.built.data["_cp_banks"] + 256 * rig.track,
            "R8": token,
            "R12": note,
            "R9": velocity,
            "M5": 0,
            "M6": 1,
            "L0": 0,
            "L1": 0,
            "L2": 0,
            "MODE1": 0x1011800,
        },
        max_steps=2000,
        read_regs=("R0",),
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert result["regs"]["R0"] == 1


def block(rig):
    result = rig.block()
    assert result.returned, result.halt
    assert not result.context_bad and not result.foreign and not result.faults
    return result.samples


def carrier(rig, *, active=None, owned=1, trigger=0, release=0, track=None):
    bank = rig.built.data["_mi_carrier_active"] // 4
    before = [rig.sess.peek(bank + i) for i in range(16)]
    selected = rig.track if track is None else track
    if active is None:
        active = rig.sess.peek(rig.built.data["_cp_probe_active"] // 4)
    result = rig.sess.call(
        "_mi_carrier_step",
        {
            "R4": selected,
            "R8": active,
            "R10": owned,
            "R12": trigger,
            "R9": release,
            "M5": 0,
            "L0": 0,
            "MODE1": 0x1011800,
        },
        max_steps=100,
        read_regs=("R0", "R1", "R2", "R3", "I0", "M5"),
    )
    assert result["halt"]["reason"] == "breakpoint", result
    after = [rig.sess.peek(bank + i) for i in range(16)]
    assert all(before[i] == after[i] for i in range(16) if i != selected)
    return result["regs"]["R0"], result["regs"]["R1"]


@pytest.mark.parametrize("track", [0, 15])
def test_idle_raw_render_does_not_establish_a_stock_carrier(spec, track):
    rig = make_rig(spec, track)
    event(rig, "note_on", 1, velocity=127)
    assert rig.read_rec_word(2 * track, 0x1B8) == 0
    samples = block(rig)
    assert any(abs(x) > 0.00001 for x in samples)
    assert rig.read_rec_word(2 * track, 0x1B8) == 0
    assert rig.sess.peek(rig.built.data["_cp_probe_active"] // 4) == 1
    # This is pre-AMP audio. No claim about final output follows from it.


def test_stock_retrigger_and_velocity_changes_do_not_reset_kernel_state(spec):
    rig, reference = make_rig(spec), make_rig(spec)
    for r in (rig, reference):
        event(r, "note_on", 1, 60, 20)
        block(r)
        event(r, "note_on", 2, 67, 127)
    rig.set_note(84, vel=1)
    rig.trigger()
    assert block(rig) == block(reference)
    assert rig.read_rec_word(0, 0x1B8) == 1  # adapter does not own carrier lifetime
    event(rig, "note_off", 1)
    block(rig)
    assert rig.sess.peek(rig.built.data["_cp_probe_active"] // 4) == 1
    event(rig, "note_off", 2)
    for _ in range(40):
        samples = block(rig)
    assert samples == (0.0,) * 32
    assert rig.sess.peek(rig.built.data["_cp_probe_active"] // 4) == 0
    assert rig.read_rec_word(0, 0x1B8) == 1  # QUIET still needs an explicit V2 handoff


@pytest.mark.parametrize("track", [0, 15])
def test_stereo_copy_does_not_advance_canopy_twice(spec, track):
    rig = make_rig(spec, track)
    event(rig, "note_on", 1, 60, 100)
    left = block(rig)
    bank = rig.built.data["_cp_banks"] // 4 + 64 * track
    before = tuple(rig.sess.peek(bank + i) for i in range(64))
    rig.voice = 2 * track + 1
    assert block(rig) == left
    assert tuple(rig.sess.peek(bank + i) for i in range(64)) == before
    rig.voice = 2 * track
    assert block(rig) != left


def test_quiet_bank_accepts_new_notes_without_stock_trigger(spec):
    rig = make_rig(spec)
    event(rig, "note_on", 1)
    block(rig)
    event(rig, "note_off", 1)
    for _ in range(40):
        block(rig)
    assert rig.sess.peek(rig.built.data["_cp_probe_active"] // 4) == 0
    event(rig, "note_on", 2, 72)
    assert any(abs(x) > 0.00001 for x in block(rig))
    assert rig.read_rec_word(0, 0x1B8) == 0


def test_carrier_edges_follow_kernel_tails_not_keys(spec):
    rig = make_rig(spec)
    assert carrier(rig, active=0, trigger=1) == (0, 0)
    event(rig, "note_on", 1)
    block(rig)
    assert carrier(rig) == (1, 0)
    event(rig, "note_on", 2, 67)
    block(rig)
    assert carrier(rig, trigger=1, release=1) == (0, 0)
    event(rig, "note_off", 1)
    block(rig)
    assert carrier(rig, release=1) == (0, 0)
    event(rig, "note_off", 2)
    block(rig)
    assert carrier(rig, release=1) == (0, 0)
    edges = []
    for _ in range(80):
        block(rig)
        edges.append(carrier(rig))
    assert edges.count((0, 1)) == 1
    assert all(edge in ((0, 0), (0, 1)) for edge in edges)
    event(rig, "note_on", 3, 72)
    block(rig)
    assert carrier(rig) == (1, 0)


def test_carrier_ownership_is_per_track_and_stock_flags_pass_through(spec):
    rig = make_rig(spec)
    assert carrier(rig, track=0, active=1) == (1, 0)
    assert carrier(rig, track=15, active=1) == (1, 0)
    assert carrier(rig, track=0, active=0) == (0, 1)
    assert carrier(rig, track=15, active=1) == (0, 0)
    assert carrier(rig, track=15, active=1, owned=0, trigger=7, release=9) == (7, 9)
    assert carrier(rig, track=15, active=1) == (1, 0)
    assert carrier(rig, track=16, active=1, trigger=7, release=9) == (7, 9)
    assert carrier(rig, track=0xFFFFFFFF, active=1, trigger=7, release=9) == (7, 9)


def pair_call(rig, entry, index, stamp, active=1, owned=1, trigger=7, release=9):
    result = rig.sess.call(
        "_mi_carrier_" + entry,
        {
            "R4": index,
            "R8": active,
            "R10": owned,
            "R11": stamp,
            "R12": trigger,
            "R9": release,
            "M5": 0,
            "L0": 0,
            "MODE1": 0x1011800,
        },
        max_steps=150,
        read_regs=("R0", "R1", "R2"),
    )
    assert result["halt"]["reason"] == "breakpoint", result
    return tuple(result["regs"][name] for name in ("R0", "R1", "R2"))


@pytest.mark.parametrize("track", [0, 15])
def test_stereo_carrier_pair_is_shared_and_expires_each_block(spec, track):
    rig = make_rig(spec, track)
    # Initial zero stamp cannot turn zeroed BSS into an owned pair.
    assert pair_call(rig, "flags", 2 * track, 0) == (7, 9, 0)
    pair_call(rig, "publish", track, 0)
    for voice in (2 * track + 1, 2 * track, 2 * track + 1):
        assert pair_call(rig, "flags", voice, 0) == (1, 0, 1)
    assert pair_call(rig, "flags", 2 * track, 1) == (7, 9, 0)
    pair_call(rig, "publish", track, 1)
    for voice in (2 * track, 2 * track + 1):
        assert pair_call(rig, "flags", voice, 1) == (0, 0, 1)
    pair_call(rig, "publish", track, 2, active=0)
    for voice in (2 * track, 2 * track + 1):
        assert pair_call(rig, "flags", voice, 2) == (0, 1, 1)
    pair_call(rig, "publish", track, 3, owned=0)
    assert pair_call(rig, "flags", 2 * track, 3) == (7, 9, 0)
    assert pair_call(rig, "flags", 2 * track + 1, 3, trigger=5, release=6) == (5, 6, 0)
    assert pair_call(rig, "flags", 32, 3) == (7, 9, 0)
    assert pair_call(rig, "flags", 0xFFFFFFFF, 3) == (7, 9, 0)


@pytest.mark.parametrize("simd", [False, True])
@pytest.mark.parametrize("voice", [0, 1, 30, 31])
def test_envelope_hook_changes_only_owned_flags_and_preserves_context(
    spec, voice, simd
):
    rig = make_rig(spec)
    base, stamp = 0x20096000, 51
    rig.sess.poke(rig.built.data["_mi_envelope_records"] // 4, base)
    rig.sess.poke(rig.built.data["_mi_carrier_stamp"] // 4, stamp)
    pair_call(rig, "publish", voice // 2, stamp)
    regs = {f"R{i}": 0x12340000 + i for i in range(16)}
    regs.update({f"S{i}": 0x56780000 + i for i in range(16)})
    regs.update(
        {
            "R4": base + 44 * voice,
            "I0": 0x20097000,
            "I6": dv.STACK,
            "I7": dv.STACK,
            "L0": 64,
            "M5": 19,
            "MODE1": 0x1011800 | (0x200000 if simd else 0),
            "ASTATX": 0x1234,
            "ASTATY": 0x4321,
            "STKYX": 0x15,
            "STKYY": 0x26,
        }
    )
    for offset, value in ((0, 0x1357), (1, 7), (2, 9), (3, 32), (4, 0x2468)):
        rig.sess.poke(dv.STACK // 4 + offset, value)
    rig.sess.runner.breakpoints |= {rig.sess.symbols["_mi_envelope_next"]}
    result = rig.sess.call(
        "_mi_envelope_entry", regs, max_steps=400, read_regs=tuple(regs)
    )
    assert result["halt"]["reason"] == "breakpoint", result
    for name, value in regs.items():
        assert result["regs"][name] == (value - 64 if name == "I7" else value), name
    assert [rig.sess.peek(dv.STACK // 4 + i) for i in range(5)] == [
        0x1357,
        1,
        0,
        32,
        0x2468,
    ]


@pytest.mark.parametrize(
    "kind", ["missing", "stale", "unaligned", "below", "above", "disabled"]
)
def test_envelope_hook_falls_through_without_changing_stock_arguments(spec, kind):
    rig = make_rig(spec)
    base, stamp = 0x20096000, 51
    rig.sess.poke(
        rig.built.data["_mi_envelope_records"] // 4, 0 if kind == "disabled" else base
    )
    rig.sess.poke(rig.built.data["_mi_carrier_stamp"] // 4, stamp)
    if kind != "missing":
        pair_call(rig, "publish", 0, stamp - (kind == "stale"))
    address = {"unaligned": base + 1, "below": base - 44, "above": base + 1408}.get(
        kind, base
    )
    rig.sess.poke(dv.STACK // 4 + 1, 7)
    rig.sess.poke(dv.STACK // 4 + 2, 9)
    result = rig.sess.call(
        "_mi_envelope_entry",
        {
            "R4": address,
            "I6": dv.STACK,
            "I7": dv.STACK,
            "M5": 0,
            "L0": 0,
            "MODE1": 0x1011800,
        },
        max_steps=400,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert (rig.sess.peek(dv.STACK // 4 + 1), rig.sess.peek(dv.STACK // 4 + 2)) == (
        7,
        9,
    )


def frame(rig):
    result = rig.sess.call(
        "_instrument_block",
        {"I3": 0x20098000, "MODE1": 0x1011800},
        max_steps=500,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    return rig.sess.peek(rig.built.data["_mi_carrier_stamp"] // 4)


@pytest.mark.parametrize("track", [0, 15])
@pytest.mark.parametrize("reset", [False, True])
def test_render_publishes_once_for_both_voices_and_keeps_reset_tails(
    spec, track, reset
):
    rig = make_rig(
        replace(spec, module="canopy-render-proof", entry="_cp_voice_render"), track
    )
    ready = rig.built.data["_cp_ready"] // 4 + track
    rig.sess.poke(ready, 1)
    event(rig, "note_on", 1)
    stamp = frame(rig)
    left = block(rig)
    assert any(abs(x) > 0.00001 for x in left)
    assert pair_call(rig, "flags", track * 2, stamp) == (1, 0, 1)
    rig.voice = track * 2 + 1
    assert block(rig) == left
    assert pair_call(rig, "flags", track * 2 + 1, stamp) == (1, 0, 1)
    # The real envelope interposer consumes the render's publication, with no
    # harness copying of carrier flags or stamps between these boundaries.
    base = 0x20096000
    rig.sess.poke(rig.built.data["_mi_envelope_records"] // 4, base)
    for voice in (track * 2, track * 2 + 1):
        rig.sess.poke(dv.STACK // 4 + 1, 7)
        rig.sess.poke(dv.STACK // 4 + 2, 9)
        result = rig.sess.call(
            "_mi_envelope_entry",
            {
                "R4": base + 44 * voice,
                "I6": dv.STACK,
                "I7": dv.STACK,
                "MODE1": 0x1011800,
            },
            max_steps=400,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert (rig.sess.peek(dv.STACK // 4 + 1), rig.sess.peek(dv.STACK // 4 + 2)) == (
            1,
            0,
        )
    rig.voice = track * 2
    # Configuration loss must not relinquish a sounding tail mid-release.
    if reset:
        packet = 0x20098000
        rig.sess.poke(packet // 4 + 2, 0)  # same epoch, track-local reset
        rig.sess.poke(packet // 4 + 8, track)
        result = rig.sess.call(
            "_instrument_reset",
            {"R4": packet, "M6": 1, "L0": 0, "L1": 0, "MODE1": 0x1011800},
            max_steps=500,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert rig.sess.peek(ready) == 0
    else:
        rig.sess.poke(ready, 0)
        event(rig, "note_off", 1)
    edges = []
    for _ in range(80):
        stamp = frame(rig)
        samples = block(rig)
        edge = pair_call(rig, "flags", track * 2, stamp)
        if edge == (0, 1, 1):
            assert samples == (0.0,) * 32
        edges.append(edge)
    assert edges[0] == (0, 0, 1)
    assert edges.count((0, 1, 1)) == 1
    assert edges[-1] == (7, 9, 0)
    rig.sess.poke(ready, 1)
    event(rig, "note_on", 2, 72)
    stamp = frame(rig)
    block(rig)
    assert pair_call(rig, "flags", track * 2, stamp) == (1, 0, 1)


def test_frame_expiry_clears_skipped_tracks_even_at_stamp_wrap(spec):
    rig = make_rig(spec)
    # An ancient zero-stamp pair must not revive on the next counter wrap.
    pair_call(rig, "publish", 15, 0)
    rig.sess.poke(rig.built.data["_mi_carrier_stamp"] // 4, 0xFFFFFFFF)
    assert frame(rig) == 0
    assert pair_call(rig, "flags", 30, 0) == (7, 9, 0)
    # Expiry leaves ACTIVE history intact, so the next render holds, not wakes.
    pair_call(rig, "publish", 15, 0)
    assert pair_call(rig, "flags", 30, 0) == (0, 0, 1)


@pytest.mark.parametrize("simd", [False, True])
def test_frame_callback_preserves_live_registers(spec, simd):
    rig = make_rig(spec)
    regs = {f"R{i}": 0x12340000 + i for i in range(16)}
    regs.update({f"S{i}": 0x56780000 + i for i in range(16)})
    regs.update(
        I0=0x20096000,
        I1=0x20097000,
        I2=0x20098000,
        I3=0x20099000,
        L0=64,
        L1=128,
        L2=256,
        M5=19,
        M6=23,
        MODE1=0x1011800 | (0x200000 if simd else 0),
        ASTATX=0x1234,
        ASTATY=0x4321,
    )
    result = rig.sess.call(
        "_instrument_block", regs, max_steps=500, read_regs=tuple(regs)
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert result["regs"] == regs
    assert rig.sess.peek(rig.built.data["_mi_carrier_stamp"] // 4) == 1
