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
    source = "\n".join(
        (SOURCE / name).read_text() for name in ("voice.s", "tuning.s", "blend.s")
    )
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

    def session(sound=None, one=False):
        bank_words = 8 if one else 64
        dm = dict(initial)
        for name, value in (sound or {}).items():
            dm[symbols["_cp_" + name] // 4] = struct.unpack(
                "<I", struct.pack("<f", value)
            )[0]
        dm.update({BANK // 4 + i: 0 for i in range(bank_words)})
        dm.update({OUTPUT // 4 + i: 0 for i in range(32)})
        for base, count in ((BANK, bank_words), (OUTPUT, 32)):
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
                    "bank": (BANK // 4 - 1, bank_words + 2),
                    "out": (OUTPUT // 4 - 1, count + 2),
                },
                max_steps=20000 * blocks,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name, base in (("bank", BANK), ("out", OUTPUT)):
                assert result[name][0] == result[name][-1] == 0xDEADBEEF
                dm.update(
                    {base // 4 - 1 + i: word for i, word in enumerate(result[name])}
                )
            bank = result["bank"][1:-1]
            slots = {
                bank[i]: bank[i : i + 8]
                for i in range(0, 8 if one else 48, 8)
                if bank[i]
            }
            return result["regs"]["R0"], slots, floats(result["out"][1:-1])

        return call

    def blend(pad, arp, current, target):
        def word(value):
            return struct.unpack("<I", struct.pack("<f", value))[0]

        other, state = OUTPUT + 256, OUTPUT + 512
        dm = dict(initial)
        for base, values in ((OUTPUT, pad), (other, arp), (state, [current, target])):
            dm.update({base // 4 + i: word(v) for i, v in enumerate(values)})
            dm[base // 4 - 1] = dm[base // 4 + len(values)] = 0xDEADBEEF
        result = pk_sharc.run_call(
            linked,
            "_cp_blend",
            regs={
                "R4": OUTPUT,
                "R8": other,
                "R12": state,
                "M5": 0,
                "M6": 1,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
            },
            dm_words=dm,
            reads={
                "pad": (OUTPUT // 4 - 1, 34),
                "arp": (other // 4 - 1, 34),
                "state": (state // 4 - 1, 4),
            },
            max_steps=2000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        for name in ("pad", "arp", "state"):
            assert result[name][0] == result[name][-1] == 0xDEADBEEF
        assert result["arp"][1:-1] == [word(v) for v in arp]
        return (
            result["regs"]["R0"],
            floats(result["pad"][1:-1]),
            floats(result["state"][1:-1]),
        )

    session.blend = blend
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


def test_arp_group_uses_exactly_one_slot_and_releases_only_its_token(engine):
    call = engine(one=True)
    assert call("render_one") == (0, {}, [0.0] * 32)
    assert call("start_one", 91, 69, 127)[0] == 1
    _, before, pcm = call("render_one")
    assert set(before) == {91} and max(abs(v) for v in pcm) > 0
    assert call("start_one", 92, 72, 127)[:2] == (0, before)
    assert call("release_one", 92)[:2] == (0, before)
    assert call("release_one", 91)[0] == 1
    for _ in range(14):
        result, slots, _ = call("render_one")
    assert result == 0 and slots == {}
    assert call("start_one", 92, 72, 127)[0] == 1


@pytest.mark.parametrize("mix", [0.0, 0.25, 0.5, 1.0])
def test_pad_arp_blend_endpoints_and_fixed_headroom(engine, mix):
    pad = [0.6 * math.sin(i) for i in range(32)]
    arp = [0.12 * math.cos(i) for i in range(32)]
    result, pcm, state = engine.blend(pad, arp, mix, mix)
    assert result == 1 and state == [mix, mix]
    assert pcm == pytest.approx(
        [(1 - mix) * p + mix * a for p, a in zip(pad, arp, strict=True)], abs=1e-7
    )
    assert max(abs(v) for v in pcm) <= 0.6


def test_pad_arp_blend_smooths_every_sample_and_retains_instance_state(engine):
    current = 0.0
    samples = []
    for _ in range(30):
        result, pcm, state = engine.blend([1.0] * 32, [0.0] * 32, current, 1.0)
        assert result == 1
        current = state[0]
        samples.extend(pcm)
    assert samples[-1] == pytest.approx(math.exp(-1), abs=1e-6)
    assert all(
        0 < a - b < 0.00105 for a, b in zip([1.0] + samples[:-1], samples, strict=True)
    )
    assert engine.blend([1.0] * 32, [0.0] * 32, 0.0, 0.0)[1] == [1.0] * 32


@pytest.mark.parametrize("target", [0.0, 1.0])
def test_pad_arp_blend_settles_exactly_without_residual_muted_lane(engine, target):
    current = 0.00003 if target == 0 else 0.99997
    result, pcm, state = engine.blend([1.0] * 32, [-1.0] * 32, current, target)
    assert result == 1 and state == [target, target]
    assert pcm == [1.0 - 2 * target] * 32


@pytest.mark.parametrize("bad", [float("nan"), float("inf"), -0.1, 1.1])
@pytest.mark.parametrize("field", [0, 1])
def test_pad_arp_blend_rejects_invalid_state_without_writing_audio(engine, bad, field):
    state = [0.5, 0.5]
    state[field] = bad
    result, pcm, after = engine.blend([0.25] * 32, [-0.5] * 32, *state)
    assert result == 0 and pcm == [0.25] * 32
    for a, b in zip(after, state, strict=True):
        assert math.isnan(a) if math.isnan(b) else a == pytest.approx(b)


def test_tokens_capacity_and_quiet_output(engine):
    call = engine()
    assert call("render") == (0, {}, [0.0] * 32)
    for token in range(1, 5):
        assert call("note_on", token, 60 + token)[0] == 1
    _, before, _ = call("render")
    for token, note, velocity in (
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


def test_stealing_preserves_tails_and_delayed_replacement_is_bounded(engine):
    call = engine()
    for token in range(1, 5):
        assert call("note_on", token, 59 + token, 127)[0] == 1
    call("render_many", blocks=120)
    for token in range(5, 9):
        assert call("note_on", token, 59 + token, 127)[0] == 1
    _, voices, _ = call("render")
    assert set(voices) == set(range(1, 7))
    assert all(voices[t][1] == 2 for t in range(1, 5))
    assert all(voices[t][1] == 1 for t in range(5, 7))
    assert call("note_off", 1)[1] == voices  # stolen release cannot affect replacement
    assert call("note_on", 9, 72, 127)[0] == 1  # queues behind its own ramp
    call("note_off", 5)  # displaced physical release must not cancel pending 9
    _, voices, pcm = call("render")
    assert set(voices) == set(range(6, 10))
    assert all(math.isfinite(x) and abs(x) <= 1.001 for x in pcm)
    assert call("note_off", 1)[0] == 0


def test_pending_note_can_be_released_before_it_sounds(engine):
    call = engine()
    for token in range(1, 5):
        call("note_on", token, 60, 127)
    call("render_many", blocks=120)
    for token in range(5, 9):
        call("note_on", token, 64, 127)
    assert call("note_off", 7)[0] == 1
    call("render")
    _, voices, _ = call("render")
    assert set(voices) == {5, 6, 8}


def test_quietest_release_is_reclaimed_before_older_notes(engine):
    call = engine()
    for token in range(1, 5):
        call("note_on", token, 60 + token, 127)
    call("render_many", blocks=120)
    call("note_off", 4)
    call("render")
    call("note_off", 3)
    call("render")
    _, voices, _ = call("note_on", 5, 72, 127)
    assert voices[1][1] == 1 and voices[4][1] == 2 and voices[5][1] == 1
    assert voices[3][1] == 0


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


@pytest.mark.parametrize("family", ["velvet", "reed"])
@pytest.mark.parametrize("note", [0, 60, 100, 110, 119, 127])
def test_pad_spectrum_is_bounded_before_nyquist(engine, family, note):
    second, third = (0, -1 / 9) if family == "velvet" else (0.35, 0.2)
    call = engine(dict(second_start=second, third_start=third, level=0.08, attack=1.0))
    call("note_on", 1, note, 127)
    _, _, pcm = call("render_many", blocks=256)
    frequency = 440 * 2 ** ((note - 69) / 12)
    w = 2 * math.pi * frequency / 48000
    h2 = second * min(1, max(0, (math.cos(w) - 0.05) * 4))
    h3 = third * min(1, max(0, (math.cos(w) - 0.5) * 4))
    # Compare every actual instruction-rendered sample with a finite spectrum,
    # including the high notes where harmonic aliases MUST be absent.
    expected = [
        0.08
        * (
            math.sin(w * (i + 1))
            + h2 * math.sin(2 * w * (i + 1))
            + h3 * math.sin(3 * w * (i + 1))
        )
        for i in range(len(pcm))
    ]
    assert max(abs(a - b) for a, b in zip(pcm, expected, strict=True)) < 0.0001
    assert all(math.isfinite(x) and abs(x) < 0.125 for x in pcm)


def test_pad_full_pool_and_fast_retrigger_keep_fixed_headroom(engine):
    call = engine(
        dict(
            second_start=0.35,
            third_start=0.2,
            level=0.08,
            attack=1.0,
            release_step=-1 / (48 * 8168),
        )
    )
    for token in range(1, 5):
        call("note_on", token, 60, 127)
    call("render_many", blocks=3)
    call("note_on", 5, 60, 127)
    call("note_on", 6, 60, 127)
    _, voices, pcm = call("render")
    assert len(voices) == 6
    assert max(abs(x) for x in pcm) < 0.75
    for token in range(7, 11):
        call("note_on", token, 60, 127)
    _, voices, pcm = call("render_many", blocks=4)
    assert len(voices) <= 6
    assert max(abs(x) for x in pcm) < 0.75
    assert set(voices).issuperset({7, 8, 9, 10})


@pytest.mark.parametrize("amount", [-0.00288883, 0.00288883])
def test_pad_max_drift_is_finite_and_phase_continuous(engine, amount):
    call = engine(dict(drift=amount, attack=1.0, level=0.08))
    call("note_on", 1, 69, 127)
    _, slots, pcm = call("render_many", blocks=256)
    base = 2 * math.pi * 440 / 48000
    delta = amount * math.sin(base)
    w = base + math.atan2(delta, 1 - 0.5 * delta * delta)
    expected = [0.08 * math.sin(w * (i + 1)) for i in range(len(pcm))]
    assert max(abs(a - b) for a, b in zip(pcm, expected, strict=True)) < 0.0002
    assert abs(1200 * math.log2(w / base)) < 5.01
    x, y = floats(slots[1][2:4])
    assert x * x + y * y == pytest.approx(1, abs=0.000002)
