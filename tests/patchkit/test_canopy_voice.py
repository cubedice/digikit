"""CANOPY's stage-1 kernel on SHARC; no stock AMP or final-output claim."""

import math
import struct
from pathlib import Path

import pytest

from tools.patchkit import pk_sharc

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "modules/machines/canopy/src/dsp"
BANK, OUTPUT = 0x20040000, 0x20041000


def floats(words):
    return [struct.unpack("<f", struct.pack("<I", word))[0] for word in words]


@pytest.fixture(scope="module")
def engine():
    source = "\n".join((SOURCE / name).read_text() for name in ("voice.s", "tuning.s"))
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _cp_render_many;
_cp_render_many:
    DM(_cp_test_count) = R10;
    DM(_cp_test_bank) = R4;
_cp_test_loop:
    R4 = DM(_cp_test_bank);
    CALL _cp_render;
    R1 = 128;
    R8 = R8 + R1;
    R1 = DM(_cp_test_count);
    R1 = R1 - 1;
    DM(_cp_test_count) = R1;
    IF NE JUMP _cp_test_loop;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _cp_test_count;
.GLOBAL _cp_test_bank;
.VAR _cp_test_count = 0;
.VAR _cp_test_bank = 0;
"""
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20020000
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob)
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(obj, 0xB90000, symbols)

    def session():
        dm = dict(initial)
        dm.update({BANK // 4 + i: 0 for i in range(32)})
        dm.update({OUTPUT // 4 + i: 0 for i in range(32)})
        for base, count in ((BANK, 32), (OUTPUT, 32)):
            dm[base // 4 - 1] = dm[base // 4 + count] = 0xDEADBEEF

        def call(entry, token=0, note=60, velocity=100, blocks=1):
            count = 32 * blocks
            if entry.startswith("render"):
                dm.update({OUTPUT // 4 + i: 0 for i in range(count)})
            dm[OUTPUT // 4 + count] = 0xDEADBEEF
            result = pk_sharc.run_call(
                linked,
                "_cp_" + entry,
                regs={
                    "R4": BANK,
                    "R8": OUTPUT if entry.startswith("render") else token,
                    "R12": note,
                    "R9": velocity,
                    "M5": 0,
                    "M6": 1,
                    "R10": blocks,
                    "L0": 0,
                    "L1": 0,
                    "L2": 0,
                    "MODE1": 0x1011800,
                },
                dm_words=dm,
                reads={
                    "bank": (BANK // 4 - 1, 34),
                    "out": (OUTPUT // 4 - 1, count + 2),
                },
                max_steps=7000 * blocks,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name, base in (("bank", BANK), ("out", OUTPUT)):
                assert result[name][0] == result[name][-1] == 0xDEADBEEF
                dm.update(
                    {base // 4 - 1 + i: word for i, word in enumerate(result[name])}
                )
            bank = result["bank"][1:-1]
            slots = {bank[i]: bank[i : i + 8] for i in range(0, 32, 8) if bank[i]}
            return result["regs"]["R0"], slots, floats(result["out"][1:-1])

        return call

    return session


def test_notes_keep_independent_phase_velocity_and_release(engine):
    a, b = engine(), engine()
    assert a("note_on", 1, 69, 20)[0] == b("note_on", 1, 69, 20)[0] == 1
    a("render")
    b("render")
    assert a("note_on", 2, 72, 127)[0] == 1
    _, slots, pcm = a("render")
    _, reference, _ = b("render")
    assert (
        slots[1] == reference[1]
    )  # another onset leaves every existing state word intact
    assert floats([slots[2][6]])[0] == pytest.approx(1.0)
    assert floats([slots[1][6]])[0] == pytest.approx(20 / 127)
    assert all(math.isfinite(x) and abs(x) < 0.25 for x in pcm)
    assert a("note_off", 1)[0] == 1
    _, after, _ = a("render")
    assert after[1][1] == 0 and after[2][1] == 1
    assert floats([after[1][7]])[0] < floats([slots[1][7]])[0]
    assert floats([after[2][7]])[0] > floats([slots[2][7]])[0]


def test_tokens_capacity_and_quiet_output(engine):
    call = engine()
    assert call("render") == (0, {}, [0.0] * 32)
    for token in range(1, 5):
        assert call("note_on", token, 60 + token)[0] == 1
    _, before, _ = call("render")
    for token, note, velocity in (
        (5, 70, 100),
        (1, 60, 100),
        (0, 60, 100),
        (5, 128, 100),
        (5, 60, 128),
        (5, 60, 0),
    ):
        result, after, _ = call("note_on", token, note, velocity)
        assert result == 0 and after == before
    assert call("note_off", 999)[1] == before
    for token in range(1, 5):
        call("note_off", token)
    # Short attack has reached 32/3840; release takes about 400 samples.
    for _ in range(14):
        status, slots, _ = call("render")
    assert status == 0 and slots == {}
    assert call("render")[2] == [0.0] * 32


@pytest.mark.parametrize("note", [0, 69, 127])
def test_sine_pitch_attack_and_sustained_stability(engine, note):
    call = engine()
    call("note_on", 1, note, 127)
    status, slots, pcm = call("render_many", blocks=1500)
    assert status == 1
    frequency = 440 * 2 ** ((note - 69) / 12)
    expected = [
        0.125
        * min((i + 1) / 3840, 1)
        * math.sin(2 * math.pi * frequency * (i + 1) / 48000)
        for i in range(48000)
    ]
    assert max(abs(x - y) for x, y in zip(pcm, expected, strict=True)) < 0.0002
    x, y = floats(slots[1][2:4])
    assert x * x + y * y == pytest.approx(1, abs=0.000002)
    call("note_off", 1)
    status, slots, pcm = call("render_many", blocks=1502)
    assert status == 0 and slots == {} and pcm[-32:] == [0.0] * 32
    assert all(math.isfinite(x) and abs(x) <= 0.126 for x in pcm)
