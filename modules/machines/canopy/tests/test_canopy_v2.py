"""CANOPY normalized V2 callbacks executed on the SHARC instruction core."""

# python-tests shards: 8
# (911 cases, about an hour on one core: the arp and FM cases render hundreds of blocks.)

import math
import struct

import pytest
from canopy_paths import DSP
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module", params=[False, True], ids=["direct", "protected"])
def canopy_object(request):
    root = DSP
    source = "\n".join(
        (root / f"{name}.s").read_text()
        for name in [
            "voice",
            "fm",
            "fm-tables",
            "wavetable",
            "wavetables",
            "tuning",
            "events",
            "instrument",
            "blend",
            "arp",
            "strum",
            "status",
            "v2",
        ]
    )
    if request.param:
        registry = REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp"
        source += "\n" + "\n".join(
            (registry / name).read_text()
            for name in ["process.s", "process-protected.s", "input.s"]
        )
        source += "\n.PKDATA _ir_rows, 512;\n.PKDATA _ir_process_rows, 256;\n.PKDATA _ir_process_flags, 32;\n"
        source += (
            REPO_ROOT / "modules/core/adapters/midi-events/src/dsp/packet.s"
        ).read_text()
        source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_packet;
_test_packet:
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    CALL _ir_process_packet;
    RTS;
"""
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_process;
_test_process:
    I13 = _test_done;
    JUMP _canopy_process;
.GLOBAL _test_reset;
_test_reset:
    I13 = _test_done;
    JUMP _canopy_reset;
_test_done:
    RTS;
.GLOBAL _test_render_context;
_test_render_context:
    CALL _cp_render_one;
    R0 = B3;
    R1 = I3;
    R2 = L3;
    RTS;
"""
    if request.param:
        source = source.replace(
            "    I13 = _test_done;\n    JUMP _canopy_process;",
            """
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    CALL _ir_process_protected;
    RTS;
""",
        )
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_many_capture;
_test_many_capture:
    DM(_test_capture) = R7;
    JUMP _test_many_begin;
.GLOBAL _test_many;
_test_many:
    R0 = 0;
    DM(_test_capture) = R0;
_test_many_begin:
    DM(_test_loop_left) = R10;
    DM(_test_loop_state) = R4;
    DM(_test_loop_input) = R8;
_test_loop:
    R4 = DM(_test_loop_state);
    R8 = DM(_test_loop_input);
    CALL _test_process;
    R0 = PASS R0;
    IF LT RTS;
    R1 = DM(_test_capture);
    R1 = PASS R1;
    IF EQ JUMP _test_no_capture;
    I2 = R1;
    R0 = DM(_test_loop_input);
    I0 = R0;
    R0 = DM(5, I0);
    I1 = R0;
    R2 = 32;
_test_capture_block:
    R0 = DM(I1, M6);
    DM(I2, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _test_capture_block;
    R0 = I2;
    DM(_test_capture) = R0;
_test_no_capture:
    R1 = DM(_test_loop_input);
    I0 = R1;
    R1 = 0;
    DM(8, I0) = R1;
    R1 = DM(_test_advance_clock);
    R1 = PASS R1;
    IF EQ JUMP _test_clock_done;
    R1 = DM(4, I0);
    R1 = R1 + 1;
    DM(4, I0) = R1;
_test_clock_done:
    R1 = DM(_test_loop_left);
    R1 = R1 - 1;
    DM(_test_loop_left) = R1;
    IF NE JUMP _test_loop;
    RTS;
.PKBSS _test_capture, 4;
.PKBSS _test_loop_left, 4;
.PKBSS _test_loop_state, 4;
.PKBSS _test_loop_input, 4;
.PKBSS _test_advance_clock, 4;
"""
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20060000
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob) + 64
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(obj, 0xB90000, {**symbols, "_mi_epoch": 0x20010000})
    return linked, sections, initial, obj, symbols, request.param


# The DSP memory every instance starts from, per assembled object: its data, the
# status block and the two state buffers with guards. Poked once and forked per
# instance; the object is kept with its session, so its id cannot be reused.
_TEMPLATES: dict[int, tuple[object, pk_sharc.CallSession]] = {}


def _template(canopy_object):
    linked, sections, initial, obj, symbols, protected = canopy_object
    entry = _TEMPLATES.get(id(linked))
    if entry is None or entry[0] is not linked:
        session = pk_sharc.CallSession([linked], 0xB9F000, fresh_registers=True)
        dm = dict(initial)
        dm.update({0x20015000 // 4 + i: 0 for i in range(13)})
        dm[0x20015000 // 4 - 1] = dm[0x20015000 // 4 + 13] = 0xDEADBEEF
        for base, size in [(0x20050000, 744), (0x20054000, 744), (0x20013000, 32)]:
            dm.update({base // 4 + i: 0 for i in range(size)})
            dm[base // 4 - 1] = dm[base // 4 + size] = 0xDEADBEEF
        for address, word in dm.items():
            session.poke(address, word)
        entry = _TEMPLATES[id(linked)] = (linked, session)
    return entry[1]


def make_canopy(canopy_object):
    """One independent simulated DSP memory and its call helper.

    The memory persists between calls (pk_sharc.CallSession); every call starts
    from a new core's registers, as a run of its own would. The calls run on the
    native SHARC core when its library is built (pk_sharc.session_backend)."""
    linked, sections, initial, obj, symbols, protected = canopy_object
    session = _template(canopy_object).fork(pk_sharc.session_backend())

    def poke(base, values):
        for i, value in enumerate(values):
            session.poke(base + i, value)

    def call(
        events=(),
        state=0x20050000,
        track=0,
        generation=1,
        reset=None,
        edits=None,
        packet=None,
        controls=(1,),
        state_edits=None,
        blocks=1,
        capture=False,
        native=None,
        post_amp=False,
        input_base=0x20011000,
        advance_clock=False,
        data_sections=False,
    ):
        session.poke(symbols["_test_advance_clock"] // 4, int(advance_clock))
        for i, value in (state_edits or {}).items():
            session.poke(state // 4 + i, value)
        words = [
            2,
            72,
            track,
            generation,
            0,
            0x20013000,
            0,
            0x20012100,
            len(events),
            0x20012000,
            len(controls),
            1,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
            0,
        ]
        words += [0, 0]  # optional V4 transport phase/deadline
        words += [0x20015000, 13]  # optional borrowed V5 status output
        words += [0] * 8  # V6 native AMP snapshot; earlier versions do not read it.
        if native is not None:
            assert len(native) == 8
            words[0:2] = [6, 128]
            words[24:32] = native
        words += [0, 0]  # V7 borrowed common-envelope output.
        if post_amp:
            assert native is not None
            words[0:2] = [7, 136]
            words[32:34] = [0x20016000, 32]
            poke(0x20016000 // 4, [0x7FC00000] * 32)
            session.poke(0x20016000 // 4 - 1, 0xDEADBEEF)
            session.poke(0x20016000 // 4 + 32, 0xDEADBEEF)
        for index, value in (edits or {}).items():
            words[index] = value
        poke(input_base // 4, words)
        poke(0x20012000 // 4, controls)
        for i, event in enumerate(events):
            poke(0x20012100 // 4 + i * 6, event)
        # data_sections: also every data section of the object, by section name.
        reads = (
            {
                name: (sections[name] // 4, len(blob) // 4)
                for name, (blob, _, _) in obj.data.items()
            }
            if data_sections
            else {}
        )
        reads.update({"state": (state // 4, 744), "out": (0x20013000 // 4, 32)})
        reads["status"] = (0x20015000 // 4, 13)
        reads["amp_curve"] = (symbols["_cp_amp_samples"] // 4, 32)
        reads["amp_end"] = (symbols["_cp_amp_end"] // 4, 1)
        if post_amp:
            reads["common_amp"] = (0x20016000 // 4, 32)
            reads["common_before"] = (0x20016000 // 4 - 1, 1)
            reads["common_after"] = (0x20016000 // 4 + 32, 1)
        for debug in (
            "enabled",
            "phase",
            "increment",
            "size_a",
            "offset_a",
            "position",
            "frame",
            "env",
            "source_mix",
            "r6",
            "r7",
        ):
            reads["wt_" + debug] = (symbols["_wt_" + debug] // 4, 1)
        reads["status_before"] = (0x20015000 // 4 - 1, 1)
        reads["status_after"] = (0x20015000 // 4 + 13, 1)
        reads.update(
            {"guard_before": (state // 4 - 1, 1), "guard_after": (state // 4 + 744, 1)}
        )
        if protected and reset is None:
            session.poke(
                symbols["_ir_process_flags"] // 4,
                7
                if words[0] == 7
                else (3 if words[0] == 6 else (1 if words[0] == 5 else 0)),
            )
            row = [3, 2, 9, 1, state, 744, state, state + 2976, 0, 0, 0, 0, 0, 0, 0, 0]
            meta = [
                2,
                9,
                20,
                linked.symbols["_canopy_process"],
                0xB90000,
                0xBC0000,
                8,
                32,
            ]
            for name, values in [("_ir_rows", row), ("_ir_process_rows", meta)]:
                poke(symbols[name] // 4, values)
        if packet is not None:
            assert protected, "packet dispatch requires the registry"
            poke(0x20014000 // 4, packet)
            # Poison audio to prove that rejection cannot replay the last block.
            poke(0x20013000 // 4, [0x3F800000] * 32)
        if capture:
            assert blocks > 1
            reads["audio"] = (0x200B0000 // 4, blocks * 32)
        result = session.call(
            "_test_packet"
            if packet is not None
            else (
                (
                    "_test_many_capture"
                    if capture
                    else ("_test_many" if blocks > 1 else "_test_process")
                )
                if reset is None
                else "_test_reset"
            ),
            regs={
                "R4": 0 if protected and reset is None else state,
                "R5": 0,
                "R9": 1,
                "R7": 0x200B0000 if capture else 0,
                "R10": blocks,
                "R12": 0x20014000 if packet else 0,
                "R8": input_base if reset is None else reset,
                "M5": 0,
                "M6": 1,
                "M13": 0,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
            },
            max_steps=100000 * blocks,
        )
        for name, (base, count) in reads.items():
            result[name] = [session.peek(base + i) for i in range(count)]
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["guard_before"] == result["guard_after"] == [0xDEADBEEF]
        assert result["status_before"] == result["status_after"] == [0xDEADBEEF]
        if post_amp:
            assert result["common_before"] == result["common_after"] == [0xDEADBEEF]
            if result["regs"]["R0"] in (0, 1) and reset is None:
                # The complete rendered mix must be reconstructible by the
                # adapter without invalid gains or over-range normalized PCM.
                import math

                for audio_bits, gain_bits in zip(
                    result["out"], result["common_amp"], strict=True
                ):
                    audio, gain = wt_float(audio_bits), wt_float(gain_bits)
                    assert math.isfinite(gain) and 0 <= gain <= 1
                    if gain == 0:
                        assert audio == 0
                    else:
                        normalized = audio / gain
                        assert math.isfinite(normalized) and abs(normalized) <= 8
                        assert normalized * gain == pytest.approx(audio, abs=1e-7)
        return result

    return call


@pytest.fixture
def canopy(canopy_object):
    return make_canopy(canopy_object)


def note(token, pitch, velocity=100, off=False, owner=0):
    return [2 if off else 1, owner, token, pitch << 8, velocity << 8, 0]


def voices(result):
    return {
        result["state"][i]: result["state"][i : i + 8]
        for i in range(0, 32, 8)
        if result["state"][i] and not result["state"][i + 1] & 4
    }


def native_amp(
    mode=2,
    attack=1 / 256,
    hold=0xFFFFFFFF,
    decay=0.9,
    sustain=0.5,
    release=0.8,
    options=0,
):
    import struct

    def bits(value):
        return struct.unpack("<I", struct.pack("<f", value))[0]

    return [
        mode,
        bits(attack),
        hold,
        bits(decay),
        bits(sustain),
        bits(release),
        options,
        0,
    ]


def test_native_amp_attack_outputs_previous_level_and_releases_independently(canopy):
    amp = native_amp()
    r = canopy([note(1, 60)], native=amp)
    assert [wt_float(v) for v in r["amp_curve"]] == pytest.approx(
        [i / 256 for i in range(32)], abs=1e-7
    )
    assert wt_float(voices(r)[1][7]) == pytest.approx(0.125)
    r = canopy([note(2, 67)], native=amp)
    assert wt_float(voices(r)[1][7]) == pytest.approx(0.25)
    assert wt_float(voices(r)[2][7]) == pytest.approx(0.125)
    r = canopy([note(1, 60, off=True)], native=amp)
    assert wt_float(voices(r)[1][7]) == pytest.approx(0.25 * 0.8**32, rel=2e-6)
    assert wt_float(voices(r)[2][7]) == pytest.approx(0.25)


def test_native_amp_slow_attack_crosses_at_repeated_float_boundary(canopy):
    amp = native_amp(attack=1e-7, decay=0.5, sustain=0)
    before = canopy([note(1, 60)], native=amp)
    slot = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    # At this level repeated f32 additions reach unity after 27 samples, while
    # A + 32*a rounds to exactly 1. Decay must begin on sample 27, not block end.
    after = canopy(native=amp, state_edits={slot * 8 + 7: wt_bits(0.9999967813491821)})
    assert [wt_float(v) for v in after["amp_curve"][27:]] == [
        1,
        0.5,
        0.25,
        0.125,
        0.0625,
    ]
    assert after["state"][717 + slot * 4] == 1
    assert wt_float(after["amp_end"][0]) == 0.03125


@pytest.mark.parametrize("canopy_object", [False], indirect=True)
def test_native_fm_preserves_nonzero_circular_address_context(canopy, canopy_object):
    linked, sections, initial, obj, symbols, _ = canopy_object
    amp = native_amp(attack=1 / 64)
    before = canopy(
        [note(1, 60)], native=amp, controls=fm_controls(), data_sections=True
    )
    slot = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    # Enter the real single-voice renderer after normal V6 configuration. Calling
    # it directly lets us inspect I3 before the status publisher reuses that DAG.
    dm = dict(initial)
    for name in obj.data:
        dm.update(
            {sections[name] // 4 + i: word for i, word in enumerate(before[name])}
        )
    dm.update({0x20050000 // 4 + i: word for i, word in enumerate(before["state"])})
    dm.update({0x20011000 // 4 + 24 + i: word for i, word in enumerate(amp)})
    reads = {
        "state": (0x20050000 // 4, 744),
        "out": (0x20013000 // 4, 32),
        "native": (symbols["_cp_amp_native"] // 4, 1),
    }

    def render(context):
        result = pk_sharc.run_call(
            linked,
            "_test_render_context",
            regs={
                "R4": 0x20050000 + slot * 32,
                "R8": 0x20013000,
                "M5": 0,
                "M6": 1,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
                **context,
            },
            dm_words=dm,
            reads=reads,
            max_steps=100000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["native"] == [1]
        return result

    reference = render({"B3": 0, "I3": 0, "L3": 0})
    context = {"B3": 0x20071000, "I3": 0x2007101C, "L3": 16}
    hostile = render(context)
    assert hostile["out"] == reference["out"] and any(hostile["out"])
    assert hostile["state"] == reference["state"]
    assert [hostile["regs"][name] for name in ("R0", "R1", "R2")] == list(
        context.values()
    )


@pytest.mark.parametrize("family", ["legacy", "wavetable", "fm"])
def test_native_amp_shapes_all_sources_and_preserves_zero_sustain_owner(canopy, family):
    controls = {
        "legacy": (3, 0, 0, 0, 5, 15),
        "wavetable": wavetable_controls(),
        "fm": fm_controls(),
    }[family]
    amp = native_amp(attack=1, decay=0, sustain=0, release=0.5)
    r = canopy([note(1, 60)], controls=controls, native=amp, blocks=9)
    assert 1 in voices(r) and voices(r)[1][1] == 1
    assert voices(r)[1][7] == 0
    assert r["regs"]["R0"] == 0
    r = canopy([note(1, 60, off=True)], controls=controls, native=amp)
    assert 1 not in voices(r)


@pytest.mark.parametrize("off", [False, True])
def test_native_amp_finite_ahd_continues_after_keyup_and_retrigger_is_independent(
    canopy, off
):
    amp = native_amp(mode=1, attack=1, hold=40, release=0.5)
    first = canopy([note(1, 60)], native=amp)
    curve = [wt_float(v) for v in first["amp_curve"]]
    assert curve == [i / 256 for i in range(32)]
    r = canopy([note(1, 60, off=True)] if off else [], native=amp)
    assert [wt_float(v) for v in r["amp_curve"]] == pytest.approx(
        [i / 256 for i in range(32, 64)], abs=1e-7
    )
    for block in range(2, 8):
        r = canopy(native=amp)
        assert [wt_float(v) for v in r["amp_curve"]] == [
            i / 256 for i in range(32 * block, 32 * (block + 1))
        ]
    r = canopy(native=amp)
    assert [wt_float(v) for v in r["amp_curve"]] == [1.0] * 32
    r = canopy(native=amp)
    assert [wt_float(v) for v in r["amp_curve"]] == pytest.approx(
        [1.0] * 8 + [0.5**i for i in range(24)], abs=1e-7
    )
    assert wt_float(r["amp_end"][0]) == 0.0  # below the voice retirement threshold
    r = canopy([note(2, 67)], native=amp)
    assert 2 in voices(r) and wt_float(voices(r)[2][7]) == 0.125


@pytest.fixture(scope="module")
def native_release_coefficients():
    import hashlib

    from modules.machines.canopy.tools import carrier_probe as stock
    from tools.sharc_dev.sharc_run import Runner
    from tools.sharc_dev.sharcldr import LoadedMemory

    path = REPO_ROOT / "out/sections/dt2-1.17/section_7_BLOB.bin"
    if not path.exists():
        pytest.skip("requires the local pinned DT2 1.17 DSP extraction")
    blob = path.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == stock.BLOB_SHA
    runner = Runner(LoadedMemory.from_stream(blob), stock.ENVELOPE)
    return {
        index: -wt_float(stock.get(runner, 0x26AF30 + index * 4)) for index in (40, 80)
    }


@pytest.mark.parametrize("release", [40, 80])
@pytest.mark.parametrize("family", ["legacy", "wavetable", "fm", "wavetable-fm"])
@pytest.mark.parametrize("post_amp", [False, True], ids=["v6", "v7"])
def test_native_amp_release_attenuates_rendered_audio_through_tail(
    canopy, native_release_coefficients, family, release, post_amp
):
    import math

    controls = {
        "legacy": (3, 0, 0, 0, 5, 15),
        "wavetable": wavetable_controls(),
        "fm": fm_controls(),
        "wavetable-fm": wavetable_fm_controls(fm_depth=70, ratio=8),
    }[family]
    q = native_release_coefficients[release]
    assert 0 < q < 1
    amp = native_amp(attack=1, decay=1, sustain=1, release=q)
    unity = native_amp(attack=1, decay=1, sustain=1, release=1)
    before = canopy(
        [note(1, 60)], controls=controls, native=amp, blocks=8, post_amp=post_amp
    )
    slot = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    assert wt_float(voices(before)[1][7]) == 1

    # Visit early, middle and quiet portions of the real stock exponential.
    # Seed only elapsed release amplitude to avoid emulating seconds of audio
    # for each source; every measured sample still passes through the complete
    # callback, native envelope and oscillator renderer. The first checkpoint
    # exercises the actual note-off transition instead of preselecting phase 4.
    for target in (1.0, 0.5, 0.1, 0.01):
        elapsed = round(math.log(target) / math.log(q))
        level = wt_float(wt_bits(q**elapsed))
        seeded = dict(enumerate(before["state"]))
        seeded[slot * 8 + 7] = wt_bits(level)
        events = [note(1, 60, off=True)] if target == 1 else []
        if not events:
            seeded[slot * 8 + 1] = 0
            seeded[717 + slot * 4] = 4
        reference = canopy(
            events,
            state=0x20054000,
            state_edits={**seeded, slot * 8 + 7: wt_bits(1.0)},
            controls=controls,
            native=unity,
            post_amp=post_amp,
        )
        released = canopy(
            events, state_edits=seeded, controls=controls, native=amp, post_amp=post_amp
        )
        envelope = []
        for _ in range(32):
            envelope.append(level)
            level = wt_float(wt_bits(level * q))
        assert [wt_float(v) for v in released["amp_curve"]] == envelope
        assert wt_float(voices(released)[1][7]) == level
        assert voices(released)[1][1] == 0
        assert released["state"][717 + slot * 4] == 4
        expected = [
            wt_float(v) * gain
            for v, gain in zip(reference["out"], envelope, strict=True)
        ]
        actual = [wt_float(v) for v in released["out"]]
        assert max(abs(v) for v in expected) > target * 1e-4
        assert actual == pytest.approx(expected, rel=2e-6, abs=target * 2e-9)
        if post_amp:
            gain = [wt_float(v) for v in released["common_amp"]]
            assert gain == envelope
            assert [x / g for x, g in zip(actual, gain, strict=True)] == pytest.approx(
                [wt_float(v) for v in reference["out"]], rel=2e-6, abs=2e-9
            )

    # A later onset must not retrigger the released note or end its quiet tail.
    previous_level = level
    later = canopy([note(2, 67)], controls=controls, native=amp, post_amp=post_amp)
    for _ in range(32):
        level = wt_float(wt_bits(level * q))
    assert wt_float(voices(later)[1][7]) == level
    assert voices(later)[1][1] == 0
    assert wt_float(voices(later)[2][7]) == 0.125
    assert voices(later)[2][1] == 1
    if post_amp:
        expected_gain = []
        for i in range(32):
            expected_gain.append(max(previous_level, i / 256))
            previous_level = wt_float(wt_bits(previous_level * q))
        assert [wt_float(v) for v in later["common_amp"]] == expected_gain


@pytest.mark.parametrize("mode", [0, 1, 2])
def test_post_amp_quiet_and_zero_sustain_fill_the_complete_borrow(canopy, mode):
    amp = native_amp(mode=mode, attack=1, hold=0, decay=0, sustain=0, release=0)
    empty = canopy(native=amp, post_amp=True)
    assert empty["out"] == [0] * 32
    assert empty["common_amp"] == [wt_bits(1 if mode == 0 else 0)] * 32
    if mode == 0:
        return
    held = canopy([note(1, 60)], native=amp, post_amp=True, blocks=10)
    if mode == 1:
        # The immediate AHD release has reached silence after its attack.
        assert held["out"] == [0] * 32
        assert held["common_amp"] == [0] * 32
        assert voices(held)[1][1] == 1
        return
    assert held["out"] == held["common_amp"] == [0] * 32
    assert voices(held)[1][1] == 1
    released = canopy([note(1, 60, off=True)], native=amp, post_amp=True)
    assert released["out"] == released["common_amp"] == [0] * 32
    assert not voices(released)


@pytest.mark.parametrize(
    "index,value",
    [(1, 128), (32, 0), (32, 0x20016001), (32, 0x200FDF84), (33, 31), (33, 33)],
)
def test_post_amp_rejects_invalid_borrow_before_admitting_notes(canopy, index, value):
    result = canopy(
        [note(1, 60)], native=native_amp(), post_amp=True, edits={index: value}
    )
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 744
    assert result["common_amp"] == [0x7FC00000] * 32


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_post_amp_v7_packet_normalization_reaches_kernel(canopy):
    result = canopy(
        packet=wire_packet([(1, 1, 60, 100, 0, 0)]),
        native=native_amp(attack=1 / 256),
        post_amp=True,
    )
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1}
    assert [wt_float(v) for v in result["common_amp"]] == [i / 256 for i in range(32)]


def test_post_amp_lane_buffers_resolve_to_distinct_data_storage(canopy, canopy_object):
    _, _, _, obj, symbols, _ = canopy_object
    pad, arp = (symbols[name] for name in ("_cp_post_pad", "_cp_post_arp"))
    assert min(pad, arp) >= 0x20000000 and max(pad, arp) + 128 <= 0x200FE000
    assert abs(pad - arp) >= 128
    result = canopy(
        [note(1, 60)],
        native=native_amp(attack=1 / 256),
        post_amp=True,
        data_sections=True,
    )

    def values(name, count):
        section, offset, _ = obj.data_symbols[name]
        return result[section][offset // 4 : offset // 4 + count]

    # PKBSS owns the data symbols. A redundant code-global declaration must
    # never redirect either lane's immediate pointer into instruction memory.
    assert values("_cp_post_lane", 1) == [arp]
    assert [wt_float(v) for v in values("_cp_post_pad", 32)] == [
        i / 256 for i in range(32)
    ]
    assert values("_cp_post_arp", 32) == [0] * 32
    assert result["common_amp"] == values("_cp_post_pad", 32)


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


def eased_reset(e0, done, count):
    """The reset and steal ramp of voice.s in single precision: from the level e0 over D = ceil(64 e0) samples (1 to 64),
    e = e0 (1 - u u (3 - 2u)), u = k / D, for samples done + 1 .. done + count."""
    e0 = f32(e0)
    d = min(64, max(1, math.ceil(f32(e0 * 64))))
    inv = f32(1 / d)
    out = []
    for k in range(done + 1, done + count + 1):
        u = 1.0 if k >= d else min(f32(k * inv), 1.0)
        s = f32(f32(u * u) * f32(3 - f32(u + u)))
        out.append(f32(f32(1 - s) * e0))
    return out


def reset_record(token, done, e0_bits):
    """A voice's sidecar while its reset or steal ramp runs: its token, the samples done, its level where the ramp began
    and CP_RESET_MARK."""
    return [token, done, e0_bits, 0x52535431]


def test_post_amp_reset_ramp_remains_smooth_and_then_exactly_quiet(canopy):
    amp = native_amp(attack=1, decay=1, sustain=1, release=1)
    controls = wavetable_fm_controls(fm_depth=70)
    canopy([note(1, 60)], native=amp, controls=controls, post_amp=True, blocks=8)
    canopy(reset=2)
    first = canopy(native=amp, controls=controls, post_amp=True)
    second = canopy(native=amp, controls=controls, post_amp=True)
    # the eased reset ramp (finding 366) from the level 1.0: 64 samples of 1 - s(k / 64)
    assert [wt_float(v) for v in first["common_amp"]] == eased_reset(1.0, 0, 32)
    assert [wt_float(v) for v in second["common_amp"]] == eased_reset(1.0, 32, 32)
    assert second["out"][-1] == 0
    quiet = canopy(native=amp, controls=controls, post_amp=True)
    assert quiet["out"] == quiet["common_amp"] == [0] * 32


@pytest.mark.parametrize("mix", [0, 64, 127])
def test_post_amp_blend_excludes_muted_lanes_and_keeps_arp_tail(canopy, mix):
    controls = wavetable_fm_controls(fm_depth=70, mix=64)
    controls[9] = 100
    controls[25] = (controls[25] & 0xFFFF0000) | 100
    amp = native_amp(attack=1, decay=1, sustain=1, release=0.99)
    first = canopy(
        [note(1, 60)], native=amp, controls=controls, post_amp=True, blocks=8
    )
    assert first["regs"]["R0"] == 0 and first["state"][136]
    tail = canopy(
        native=amp, controls=controls, post_amp=True, state_edits={146: 89999}
    )
    assert any(tail["state"][slot * 8 + 1] & 4 for slot in (4, 5))
    assert all(0 <= wt_float(v) <= 1 for v in tail["common_amp"])
    # Complete the replacement arp's onset guard before checking
    # blend endpoints at full sustain.
    for _ in range(7):
        canopy(native=amp, controls=controls, post_amp=True)
    # The pad has finished its native envelope while an independently owned
    # arp remains audible. Exact blend endpoints must exclude the muted lane.
    slot = next(slot for slot in range(4) if first["state"][slot * 8] == 1)
    seeded = {slot * 8 + 7: 0, 717 + slot * 4: 1, 144: wt_bits(mix / 127)}
    controls[6] = mix
    controls[21] = (controls[21] & 0xFFFF0000) | mix
    arp_only = canopy(native=amp, controls=controls, post_amp=True, state_edits=seeded)
    gain = [wt_float(v) for v in arp_only["common_amp"]]
    assert gain == pytest.approx([mix / 127] * 32, abs=1e-7)
    if mix == 0:
        assert arp_only["out"] == [0] * 32
    else:
        assert any(arp_only["out"])


def test_post_amp_source_transition_and_pcm_match_v6(canopy):
    amp = native_amp(attack=1, decay=1, sustain=1, release=1)
    source = wavetable_controls()
    first = canopy([note(1, 60)], native=amp, controls=source, blocks=8)
    # Clone the entire same phase/history snapshot into the independent V7 lane.
    # Its first transition callback receives the clone before rendering.
    seed = dict(enumerate(first["state"]))
    for index in range(10):
        old = canopy(native=amp, controls=fm_controls())
        new = canopy(
            native=amp,
            controls=fm_controls(),
            state=0x20054000,
            state_edits=seed if index == 0 else None,
            post_amp=True,
        )
        assert old["state"] == new["state"]
        assert old["out"] == new["out"]
        gain = [wt_float(v) for v in new["common_amp"]]
        if index < 4:
            assert gain == [1 - (index * 32 + i + 1) / 128 for i in range(32)]
        elif index < 8:
            assert gain == [((index - 4) * 32 + i + 1) / 128 for i in range(32)]
        else:
            assert gain == [1] * 32


@pytest.mark.parametrize("family", ["legacy", "wavetable", "fm", "wavetable_fm"])
def test_post_amp_polyphony_tracks_the_loudest_independent_envelope(canopy, family):
    controls = {
        "legacy": (3, 0, 0, 0, 5, 15),
        "wavetable": wavetable_controls(),
        "fm": fm_controls(),
        "wavetable_fm": wavetable_fm_controls(fm_depth=75),
    }[family]
    amp = native_amp(attack=1 / 256, decay=1, sustain=1, release=0.95)
    first = canopy(
        [note(i + 1, 48 + 7 * i) for i in range(3)],
        controls=controls,
        native=amp,
        post_amp=True,
        blocks=8,
    )
    seed = {}
    for token, level in ((1, 0.9), (2, 0.4), (3, 0.1)):
        slot = next(slot for slot in range(4) if first["state"][slot * 8] == token)
        seed[slot * 8 + 7] = wt_bits(level)
        seed[717 + slot * 4] = 1
    result = canopy(
        [note(1, 48, off=True), note(2, 55, off=True), note(4, 72)],
        controls=controls,
        native=amp,
        post_amp=True,
        state_edits=seed,
    )
    level, q, expected = wt_float(wt_bits(0.9)), wt_float(wt_bits(0.95)), []
    for sample in range(32):
        expected.append(max(level, sample / 256, wt_float(wt_bits(0.1))))
        level = wt_float(wt_bits(level * q))
    assert [wt_float(v) for v in result["common_amp"]] == expected
    assert len(voices(result)) == 4
    assert voices(result)[1][1] == voices(result)[2][1] == 0
    assert voices(result)[3][1] == voices(result)[4][1] == 1


def test_native_amp_infinite_release_yields_to_bounded_reset(canopy):
    amp = native_amp(mode=1, attack=1, release=1)
    canopy([note(1, 60)], native=amp, blocks=8)
    r = canopy([note(1, 60, off=True)], native=amp, blocks=4)
    assert wt_float(voices(r)[1][7]) == 1.0
    reset = canopy(reset=2)
    assert reset["instructions"] <= 1600
    r = canopy(native=amp, generation=2, blocks=3)
    assert not voices(r)
    fresh = canopy([note(1, 60)], native=amp, generation=2)
    assert wt_float(fresh["amp_curve"][0]) == 0.0
    assert wt_float(voices(fresh)[1][7]) == 0.125


@pytest.mark.parametrize("attack,initial", [(1.0, 1 / 512), (1 / 64, 1 / 512)])
def test_native_amp_restart_option_initializes_each_onset(canopy, attack, initial):
    r = canopy([note(1, 60)], native=native_amp(attack=attack, options=1))
    assert wt_float(r["amp_curve"][0]) == initial


def test_native_amp_fallback_and_mode_changes_do_not_resume_stale_phase(canopy):
    amp = native_amp(attack=1 / 256)
    first = canopy([note(1, 60)], native=amp)
    assert wt_float(voices(first)[1][7]) == 0.125
    legacy = canopy(native=[0] * 8)
    assert legacy["state"][716:] == [0] * 28
    fresh = canopy(native=amp)
    assert wt_float(fresh["amp_curve"][0]) == 0.0
    assert wt_float(voices(fresh)[1][7]) == 0.125
    changed = canopy(native=native_amp(mode=1, attack=1 / 256, hold=40))
    assert wt_float(changed["amp_curve"][0]) == 0.125
    assert wt_float(voices(changed)[1][7]) == 0.25


@pytest.mark.parametrize("family", ["legacy", "wavetable", "fm"])
def test_native_amp_mode_zero_preserves_legacy_pcm(canopy, family):
    controls = {
        "legacy": (3, 0, 64, 0, 5, 15),
        "wavetable": wavetable_controls(),
        "fm": fm_controls(),
    }[family]
    events = [note(1, 60), note(2, 67)]
    legacy = canopy(events, controls=controls, edits={0: 5, 1: 96}, blocks=4)
    fallback = canopy(
        events, controls=controls, native=[0] * 8, state=0x20054000, blocks=4
    )
    assert fallback["out"] == legacy["out"]
    assert fallback["state"] == legacy["state"]


def test_native_amp_arp_tail_continues_finite_ahd_without_restarting(canopy):
    base = list(STRUM)
    base[6], base[15] = 127, 0
    controls = presentation_controls(base)
    amp = native_amp(mode=1, attack=1, hold=1000, release=0.5)
    before = canopy([note(1, 60)], controls=controls, native=amp, blocks=9)
    assert before["state"][136] == 1
    prior_phase, prior_count = before["state"][741:743]
    assert prior_phase == 3 and prior_count == 32
    after = canopy(controls=controls, native=amp, state_edits={146: 89999})
    tails = [slot for slot in (4, 5) if after["state"][slot * 8 + 1] & 4]
    assert len(tails) == 1 and after["state"][136] == 2
    slot = tails[0]
    assert wt_float(after["state"][slot * 8 + 7]) == 1.0
    assert after["state"][716 + slot * 4 : 720 + slot * 4] == [
        1,
        prior_phase,
        prior_count + 32,
        1,
    ]


def test_native_amp_pad_steal_copies_phase_and_overrides_infinite_release(canopy):
    amp = native_amp(attack=1, sustain=1, release=1)
    before = canopy([note(i + 1, 60 + 3 * i) for i in range(4)], native=amp, blocks=8)
    source = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    level = before["state"][source * 8 + 7]
    after = canopy([note(5, 74)], native=amp)
    tail = next(slot for slot in (4, 5) if after["state"][slot * 8] == 1)
    # the stolen voice ramps out in the tail: its sidecar holds the ramp from its level (finding 366)
    assert after["state"][716 + tail * 4 : 720 + tail * 4] == reset_record(1, 32, level)
    assert wt_float(after["state"][tail * 8 + 7]) == 0.5
    after = canopy(native=amp)
    assert after["state"][tail * 8] == 0
    assert set(voices(after)) == {2, 3, 4, 5}


def test_native_amp_rejected_steal_batch_preserves_envelope_metadata(canopy):
    amp = native_amp(attack=1, sustain=1, release=1)
    before = canopy([note(i + 1, 60 + 3 * i) for i in range(4)], native=amp, blocks=8)
    # The first event steals a rendered voice into a tail; duplicate admission
    # fails inside the staged event engine, after that metadata has moved.
    rejected = canopy([note(5, 74), note(5, 76)], native=amp)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF
    assert rejected["state"] == before["state"]
    after = canopy([note(5, 74)], native=amp)
    tail = next(slot for slot in (4, 5) if after["state"][slot * 8] == 1)
    source = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    # the steal that is taken this time ramps the voice out in the tail (finding 366)
    assert after["state"][716 + tail * 4 : 720 + tail * 4] == reset_record(
        1, 32, before["state"][source * 8 + 7]
    )


@pytest.mark.parametrize(
    "index,value",
    [
        (24, 3),
        (24, 0xFFFFFFFF),
        (25, 0),
        (25, 0x7FC00000),
        (26, 0x80000000),
        (27, 0xBF000000),
        (28, 0x3F800001),
        (29, 0x7F800000),
        (30, 2),
        (31, 1),
    ],
)
def test_native_amp_rejects_malformed_snapshot_before_admitting_notes(
    canopy, index, value
):
    r = canopy([note(1, 60)], native=native_amp(), edits={index: value})
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert not voices(r) and r["state"] == [0] * 744


def test_native_amp_v6_input_bounds_and_status(canopy):
    r = canopy(
        [note(1, 60)],
        controls=presentation_controls(),
        native=native_amp(),
        input_base=0x200FDF80,
    )
    assert r["regs"]["R0"] == 0 and r["status"][0] == 1
    r = canopy([note(2, 64)], native=native_amp(), input_base=0x200FDF84)
    assert r["regs"]["R0"] == 0xFFFFFFFF and 2 not in voices(r)


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_native_amp_v6_packet_normalization_reaches_kernel(canopy):
    r = canopy(packet=wire_packet([(1, 1, 60, 100, 0, 0)]), native=native_amp())
    assert r["regs"]["R0"] == 0 and 1 in voices(r)
    assert wt_float(voices(r)[1][7]) == 0.125


@pytest.mark.parametrize(
    "mode,attack,hold",
    [
        (2, 1.0, 0xFFFFFFFF),
        (2, 1 / 31, 0xFFFFFFFF),
        (2, 1 / 512, 0xFFFFFFFF),
        (1, 1 / 16, 8),
    ],
    ids=["adsr-fast", "adsr-boundary", "adsr-slow", "ahd-boundary"],
)
@pytest.mark.parametrize(
    "spread,order,descending",
    [
        (0, 4, False),
        (3, 4, False),
        (-3, 4, False),
        (-3, 4, True),
        (-3, 3, True),
        (3, 3, True),
    ],
    ids=["neutral", "expressive", "downward", "reverse", "played-down", "played-up"],
)
def test_native_amp_fm_handoff_stays_within_ceiling(
    canopy, mode, attack, hold, spread, order, descending, record_property
):
    measured = []

    def native_call(*args, **kwargs):
        result = canopy(
            *args,
            native=native_amp(
                mode=mode, attack=attack, hold=hold, sustain=1, release=0.9991
            ),
            post_amp=True,
            **kwargs,
        )
        if kwargs.get("blocks", 1) == 1:
            measured.append(result["instructions"])
        return result

    test_fm_source_handoff_with_eight_onsets_fits_ceiling(
        native_call, True, spread, order, descending
    )
    reset = canopy(reset=1)
    assert reset["instructions"] <= 1600 and reset["state"] == [0] * 744
    record_property("max_process_instructions", max(measured))
    record_property("reset_instructions", reset["instructions"])
    print(
        "Native AMP mode/attack/hold/process/reset",
        mode,
        attack,
        hold,
        max(measured),
        reset["instructions"],
    )


def test_v5_status_reports_admitted_notes_without_inventing_key_ownership(canopy):
    controls = presentation_controls()
    r = canopy(
        [note(1, 0), note(2, 127)], controls=controls, edits={0: 5, 1: 96, 4: 123}
    )
    ready, *w = r["status"]
    assert ready == 1 and w[0] == 123 and w[2:4] == [1, 1]
    assert (w[1] >> 20) & 7 == 2
    pitches, identities = [], []
    for slot in range(4):
        if w[1] & (1 << (16 + slot)):
            pitches.append((w[5] >> (slot * 8)) & 255)
            member = (w[6] >> (slot * 8)) & 255
            assert member & 7 == 0  # An audio gate cannot prove physical ownership.
            identities.append(member >> 3)
    assert sorted(pitches) == [0, 127] and len(set(identities)) == 2


@pytest.mark.parametrize("wire_parity", [0, 1])
def test_v5_status_fragments_reconstruct_the_applied_presentation_bank(
    canopy, wire_parity
):
    controls = presentation_controls(mode=1)
    fragments = {}
    fast_blocks = []
    for block in range(88):
        r = canopy(controls=controls, edits={0: 5, 1: 96, 4: block, 11: 7})
        ready, *w = r["status"]
        assert ready == 1 and w[0] == block and w[2] == 1
        # Observe only one parity, just like the multiplexed wire reply.
        if block % 2 != wire_parity:
            continue
        if w[1] >> 30:
            assert w[3] == 1 and w[5] == 7
            index = w[4] & 255
            assert w[4] == 0x20B00 | index
            fragments[index] = w[6:]
        else:
            assert (w[1] >> 6) & 3 == 1 and w[3] == 7 and w[11] == 1
            fast_blocks.append(block)
    assert set(fragments) == set(range(11))
    bank = sum((fragments[i] for i in range(11)), [])
    assert bank == controls[16:] + [0] * 50
    assert max(b - a for a, b in zip(fast_blocks, fast_blocks[1:], strict=False)) <= 8


def test_v5_status_tracks_actual_arp_gate_and_release_tails(canopy):
    base = list(STRUM)
    base[6], base[15] = 127, 0
    controls = presentation_controls(base)
    r = canopy([note(1, 60), note(2, 64)], controls=controls, edits={0: 5, 1: 96})
    w = r["status"][1:]
    assert w[7] & 255 == 60 and w[7] & 256 and w[7] & 512
    assert w[1] & (1 << 26) and w[1] & (1 << 27)
    assert 0 < w[8] <= 127
    r = canopy(controls=controls, edits={0: 5, 1: 96}, state_edits={146: 45000})
    w = r["status"][1:]
    assert w[7] & 255 == 60 and not w[1] & (1 << 26)
    # The first four callbacks are fast snapshots.
    canopy([note(2, 64, off=True)], controls=controls, edits={0: 5, 1: 96})
    r = canopy(controls=controls, edits={0: 5, 1: 96})
    w = r["status"][1:]
    assert (w[1] >> 20) & 7 == 1 and (w[1] >> 23) & 7 >= 1
    assert r["instructions"] < 100000


def test_v5_status_does_not_change_rendering_and_clears_ready_on_rejection(canopy):
    controls = presentation_controls()
    batch = [note(1, 60), note(2, 64)]
    legacy = canopy(batch, controls=controls, edits={0: 4, 1: 88})
    current = canopy(batch, state=0x20054000, controls=controls, edits={0: 5, 1: 96})
    assert current["out"] == legacy["out"]
    assert current["state"][:188] == legacy["state"][:188]
    bad = controls.copy()
    bad[18] ^= 1
    r = canopy(controls=bad, edits={0: 5, 1: 96})
    assert r["regs"]["R0"] == 0xFFFFFFFF and r["status"][0] == 0


@pytest.mark.parametrize("clock", [1, 2], ids=["grid", "phase"])
def test_v5_status_clock_acquisition_loss_stop_and_recovery(canopy, clock):
    controls = presentation_controls(base=MASTER if clock == 1 else FINE)

    def sample(block, boundary, flags=3, events=()):
        r = canopy(
            events,
            controls=controls,
            edits={
                **position(block, boundary, flags=flags | 0x300),
                0: 5,
                1: 96,
                20: max(0, boundary - 1) * 5400000,
                21: max(0, boundary - 1) * 5400000,
            },
        )
        assert r["status"][0] == 1
        assert r["status"][2] >> 30 == 0
        assert (r["status"][10] >> 16) & 255 == 3
        return (r["status"][10] >> 8) & 255

    assert sample(0, 0, flags=0, events=[note(1, 60)]) == 1
    assert sample(1, 1) == 1
    assert sample(188, 2) == 2
    assert sample(600, 2) == 3
    # Four detail callbacks separate successive fast publication groups.
    for block in range(601, 605):
        canopy(controls=controls, edits={**position(block, 2), 0: 5, 1: 96})
    assert sample(605, 2, flags=0x400) == 4
    assert sample(606, 2, flags=0) == 1
    assert sample(607, 3) == 1
    assert sample(794, 4) == 2


@pytest.mark.parametrize("version,size", [(5, 96), (6, 128), (7, 136)])
def test_v5_pattern_tempo_schedules_without_phase_provider(canopy, version, size):
    controls = presentation_controls(base=FINE)
    r = canopy(
        [note(1, 60), note(2, 64)],
        controls=controls,
        native=[0] * 8 if version >= 6 else None,
        post_amp=version == 7,
        edits={0: version, 1: size, 12: 0x800 | (14400 << 16)},
        blocks=600,
        advance_clock=True,
    )
    elapsed = 599 * 2 * 14400 * 3
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000
    assert r["state"][170] == 2


@pytest.mark.parametrize("version,size", [(5, 96), (6, 128), (7, 136)])
def test_v5_pattern_arp_rejects_uncapable_phase_without_tempo(canopy, version, size):
    controls = presentation_controls(base=FINE)
    for block, boundary in [(0, 1), (188, 2), (376, 3)]:
        r = canopy(
            [note(1, 60)] if block == 0 else (),
            controls=controls,
            native=[0] * 8 if version >= 6 else None,
            post_amp=version == 7,
            edits={
                **position(block, boundary, flags=0x103),
                0: version,
                1: size,
                # Stale phase words can appear plausible and advance, but
                # only grid timing is advertised and no BPM has been accepted.
                20: (boundary - 1) * 5400000,
                21: (boundary - 1) * 5400000,
            },
        )
        assert r["state"][149] == 0 and r["state"][147] == 0
        assert r["state"][136] == 0
        assert (r["status"][10] >> 8) & 255 == 1
        assert (r["status"][10] >> 16) & 255 == 1


def test_v5_status_token_change_starts_a_new_immutable_detail_revision(canopy):
    controls = presentation_controls()
    first = canopy([note(11, 60)], controls=controls, edits={0: 5, 1: 96})
    revision = first["status"][12]
    fragments = {}
    for block in range(1, 88):
        r = canopy(controls=controls, edits={0: 5, 1: 96, 4: block})
        w = r["status"][1:]
        if w[1] >> 30:
            assert w[3] == revision
            fragments[w[4] & 255] = w[6:]
    bank = sum((fragments[i] for i in range(11)), [])
    assert sorted(t for t in bank[38:42] if t) == [11]
    changed = canopy(
        [note(11, 60, off=True), note(12, 60)], controls=controls, edits={0: 5, 1: 96}
    )
    assert changed["status"][2] >> 30 == 0
    assert changed["status"][12] == revision + 1


@pytest.mark.parametrize("other_owner", [0x100, 0x200])
def test_v2_chord_independent_release_and_state_isolation(canopy, other_owner):
    assert canopy(reset=1)["state"] == [0] * 744
    result = canopy([note(1, 60), note(2, 64)])
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1, 2}
    result = canopy([note(3, 67, owner=other_owner)])
    assert set(voices(result)) == {1, 2, 3}
    assert any(result["out"])
    held = voices(result)
    other = canopy([note(4, 72)], state=0x20054000, track=1)
    assert set(voices(other)) == {4}
    result = canopy([note(2, 64, 0, off=True)])
    assert voices(result)[1][1] == held[1][1] == 1
    assert voices(result)[3][1] == held[3][1] == 1
    assert voices(result)[2][1] == 0
    assert result["state"][128:132] == [1, 0, 1, 1]


def test_v2_pedal_release_preserves_physically_held_notes(canopy):
    canopy([note(1, 60), note(2, 64)])
    canopy([[3, 0, 0, 64, 127, 0]])
    result = canopy([note(1, 60, 0, off=True)])
    assert voices(result)[1][1] == 1
    result = canopy([[3, 0, 0, 64, 0, 0]])
    assert voices(result)[1][1] == 0
    assert voices(result)[2][1] == 1


@pytest.mark.parametrize(
    "bad",
    [
        [1, 0, 2, 64 << 8, 100 << 8, 1],
        [1, 0x300, 2, 64 << 8, 100 << 8, 0],
        [1, 0x201, 2, 64 << 8, 100 << 8, 0],
        [3, 0x200, 0, 64, 127, 0],
        [4, 0x200, 0, 0, 8192, 0],
        [1, 0, 0, 64 << 8, 100 << 8, 0],
        [1, 0, 2, 64 << 8, 0, 0],
        [4, 0, 0, 0, 16384, 0],
    ],
)
def test_invalid_normalized_batch_is_atomic(canopy, bad):
    before = canopy([note(1, 60)])["state"]
    result = canopy([note(2, 64), bad])
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == before


def test_generation_change_requires_reset_and_reset_is_per_instance(canopy):
    canopy([note(1, 60)])
    canopy([note(2, 67)], state=0x20054000, track=1)
    assert canopy(generation=2)["regs"]["R0"] == 0xFFFFFFFF
    assert canopy(reset=3)["state"] == [0] * 744
    assert canopy(generation=2)["regs"]["R0"] == 1
    assert set(voices(canopy(state=0x20054000, track=1))) == {2}


@pytest.mark.parametrize("other_owner", [0x100, 0x200])
def test_velocity_is_per_note_and_other_route_bend_isolated(canopy, other_owner):
    result = canopy([note(1, 60, 20), note(2, 67, 127, owner=other_owner)])
    before = voices(result)
    assert before[1][6] < before[2][6]  # positive IEEE float gain words
    result = canopy([[4, 0, 0, 0, 16383, 0]])
    bent = voices(result)
    assert bent[1][4:6] != before[1][4:6]
    assert bent[2][4:7] == before[2][4:7]
    assert bent[1][6] == before[1][6]
    result = canopy([[3, 0, 0, 121, 0, 0]])
    assert voices(result)[1][4:7] == before[1][4:7]
    assert voices(result)[2][4:7] == before[2][4:7]


def test_full_batch_stealing_is_bounded(canopy):
    result = canopy([note(i + 1, 60 + i) for i in range(8)])
    assert result["regs"]["R0"] == 0
    assert result["instructions"] < 100000
    assert any(result["out"])
    for _ in range(2):
        result = canopy()
    assert set(voices(result)) == {5, 6, 7, 8}


def test_other_notes_do_not_reset_held_voice_phase_or_gain(canopy):
    # Reference has no E; otherwise C/G receive identical timed commands.
    canopy([note(1, 60), note(2, 64)])
    canopy([note(1, 60)], state=0x20054000, track=1)
    actual = canopy([note(3, 67)])
    reference = canopy([note(3, 67)], state=0x20054000, track=1)
    for token in (1, 3):
        assert voices(actual)[token] == voices(reference)[token]
    actual = canopy([note(2, 64, 0, off=True)])
    reference = canopy(state=0x20054000, track=1)
    for token in (1, 3):
        assert voices(actual)[token] == voices(reference)[token]
    for _ in range(3):
        actual = canopy()
        reference = canopy(state=0x20054000, track=1)
        for token in (1, 3):
            assert voices(actual)[token] == voices(reference)[token]


def wire_packet(events, *, track=0, generation=1, revision=1, epoch=1, movement=None):
    words = [0x494E0101, 1 | (len(events) << 8), epoch, 1, 1, revision, 0, 0] + [0] * 24
    for i, (token, kind, pitch, value, port, channel) in enumerate(events):
        words[8 + 3 * i : 11 + 3 * i] = [
            token,
            generation,
            kind | port << 4 | channel << 6 | track << 10 | pitch << 14 | value << 21,
        ]
    if movement is not None:
        words[1] = 5 | (len(events) << 8)
        words[4] = movement
    a = b = 0
    for i, word in enumerate(words):
        if i != 7:
            a = (a + word) & 0xFFFFFFFF
            b = (b + a) & 0xFFFFFFFF
    words[7] = a ^ b
    return words


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
@pytest.mark.parametrize("other_route", [(1, 3), (2, 0)])
def test_transport_packet_reaches_instance_and_preserves_other_note(
    canopy, other_route
):
    result = canopy(
        packet=wire_packet([(1, 1, 60, 24, 0, 0), (2, 1, 64, 127, *other_route)])
    )
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1, 2}
    assert voices(result)[1][6] < voices(result)[2][6]
    canopy(packet=wire_packet([(0, 3, 64, 127, 0, 0)]))
    result = canopy(packet=wire_packet([(1, 2, 60, 0, 0, 0)]))
    assert voices(result)[1][1] == 1
    result = canopy(packet=wire_packet([(0, 3, 64, 0, 0, 0)]))
    assert voices(result)[1][1] == 0
    assert voices(result)[2][1] == 1


def test_panel_cancel_and_release_preserve_external_notes(canopy):
    canopy([note(1, 60), note(2, 60, owner=0x200), note(3, 67, owner=0x200)])
    result = canopy([note(2, 60, off=True, owner=0x200)])
    assert voices(result)[1][1] == 1
    assert voices(result)[2][1] == 0
    assert voices(result)[3][1] == 1
    result = canopy([[3, 0x200, 0, 120, 0, 0]])
    assert voices(result)[1][1] == 1
    assert not any(v[1] == 1 for t, v in voices(result).items() if t != 1)


@pytest.mark.parametrize(
    "change", [{"track": 1}, {"generation": 2}, {"revision": 2}, {"epoch": 2}]
)
@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_stale_transport_packet_never_calls_instrument(canopy, change):
    before = canopy([note(1, 60)])["state"]
    result = canopy(packet=wire_packet([(2, 1, 64, 127, 0, 0)], **change))
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == before
    assert result["out"] == [0] * 32


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_invalid_late_event_does_not_apply_earlier_event(canopy):
    before = canopy([note(1, 60)])["state"]
    result = canopy(packet=wire_packet([(2, 1, 64, 127, 0, 0), (3, 1, 67, 0, 0, 0)]))
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == before
    assert result["out"] == [0] * 32


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_packet_pipeline_empty_frame_preserves_phase_and_rejects_corruption(canopy):
    canopy(packet=wire_packet([(1, 1, 60, 100, 0, 0)]))
    result = canopy(packet=[])
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1}
    assert any(result["out"])
    before = result["state"]
    packet = wire_packet([(2, 1, 67, 100, 0, 0)])
    packet[7] ^= 1
    result = canopy(packet=packet)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == before
    assert result["out"] == [0] * 32


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_packet_bend_and_full_event_batch(canopy):
    result = canopy(
        packet=wire_packet([(i + 1, 1, 60 + i, 100, i % 2, 0) for i in range(8)])
    )
    assert result["regs"]["R0"] == 0
    for _ in range(2):
        result = canopy(packet=[])
    assert set(voices(result)) == {5, 6, 7, 8}
    before = voices(result)
    result = canopy(packet=wire_packet([(0, 4, 127, 127, 0, 0)]))
    for token in (5, 7):
        assert voices(result)[token][4:6] != before[token][4:6]
    for token in (6, 8):
        assert voices(result)[token][4:7] == before[token][4:7]


def test_replace_reset_preserves_phase_then_finishes_bounded_tail(canopy):
    import struct

    canopy([note(1, 60)])
    before = canopy()
    reset = canopy(reset=2)
    assert reset["state"][2:8] == before["state"][2:8]
    assert reset["state"][1] == 2
    assert reset["state"][48:132] == [0] * 84
    assert reset["state"][132:200] == before["state"][132:200]
    first = canopy(generation=2)
    second = canopy(generation=2)
    quiet = canopy(generation=2)
    assert any(first["out"])
    assert quiet["regs"]["R0"] == 1
    assert quiet["out"] == [0] * 32
    assert second["state"][0] == 0
    values = [
        struct.unpack("<f", struct.pack("<I", word))[0]
        for word in first["out"] + second["out"]
    ]
    assert max(abs(b - a) for a, b in zip(values, values[1:], strict=False)) < 0.1


@pytest.mark.parametrize(
    "controls",
    [
        (1,),
        (3, 1, 127, 127, 0, 127),
        (4, 1, 127, 127, 0, 127, 127, 300, 8, 100),
        (5, 1, 127, 127, 0, 127, 127, 300, 8, 100, 4, 3, 75, 0, 0, 750),
    ],
)
def test_callback_instruction_ceiling(canopy, controls):
    from functools import partial

    canopy = partial(canopy, controls=controls)
    measured = {}
    measured["create"] = canopy(reset=1)["instructions"]
    measured["eight_on"] = canopy([note(i + 1, 48 + i, 127) for i in range(8)])[
        "instructions"
    ]
    measured["eight_replace"] = canopy([note(i + 9, 60 + i, 127) for i in range(8)])[
        "instructions"
    ]
    measured["six_slots"] = canopy()["instructions"]
    measured["eight_bends"] = canopy([[4, 0, 0, 0, 16383 - i, 0] for i in range(8)])[
        "instructions"
    ]
    measured["eight_pedals"] = canopy(
        [[3, 0, 0, 64, 127 if i % 2 else 0, 0] for i in range(8)]
    )["instructions"]
    measured["eight_off"] = canopy(
        [note(i + 9, 60 + i, 0, off=True) for i in range(8)]
    )["instructions"]
    measured["replace_reset"] = canopy(reset=2)["instructions"]
    measured["release_tail"] = canopy()["instructions"]
    measured["unload"] = canopy(reset=4)["instructions"]
    assert max(measured[k] for k in ("create", "replace_reset", "unload")) < 1600
    assert max(measured.values()) < 100000
    print("CANOPY instruction counts (not device cycles):", measured)


@pytest.mark.parametrize(
    "bad",
    [
        (3, 2, 0, 0, 5, 15),
        (3, 0, 128, 0, 5, 15),
        (3, 0, 0, 128, 5, 15),
        (3, 0, 0, 0, 128, 15),
        (3, 0, 0, 0, 5, 128),
        (3, 0, 0, 0, 5),
    ],
)
def test_pad_invalid_controls_cannot_mutate_notes(canopy, bad):
    before = canopy([note(1, 60)])["state"]
    rejected = canopy([note(2, 64)], controls=bad)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF
    assert rejected["state"] == before


def test_pad_live_settings_preserve_tokens_phase_and_instance_state(canopy):
    import struct

    def fp(w):
        return struct.unpack("<f", struct.pack("<I", w))[0]

    a = (3, 0, 127, 0, 5, 15)
    b = (3, 1, 0, 127, 127, 127)
    first = canopy([note(1, 60)], controls=a)
    second = canopy(controls=b)
    assert set(voices(second)) == {1}
    assert voices(second)[1][2:4] != voices(first)[1][2:4]
    assert fp(voices(second)[1][7]) > fp(voices(first)[1][7])
    assert 0 < fp(second["state"][132]) < fp(first["state"][132])
    assert 0 < fp(second["state"][134]) < 0.00289
    other = canopy([note(2, 67)], state=0x20054000, track=1, controls=b)
    assert other["state"][132] == 0  # no color leaked from the first instance
    after = canopy(controls=b)
    assert fp(after["state"][132]) < fp(second["state"][132])
    assert set(voices(after)) == {1}
    assert after["instructions"] < 100000


def test_pad_envelope_range_and_reset_keep_the_tail_spectrum(canopy):
    import struct

    def fp(w):
        return struct.unpack("<f", struct.pack("<I", w))[0]

    c = (3, 1, 127, 127, 127, 127)
    first = canopy([note(1, 60)], controls=c)
    assert fp(voices(first)[1][7]) == pytest.approx(32 / (48 * 1788), rel=1e-5)
    before = canopy(controls=c)
    released = canopy([note(1, 60, off=True)], controls=c)
    assert fp(voices(before)[1][7]) - fp(voices(released)[1][7]) == pytest.approx(
        32 / (48 * 8168), rel=1e-4
    )
    reset = canopy(reset=2)
    assert reset["state"][132:200] == released["state"][132:200]
    assert reset["state"][48:132] == [0] * 84
    assert canopy(generation=2, controls=c)["regs"]["R0"] == 0
    assert canopy(generation=2, controls=c)["regs"]["R0"] == 1


@pytest.mark.parametrize("gain", [0x7FC00000, 0x7F800000, 0x49742400])
def test_pad_bad_audio_is_silenced_before_the_stock_chain(canopy, gain):
    controls = (3, 1, 127, 127, 0, 127)
    result = canopy([note(1, 60)], controls=controls)
    slot = next(i for i in range(0, 32, 8) if result["state"][i] == 1)
    bad = canopy(controls=controls, state_edits={slot + 6: gain})
    assert bad["regs"]["R0"] == 0xFFFFFFFF
    assert bad["out"] == [0] * 32


def test_pad_drift_wrap_preserves_note_and_bounded_phase(canopy):
    import struct

    controls = (3, 1, 127, 127, 5, 15)
    before = canopy([note(1, 60)], controls=controls)
    phase = struct.unpack("<I", struct.pack("<f", 0.99995))[0]
    after = canopy(controls=controls, state_edits={133: phase})
    value = struct.unpack("<f", struct.pack("<I", after["state"][133]))[0]
    assert 0 <= value < 0.0002
    assert set(voices(after)) == set(voices(before)) == {1}
    assert after["regs"]["R0"] == 0


LOCAL = (4, 0, 64, 0, 0, 0, 127, 120, 4, 50)


def test_local_arp_is_independent_of_four_pad_owners(canopy):
    r = canopy([note(1, 67), note(2, 60), note(3, 64), note(4, 72)], controls=LOCAL)
    assert set(voices(r)) == {1, 2, 3, 4}
    assert r["state"][136] == 1
    assert r["state"][151] == 60
    pad = voices(r)
    r = canopy(controls=LOCAL, state_edits={146: 89999})
    assert r["state"][151] == 64
    for _ in range(3):
        r = canopy(controls=LOCAL)
    assert r["state"][136] == 2
    assert set(voices(r)) == set(pad)
    assert all(v[1] == 1 for v in voices(r).values())
    assert r["instructions"] < 100000


def test_local_arp_chord_edit_excludes_released_tail(canopy):
    canopy([note(1, 60), note(2, 64), note(3, 67)], controls=LOCAL)
    r = canopy([note(2, 64, off=True)], controls=LOCAL)
    assert r["state"][151] == 60
    r = canopy(controls=LOCAL, state_edits={146: 89999})
    assert r["state"][151] == 67
    assert voices(r)[2][1] == 0
    assert voices(r)[1][1] == voices(r)[3][1] == 1


def test_local_arp_gate_and_disable_never_release_pad(canopy):
    canopy([note(1, 60)], controls=LOCAL)
    r = canopy(controls=LOCAL, state_edits={146: 45000})
    assert r["state"][137] == 0
    assert voices(r)[1][1] == 1
    r = canopy(controls=LOCAL[:6] + (0,) + LOCAL[7:])
    assert r["state"][147] == r["state"][150] == 0
    assert voices(r)[1][1] == 1


def test_local_arp_reset_cancels_pending_and_bounds_both_lanes(canopy):
    canopy([note(1, 60), note(2, 64)], controls=LOCAL)
    canopy(controls=LOCAL, state_edits={146: 89999})
    r = canopy(reset=2)
    assert r["state"][137] == 2
    assert r["state"][146:200] == [0] * 54
    for _ in range(4):
        r = canopy(controls=(3, 0, 64, 0, 0, 0))
    assert r["state"][136] == 0
    assert not voices(r)


@pytest.mark.parametrize(
    "index,value",
    [(0, 5), (6, 128), (7, 29), (7, 301), (8, 0), (8, 9), (9, 0), (9, 101)],
)
def test_local_arp_rejects_bad_controls_atomically(canopy, index, value):
    before = canopy([note(1, 60)], controls=LOCAL)["state"]
    bad = list(LOCAL)
    bad[index] = value
    r = canopy([note(2, 64)], controls=bad)
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert r["state"] == before


def test_local_arp_pending_candidate_cannot_survive_release(canopy):
    canopy([note(1, 60), note(2, 64)], controls=LOCAL)
    canopy(controls=LOCAL, state_edits={146: 89999})
    r = canopy([note(2, 64, off=True)], controls=LOCAL)
    assert r["state"][150] == 0


@pytest.mark.parametrize("owner", [0, 0x200])
def test_local_arp_live_bend_preserves_identity(canopy, owner):
    a = canopy([note(1, 60, owner=owner)], controls=LOCAL)
    b = canopy([[4, 0, 0, 0, 16383, 0]], controls=LOCAL)
    assert a["state"][136] == b["state"][136] == 1
    if owner == 0x200:
        assert a["state"][140:142] == b["state"][140:142]
    else:
        assert a["state"][140:142] != b["state"][140:142]
    assert b["state"][137] == 1


@pytest.mark.parametrize("bpm,rate", [(137, 3), (300, 8)])
def test_local_arp_real_block_clock_retains_rational_phase(canopy, bpm, rate):
    controls = LOCAL[:7] + (bpm, rate, 50)
    r = canopy([note(1, 60), note(2, 64), note(3, 67)], controls=controls, blocks=600)
    assert r["state"][149] == 1 + (599 * bpm * rate) // 90000
    assert r["state"][146] == (599 * bpm * rate) % 90000
    assert set(voices(r)) == {1, 2, 3}


def test_local_arp_output_corruption_is_silenced(canopy):
    r = canopy([note(1, 60)], controls=LOCAL)
    r = canopy(controls=LOCAL, state_edits={138: 0x7FC00000})
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert r["out"] == [0] * 32


@pytest.mark.parametrize(
    "order,expected",
    [
        (0, [60, 64, 67, 72, 76, 79, 60]),
        (1, [79, 76, 72, 67, 64, 60, 79]),
        (2, [60, 64, 67, 72, 76, 79, 76, 72, 67, 64, 60]),
        (3, [67, 60, 64, 79, 72, 76, 67]),
    ],
)
def test_movement_order_and_octaves(canopy, order, expected):
    controls = (5,) + LOCAL[1:] + (order, 2, 0, 0, 0, 0)
    r = canopy([note(1, 67), note(2, 60), note(3, 64)], controls=controls)
    pitches = [r["state"][151]]
    for _ in expected[1:]:
        r = canopy(controls=controls, state_edits={146: 89999})
        pitches.append(r["state"][151])
    assert pitches == expected
    assert set(voices(r)) == {1, 2, 3}


def test_movement_octaves_omit_out_of_range_and_bend_retains_octave(canopy):
    controls = (5,) + LOCAL[1:] + (0, 3, 0, 0, 0, 0)
    r = canopy([note(1, 115)], controls=controls)
    assert r["state"][151] == 115
    r = canopy(controls=controls, state_edits={146: 89999})
    assert r["state"][151] == 127
    for _ in range(3):
        r = canopy(controls=controls)
    before = r["state"][140:142]
    r = canopy([[4, 0, 0, 0, 8192, 0]], controls=controls)
    assert r["state"][140:142] == before
    r = canopy(controls=controls, state_edits={146: 89999})
    assert r["state"][151] == 115


def test_movement_random_stays_in_chord_and_is_instance_local(canopy):
    controls = (5,) + LOCAL[1:] + (4, 2, 0, 0, 0, 0)
    a = canopy([note(1, 60), note(2, 67)], controls=controls)
    b = canopy([note(1, 60), note(2, 67)], controls=controls, state=0x20054000, track=1)
    assert a["state"][151] == b["state"][151]
    values = set()
    for _ in range(20):
        a = canopy(controls=controls, state_edits={146: 89999})
        values.add(a["state"][151])
    assert values == {60, 67, 72, 79}


def test_movement_swing_latches_at_pair_boundary(canopy):
    controls = (5,) + LOCAL[1:] + (0, 1, 50, 0, 0, 0)
    r = canopy([note(1, 60)], controls=controls)
    assert r["state"][158:160] == [0, 50]
    r = canopy(controls=controls, state_edits={146: 90000})
    assert r["state"][149] == 1
    changed = controls[:12] + (25,) + controls[13:]
    r = canopy(controls=changed, state_edits={146: 134999})
    assert r["state"][149] == 2
    assert r["state"][158:160] == [1, 50]
    r = canopy(controls=changed, state_edits={146: 44999})
    assert r["state"][149] == 3
    assert r["state"][158:160] == [0, 25]


@pytest.mark.parametrize(
    "index,value",
    [(10, 5), (10, 0xFFFFFFFF), (11, 0), (11, 4), (12, 76), (12, 0xFFFFFFFF)],
)
def test_movement_invalid_configuration_is_atomic(canopy, index, value):
    controls = (5,) + LOCAL[1:] + (0, 1, 0, 0, 0, 0)
    before = canopy([note(1, 60)], controls=controls)["state"]
    bad = list(controls)
    bad[index] = value
    r = canopy([note(2, 64)], controls=bad)
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert r["state"] == before


def test_shared_arp_tails_keep_lane_and_do_not_alias_pad_tokens(canopy):
    c = (5, 0, 64, 0, 0, 127, 127, 120, 4, 50, 0, 1, 0, 0, 0, 0)
    canopy([note(1, 60), note(2, 64), note(3, 67), note(4, 72)], controls=c)
    for _ in range(3):
        canopy(controls=c)
    r = canopy(controls=c, state_edits={146: 89999})
    assert r["state"][32] == 1 and r["state"][33] == 4
    assert r["state"][136] == 2
    tail = r["state"][36:38]
    r = canopy([note(1, 60, off=True), [4, 0, 0, 0, 16383, 0]], controls=c)
    assert r["state"][33] == 4
    assert r["state"][36:38] == tail
    assert voices(r)[1][1] == 0
    for _ in range(4):
        r = canopy(controls=c, state_edits={146: 89999})
    assert sum(bool(r["state"][i]) for i in (0, 8, 16, 24, 32, 40, 136)) <= 7
    r = canopy(reset=2)
    assert r["state"][33] == 6
    for _ in range(4):
        r = canopy(controls=c)
    assert not any(r["state"][i] for i in (0, 8, 16, 24, 32, 40, 136))


MASTER = (5,) + LOCAL[1:8] + (1,) + LOCAL[9:] + (0, 1, 0, 1, 0, 0)


def position(block, boundary, step=0, generation=1, flags=3):
    return {4: block, 12: flags, 13: generation, 14: boundary, 15: step, 16: 16}


FINE = MASTER[:8] + (3,) + MASTER[9:13] + (2, 2, 0)


def phase_position(ticks, block, origin=0, **kwargs):
    boundary, remainder = divmod(ticks, 5400000)
    return {
        **position(block, boundary + 1, boundary % 16, **kwargs),
        0: 4,
        1: 88,
        20: (origin + ticks) & 0xFFFFFFFF,
        21: (origin + ticks - remainder) & 0xFFFFFFFF,
    }


@pytest.mark.parametrize("origin", [0, 0xFFD00000])
def test_fine_master_triplet_carry_gate_and_wrap(canopy, origin):
    # Real phase advances, including uint32 rollover; no scheduler state edits.
    events = [note(1, 60), note(2, 64), note(3, 67)]
    onsets = []
    serial = 0
    for block in range(94):
        ticks = block * 300000
        r = canopy(
            events if block == 0 else (),
            controls=FINE,
            edits=phase_position(ticks, block, origin),
        )
        assert r["regs"]["R0"] in (0, 1)
        if r["state"][149] != serial:
            serial = r["state"][149]
            onsets.append((ticks, r["state"][151]))
        if ticks == 9000000:
            assert r["state"][137] == 0  # 50% gate at half a triplet
    assert onsets == [(5400000, 60), (12600000, 64), (19800000, 67), (27000000, 60)]
    assert r["state"][146] == 2700000


def test_fine_master_backward_correction_loss_and_recovery(canopy):
    canopy([note(1, 60)], controls=FINE, edits=phase_position(0, 0))
    r = canopy(controls=FINE, edits=phase_position(5400000, 18))
    assert r["state"][149] == 1
    canopy(controls=FINE, edits=phase_position(5700000, 19))
    r = canopy(controls=FINE, edits=phase_position(5550000, 20))
    assert r["state"][146] == 900000
    r = canopy(controls=FINE, edits=phase_position(6000000, 21))
    assert r["state"][146] == 1800000  # correction is not counted twice
    r = canopy(controls=FINE, edits=phase_position(6000000, 55))
    assert r["state"][170] == 3 and r["state"][147] == 0
    r = canopy(controls=FINE, edits=phase_position(10800000, 60))
    assert r["state"][149] == 1
    r = canopy(controls=FINE, edits=phase_position(16200000, 78))
    assert r["state"][149] == 2
    for block in range(79, 83):
        r = canopy(controls=FINE, edits=position(block, 4))  # old provider
    assert r["state"][136] == 0 and r["state"][170] == 3
    assert voices(r)[1][1] == 1


def test_fine_master_short_provider_gap_preserves_grid_and_sequence(canopy):
    """Two absent wire frames must not replay or advance a held chord early."""
    controls = FINE[:8] + (8,) + FINE[9:]
    continuous_state, gapped_state = 0x20050000, 0x20054000

    def frame(block, ticks, available=True):
        return {
            **phase_position(ticks, block),
            0: 5,
            1: 96,
            12: 0x303 if available else 0,
        }

    notes = [note(1, 60), note(2, 64), note(3, 67)]
    for state in (continuous_state, gapped_state):
        for block in range(19):
            first = canopy(
                notes if block == 0 else (),
                controls=controls,
                state=state,
                edits=frame(block, block * 300000),
            )
        assert first["state"][149] == 1 and first["state"][151] == 60
    for block in range(19, 30):
        ticks = block * 300000
        uninterrupted = canopy(
            controls=controls, state=continuous_state, edits=frame(block, ticks)
        )
        intermittent = canopy(
            controls=controls,
            state=gapped_state,
            edits=frame(block, ticks, available=block not in (21, 22)),
        )
        if block in (21, 22):
            assert intermittent["state"][173] == block - 20, (
                block,
                intermittent["state"][160:174],
            )
            assert intermittent["state"][170] == 2, (
                block,
                intermittent["state"][160:174],
            )
        if block >= 23:
            assert intermittent["state"][149] == uninterrupted["state"][149], (
                block,
                intermittent["state"][136:174],
                uninterrupted["state"][136:174],
            )
            assert intermittent["state"][151] == uninterrupted["state"][151], block
            assert intermittent["state"][146] == uninterrupted["state"][146], block
    assert uninterrupted["state"][149] == intermittent["state"][149] == 2
    assert uninterrupted["state"][151] == intermittent["state"][151] == 64


def test_fine_master_short_gaps_during_initial_observation_acquire(canopy):
    """A fresh boundary can acquire after short gaps without an early note."""
    controls = presentation_controls(base=FINE)
    reference_state, gapped_state = 0x20050000, 0x20054000
    notes = [note(1, 60), note(2, 64), note(3, 67)]
    for block in range(19):
        for state in (reference_state, gapped_state):
            missing = state == gapped_state and block in (4, 5, 10, 11, 16, 17)
            timing = {
                **phase_position(block * 300000, block),
                0: 5,
                1: 96,
                12: 0 if missing else 0x303,
            }
            result = canopy(
                notes if block == 0 else (),
                controls=controls,
                state=state,
                edits=timing,
                state_edits={188: 0} if missing else None,
            )
            if block < 18:
                assert result["state"][149] == 0
                if missing:
                    assert result["state"][170] == 1
                    assert not result["status"][2] & 0x40000000
                    assert (result["status"][10] >> 8) & 255 == 1
            else:
                assert result["state"][149] == 1 and result["state"][151] == 60
                assert result["state"][170] == 2


def test_fine_master_missing_provider_before_acquisition_fails_closed(canopy):
    controls = presentation_controls(base=FINE)
    result = canopy(
        [note(1, 60)],
        controls=controls,
        edits={**phase_position(0, 0), 0: 5, 1: 96, 12: 0},
    )
    assert result["state"][173] == 0
    assert result["state"][170] == 3 and result["state"][147] == 0
    assert result["state"][149] == 0


@pytest.mark.parametrize("transition", ["expired", "stop", "long", "discontinuity"])
def test_fine_master_provider_gap_remains_bounded(canopy, transition):
    """The hold cannot turn STOP, expiry or a bad clock into a local clock."""
    controls = presentation_controls(base=FINE)

    def frame(block, ticks, flags):
        return {**phase_position(ticks, block), 0: 5, 1: 96, 12: flags}

    canopy([note(1, 60)], controls=controls, edits=frame(0, 0, 0x303))
    started = canopy(controls=controls, edits=frame(1, 5400000, 0x303))
    assert started["state"][149] == 1 and started["state"][170] == 2
    held = canopy(controls=controls, edits=frame(2, 5472000, 0))
    assert held["state"][173] == 1 and held["state"][149] == 1
    assert (held["status"][10] >> 8) & 255 == 2
    if transition == "long":
        held = canopy(controls=controls, edits=frame(3, 5544000, 0))
        assert held["state"][173] == 2 and held["state"][170] == 2
        result = canopy(controls=controls, edits=frame(4, 5616000, 0))
        assert result["state"][173] == 0 and result["state"][170] == 3
        assert result["state"][147] == 0
    elif transition == "expired":
        result = canopy(controls=controls, edits=frame(3, 5544000, 0x300))
        assert result["state"][173] == 0 and result["state"][170] == 3
        assert result["state"][147] == 0
    elif transition == "stop":
        result = canopy(controls=controls, edits=frame(3, 5544000, 0x700))
        assert result["state"][173] == 0 and result["state"][147] == 0
        assert (result["status"][10] >> 8) & 255 == 4
    else:
        result = canopy(controls=controls, edits=frame(3, 7200000, 0x303))
        assert result["state"][173] == 0 and result["state"][170] == 1
        assert result["state"][147] == 0 and result["state"][149] == 1


def test_fine_master_swing_and_tempo_change(canopy):
    controls = FINE[:8] + (4,) + FINE[9:12] + (50,) + FINE[13:]
    serial, ticks, onsets = 0, 0, []
    for block in range(100):
        r = canopy(
            [note(1, 60)] if block == 0 else (),
            controls=controls,
            edits=phase_position(ticks, block),
        )
        if r["state"][149] != serial:
            serial = r["state"][149]
            onsets.append(ticks)
        ticks += 300000 if block < 40 else 600000
    assert onsets[:5] == [5400000, 13800000, 16200000, 24600000, 27000000]
    # Alternating long/short pairs retain their exact musical total across tempo change.
    assert onsets[4] - onsets[2] == 10800000


def stopped_pattern(block=0, tempo=14400):
    """Coherent selected stock tempo (BPM*120), with transport stopped."""
    return {0: 5, 1: 96, 4: block, 12: 0xF00 | (tempo << 16)}


@pytest.mark.parametrize("tempo,rate", [(3600, 1), (16441, 3), (36000, 8)])
def test_pattern_arp_plays_stopped_at_fractional_stock_tempo(canopy, tempo, rate):
    controls = FINE[:8] + (rate,) + FINE[9:]
    r = canopy(
        [note(1, 60), note(2, 64), note(3, 67)],
        controls=controls,
        edits=stopped_pattern(tempo=tempo),
        blocks=600,
        advance_clock=True,
    )
    elapsed = 599 * 2 * tempo * rate
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000
    assert set(voices(r)) == {1, 2, 3}
    assert r["state"][162] == 4 and r["state"][170] == 2


@pytest.mark.parametrize("restart", [0, 2])
def test_stopped_arp_advances_with_stable_phrase_and_only_restarts_on_new_phrase(
    canopy, restart
):
    controls = FINE[:8] + (8,) + FINE[9:14] + (restart, 0)
    timing = {**stopped_pattern(tempo=21600), 18: 0x80000001}
    r = canopy(
        [note(1, 60), note(2, 64), note(3, 67)],
        controls=controls,
        edits=timing,
        blocks=190,
        advance_clock=True,
    )
    assert r["state"][149] == 4
    assert r["state"][151] == 60
    r = canopy(
        controls=controls,
        edits={**stopped_pattern(190, 21600), 18: 0x80000002},
    )
    if restart == 0:
        assert r["state"][149] == 5 and r["state"][151] == 60
    else:
        assert r["state"][149] == 4


def test_pattern_arp_stopped_gate_swing_live_tempo_and_note_release(canopy):
    controls = FINE[:8] + (4,) + FINE[9:12] + (50,) + FINE[13:]
    r = canopy(
        [note(1, 60)],
        controls=controls,
        edits=stopped_pattern(),
        blocks=140,
        advance_clock=True,
    )
    assert r["state"][149] == 1 and r["state"][137] == 1
    r = canopy(controls=controls, edits=stopped_pattern(140))
    assert r["state"][137] == 1
    r = canopy(controls=controls, edits=stopped_pattern(141))
    assert r["state"][137] == 0  # halfway through the long swung interval
    # Double the pattern tempo without resetting the accumulated phase.
    r = canopy(
        controls=controls,
        edits=stopped_pattern(142, 28800),
        blocks=71,
        advance_clock=True,
    )
    assert r["state"][149] == 2 and r["state"][146] == 201600
    r = canopy(
        controls=controls,
        edits=stopped_pattern(213, 28800),
        blocks=47,
        advance_clock=True,
    )
    assert r["state"][149] == 3 and r["state"][146] == 230400
    r = canopy(
        [note(1, 60, off=True)],
        controls=controls,
        edits=stopped_pattern(260),
        blocks=600,
        advance_clock=True,
    )
    assert r["state"][149] == 3 and r["state"][136] == 0
    assert not voices(r)


@pytest.mark.parametrize("restart", [0, 2], ids=["trig", "continue"])
def test_pattern_arp_transport_and_phase_churn_do_not_reset_cadence(canopy, restart):
    controls = presentation_controls(base=FINE[:8] + (8,) + FINE[9:14] + (restart, 0))
    notes = [note(1, 60), note(2, 64), note(3, 67)]
    serial, onsets = 0, []
    for block in range(120):
        # PLAY/STOP, missing phase, backward corrections, generation changes,
        # and repeated/wrapped transport stamps do not change audio time.
        flags = (0xF00, 0xB03, 0x800, 0, 0xB00, 0xF00)[block % 6]
        r = canopy(
            notes if block == 0 else (),
            controls=controls,
            edits={
                **stopped_pattern(block if block % 5 else 0xFFFFFFFF, 36000),
                12: flags | ((36000 << 16) if flags else 0),
                13: block // 3 + 1,
                14: (block * 97) & 0xFFFFFFFF,
                15: block % 16,
                18: 0x80000001,
                20: (0xFFFFFFFF - block * 172900) & 0xFFFFFFFF,
                21: block * 100000,
            },
            state_edits={188: 0},
        )
        elapsed = block * 2 * 36000 * 8
        assert r["state"][146] == elapsed % 21600000, block
        assert r["state"][149] == 1 + elapsed // 21600000, block
        assert r["state"][147] == 1 and r["state"][170] == 2, block
        assert (r["status"][10] >> 8) & 255 == 2, block
        if r["state"][149] != serial:
            serial = r["state"][149]
            onsets.append((block, r["state"][151]))
    assert onsets == [(0, 60), (38, 64), (75, 67), (113, 60)]


@pytest.mark.parametrize("flags", [0x800, 0x801, 0x802, 0x803, 0xB00, 0xF00])
def test_pattern_arp_accepts_tempo_regardless_transport_flags(canopy, flags):
    controls = presentation_controls(base=FINE)
    r = canopy(
        [note(1, 60), note(2, 64)],
        controls=controls,
        edits={**stopped_pattern(tempo=21600), 12: flags | (21600 << 16)},
        blocks=350,
        advance_clock=True,
    )
    elapsed = 349 * 2 * 21600 * 3
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000
    assert r["state"][170] == 2


@pytest.mark.parametrize(
    "flags",
    [
        0,  # no provider
        0x700 | (30000 << 16),  # expired phase with no tempo capability
        0xF00,  # zero tempo
        0xB03 | (3599 << 16),  # below the selected-tempo range
        0xF00 | (36001 << 16),  # above the selected-tempo range
    ],
)
def test_pattern_arp_retains_last_tempo_through_provider_outage(canopy, flags):
    controls = presentation_controls(base=FINE)
    canopy([note(1, 60), note(2, 64)], controls=controls, edits=stopped_pattern())
    # More than an entire quarter note with no usable provider: gates and
    # subsequent notes still follow the last selected 120 BPM precisely.
    r = canopy(
        controls=controls,
        edits={**stopped_pattern(1), 12: flags},
        blocks=800,
        advance_clock=True,
    )
    elapsed = 800 * 2 * 14400 * 3
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000
    assert r["state"][170] == 2
    r = canopy(
        controls=controls,
        edits={**stopped_pattern(801), 12: flags},
        state_edits={188: 0},
    )
    assert (r["status"][10] >> 8) & 255 == 2
    # A new, valid BPM is accepted immediately without a replay or phase reset.
    r = canopy(controls=controls, edits=stopped_pattern(802, 21600))
    elapsed += 2 * 14400 * 3 + 2 * 21600 * 3
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000


@pytest.mark.parametrize(
    "flags", [0, 0x800, 0xF00 | (3599 << 16), 0xB00 | (36001 << 16)]
)
def test_pattern_arp_cannot_invent_tempo_before_first_valid_value(canopy, flags):
    r = canopy(
        [note(1, 60)],
        controls=FINE,
        edits={**stopped_pattern(), 12: flags},
        blocks=20,
        advance_clock=True,
    )
    assert r["state"][149] == 0 and r["state"][147] == 0
    r = canopy(controls=FINE, edits=stopped_pattern(20))
    assert r["state"][149] == 1 and r["state"][146] == 0


def test_pattern_arp_provider_gaps_preserve_gate_and_note_sequence(canopy):
    controls = presentation_controls(base=FINE[:8] + (8,) + FINE[9:])
    reference, gapped = 0x20050000, 0x20054000
    notes = [note(1, 60), note(2, 64), note(3, 67)]
    # One outage crosses both the 50% gate and the next note onset.
    for block in range(42):
        timing = stopped_pattern(block, 36000)
        expected = canopy(
            notes if block == 0 else (),
            controls=controls,
            state=reference,
            edits=timing,
        )
        actual = canopy(
            notes if block == 0 else (),
            controls=controls,
            state=gapped,
            edits={**timing, 12: 0} if 9 <= block <= 38 else timing,
        )
        assert actual["state"][170] == 2
        assert actual["state"][146] == expected["state"][146], block
        assert actual["state"][149] == expected["state"][149], block
        assert actual["state"][151] == expected["state"][151], block
        assert actual["state"][137] == expected["state"][137], block
    assert actual["state"][149] == 2 and actual["state"][151] == 64


def test_pattern_arp_cached_tempo_is_per_instance(canopy):
    controls = FINE[:8] + (8,) + FINE[9:]
    # Different cached tempos make cross-track scratch leakage observable even
    # when both tracks subsequently lose their timing provider together.
    for state, tempo in [(0x20050000, 14400), (0x20054000, 21600)]:
        canopy(
            [note(1, 60), note(2, 64)],
            state=state,
            controls=controls,
            edits=stopped_pattern(tempo=tempo),
        )
    for batch in range(3):
        for state, tempo in [(0x20050000, 14400), (0x20054000, 21600)]:
            r = canopy(
                state=state,
                controls=controls,
                edits={**stopped_pattern(1 + batch * 80), 12: 0},
                blocks=80,
                advance_clock=True,
            )
            elapsed = (batch + 1) * 80 * 2 * tempo * 8
            assert r["state"][149] == 1 + elapsed // 21600000
            assert r["state"][146] == elapsed % 21600000
            assert r["state"][170] == 2


@pytest.mark.parametrize("idle", ["no_keys", "mix_zero"])
def test_pattern_arp_remembers_selected_tempo_before_a_phrase(canopy, idle):
    controls = FINE[:8] + (8,) + FINE[9:14] + (0, 0)
    muted = controls[:6] + (0,) + controls[7:]
    canopy(
        controls=muted if idle == "mix_zero" else controls,
        edits=stopped_pattern(tempo=21600),
    )
    r = canopy(
        [note(1, 60), note(2, 64)],
        controls=controls,
        edits={**stopped_pattern(1), 12: 0},
        blocks=190,
        advance_clock=True,
    )
    elapsed = 189 * 2 * 21600 * 8
    assert r["state"][149] == 1 + elapsed // 21600000
    assert r["state"][146] == elapsed % 21600000
    assert r["state"][170] == 2


def test_pattern_arp_frame_stamp_repeats_jumps_and_wrap_are_not_time(canopy):
    canopy([note(1, 60)], controls=FINE, edits=stopped_pattern(0xFFFFFFFF))
    for call, stamp in enumerate([0, 0, 0xFFFFFFFF, 500, 20], start=1):
        r = canopy(controls=FINE, edits=stopped_pattern(stamp))
        assert r["state"][149] == 1
        assert r["state"][146] == call * 86400
        assert r["state"][170] == 2


def test_pattern_arp_stopped_status_reports_running(canopy):
    controls = presentation_controls(base=FINE)
    r = canopy([note(1, 60)], controls=controls, edits=stopped_pattern())
    assert (r["status"][10] >> 8) & 255 == 2


@pytest.mark.parametrize("restart,expected", [(0, 60), (1, 60), (2, 64)])
def test_fine_master_restart_and_phase_discontinuity(canopy, restart, expected):
    controls = FINE[:14] + (restart, 0)
    canopy(
        [note(1, 60), note(2, 64)],
        controls=controls,
        edits={**phase_position(0, 0), 18: 1, 19: 10},
    )
    canopy(controls=controls, edits={**phase_position(5400000, 18), 18: 1, 19: 10})
    # A large forward correction must never emit a catch-up burst.
    r = canopy(controls=controls, edits={**phase_position(7800000, 19), 18: 1, 19: 10})
    assert r["state"][149] == 1 and r["state"][147] == 0
    r = canopy(controls=controls, edits={**phase_position(10800000, 36), 18: 2, 19: 11})
    assert r["state"][149] == 2 and r["state"][151] == expected


def test_master_waits_for_interval_then_gates_and_tracks_tempo(canopy):
    r = canopy([note(1, 60), note(2, 64)], controls=MASTER, edits=position(100, 1))
    assert r["state"][149] == 0
    r = canopy(controls=MASTER, edits=position(287, 2, 1))
    assert r["state"][149] == 1 and r["state"][151] == 60
    assert r["state"][166] == 187
    r = canopy(controls=MASTER, edits=position(379, 2, 1))
    assert r["state"][137] == 1
    r = canopy(controls=MASTER, edits=position(381, 2, 1))
    assert r["state"][137] == 0
    r = canopy(controls=MASTER, edits=position(412, 3, 2))
    assert r["state"][149] == 2 and r["state"][151] == 64
    assert r["state"][166] == 125


def test_master_clock_loss_and_stop_never_fall_back_to_local(canopy):
    canopy([note(1, 60)], controls=MASTER, edits=position(100, 1))
    canopy(controls=MASTER, edits=position(200, 2, 1))
    r = canopy(controls=MASTER, edits=position(401, 2, 1))
    assert r["state"][147] == 0 and r["state"][162] == 0
    assert r["state"][170] == 3
    for i in range(4):
        r = canopy(controls=MASTER, edits=position(402 + i, 2, 1, flags=0))
    assert r["state"][136] == 0
    assert voices(r)[1][1] == 1
    r = canopy(controls=MASTER, edits=position(700, 3, 2))
    assert r["state"][149] == 1
    r = canopy(controls=MASTER, edits=position(800, 4, 3))
    assert r["state"][149] == 2


def test_master_missed_boundary_discards_gate_and_observes_again(canopy):
    canopy([note(1, 60)], controls=MASTER, edits=position(100, 1))
    canopy(controls=MASTER, edits=position(200, 2, 1))
    r = canopy(controls=MASTER, edits=position(400, 4, 3))
    assert r["state"][149] == 1 and r["state"][166] == 0
    r = canopy(controls=MASTER, edits=position(500, 5, 4))
    assert r["state"][149] == 2


@pytest.mark.parametrize("restart,expected", [(1, 60), (2, 67)])
def test_master_pattern_restart_and_continue(canopy, restart, expected):
    controls = MASTER[:14] + (restart, 0)
    canopy(
        [note(1, 60), note(2, 64), note(3, 67)], controls=controls, edits=position(0, 1)
    )
    canopy(controls=controls, edits=position(100, 2, 1))
    canopy(controls=controls, edits=position(200, 3, 2))
    r = canopy(controls=controls, edits=position(300, 4, 0))
    assert r["state"][151] == expected


def test_master_rate_is_whole_boundaries_and_source_change_reobserves(canopy):
    controls = MASTER[:8] + (2,) + MASTER[9:]
    canopy([note(1, 60)], controls=controls, edits=position(0, 1))
    canopy(controls=controls, edits=position(100, 2, 1))
    r = canopy(controls=controls, edits=position(200, 3, 2))
    assert r["state"][149] == 1
    r = canopy(controls=controls, edits=position(300, 4, 3))
    assert r["state"][149] == 2
    local = controls[:13] + (0, 0, 0)
    r = canopy(controls=local, edits=position(301, 4, 3))
    assert r["state"][149] == 3
    r = canopy(controls=controls, edits=position(302, 4, 3))
    assert r["state"][149] == 3 and r["state"][166] == 0


STRUM = (5, 0, 64, 0, 0, 0, 0, 120, 4, 50, 0, 1, 0, 0, 0, 6)


def mapped(phrase=1, generated=True, **kwargs):
    return {0: 3, 1: 80, 18: phrase | (0x80000000 if generated else 0), **kwargs}


def test_generated_strum_spreads_onsets_on_audio_blocks(canopy):
    r = canopy([note(1, 60), note(2, 64), note(3, 67)], controls=STRUM, edits=mapped())
    assert [voices(r)[i][1] for i in (1, 2, 3)] == [1, 9, 9]
    for _ in range(2):
        r = canopy(controls=STRUM, edits=mapped())
    assert voices(r)[2][1] == 9
    r = canopy(controls=STRUM, edits=mapped())
    assert voices(r)[2][1] == 1 and voices(r)[3][1] == 9
    for _ in range(3):
        r = canopy(controls=STRUM, edits=mapped())
    assert [voices(r)[i][1] for i in (1, 2, 3)] == [1, 1, 1]


def test_play_is_not_strummed_and_chord_release_cancels_pending(canopy):
    r = canopy(
        [note(1, 60), note(2, 64)], controls=STRUM, edits=mapped(generated=False)
    )
    assert all(v[1] == 1 for v in voices(r).values())
    canopy(reset=1)
    canopy([note(1, 60), note(2, 64)], controls=STRUM, edits=mapped())
    canopy([note(2, 64, off=True)], controls=STRUM, edits=mapped())
    for _ in range(8):
        r = canopy(controls=STRUM, edits=mapped())
    assert set(voices(r)) == {1}
    assert not any(r["state"][176:184:2])


def test_transport_stop_cancels_strum_without_releasing_keyboard_pad(canopy):
    canopy([note(1, 60), note(2, 64)], controls=STRUM, edits={**mapped(), 12: 3})
    r = canopy(controls=STRUM, edits=mapped())
    assert set(voices(r)) == {1}
    for _ in range(8):
        r = canopy(controls=STRUM, edits=mapped())
    assert set(voices(r)) == {1}


@pytest.mark.parametrize("restart,expected", [(0, 60), (2, 64)])
def test_explicit_phrase_restarts_arp_without_retriggering_common_pad(
    canopy, restart, expected
):
    controls = STRUM[:6] + (127,) + STRUM[7:14] + (restart, 0)
    canopy([note(1, 60), note(2, 64)], controls=controls, edits=mapped())
    r = canopy(controls=controls, edits=mapped(), state_edits={146: 89999})
    assert r["state"][151] == 64
    r = canopy([[5, 0, 0, 0, 0, 0]], controls=controls, edits=mapped(phrase=2))
    assert r["state"][151] == expected
    assert set(voices(r)) == {1, 2}
    assert all(v[1] == 1 for v in voices(r).values())


@pytest.mark.parametrize("version,size", [(3, 80), (5, 96), (6, 128), (7, 136)])
def test_pattern_identity_change_restarts_local_arp_mid_pattern(canopy, version, size):
    controls = STRUM[:6] + (127,) + STRUM[7:14] + (1, 0)
    timing = {
        **mapped(),
        0: version,
        1: size,
        12: (0xB03 | (14400 << 16)) if version >= 5 else 3,
        13: 1,
        14: 10,
        15: 4,
        19: 0x43170000,
    }
    kwargs = {"native": [0] * 8 if version >= 6 else None, "post_amp": version == 7}
    canopy([note(1, 60), note(2, 64)], controls=controls, edits=timing, **kwargs)
    before = canopy(controls=controls, edits=timing, state_edits={146: 89999}, **kwargs)
    assert before["state"][151] == 64
    r = canopy(controls=controls, edits={**timing, 19: 0x43180000}, **kwargs)
    assert r["state"][151] == 60
    assert r["state"][149] == before["state"][149] + 1
    assert set(voices(r)) == {1, 2}


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_mapped_wire_preserves_sample_offsets_and_commits_phrase(canopy):
    packet = wire_packet(
        [(1, 1, 60, 127, 0, 0), (2, 1, 64, 127, 0, 0)], movement=0x80000002
    )
    r = canopy(packet=packet, controls=STRUM, edits={0: 3, 1: 80})
    assert r["state"][174] == 2
    assert voices(r)[1][1] == 1 and voices(r)[2][1] == 9
    r = canopy(
        packet=wire_packet([(0, 5, 0, 0, 0, 0)], movement=0x80000003),
        controls=STRUM,
        edits={0: 3, 1: 80},
    )
    assert r["state"][174] == 3
    assert set(voices(r)) == {1, 2}


def test_continue_preserves_phase_across_empty_chord(canopy):
    controls = (5,) + LOCAL[1:] + (0, 1, 0, 0, 2, 0)
    r = canopy([note(1, 60)], controls=controls)
    ordinal = r["state"][148]
    r = canopy([note(1, 60, off=True)], controls=controls, state_edits={146: 10000})
    assert r["state"][147] == 1
    assert r["state"][146] == 10000 + controls[7] * controls[8]
    assert r["state"][148] == ordinal
    phase = r["state"][146]
    r = canopy([note(2, 64)], controls=controls)
    assert r["state"][146] == phase + controls[7] * controls[8]
    assert r["state"][149] == 1  # no immediate onset on new membership
    r = canopy(controls=controls, state_edits={146: 89999})
    assert r["state"][149] == 2 and r["state"][151] == 64


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
@pytest.mark.parametrize(
    "events,movement,header",
    [
        ([(0, 5, 0, 0, 0, 0)], 0, {0: 3, 1: 80}),
        ([(0, 5, 0, 0, 0, 0)], 0x80000000, {0: 3, 1: 80}),
        ([(0, 5, 0, 0, 0, 0)], 2, {}),
        ([(1, 5, 0, 0, 0, 0)], 2, {0: 3, 1: 80}),
        ([(0, 5, 60, 0, 0, 0)], 2, {0: 3, 1: 80}),
        ([(0, 5, 0, 0, 0, 0), (2, 1, 64, 127, 0, 0)], 2, {0: 3, 1: 80}),
    ],
)
def test_malformed_mapped_commit_cannot_mutate_instance(
    canopy, events, movement, header
):
    before = canopy([note(1, 60)], controls=STRUM)["state"]
    r = canopy(
        packet=wire_packet(events, movement=movement), controls=STRUM, edits=header
    )
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert r["state"] == before
    assert r["out"] == [0] * 32


def test_v4_input_preserves_mapped_phrase_and_strum(canopy):
    r = canopy(
        events=[note(1, 60), note(2, 64), note(3, 67)],
        controls=STRUM,
        edits={0: 4, 1: 88, 18: 0x80000001, 20: 1234, 21: 1000},
    )
    assert r["regs"]["R0"] == 0
    assert r["state"][175] == 0x80000001
    assert any(r["state"][8 * i + 1] == 9 for i in range(4))


def presentation_controls(base=STRUM, mode=0):
    ui = [
        0,
        0,
        base[2],
        base[3],
        base[15] * 2 // 3,
        base[6],
        base[8],
        base[10],
        base[11],
        base[9],
        base[12],
        base[14],
        0,
        0,
        base[4],
        base[5],
    ]
    return [6 | (mode << 8), *base[1:], *ui]


@pytest.mark.parametrize("mode", [0, 1, 2])
def test_presentation_bank_preserves_audio_and_movement(canopy, mode):
    base = (5, 1, 75, 22, 7, 31, 64, 123, 5, 70, 2, 2, 12, 0, 1, 6)
    batch = [note(1, 60), note(2, 64), note(3, 67)]
    old = canopy(batch, controls=base, edits=mapped(phrase=1))
    new = canopy(
        batch,
        controls=presentation_controls(base, mode),
        edits=mapped(phrase=1),
        state=0x20054000,
    )
    assert new["regs"]["R0"] == old["regs"]["R0"]
    assert new["out"] == old["out"]
    assert new["state"][:680] == old["state"][:680]
    assert new["state"][716:] == old["state"][716:]
    # Only a 32-word bank participates in source-family transitions. Its raw
    # presentation encoding is remembered without changing voice/audio state.
    assert old["state"][680:716] == [0] * 36
    assert new["state"][680:712] == presentation_controls(base, mode)
    assert new["state"][712:716] == [base[1], 0x3F800000, 0, 1]


@pytest.mark.parametrize(
    "offset,value",
    [
        (0, 0x306),
        (0, 7),
        (16, 12),
        (17, 4),
        (18, 0),
        (20, 5),
        (21, 127),
        (28, 2),
        (29, 1),
        (31, 0xFFFFFFFF),
    ],
)
def test_presentation_bank_rejects_inconsistent_audio_claims(canopy, offset, value):
    controls = presentation_controls()
    before = canopy([note(1, 60)], controls=controls)["state"]
    controls[offset] = value
    rejected = canopy([note(2, 64)], controls=controls)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF and rejected["state"] == before


def native_controls(raw=0x4000, base=STRUM):
    values = presentation_controls(base)
    values[0] = 7 | (raw << 16)
    return values


def spread_controls(spread=0, order=0, raw=0x4000):
    base = (5,) + LOCAL[1:] + (order, 1, 0, 0, 0, 0)
    values = native_controls(raw, base)
    values[0] = 8 | (raw << 16)
    values[11] = values[24] = spread + 3
    return values


@pytest.mark.parametrize("pitch", [60, 64, 67, 71])
def test_same_pitch_arp_retrigger_starts_in_phase_with_held_pad(canopy, pitch):
    import struct

    controls = spread_controls()
    controls[6] = controls[21] = 64
    canopy([note(1, pitch)], controls=controls, blocks=37)
    r = canopy(controls=controls, state_edits={146: 89999})
    for _ in range(4):
        r = canopy(controls=controls)
    assert r["state"][155] == 1  # the held pad owns this arp candidate
    pad = voices(r)[1]
    assert pad[4:6] == r["state"][140:142]

    def phase(words):
        return struct.unpack("<2f", struct.pack("<2I", *words))

    assert phase(pad[2:4]) == pytest.approx(phase(r["state"][138:140]), abs=0.000002)


def test_same_pitch_arp_phase_survives_full_shared_tail_pool(canopy):
    import struct

    controls = spread_controls()
    controls[5] = controls[31] = 127
    canopy([note(i, 60) for i in range(1, 5)], controls=controls, blocks=8)
    r = canopy([note(5, 60), note(6, 60)], controls=controls)
    assert r["state"][32] and r["state"][40]
    old_arp = r["state"][136]
    r = canopy(controls=controls, state_edits={146: 89999})
    for _ in range(5):
        r = canopy(controls=controls)
    assert r["state"][136] and r["state"][136] != old_arp
    pad = voices(r)[r["state"][155]]
    phase = struct.unpack("<4f", struct.pack("<4I", *pad[2:4], *r["state"][138:140]))
    assert phase[:2] == pytest.approx(phase[2:], abs=0.000002)


@pytest.mark.parametrize("spread", [-1, 1])
def test_octave_arp_does_not_copy_the_base_note_phase(canopy, spread):
    import struct

    controls = spread_controls(spread, order=3)
    canopy([note(1, 60)], controls=controls, blocks=37)
    r = canopy(controls=controls, state_edits={146: 89999})
    assert r["state"][151] == 60 + 12 * spread
    pad = voices(r)[1]
    assert pad[4:6] != r["state"][140:142]
    phase = struct.unpack("<4f", struct.pack("<4I", *pad[2:4], *r["state"][138:140]))
    assert max(abs(phase[i] - phase[i + 2]) for i in range(2)) > 0.01


@pytest.mark.parametrize("old_octaves", [1, 2, 3])
def test_signed_spread_preserves_legacy_upward_audio(canopy, old_octaves):
    base = (5,) + LOCAL[1:] + (0, old_octaves, 0, 0, 0, 0)
    old = native_controls(base=base)
    new = spread_controls(old_octaves - 1)
    for ordinal in range(13):
        events = [note(1, 60), note(2, 64), note(3, 67)] if not ordinal else []
        a = canopy(events, controls=old, state_edits={146: 89999})
        b = canopy(events, controls=new, state=0x20054000, state_edits={146: 89999})
        assert a["out"] == b["out"]
        assert a["state"][:680] == b["state"][:680]
        assert a["state"][712:] == b["state"][712:]
        # Legacy octave counts and signed spread retain distinct raw encodings,
        # while all rendered voice state and source-transition state agree.
        assert a["state"][680:712] == old
        assert b["state"][680:712] == new


@pytest.mark.parametrize("spread", [-3, -2, -1, 0, 1, 2, 3])
def test_signed_spread_uses_only_one_direction_and_keeps_pad_pitch(canopy, spread):
    controls = spread_controls(spread, order=3)
    r = canopy([note(1, 67), note(2, 60), note(3, 64), note(4, 72)], controls=controls)
    pad_rotation = [r["state"][i * 8 + 4 : i * 8 + 7] for i in range(4)]
    offsets = range(0, spread + (1 if spread >= 0 else -1), 1 if spread >= 0 else -1)
    expected = [n + 12 * octaves for octaves in offsets for n in [67, 60, 64, 72]]
    pitches = [r["state"][151]]
    for _ in expected[1:]:
        r = canopy(controls=controls, state_edits={146: 89999})
        pitches.append(r["state"][151])
    assert pitches == expected
    assert [r["state"][i * 8 + 4 : i * 8 + 7] for i in range(4)] == pad_rotation
    assert max(pitches) <= 127 and min(pitches) >= 0


@pytest.mark.parametrize(
    "spread,pitch,expected", [(-3, 12, [12, 0, 12]), (3, 115, [115, 127, 115])]
)
def test_signed_spread_skips_unplayable_extensions(canopy, spread, pitch, expected):
    controls = spread_controls(spread, order=3)
    r = canopy([note(1, pitch)], controls=controls)
    pitches = [r["state"][151]]
    for _ in expected[1:]:
        r = canopy(controls=controls, state_edits={146: 89999})
        pitches.append(r["state"][151])
    assert pitches == expected


def test_live_signed_spread_change_applies_at_next_onset_without_resetting_pad(canopy):
    r = canopy([note(1, 60)], controls=spread_controls(3))
    pad = r["state"][:8]
    r = canopy(controls=spread_controls(-3), state_edits={146: 89999})
    assert r["state"][151] == 36  # ordinal 1 in [24,36,48,60]
    assert r["state"][0:2] == pad[0:2] and r["state"][4:7] == pad[4:7]
    for _ in range(3):
        r = canopy(controls=spread_controls(-3))
    assert r["state"][198] == 36
    bent = canopy([[4, 0, 0, 0, 16383, 0]], controls=spread_controls(-3))
    assert bent["state"][198] == 36 and bent["state"][199] == 8191
    assert bent["state"][140:142] != r["state"][140:142]
    for _ in range(3):
        r = canopy(controls=spread_controls(-3, raw=0x4C00))
    reference = canopy(
        [note(2, 48), [4, 0, 0, 0, 16383, 0]],
        state=0x20054000,
        controls=spread_controls(),
    )
    assert r["state"][140:142] == reference["state"][140:142]


@pytest.mark.parametrize("bad", [-1, 7, 0xFFFFFFFF])
def test_signed_spread_rejects_invalid_bank_before_mutating_voices(canopy, bad):
    controls = spread_controls()
    before = canopy([note(1, 60)], controls=controls)["state"]
    controls[11] = controls[24] = bad & 0xFFFFFFFF
    rejected = canopy([note(2, 64)], controls=controls)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF and rejected["state"] == before


@pytest.mark.parametrize(
    "raw,pitch",
    [
        (0x4000, 60),
        (0x4C00, 72),
        (0x3400, 48),
        (0x4080, None),
        (0x400, 0),
        (0x7C00, 120),
    ],
)
def test_native_tune_transposes_held_notes_without_retrigger(canopy, raw, pitch):
    first = canopy([note(1, 60)], controls=native_controls(), blocks=2)
    tuned = canopy(controls=native_controls(raw), edits={11: 2})
    assert tuned["regs"]["R0"] == 0
    assert voices(tuned)[1][0:2] == voices(first)[1][0:2]
    assert tuned["state"][197] == (raw - 0x4000) & 0xFFFFFFFF
    if pitch is not None:
        reference = canopy(
            [note(1, pitch)], state=0x20054000, controls=native_controls()
        )
        assert voices(tuned)[1][4:6] == voices(reference)[1][4:6]
    else:
        assert voices(tuned)[1][4:6] != voices(first)[1][4:6]
    restored = canopy(controls=native_controls(), edits={11: 3})
    assert voices(restored)[1][4:6] == voices(first)[1][4:6]


@pytest.mark.parametrize("raw", [0x3FF, 0x7C01, 0xFFFF])
def test_native_tune_rejects_invalid_bank_atomically(canopy, raw):
    first = canopy([note(1, 60)], controls=native_controls())
    rejected = canopy(controls=native_controls(raw))
    assert rejected["regs"]["R0"] & 0x80000000
    assert rejected["state"] == first["state"]


def test_native_tune_fraction_and_route_bend_combine(canopy):
    import math
    import struct

    r = canopy([note(1, 60), [4, 0, 0, 0, 12288, 0]], controls=native_controls(0x4080))
    sine, cosine = [
        struct.unpack("<f", v.to_bytes(4, "little"))[0] for v in voices(r)[1][4:6]
    ]
    measured = math.atan2(sine, cosine) * 48000 / (2 * math.pi)
    expected = 440 * 2 ** ((61.5 - 69) / 12)
    assert measured == pytest.approx(expected, rel=2e-5)


@pytest.mark.parametrize("owner", [0, 0x200])
def test_native_tune_reaches_running_arp_and_survives_legacy_controls(canopy, owner):
    base = list(STRUM)
    base[6] = 127
    first = canopy(
        [note(1, 60, owner=owner)], controls=native_controls(base=base), blocks=3
    )
    assert first["state"][136] and first["state"][198] == 60
    changed = canopy(controls=native_controls(0x4C00, base), edits={11: 2})
    assert changed["state"][136] == first["state"][136]
    assert changed["state"][140:142] != first["state"][140:142]
    restored = canopy(controls=presentation_controls(base), edits={11: 3})
    assert restored["state"][140:142] == first["state"][140:142]
    changed = canopy(controls=native_controls(0x4C00, base), edits={11: 4})
    assert changed["state"][140:142] != first["state"][140:142]


def test_native_tune_detail_ack_contains_exact_applied_raw_value(canopy):
    controls = native_controls(0x4080)
    fragments = {}
    for block in range(88):
        r = canopy(controls=controls, edits={0: 5, 1: 96, 4: block, 11: 7})
        ready, *words = r["status"]
        assert ready == 1
        if words[1] >> 30:
            assert words[5] == 7
            fragments[words[4] & 255] = words[6:]
        else:
            assert (words[1] >> 6) & 3 == 0
    bank = sum((fragments[i] for i in range(11)), [])
    assert bank[16] == 0x4080
    assert bank[:16] == controls[16:]


@pytest.mark.parametrize("spread", [None, -3, 3])
def test_native_tune_busy_callback_stays_within_declared_instruction_bound(
    canopy, spread
):
    base = (5, 1, 127, 127, 0, 127, 127, 300, 8, 100, 4, 3, 75, 0, 0, 750)
    controls = native_controls(0x4080, base)
    if spread is not None:
        controls[0] = 8 | (0x4080 << 16)
        controls[11] = controls[24] = spread + 3
    measured = [
        canopy([note(i + 1, 48 + i, 127) for i in range(8)], controls=controls)[
            "instructions"
        ]
    ]
    measured.append(
        canopy([note(i + 9, 60 + i, 127) for i in range(8)], controls=controls)[
            "instructions"
        ]
    )
    changed = controls.copy()
    changed[0] = (changed[0] & 0xFFFF) | (0x4C80 << 16)
    measured.append(
        canopy(
            [[4, 0, 0, 0, 16383 - i, 0] for i in range(8)],
            controls=changed,
            edits={11: 2},
        )["instructions"]
    )
    assert max(measured) < 100000


def wavetable_controls(
    table=0,
    position=350,
    depth=0,
    rate=100,
    shape=0,
    reset=0,
    clock=0,
    env=100,
    attack=5,
    release=15,
    velocity=100,
    wheel=100,
    mix=0,
):
    values = spread_controls()
    values[0] = 9 | (0x4000 << 16)
    values[1] = 2
    values[6] = values[21] = mix
    for index, value in enumerate(
        (
            table,
            position,
            depth,
            rate,
            shape,
            reset,
            clock,
            env,
            attack,
            release,
            velocity,
            wheel,
        )
    ):
        values[16 + index] |= value << 16
    return values


def test_wavetable_factory_voice_executes_on_actual_sharc(canopy):
    import math
    import struct

    r = canopy([note(1, 60)], controls=wavetable_controls(), blocks=4)
    assert r["regs"]["R0"] == 0
    samples = [struct.unpack("<f", w.to_bytes(4, "little"))[0] for w in r["out"]]
    assert all(math.isfinite(x) and abs(x) < 1 for x in samples)
    assert max(abs(x) for x in samples) > 0.001, (
        r["state"][:48],
        {k: v for k, v in r.items() if k.startswith("wt_")},
    )
    assert any(r["state"][200:232])
    assert r["instructions"] / 4 < 100000


def wt_float(word):
    import struct

    return struct.unpack("<f", word.to_bytes(4, "little"))[0]


def wt_bits(value):
    import struct

    return struct.unpack("<I", struct.pack("<f", value))[0]


@pytest.mark.parametrize("table", range(5))
@pytest.mark.parametrize("pitch", [0, 60, 96, 127])
def test_wavetable_tables_pitch_and_mips_execute_without_aliasing_rich_bands(
    canopy, table, pitch
):
    import math

    r = canopy(
        [note(1, pitch)],
        controls=wavetable_controls(table=table, position=700),
        blocks=2,
    )
    assert r["regs"]["R0"] == 0
    increment = wt_float(r["wt_increment"][0])
    assert increment == pytest.approx(440 * 2 ** ((pitch - 69) / 12) / 48000, rel=2e-6)
    harmonics = {0: 64, 513: 32, 770: 16, 899: 8, 964: 4, 1029: 2, 1094: 1}[
        r["wt_offset_a"][0]
    ]
    assert increment * harmonics < 0.400001 or harmonics == 1
    assert all(math.isfinite(wt_float(w)) and abs(wt_float(w)) < 1 for w in r["out"])
    assert max(abs(wt_float(w)) for w in r["out"]) > 1e-5


@pytest.mark.parametrize(
    "index,value",
    [
        (0, 5),
        (1, 701),
        (2, 101),
        (3, 0),
        (3, 1001),
        (4, 3),
        (5, 3),
        (6, 2),
        (7, 201),
        (8, 128),
        (9, 128),
        (10, 201),
        (11, 201),
        (12, 1),
    ],
)
def test_wavetable_rejects_malformed_controls_before_voice_or_wheel_changes(
    canopy, index, value
):
    good = wavetable_controls()
    before = canopy([note(1, 60)], controls=good)["state"]
    bad = list(good)
    bad[16 + index] = (bad[16 + index] & 0xFFFF) | (value << 16)
    r = canopy([[3, 0, 0, 1, 100, 0], note(2, 64)], controls=bad)
    assert r["regs"]["R0"] == 0xFFFFFFFF and r["state"] == before


def test_wavetable_wheel_routes_are_atomic_and_reset_independently(canopy):
    controls = wavetable_controls(wheel=170)
    r = canopy([[3, 0, 0, 1, 100, 0], [3, 0x100, 0, 1, 40, 0]], controls=controls)
    assert r["state"][264] == 100 and r["state"][280] == 40
    before = r["state"]
    r = canopy([[3, 0, 0, 1, 127, 0], [3, 0, 0, 55, 1, 0]], controls=controls)
    assert r["regs"]["R0"] == 0xFFFFFFFF and r["state"] == before
    r = canopy([[3, 0, 0, 121, 0, 0]], controls=controls)
    assert r["state"][264] == 0 and r["state"][280] == 40
    r = canopy([[3, 0x100, 0, 120, 0, 0]], controls=controls)
    assert r["state"][280] == 40


def test_wavetable_scan_phase_is_shared_and_phrase_restart_ignores_added_notes(canopy):
    controls = wavetable_controls(depth=100, rate=1000, reset=1)
    r = canopy([note(1, 60)], controls=controls)
    first = wt_float(r["state"][239])
    r = canopy([note(2, 64)], controls=controls)
    assert wt_float(r["state"][239]) == pytest.approx(first * 2, rel=1e-6)
    canopy([note(1, 60, off=True), note(2, 64, off=True)], controls=controls)
    r = canopy([note(3, 67)], controls=controls)
    assert wt_float(r["state"][239]) == pytest.approx(first, rel=1e-6)


def test_wavetable_pattern_reset_requires_new_boundary_and_preserves_midpattern_progress(
    canopy,
):
    controls = wavetable_controls(depth=100, rate=1000, reset=2)
    timing = {0: 4, 1: 88, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 19: 123}
    r = canopy([note(1, 60)], controls=controls, edits=timing)
    first = wt_float(r["state"][239])
    r = canopy(controls=controls, edits=timing)
    assert wt_float(r["state"][239]) > first
    r = canopy(controls=controls, edits={**timing, 14: 2, 15: 1})
    assert wt_float(r["state"][239]) == pytest.approx(first * 3, rel=1e-6)
    r = canopy(controls=controls, edits={**timing, 14: 17, 15: 0})
    assert wt_float(r["state"][239]) == pytest.approx(first, rel=1e-6)


def test_wavetable_master_scan_holds_on_lost_clock_and_resumes_without_jump(canopy):
    controls = wavetable_controls(depth=100, rate=5, clock=1)
    timing = {0: 4, 1: 88, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 20: 1000, 21: 100}
    r = canopy([note(1, 60)], controls=controls, edits=timing)
    assert wt_float(r["state"][239]) == 0
    r = canopy(controls=controls, edits={**timing, 20: 217000})
    assert wt_float(r["state"][239]) == pytest.approx(0.01, rel=1e-6)
    r = canopy(controls=controls, edits={**timing, 12: 0, 20: 500000})
    assert wt_float(r["state"][239]) == pytest.approx(0.01, rel=1e-6)
    r = canopy(controls=controls, edits={**timing, 20: 10000000})
    assert wt_float(r["state"][239]) == pytest.approx(0.01, rel=1e-6)


def test_wavetable_table_changes_are_bounded_and_latest_request_wins(canopy):
    controls = wavetable_controls()
    r = canopy([note(1, 60)], controls=controls, blocks=2)
    original = voices(r)[1]
    r = canopy(controls=wavetable_controls(table=1))
    assert r["state"][235:237] == [0, 1]
    assert 0 < wt_float(r["state"][237]) < 1
    r = canopy(controls=wavetable_controls(table=4))
    assert r["state"][235:237] == [0, 1] and r["state"][238] == 4
    r = canopy(controls=wavetable_controls(table=4), blocks=32)
    assert r["state"][235:237] == [1, 4]
    assert voices(r)[1][:2] == original[:2] and voices(r)[1][4:7] == original[4:7]


def test_wavetable_reset_preserves_timbre_through_bounded_tail(canopy):
    controls = wavetable_controls(table=4, env=175, wheel=130)
    r = canopy([note(1, 60), [3, 0, 0, 1, 90, 0]], controls=controls, blocks=32)
    history = r["state"][200:264]
    reset = canopy(reset=2)
    assert reset["state"][200:263] == history[:-1]
    assert reset["regs"]["R0"] == 0
    assert reset["state"][263] == 1
    assert reset["state"][264:296] == [0] * 32
    r = canopy(controls=controls)
    assert any(r["out"])
    r = canopy(controls=controls, blocks=3)
    assert all(r["state"][i] == 0 for i in range(0, 48, 8))


def test_wavetable_worst_case_chord_arp_tails_table_transition_stays_within_ceiling(
    canopy,
):
    controls = wavetable_controls(table=0, position=345, depth=100, env=150, mix=64)
    canopy([note(i + 1, 60 + i * 3) for i in range(4)], controls=controls, blocks=12)
    r = canopy(
        [note(i + 5, 62 + i * 3) for i in range(4)],
        controls=wavetable_controls(table=4, position=345, depth=100, env=150, mix=64),
    )
    assert r["regs"]["R0"] == 0
    assert r["state"][32] and r["state"][40]
    assert r["instructions"] < 100000
    print("WT worst-case instructions", r["instructions"])


@pytest.mark.parametrize("pitch", [60, 96, 120])
def test_wavetable_rendered_samples_match_independent_interpolation_and_spectral_reference(
    canopy, pitch
):
    import numpy as np

    from modules.machines.canopy.tools.wavetables import MIPS
    from modules.machines.canopy.tools.wavetables import (
        dequantized as frames,  # the 16-bit samples the DSP plays
    )

    controls = wavetable_controls(table=1, position=350)
    r = canopy(
        [note(1, pitch, 127)],
        controls=controls,
        state_edits={232: wt_bits(1.0)},
        blocks=256,
        capture=True,
    )
    audio = np.array([wt_float(w) for w in r["audio"]])
    frequency = 440 * 2 ** ((pitch - 69) / 12)
    increment = frequency / 48000
    mip = next((i for i, (_, h) in enumerate(MIPS) if increment * h < 0.4), 6)
    mip_mix = max(0, min(1, (increment * MIPS[mip][1] - 0.2) * 5))
    waves = frames(1)
    phase = (np.arange(1, len(audio) + 1) * increment) % 1

    def sample(frame, level):
        size = MIPS[level][0]
        indices = phase * size
        index = indices.astype(int)
        wave = np.array(waves[frame][level])
        return wave[index] + (wave[index + 1] - wave[index]) * (indices - index)

    def frame_sample(frame):
        return (1 - mip_mix) * sample(frame, mip) + mip_mix * sample(
            frame, min(mip + 1, 6)
        )

    expected = (frame_sample(3) + frame_sample(4)) * 0.5 * 0.08
    # Exclude initial amplitude attack. Fixed gain is required: no per-note AGC.
    residual = audio[4096:] - expected[4096:]
    assert np.sqrt(np.mean(residual**2)) < 0.000015
    assert np.sqrt(np.mean(audio[4096:] ** 2)) == pytest.approx(
        np.sqrt(np.mean(expected[4096:] ** 2)), rel=0.002
    )
    spectrum = np.abs(np.fft.rfft(audio[4096:] * np.hanning(4096))) ** 2
    bins = np.fft.rfftfreq(4096, 1 / 48000)
    allowed = np.zeros(len(bins), dtype=bool)
    for harmonic in range(1, MIPS[mip][1] + 1):
        allowed |= np.abs(bins - harmonic * frequency) < 6 * 48000 / 4096
    alias_power = spectrum[~allowed].sum() / spectrum.sum()
    assert alias_power < 10 ** (-42 / 10)
    print(
        "WT rendered spectrum",
        pitch,
        "aliasdB",
        10 * np.log10(alias_power),
        "RMS",
        np.sqrt(np.mean(audio[4096:] ** 2)),
    )


def test_wavetable_arp_inherits_parent_timbre_envelope_even_during_arp_release(canopy):
    controls = wavetable_controls(mix=64, env=175, wheel=150, velocity=130)
    r = canopy(
        [note(1, 60), [3, 0, 0, 1, 100, 0]],
        controls=controls,
        state_edits={232: wt_bits(1.0)},
        blocks=3,
    )
    for edits in ({}, {137: 0}, {146: 89999}, {}, {}):
        r = canopy(controls=controls, state_edits=edits)
        aux = {
            (r["state"][i], r["state"][i + 2] & 256): r["state"][i : i + 4]
            for i in range(200, 232, 4)
            if r["state"][i]
        }
        arp_token = r["state"][136]
        if arp_token:
            assert aux[arp_token, 256][1] == aux[1, 0][1]
            assert aux[arp_token, 256][2] == 256


def test_wavetable_route_wheel_and_velocity_move_timbre_with_smooth_live_edits(canopy):
    controls = wavetable_controls(wheel=200, velocity=150)
    r = canopy([note(1, 60, 127), note(2, 60, 64, owner=0x100)], controls=controls)
    positions = {
        r["state"][200 + i * 4]: wt_float(r["state"][248 + i])
        for i in range(8)
        if r["state"][200 + i * 4]
    }
    assert positions[1] > positions[2]
    r = canopy([[3, 0x100, 0, 1, 127, 0]], controls=controls)
    changed = {
        r["state"][200 + i * 4]: wt_float(r["state"][248 + i])
        for i in range(8)
        if r["state"][200 + i * 4]
    }
    assert changed[1] == pytest.approx(positions[1])
    assert positions[2] < changed[2] < positions[2] + 0.3


def test_wavetable_arp_serial_namespace_cannot_overwrite_other_pad_route_or_envelope(
    canopy,
):
    controls = wavetable_controls(mix=64, env=175, wheel=150)
    r = canopy(
        [note(1, 60, 127), note(2, 64, 70, owner=0x100)], controls=controls, blocks=2
    )
    r = canopy(controls=controls, state_edits={146: 89999})
    r = canopy(controls=controls, blocks=3)
    aux = {
        (r["state"][i], r["state"][i + 2] & 256): r["state"][i : i + 4]
        for i in range(200, 232, 4)
        if r["state"][i]
    }
    assert (1, 0) in aux and (2, 0) in aux and (r["state"][136], 256) in aux
    assert aux[1, 0][2] == 0 and aux[2, 0][2] == 16
    assert aux[r["state"][136], 256][2] == 272
    assert aux[1, 0][1] == aux[2, 0][1]


def test_wavetable_fast_master_division_wraps_multiple_elapsed_cycles(canopy):
    controls = wavetable_controls(rate=7, clock=1, depth=100)
    timing = {0: 4, 1: 88, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 20: 0, 21: 100}
    canopy([note(1, 60)], controls=controls, edits=timing)
    r = canopy(controls=controls, edits={**timing, 20: 21000000})
    assert 0 <= wt_float(r["state"][239]) < 1
    assert wt_float(r["state"][239]) == pytest.approx(
        (21000000 / 21600000 * 4) % 1, abs=1e-6
    )


def test_wavetable_smooth_random_moves_during_its_first_cycle(canopy):
    controls = wavetable_controls(rate=1000, depth=100, shape=2)
    first = canopy([note(1, 60)], controls=controls)
    later = canopy(controls=controls, blocks=12)
    assert 0 < wt_float(later["state"][239]) < 0.1
    assert abs(wt_float(first["state"][247])) > 0
    assert abs(wt_float(later["state"][247])) > abs(wt_float(first["state"][247]))


@pytest.mark.parametrize("ticks", [21000000, 21600000])
def test_wavetable_smooth_random_elapsed_cycles_match_incremental_clock(canopy, ticks):
    controls = wavetable_controls(rate=7, clock=1, depth=100, shape=2)
    timing = {0: 4, 1: 88, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 20: 0, 21: 100}
    canopy([note(1, 60)], controls=controls, edits=timing)
    jumped = canopy(controls=controls, edits={**timing, 20: ticks})
    canopy(reset=1)
    canopy([note(1, 60)], controls=controls, edits=timing)
    for tick in [*range(5400000, ticks, 5400000), ticks]:
        stepped = canopy(controls=controls, edits={**timing, 20: tick})
    assert jumped["state"][244:247] == stepped["state"][244:247]
    assert wt_float(jumped["state"][239]) == pytest.approx(
        wt_float(stepped["state"][239]), abs=1e-6
    )
    assert wt_float(jumped["state"][247]) == pytest.approx(
        wt_float(stepped["state"][247]), abs=1e-6
    )


def test_wavetable_replace_tail_drain_invalidates_history_before_reused_generation_tokens(
    canopy,
):
    controls = wavetable_controls(table=3, env=180, wheel=150, velocity=150)
    old = canopy([note(1, 60, 127)], controls=controls, blocks=40)
    canopy(reset=2)
    drained = canopy(controls=controls, generation=2, blocks=4)
    assert drained["state"][200:232] == [0] * 32
    assert drained["state"][263] == 0
    fresh = canopy([note(1, 60, 64, owner=0x100)], controls=controls, generation=2)
    aux = [
        fresh["state"][i : i + 4] for i in range(200, 232, 4) if fresh["state"][i] == 1
    ]
    assert len(aux) == 1 and aux[0][2] == 16
    assert wt_float(aux[0][1]) < 0.02
    assert aux[0][1] != next(
        old["state"][i + 1] for i in range(200, 232, 4) if old["state"][i] == 1
    )


def test_wavetable_master_clock_generation_change_reobserves_without_free_phase_reset(
    canopy,
):
    controls = wavetable_controls(rate=5, clock=1, reset=0)
    timing = {0: 4, 1: 88, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 20: 1000, 21: 100}
    canopy([note(1, 60)], controls=controls, edits=timing)
    r = canopy(controls=controls, edits={**timing, 20: 217000})
    phase = r["state"][239]
    r = canopy(controls=controls, edits={**timing, 13: 2, 20: 10000000})
    assert r["state"][239] == phase
    r = canopy(controls=controls, edits={**timing, 13: 2, 20: 10216000})
    assert wt_float(r["state"][239]) == pytest.approx(0.02, rel=1e-6)


@pytest.mark.parametrize("version,size", [(5, 96), (6, 128), (7, 136)])
def test_wavetable_v5_master_requires_fine_clock_capability(canopy, version, size):
    controls = wavetable_controls(rate=5, clock=1)
    flags = 0x903 | (14400 << 16)
    timing = {
        0: version,
        1: size,
        12: flags,
        13: 1,
        14: 1,
        15: 0,
        16: 16,
        20: 1000,
        21: 100,
    }
    kwargs = {"native": [0] * 8 if version >= 6 else None, "post_amp": version == 7}
    canopy([note(1, 60)], controls=controls, edits=timing, **kwargs)
    r = canopy(controls=controls, edits={**timing, 20: 217000}, **kwargs)
    assert r["state"][239] == 0
    canopy(controls=controls, edits={**timing, 12: flags | 0x200, 20: 217000}, **kwargs)
    r = canopy(
        controls=controls, edits={**timing, 12: flags | 0x200, 20: 433000}, **kwargs
    )
    assert wt_float(r["state"][239]) == pytest.approx(0.01, rel=1e-6)
    # Losing the capability freezes scan even if a stale phase keeps advancing.
    r = canopy(controls=controls, edits={**timing, 20: 649000}, **kwargs)
    assert wt_float(r["state"][239]) == pytest.approx(0.01, rel=1e-6)


def fm_controls(
    algorithm=0,
    balance=50,
    ratio_a=3,
    ratio_b=8,
    feedback=0,
    attack=5,
    decay=40,
    sustain=50,
    release=15,
    env=100,
    velocity=100,
    wheel=100,
    depth=70,
    mix=0,
):
    values = spread_controls()
    values[0] = 10 | (0x4000 << 16)
    values[1] = 3
    values[2] = values[18] = depth
    values[6] = values[21] = mix
    for index, value in enumerate(
        (
            algorithm,
            balance,
            ratio_a,
            ratio_b,
            feedback,
            attack,
            decay,
            sustain,
            release,
            env,
            velocity,
            wheel,
        )
    ):
        values[16 + index] |= value << 16
    return values


def fm_histories(result):
    return {
        (result["state"][i], result["state"][i + 1] & 256): result["state"][i : i + 48]
        for i in range(296, 680, 48)
        if result["state"][i]
    }


def wavetable_fm_controls(fm_depth=0, ratio=3, fm_env=100, fm_wheel=100, **kwargs):
    """CPSB retains the complete wavetable bank and adds its four FM controls."""
    values = wavetable_controls(**kwargs)
    values[0] = (values[0] & ~255) | 11
    for index, value in enumerate((fm_depth, ratio, fm_env, fm_wheel), 28):
        values[index] = (values[index] & 65535) | (value << 16)
    return values


def wavetable_fm_histories(result):
    """Read owner-keyed PM history; pad and arp serials have separate namespaces."""
    state = result["state"]
    return {
        (state[200 + 4 * i], state[202 + 4 * i] & 256): state[
            337 + 48 * i : 343 + 48 * i
        ]
        for i in range(8)
        if state[200 + 4 * i] and state[337 + 48 * i] == state[200 + 4 * i]
    }


@pytest.mark.parametrize("table", range(5))
def test_wavetable_fm_depth_zero_matches_legacy_bank(canopy, table):
    params = dict(table=table, position=345, depth=100, rate=1000, shape=2, mix=64)
    events = [note(i + 1, 48 + i * 7) for i in range(4)]
    old = canopy(events, controls=wavetable_controls(**params), blocks=4)
    new = canopy(
        events,
        controls=wavetable_fm_controls(ratio=15, **params),
        state=0x20054000,
        blocks=4,
    )
    assert old["out"] == new["out"]
    assert old["state"][:176] == new["state"][:176]
    assert old["state"][200:296] == new["state"][200:296]
    assert old["state"][716:] == new["state"][716:]


def test_wavetable_fm_modulates_selected_table_and_retains_position_scan(canopy):
    import math

    params = dict(table=2, position=170, depth=70, rate=1000, shape=1)
    dry = canopy(
        [note(1, 60, 127)],
        controls=wavetable_fm_controls(**params),
        state_edits={232: wt_bits(1)},
        blocks=16,
    )
    modulated = canopy(
        [note(1, 60, 127)],
        controls=wavetable_fm_controls(fm_depth=80, ratio=8, **params),
        state=0x20054000,
        state_edits={232: wt_bits(1)},
        blocks=16,
    )
    assert (
        max(
            abs(wt_float(a) - wt_float(b))
            for a, b in zip(dry["out"], modulated["out"], strict=True)
        )
        > 0.001
    )
    assert voices(dry)[1] == voices(modulated)[1]
    assert dry["state"][233:248] == modulated["state"][233:248]
    assert wt_float(modulated["state"][239]) > 0
    previous = modulated
    for changed in (dict(position=620), dict(table=4), dict(depth=0)):
        unchanged = canopy(
            controls=wavetable_fm_controls(fm_depth=80, ratio=8, **params),
            state_edits=dict(enumerate(previous["state"])),
            blocks=4,
        )
        params.update(changed)
        modulated = canopy(
            controls=wavetable_fm_controls(fm_depth=80, ratio=8, **params),
            state=0x20054000,
            blocks=4,
        )
        assert voices(modulated)[1][:2] == voices(previous)[1][:2]
        # Compare identical oscillator/history snapshots: ordinary phase
        # advancement alone must not satisfy the live timbre-edit witness.
        assert modulated["out"] != unchanged["out"]
        assert voices(modulated)[1] == voices(unchanged)[1]
        assert wt_float(modulated["state"][239]) > wt_float(previous["state"][239])
        assert all(
            math.isfinite(wt_float(word)) and abs(wt_float(word)) < 0.1
            for word in modulated["out"]
        )
        previous = modulated
    assert modulated["state"][238] == 4


@pytest.mark.parametrize("index,bad", [(28, 101), (29, 16), (30, 201), (31, 201)])
def test_wavetable_fm_invalid_extension_cannot_apply_wheel_or_note(canopy, index, bad):
    controls = wavetable_fm_controls(fm_depth=70, fm_wheel=200)
    before = canopy([note(1, 60)], controls=controls)["state"]
    controls[index] = (controls[index] & 65535) | bad << 16
    rejected = canopy([[3, 0, 0, 1, 127, 0], note(2, 64)], controls=controls)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF
    assert rejected["state"] == before


@pytest.mark.parametrize("version", [9, 10])
def test_wavetable_fm_old_version_cannot_enable_new_extension(canopy, version):
    controls = wavetable_fm_controls(fm_depth=70)
    controls[0] = (controls[0] & ~255) | version
    before = canopy([note(1, 60)], controls=wavetable_controls())["state"]
    rejected = canopy([note(2, 64)], controls=controls)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF
    assert rejected["state"] == before


def test_wavetable_fm_status_echoes_complete_bank_and_native_tune(canopy):
    controls = wavetable_fm_controls(
        fm_depth=100, ratio=15, fm_env=175, fm_wheel=180, table=4, position=345
    )
    controls[0] = (controls[0] & 65535) | 0x4080 << 16
    timing = {0: 5, 1: 96, 11: 7}
    first = canopy([note(17, 60), note(18, 67)], controls=controls, edits=timing)
    fragments = {}
    for index in range(11):
        # Existing status tests cover multiplexing cadence. Address one detail
        # slot per call here to test this schema without 88 idle callbacks.
        result = canopy(
            controls=controls, edits=timing, state_edits={188: index * 8 + 4}
        )
        ready, *words = result["status"]
        assert ready == 1 and words[1] >> 30 == 1
        assert words[4] == 0x50B00 | index
        assert words[5] == 7
        fragments[index] = words[6:]
    bank = sum((fragments[i] for i in range(11)), [])
    assert bank[:16] == [word & 65535 for word in controls[16:]]
    assert bank[16:32] == [word >> 16 for word in controls[16:]]
    assert bank[38:42] == first["state"][189:193]
    assert bank[42] == 0x4080
    assert bank[32:38] == [0] * 6 and bank[43:] == [0] * 23


@pytest.mark.parametrize("latch", [0, 1])
def test_wavetable_fm_depth_upper_half_cannot_corrupt_fast_status(canopy, latch):
    controls = wavetable_fm_controls(fm_depth=100, ratio=15)
    controls[28] = (controls[28] & 0xFFFF0000) | latch
    result = canopy(controls=controls, edits={0: 5, 1: 96})
    ready, *words = result["status"]
    assert ready == 1
    assert words[1] == latch << 9  # No notes, arp, detail marker or phantom flags.


def test_wavetable_fm_live_ratio_and_depth_keep_phase_and_ramp(canopy):
    controls = wavetable_fm_controls(fm_depth=10, ratio=3)
    before = canopy([note(1, 60, 127)], controls=controls, blocks=8)
    initial = wavetable_fm_histories(before)[1, 0]
    changed = canopy(controls=wavetable_fm_controls(fm_depth=100, ratio=15))
    current = wavetable_fm_histories(changed)[1, 0]
    assert current[0] == initial[0] == 1
    assert wt_float(initial[2]) < wt_float(current[2]) < 8
    assert wt_float(initial[3]) < wt_float(current[3])
    assert 0 <= wt_float(current[1]) < 1
    assert current[1] != initial[1]
    assert voices(changed)[1][:2] == voices(before)[1][:2]
    # A second block continues toward the request rather than resetting it.
    later = canopy(controls=wavetable_fm_controls(fm_depth=100, ratio=15))
    history = wavetable_fm_histories(later)[1, 0]
    assert wt_float(current[2]) < wt_float(history[2]) < 8
    assert wt_float(current[3]) < wt_float(history[3])


def test_wavetable_fm_negative_roundoff_cannot_read_past_phase_endpoint(canopy_object):
    linked, _, initial, _, symbols, _ = canopy_object
    history = 0x20050000
    memory = dict(initial)
    for name, value in {
        "_wt_pm_active": 1,
        "_wt_pm_aux": history,
        "_wt_pm_base": wt_bits(0.00001),
        "_wt_pm_key_scale": wt_bits(1),
        "_wt_pm_env_amount": 0,
        "_wt_env": 0,
        "_wt_phase": 0,
        "_wt_increment": 0,
        "_wt_pm_ratio_step": 0,
    }.items():
        memory[symbols[name] // 4] = value
    for index, word in enumerate(
        [1, wt_bits(0.75), wt_bits(1), wt_bits(0.00001), 0, 0]
    ):
        memory[history // 4 + index] = word
    # The block pass reads each sample's envelope and phase from its records
    # (zero here) and publishes the last read phase.
    result = pk_sharc.run_call(
        linked,
        "_wt_pm_block",
        regs={
            "M4": 8,
            "M5": 0,
            "M6": 1,
            "M13": 0,
            "L0": 0,
            "L1": 0,
            "L2": 0,
            "MODE1": 0x1011800,
        },
        dm_words=memory,
        reads={"phase": (symbols["_wt_read_phase"] // 4, 1)},
        max_steps=5000,
    )
    assert result["halt"]["reason"] == "breakpoint"
    # The negative PM offset is smaller than half an ulp below 1. Wrapping
    # must handle its rounded endpoint before lookup(index + 1) occurs.
    assert wt_float(result["phase"][0]) == 0


@pytest.mark.parametrize("table", [0, 2, 4])
def test_wavetable_fm_samples_match_independent_phase_modulated_table(canopy, table):
    import math

    from modules.machines.canopy.tools.wavetables import MIPS
    from modules.machines.canopy.tools.wavetables import (
        dequantized as frames,  # the 16-bit samples the DSP plays
    )

    controls = wavetable_fm_controls(fm_depth=40, ratio=3, table=table, position=350)
    amp = native_amp(attack=1, decay=1, sustain=1)
    before = canopy(
        [note(1, 60, 127)],
        controls=controls,
        native=amp,
        state_edits={232: wt_bits(1)},
        blocks=32,
    )
    history = [wt_float(word) for word in wavetable_fm_histories(before)[1, 0]]
    voice = voices(before)[1]
    phase_start = math.atan2(wt_float(voice[2]), wt_float(voice[3])) / (2 * math.pi)
    increment = math.atan2(wt_float(voice[4]), wt_float(voice[5])) / (2 * math.pi)
    requested_ratio = 2
    ratio_bound = max(1, history[2], requested_ratio)
    q = ratio_bound * increment
    key_scale = max(0, min(1, (0.2 - q) * 10)) * min(1, 0.007 / q)
    # Key tracking attenuates the final PM index once; the depth trajectory is
    # squared to give the knob useful travel near its unmodulated endpoint.
    target_depth = 0.85 * math.sqrt(key_scale)
    reachable_gain = 1 - (1 - 0.004158) ** 32
    reachable_ratio = (
        max(history[2], history[2] + (requested_ratio - history[2]) * reachable_gain)
        + 0.00004
    )
    reachable_depth = (
        max(history[3], history[3] + (target_depth - history[3]) * reachable_gain)
        * 1.00001
    )
    frequency_bound = increment * (1 + 4 * reachable_depth * reachable_ratio)
    mip = next((i for i, (_, h) in enumerate(MIPS) if frequency_bound * h < 0.4), 6)
    mip_mix = max(0, min(1, (frequency_bound * MIPS[mip][1] - 0.2) * 5))
    waves = frames(table)

    def sample(frame, level, phase):
        size = MIPS[level][0]
        coordinate = (phase % 1) * size
        index = int(coordinate)
        wave = waves[frame][level]
        return wave[index] + (wave[index + 1] - wave[index]) * (coordinate - index)

    def reference(smooth):
        phase = phase_start
        mod_phase, ratio, depth = history[1:4]
        ratio_step = (requested_ratio - ratio) * (1 - (1 - 0.004158) ** 32) / 32
        result = []
        for _ in range(32):
            phase = (phase + increment) % 1
            ratio = ratio + ratio_step if smooth else requested_ratio
            depth = (
                depth + (target_depth - depth) * 0.004158 if smooth else target_depth
            )
            mod_phase = (mod_phase + increment * ratio) % 1
            read_phase = phase + math.sin(2 * math.pi * mod_phase) * depth**2 / (
                2 * math.pi
            )
            value = 0
            for frame in (3, 4):
                a = sample(frame, mip, read_phase)
                b = sample(frame, min(mip + 1, 6), read_phase)
                value += (a + (b - a) * mip_mix) * 0.5
            result.append(value * 0.08)
        return result, (mod_phase, ratio, depth)

    rendered = canopy(
        controls=wavetable_fm_controls(fm_depth=85, ratio=8, table=table, position=350),
        native=amp,
    )
    actual = [wt_float(word) for word in rendered["out"]]
    expected, end = reference(True)
    abrupt, _ = reference(False)
    assert max(abs(a - b) for a, b in zip(actual, expected, strict=True)) < 0.00003
    assert max(abs(a - b) for a, b in zip(actual, abrupt, strict=True)) > 0.0001
    assert [
        wt_float(word) for word in wavetable_fm_histories(rendered)[1, 0][1:4]
    ] == pytest.approx(end, abs=0.00001)
    expected_offset = sum(size + 1 for size, _ in MIPS[:mip])
    assert rendered["wt_offset_a"] == [expected_offset]


@pytest.mark.parametrize("pitch", [96, 108, 127])
def test_wavetable_fm_out_of_band_modulator_tapers_to_original_waveform(canopy, pitch):
    import math

    # At 8x these pitches put the modulator above the .20-cycle cutoff. The
    # published taper must remove PM, retaining the original mip-filtered WT;
    # this is a bounded cutoff witness, not a universal alias-free claim.
    assert 8 * 440 * 2 ** ((pitch - 69) / 12) / 48000 > 0.2
    original = canopy(
        [note(1, pitch, 127)], controls=wavetable_controls(table=4), blocks=4
    )
    modulated = canopy(
        [note(1, pitch, 127)],
        controls=wavetable_fm_controls(fm_depth=100, ratio=15, table=4),
        state=0x20054000,
        blocks=4,
    )
    assert wavetable_fm_histories(modulated)[1, 0][3] == wt_bits(0)
    assert original["out"] == modulated["out"]
    assert all(
        math.isfinite(wt_float(word)) and abs(wt_float(word)) < 0.1
        for word in modulated["out"]
    )


@pytest.mark.parametrize("canopy_object", [False], indirect=True)
@pytest.mark.parametrize("table", range(5))
@pytest.mark.parametrize("pitch", [60, 84, 96])
@pytest.mark.parametrize("ratio_index,ratio", [(3, 1), (15, 8)])
def test_wavetable_fm_integer_ratio_nonharmonic_residual_is_bounded(
    canopy, table, pitch, ratio_index, ratio, record_property
):
    import numpy as np

    result = canopy(
        [note(1, pitch, 127)],
        controls=wavetable_fm_controls(
            fm_depth=100, ratio=ratio_index, table=table, position=350
        ),
        state_edits={232: wt_bits(1)},
        blocks=256,
        capture=True,
    )
    audio = np.array([wt_float(word) for word in result["audio"][-4096:]])
    assert np.isfinite(audio).all() and np.max(np.abs(audio)) < 0.1
    assert np.sqrt(np.mean(audio**2)) > 0.001
    frequency = 440 * 2 ** ((pitch - 69) / 12)
    bins = np.fft.rfftfreq(len(audio), 1 / 48000)
    spectrum = np.abs(np.fft.rfft(audio * np.hanning(len(audio)))) ** 2
    allowed = np.zeros(len(bins), dtype=bool)
    # Integer-ratio PM has harmonics of the carrier, including a possible DC
    # component. This measures energy outside that lattice; aliases landing
    # on the lattice are not distinguished and no alias-free claim is made.
    for harmonic in range(int(24000 / frequency) + 1):
        allowed |= np.abs(bins - harmonic * frequency) < 4 * 48000 / len(audio)
    residual = spectrum[~allowed].sum() / spectrum.sum()
    residual_db = float(10 * np.log10(max(residual, 1e-30)))
    assert residual_db < -40
    depth = wt_float(wavetable_fm_histories(result)[1, 0][3])
    q = ratio * frequency / 48000
    key_scale = max(0, min(1, (0.2 - q) * 10)) * min(1, 0.007 / q)
    assert depth**2 <= key_scale + 0.00001
    record_property("nonharmonic_residual_db", residual_db)
    print("WT FM integer-ratio residual dB", table, pitch, ratio, residual_db)


def test_wavetable_fm_wheel_is_per_route_and_reset_preserves_other_owner(canopy):
    controls = wavetable_fm_controls(fm_depth=10, fm_wheel=200)
    before = canopy(
        [note(1, 60, 100), note(2, 60, 100, owner=0x100)],
        controls=controls,
        blocks=2,
    )
    histories = wavetable_fm_histories(before)
    assert histories[1, 0][3] == histories[2, 0][3]
    changed = canopy([[3, 0, 0, 1, 127, 0]], controls=controls, blocks=3)
    histories = wavetable_fm_histories(changed)
    assert wt_float(histories[1, 0][3]) > wt_float(histories[2, 0][3])
    reset_other = canopy([[3, 0x100, 0, 121, 0, 0]], controls=controls)
    assert reset_other["state"][264] == 127
    reset = canopy([[3, 0, 0, 121, 0, 0]], controls=controls)
    assert reset["state"][264] == 0
    assert set(voices(reset)) == {1, 2}


@pytest.mark.parametrize("amount", [0, 200])
def test_wavetable_fm_signed_envelope_changes_timbre_without_changing_amp(
    canopy, amount
):
    amp = native_amp(attack=1 / 64, sustain=0.7, release=0.99)
    neutral = canopy(
        [note(1, 60, 127)],
        controls=wavetable_fm_controls(fm_depth=50, attack=0),
        native=amp,
        blocks=8,
    )
    expressive = canopy(
        [note(1, 60, 127)],
        controls=wavetable_fm_controls(fm_depth=50, fm_env=amount, attack=0),
        native=amp,
        state=0x20054000,
        blocks=8,
    )
    a = wt_float(wavetable_fm_histories(neutral)[1, 0][3])
    b = wt_float(wavetable_fm_histories(expressive)[1, 0][3])
    assert b < a if amount < 100 else b > a
    assert voices(expressive) == voices(neutral)
    assert expressive["state"][200:296] == neutral["state"][200:296]
    assert expressive["state"][716:] == neutral["state"][716:]


def test_wavetable_fm_rendering_is_instance_local(canopy):
    controls = wavetable_fm_controls(fm_depth=75, ratio=7, fm_wheel=180)
    original = canopy([note(1, 60)], controls=controls, blocks=3)
    snapshot = dict(enumerate(original["state"]))
    expected = canopy(controls=controls)
    canopy(
        [note(8, 99, 127), [3, 0, 0, 1, 127, 0]],
        controls=wavetable_fm_controls(fm_depth=100, ratio=15, table=4),
        state=0x20054000,
        blocks=2,
    )
    actual = canopy(controls=controls, state_edits=snapshot)
    assert actual["out"] == expected["out"]
    assert actual["state"] == expected["state"]


def test_wavetable_fm_replace_tail_drain_clears_reused_token_history(canopy):
    controls = wavetable_fm_controls(fm_depth=80, ratio=8, fm_env=175, fm_wheel=180)
    old = canopy([note(1, 60, 127), [3, 0, 0, 1, 127, 0]], controls=controls, blocks=16)
    reset = canopy(reset=2)
    assert reset["instructions"] <= 1600
    assert reset["regs"]["R0"] == 0
    tail = canopy(controls=controls, generation=2, blocks=4)
    assert not voices(tail) and not wavetable_fm_histories(tail)
    fresh = canopy([note(1, 67, 64, owner=0x100)], controls=controls, generation=2)
    assert len(wavetable_fm_histories(fresh)) == 1
    assert wavetable_fm_histories(fresh)[1, 0] != wavetable_fm_histories(old)[1, 0]
    assert fresh["state"][264:296] == [0] * 32
    assert set(voices(fresh)) == {1}


@pytest.mark.parametrize("drift,parent", [(0, 1), (0, 2), (127, 1), (127, 2)])
def test_wavetable_fm_same_pitch_arp_inherits_parent_modulation(canopy, drift, parent):
    controls = wavetable_fm_controls(fm_depth=70, ratio=7, fm_env=175, fm_wheel=180)
    controls[3] = drift
    controls[19] = (controls[19] & 0xFFFF0000) | drift
    canopy([note(parent, 60, 100), [3, 0, 0, 1, 90, 0]], controls=controls, blocks=8)
    controls[6] = 64
    controls[21] = (controls[21] & 0xFFFF0000) | 64
    current = canopy(controls=controls)
    histories = wavetable_fm_histories(current)
    pad = histories[parent, 0]
    arp = histories[current["state"][136], 256]
    assert current["state"][155] == parent
    if drift == 0 or parent == 1:
        assert [wt_float(word) for word in arp[1:4]] == pytest.approx(
            [wt_float(word) for word in pad[1:4]], abs=2e-5
        )
    else:
        # Opposite parity has different drift: its independent oscillator may
        # not inherit phase or the pitch-scaled PM depth. The underlying WT
        # envelope and input route still follow the parent.
        assert abs(wt_float(arp[1]) - wt_float(pad[1])) > 0.001
        aux = {
            (current["state"][i], current["state"][i + 2] & 256): current["state"][
                i : i + 4
            ]
            for i in range(200, 232, 4)
            if current["state"][i]
        }
        child_env = aux[current["state"][136], 256]
        assert child_env[1] == aux[parent, 0][1]
        assert child_env[2] & 63 == aux[parent, 0][2] & 63
        assert 0 < wt_float(arp[3]) < wt_float(pad[3])


def test_wavetable_fm_source_roundtrip_preserves_held_owners(canopy):
    controls = wavetable_fm_controls(fm_depth=90, ratio=12, table=3)
    current = canopy([note(1, 60), note(2, 67)], controls=controls, blocks=8)
    owners = {token: row[:2] for token, row in voices(current).items()}
    for destination in (fm_controls(algorithm=3, feedback=80), controls):
        for _ in range(12):
            current = canopy(controls=destination)
            assert {token: row[:2] for token, row in voices(current).items()} == owners
            assert current["regs"]["R0"] == 0
            assert current["instructions"] < 100000
        assert current["state"][712] == destination[1]
        assert wt_float(current["state"][713]) == 1
    assert set(wavetable_fm_histories(current)) == {(1, 0), (2, 0)}


@pytest.mark.parametrize("spread", [0, -3, 3])
@pytest.mark.parametrize("interior", [False, True], ids=["clamped", "interior"])
@pytest.mark.parametrize(
    "attack", [1 / 31, 1 / 512, 1e-7], ids=["boundary", "slow", "slow-float-boundary"]
)
def test_wavetable_fm_native_amp_seven_groups_and_transition_fit_ceiling(
    canopy, spread, record_property, attack, interior
):
    controls = wavetable_fm_controls(
        fm_depth=100,
        ratio=15,
        fm_env=200,
        fm_wheel=200,
        table=0,
        position=345,
        depth=0 if interior else 100,
        rate=1000,
        shape=2,
        env=100 if interior else 175,
        wheel=100 if interior else 180,
        mix=64,
    )
    controls[0] = (controls[0] & 65535) | (0x4080 << 16)
    controls[3] = 127
    controls[19] = (controls[19] & 0xFFFF0000) | 127
    controls[10] = 4
    controls[23] = (controls[23] & 0xFFFF0000) | 4
    controls[11] = spread + 3
    controls[24] = (controls[24] & 0xFFFF0000) | (spread + 3)
    amp = native_amp(attack=attack, sustain=1, release=0.9991)
    canopy(
        [note(i + 1, 48 + 3 * i, 127) for i in range(4)]
        + [[3, 0, 0, 1, 127, 0], [4, 0, 0, 0, 16383, 0]],
        controls=controls,
        native=amp,
        post_amp=True,
        blocks=12,
    )
    controls[16] = (controls[16] & 65535) | 4 << 16
    boundary = {}
    if attack == 1e-7:
        # These valid levels approach the repeated-float attack boundary and
        # force scalar phase handling while the table transition is busiest.
        boundary = {slot * 8 + 7: wt_bits(0.9999967813491821) for slot in range(4)}
        boundary[143] = wt_bits(0.9999967813491821)
    busy = canopy(
        [note(i + 5, 50 + 3 * i, 127) for i in range(8)],
        controls=controls,
        native=amp,
        post_amp=True,
        state_edits={146: 89999, **boundary},
    )
    assert busy["regs"]["R0"] == 0
    assert len(voices(busy)) == 4
    assert busy["state"][32] and busy["state"][40] and busy["state"][136]
    assert busy["state"][235:237] == [0, 4]
    if interior:
        # Frames 3 and 4 must both be interpolated in both changing tables;
        # a clamped final frame takes the cheaper single-frame branch.
        assert busy["wt_frame"] == [3 * 1159]
    assert busy["instructions"] < 100000
    if boundary:
        # New onsets start their own envelopes, so exercise the following
        # callback with all held pad/arp envelopes reaching the same boundary.
        boundary.update({717 + slot * 4: 2 for slot in range(4)})
        boundary[741] = 2
    steady = canopy(controls=controls, native=amp, post_amp=True, state_edits=boundary)
    assert steady["instructions"] < 100000
    reset = canopy(reset=1)
    assert reset["state"] == [0] * 744 and reset["instructions"] <= 1600
    record_property(
        "max_process_instructions", max(busy["instructions"], steady["instructions"])
    )
    record_property("reset_instructions", reset["instructions"])
    print(
        "WT FM seven-group native AMP process/reset",
        "interior" if interior else "clamped",
        spread,
        attack,
        busy["instructions"],
        steady["instructions"],
        reset["instructions"],
    )


@pytest.mark.parametrize("algorithm", range(4))
def test_fm_graphs_execute_actual_sharc_and_keep_note_ownership(canopy, algorithm):
    import math

    controls = fm_controls(algorithm=algorithm, feedback=40)
    r = canopy([note(1, 60, 127)], controls=controls, blocks=2)
    assert r["regs"]["R0"] == 0
    samples = [wt_float(w) for w in r["out"]]
    assert all(math.isfinite(x) and abs(x) <= 0.08 for x in samples)
    assert max(map(abs, samples)) > 0.001, (
        r["state"][:8],
        r["state"][680:716],
        fm_histories(r),
    )
    assert set(voices(r)) == {1}
    assert (1, 0) in fm_histories(r)
    assert r["instructions"] / 2 < 100000


@pytest.mark.parametrize("event_count", [4, 8])
def test_fm_worst_case_seven_groups_and_graph_morph_fit_ceiling(canopy, event_count):
    controls = fm_controls(
        algorithm=3, feedback=100, env=200, velocity=200, wheel=200, mix=64, depth=127
    )
    canopy([note(i + 1, 60 + 3 * i) for i in range(4)], controls=controls, blocks=12)
    r = canopy(
        [note(i + 5, 62 + 3 * i) for i in range(event_count)],
        controls=fm_controls(algorithm=0, feedback=100, mix=64, depth=127),
    )
    assert r["regs"]["R0"] == 0
    assert r["state"][32] and r["state"][40] and r["state"][136]
    assert r["instructions"] < 100000
    print("FM seven-group graph-transition instructions", r["instructions"])


@pytest.mark.parametrize(
    "control,bad",
    [
        (16, 4),
        (17, 101),
        (18, 16),
        (19, 16),
        (20, 101),
        (21, 128),
        (22, 128),
        (23, 101),
        (24, 128),
        (25, 201),
        (26, 201),
        (27, 201),
        (28, 1),
    ],
)
def test_fm_rejects_bad_bank_before_any_voice_write(canopy, control, bad):
    controls = fm_controls()
    before = canopy([note(1, 60)], controls=controls)["state"]
    controls[control] = (controls[control] & 65535) | bad << 16
    r = canopy([note(2, 64)], controls=controls)
    assert r["regs"]["R0"] == 0xFFFFFFFF
    assert r["state"] == before


def test_fm_reset_tail_then_reused_token_has_fresh_route_and_envelope(canopy):
    controls = fm_controls(env=200, wheel=200)
    warm = canopy(
        [note(1, 60, 127), [3, 0, 0, 1, 127, 0]], controls=controls, blocks=80
    )
    history = fm_histories(warm)[1, 0]
    assert wt_float(history[5]) > 0
    reset = canopy(reset=2)
    assert reset["regs"]["R0"] == 0
    assert reset["instructions"] <= 1600
    assert reset["state"][296:680] == warm["state"][296:680]
    tail = canopy(controls=controls, blocks=4, generation=2)
    assert not fm_histories(tail)
    fresh = canopy([note(1, 64, 60, owner=0x100)], controls=controls, generation=2)
    h = fm_histories(fresh)[1, 0]
    assert h[1] & 63 == 16
    assert wt_float(h[5]) < wt_float(history[5])


def test_fm_full_reset_fits_published_ceiling_and_clears_all_state(canopy):
    canopy([note(1, 60)], controls=fm_controls(), blocks=8)
    r = canopy(reset=1)
    assert r["regs"]["R0"] == 0 and r["state"] == [0] * 744
    assert r["instructions"] <= 1600
    print("FM full reset instructions", r["instructions"])


def test_fm_source_change_preserves_held_owners_without_running_both_engines(canopy):
    r = canopy([note(1, 60), note(2, 64)], controls=wavetable_controls(), blocks=16)
    before = {token: row[:2] for token, row in voices(r).items()}
    for controls in [
        fm_controls(),
        wavetable_controls(table=4),
        fm_controls(algorithm=3),
    ]:
        for _ in range(12):
            r = canopy(controls=controls)
            assert {token: row[:2] for token, row in voices(r).items()} == before
            assert r["regs"]["R0"] == 0 and r["instructions"] < 100000
        assert r["state"][712] == controls[1]
        assert wt_float(r["state"][713]) == 1


def test_fm_reentry_reinitializes_dirty_history_payload(canopy):
    controls = fm_controls(algorithm=3, feedback=100, env=200, velocity=200)
    canopy([note(1, 60, 127), note(2, 67, 90)], controls=controls, blocks=16)
    canopy(controls=wavetable_controls(table=4), blocks=12)
    faded = canopy(controls=controls, blocks=4)
    assert wt_float(faded["state"][713]) == 0
    assert any(faded["state"][296:680])  # Previous FM payload remains dirty.
    # The same held-note state with empty history is the reentry oracle. A
    # source handoff must initialize every reused record before rendering it.
    empty_history = dict(enumerate(faded["state"]))
    empty_history.update(dict.fromkeys(range(296, 680), 0))
    reference = canopy(controls=controls, state=0x20054000, state_edits=empty_history)
    actual = canopy(controls=controls)
    assert actual["out"] == reference["out"]
    assert actual["state"] == reference["state"]


@pytest.mark.parametrize("enter_fm", [True, False])
@pytest.mark.parametrize(
    "spread,order,descending",
    [
        (0, 4, False),
        (3, 4, False),
        (-3, 4, False),
        (-3, 4, True),
        (-3, 3, True),
        (3, 3, True),
    ],
    ids=["neutral", "expressive", "downward", "reverse", "played-down", "played-up"],
)
def test_fm_source_handoff_with_eight_onsets_fits_ceiling(
    canopy, enter_fm, spread, order, descending
):
    fm = fm_controls(
        algorithm=3, feedback=100, depth=127, mix=64, env=200, velocity=200, wheel=200
    )
    wavetable = wavetable_controls(table=4, depth=100)
    wavetable[6] = wavetable[21] = 64
    initial = [
        note(i + 1, 60 + 3 * (3 - i if descending else i), 127) for i in range(4)
    ]
    if spread:
        for controls in (fm, wavetable):
            controls[0] = (controls[0] & 65535) | (0x4080 << 16)
            controls[3] = 127
            controls[19] = (controls[19] & 0xFFFF0000) | 127
            controls[10] = order
            controls[11] = spread + 3
            controls[23] = (controls[23] & 0xFFFF0000) | order
            controls[24] = (controls[24] & 0xFFFF0000) | (spread + 3)
        initial += [[3, 0, 0, 1, 127, 0], [4, 0, 0, 0, 16383, 0]]
    source, destination = (wavetable, fm) if enter_fm else (fm, wavetable)
    canopy(initial, controls=source, blocks=12)
    faded = canopy(controls=destination, blocks=4)
    assert wt_float(faded["state"][713]) == 0
    # The next callback adopts the new engine and initializes its histories
    # while the full event batch creates both shared tails and pending voices.
    r = canopy(
        [note(i + 5, 62 + 3 * (7 - i if descending else i), 127) for i in range(8)],
        controls=destination,
        state_edits={146: 89999} if spread else None,
    )
    assert r["regs"]["R0"] == 0
    assert r["state"][712] == destination[1]
    assert r["state"][32] and r["state"][40] and r["state"][136]
    assert r["instructions"] < 100000
    print(
        "FM source-handoff eight-onset instructions",
        enter_fm,
        spread,
        order,
        descending,
        r["instructions"],
    )


@pytest.mark.parametrize(
    "drift,parent_token,phase",
    [(0, 1, None), (127, 1, None), (0, 2, None), (127, 2, None), (0, 2, 0.5)],
)
def test_fm_same_pitch_arp_inherits_full_parent_signal_history(
    canopy, drift, parent_token, phase
):
    controls = fm_controls(algorithm=3, feedback=55, env=180, velocity=125, wheel=150)
    controls[3] = drift
    controls[19] = (controls[19] & 0xFFFF0000) | drift
    canopy(
        [note(parent_token, 60, 110), [3, 0, 0, 1, 90, 0]], controls=controls, blocks=8
    )
    controls = fm_controls(
        algorithm=1,
        ratio_a=5,
        ratio_b=7,
        feedback=30,
        env=180,
        velocity=125,
        wheel=150,
        mix=64,
    )
    controls[3] = drift
    controls[19] = (controls[19] & 0xFFFF0000) | drift
    # The second triangle half can produce IEEE negative zero at DRFT0.
    r = canopy(
        controls=controls,
        state_edits={} if phase is None else {133: wt_bits(phase)},
    )
    assert r["state"][155] == parent_token and r["state"][136] == 1
    histories = fm_histories(r)
    pad = histories[parent_token, 0]
    child = histories[r["state"][136], 256]
    if drift == 0 or parent_token == 1:
        # The audible filter and modulation state must agree, not just carrier phase.
        for i in [2, 3, 4, 5, 7, 8, *range(9, 41), 47]:
            assert wt_float(child[i]) == pytest.approx(wt_float(pad[i]), abs=0.00002), i
    else:
        # Opposite token parity has opposite drift: retain independent PM/FIR
        # history while inheriting the parent's timbre envelope and controls.
        for i in [5, 8, *range(32, 41)]:
            assert wt_float(child[i]) == pytest.approx(wt_float(pad[i]), abs=0.00002), i
        assert abs(wt_float(child[2]) - wt_float(pad[2])) > 0.01
    assert child[1] & 63 == pad[1] & 63 == 0


def test_fm_wheel_is_per_route_and_cc121_resets_only_its_owner(canopy):
    controls = fm_controls(depth=10, wheel=200)
    before = canopy(
        [note(1, 60, 100), note(2, 60, 100, owner=0x100)], controls=controls, blocks=2
    )
    assert fm_histories(before)[1, 0][8] == fm_histories(before)[2, 0][8]
    r = canopy([[3, 0, 0, 1, 127, 0]], controls=controls, blocks=3)
    histories = fm_histories(r)
    assert wt_float(histories[1, 0][8]) > wt_float(histories[2, 0][8])
    held = canopy([[3, 0x100, 0, 121, 0, 0]], controls=controls)
    assert held["state"][264] == 127
    reset = canopy([[3, 0, 0, 121, 0, 0]], controls=controls)
    assert reset["state"][264] == 0


def test_fm_feedback_extreme_and_owner_churn_preserve_eight_record_bound(canopy):
    import math

    for batch in range(7):
        controls = fm_controls(
            algorithm=batch % 4,
            ratio_a=15,
            ratio_b=15,
            feedback=100,
            depth=127,
            env=200,
            mix=64,
        )
        r = canopy(
            [note(batch * 4 + i + 1, 48 + batch * 3 + i) for i in range(4)],
            controls=controls,
            blocks=3,
        )
        assert r["regs"]["R0"] == 0
        assert all(
            math.isfinite(wt_float(w)) and abs(wt_float(w)) < 1 for w in r["out"]
        )
        assert len(fm_histories(r)) <= 8
        assert set(voices(r)).issubset(set(range(batch * 4 + 1, batch * 4 + 5)))


def test_fm_live_ratio_bandwidth_taper_is_sample_linear(canopy):
    import math

    from modules.machines.canopy.tools.fm_tables import halfband

    before = canopy([note(1, 84, 127)], controls=fm_controls(depth=127), blocks=20)
    history = [wt_float(w) for w in fm_histories(before)[1, 0]]
    increment = math.atan2(
        wt_float(voices(before)[1][4]), wt_float(voices(before)[1][5])
    ) / (4 * math.pi)
    target_scale = max(0, min(1, (0.18 - 8 * increment) * 8)) * min(
        1, 0.007 / (8 * increment)
    )
    rendered = canopy(controls=fm_controls(ratio_a=15, ratio_b=15, depth=127))
    actual = [wt_float(w) for w in rendered["out"]]
    assert wt_float(fm_histories(rendered)[1, 0][47]) == pytest.approx(
        target_scale, abs=1e-6
    )

    def reference(linear):
        a, b, c = history[2:5]
        depth = history[8]
        even, odd = history[9:24], history[24:32]
        ratio_a, ratio_b = history[37:39]
        ramp_gain = (1 - (1 - 0.004158) ** 32) / 32
        step_a, step_b = (8 - ratio_a) * ramp_gain, (8 - ratio_b) * ramp_gain
        taps = halfband()
        output = []
        for sample in range(32):
            depth += (1 - depth) * 0.004158
            scale = (
                history[47] + (target_scale - history[47]) * (sample + 1) / 32
                if linear
                else target_scale
            )
            ratio_a += step_a
            ratio_b += step_b
            internal = []
            for _ in range(2):
                a += ratio_a * increment
                b += ratio_b * increment
                c += increment
                index = 4 * depth * scale
                x = math.sin(2 * math.pi * c + index * math.sin(2 * math.pi * a))
                y = math.sin(2 * math.pi * c + index * math.sin(2 * math.pi * b))
                internal.append((x + y) * 0.5)
            value = taps[0] * (internal[1] + even[14])
            value += sum(
                taps[2 * j] * (even[j - 1] + even[14 - j]) for j in range(1, 8)
            )
            value += 0.5 * odd[6]
            output.append(value * 0.08)
            even = [internal[1], *even[:14]]
            odd = [internal[0], *odd[:7]]
        return output

    expected = reference(True)
    abrupt = reference(False)
    assert max(abs(a - b) for a, b in zip(actual, expected, strict=True)) < 0.00002
    # This witness would fail if the new cap were applied at the block edge.
    assert max(abs(a - b) for a, b in zip(actual, abrupt, strict=True)) > 0.001


def _isolation_schedule():
    """Two differently configured instances: wavetable PM with arp, FM with arp."""
    a = dict(
        controls=wavetable_fm_controls(
            fm_depth=60, table=2, position=345, depth=100, rate=1000, shape=2, mix=64
        ),
        native=native_amp(),
        post_amp=True,
        state=0x20050000,
        track=0,
    )
    b = dict(
        controls=fm_controls(algorithm=2, feedback=40, mix=96),
        native=native_amp(attack=1 / 16, sustain=0.8),
        post_amp=True,
        state=0x20054000,
        track=10,
    )
    events_a = [
        [note(i + 1, 48 + 7 * i) for i in range(4)],
        [],
        [note(2, 55, 0, off=True)],
        [],
        [note(5, 64)],
        [],
        [note(1, 48, 0, off=True), note(3, 62, 0, off=True)],
        [],
    ]
    events_b = [
        [note(1, 62), note(2, 69)],
        [note(3, 74)],
        [],
        [note(1, 62, 0, off=True)],
        [],
        [note(4, 57, 90)],
        [],
        [note(2, 69, 0, off=True)],
    ]
    return a, b, events_a, events_b


def _isolation_trace(result):
    return (
        result["regs"]["R0"],
        list(result["out"]),
        list(result["common_amp"]),
        list(result["state"]),
    )


def test_two_instances_interleaved_match_each_instance_alone(canopy_object):
    """Several admitted instances share the module's scratch globals. Each block
    of one instance must be bit-identical whether or not another instance, with
    other notes, family and arp, ran between its blocks."""
    a, b, events_a, events_b = _isolation_schedule()
    alone_a, alone_b = make_canopy(canopy_object), make_canopy(canopy_object)
    expected_a = [_isolation_trace(alone_a(e, **a)) for e in events_a]
    expected_b = [_isolation_trace(alone_b(e, **b)) for e in events_b]
    shared = make_canopy(canopy_object)
    for block, (ea, eb) in enumerate(zip(events_a, events_b, strict=True)):
        assert _isolation_trace(shared(ea, **a)) == expected_a[block], ("A", block)
        assert _isolation_trace(shared(eb, **b)) == expected_b[block], ("B", block)
    assert any(any(trace[1]) for trace in expected_a)
    assert any(any(trace[1]) for trace in expected_b)
