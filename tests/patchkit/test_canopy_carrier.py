"""CANOPY through the real voice adapter; stock AMP is deliberately not simulated."""

from dataclasses import replace
from pathlib import Path

import pytest
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
