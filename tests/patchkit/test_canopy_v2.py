"""CANOPY normalized V2 callbacks executed on the SHARC instruction core."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module", params=[False, True], ids=["direct", "protected"])
def canopy_object(request):
    root = REPO_ROOT / "modules/machines/canopy/src/dsp"
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
    R1 = DM(_test_loop_left);
    R1 = R1 - 1;
    DM(_test_loop_left) = R1;
    IF NE JUMP _test_loop;
    RTS;
.PKBSS _test_capture, 4;
.PKBSS _test_loop_left, 4;
.PKBSS _test_loop_state, 4;
.PKBSS _test_loop_input, 4;
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


@pytest.fixture
def canopy(canopy_object):
    linked, sections, initial, obj, symbols, protected = canopy_object
    dm = dict(initial)
    dm.update({0x20015000 // 4 + i: 0 for i in range(13)})
    dm[0x20015000 // 4 - 1] = dm[0x20015000 // 4 + 13] = 0xDEADBEEF
    for base, size in [(0x20050000, 744), (0x20054000, 744), (0x20013000, 32)]:
        dm.update({base // 4 + i: 0 for i in range(size)})
        dm[base // 4 - 1] = dm[base // 4 + size] = 0xDEADBEEF

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
        input_base=0x20011000,
    ):
        dm.update({state // 4 + i: v for i, v in (state_edits or {}).items()})
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
        for index, value in (edits or {}).items():
            words[index] = value
        dm.update({input_base // 4 + i: value for i, value in enumerate(words)})
        dm.update({0x20012000 // 4 + i: v for i, v in enumerate(controls)})
        for i, event in enumerate(events):
            dm.update(
                {0x20012100 // 4 + i * 6 + j: value for j, value in enumerate(event)}
            )
        reads = {
            name: (sections[name] // 4, len(blob) // 4)
            for name, (blob, _, _) in obj.data.items()
        }
        reads.update({"state": (state // 4, 744), "out": (0x20013000 // 4, 32)})
        reads["status"] = (0x20015000 // 4, 13)
        reads["amp_curve"] = (symbols["_cp_amp_samples"] // 4, 32)
        reads["amp_end"] = (symbols["_cp_amp_end"] // 4, 1)
        for debug in (
            "enabled",
            "phase",
            "increment",
            "size_a",
            "offset_a",
            "position",
            "frame",
            "frame_base",
            "result",
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
            dm[symbols["_ir_process_flags"] // 4] = (
                3 if words[0] == 6 else (1 if words[0] == 5 else 0)
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
                dm.update(
                    {symbols[name] // 4 + i: value for i, value in enumerate(values)}
                )
        if packet is not None:
            assert protected, "packet dispatch requires the registry"
            dm.update({0x20014000 // 4 + i: value for i, value in enumerate(packet)})
            # Poison audio to prove that rejection cannot replay the last block.
            dm.update({0x20013000 // 4 + i: 0x3F800000 for i in range(32)})
        if capture:
            assert blocks > 1
            reads["audio"] = (0x200B0000 // 4, blocks * 32)
        result = pk_sharc.run_call(
            linked,
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
            dm_words=dm,
            reads=reads,
            max_steps=100000 * blocks,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["guard_before"] == result["guard_after"] == [0xDEADBEEF]
        assert result["status_before"] == result["status_after"] == [0xDEADBEEF]
        for name, (base, _) in reads.items():
            dm.update({base + i: value for i, value in enumerate(result[name])})
        return result

    return call


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
    attack=1 / 64,
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
        [i / 64 for i in range(32)], abs=1e-7
    )
    assert wt_float(voices(r)[1][7]) == pytest.approx(0.5)
    r = canopy([note(2, 67)], native=amp)
    assert wt_float(voices(r)[1][7]) == pytest.approx(1.0)
    assert wt_float(voices(r)[2][7]) == pytest.approx(0.5)
    r = canopy([note(1, 60, off=True)], native=amp)
    assert wt_float(voices(r)[1][7]) == pytest.approx(0.8**32, rel=2e-6)
    assert wt_float(voices(r)[2][7]) == pytest.approx(1.0)


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
    before = canopy([note(1, 60)], native=amp, controls=fm_controls())
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
    r = canopy([note(1, 60)], controls=controls, native=amp)
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
    assert curve == [0.0] + [1.0] * 31
    r = canopy([note(1, 60, off=True)] if off else [], native=amp)
    assert [wt_float(v) for v in r["amp_curve"]] == pytest.approx(
        [1.0] * 9 + [0.5**i for i in range(23)], abs=1e-7
    )
    assert wt_float(r["amp_end"][0]) == pytest.approx(0.5**23, rel=1e-6)
    r = canopy([note(2, 67)], native=amp)
    assert 2 in voices(r) and wt_float(voices(r)[2][7]) == 1.0


def test_native_amp_infinite_release_yields_to_bounded_reset(canopy):
    amp = native_amp(mode=1, attack=1, release=1)
    canopy([note(1, 60)], native=amp)
    r = canopy([note(1, 60, off=True)], native=amp, blocks=4)
    assert wt_float(voices(r)[1][7]) == 1.0
    reset = canopy(reset=2)
    assert reset["instructions"] <= 1600
    r = canopy(native=amp, generation=2, blocks=3)
    assert not voices(r)
    fresh = canopy([note(1, 60)], native=amp, generation=2)
    assert wt_float(fresh["amp_curve"][0]) == 0.0
    assert wt_float(voices(fresh)[1][7]) == 1.0


@pytest.mark.parametrize("attack,initial", [(1.0, 1.0), (1 / 64, 1 / 128)])
def test_native_amp_restart_option_initializes_each_onset(canopy, attack, initial):
    r = canopy([note(1, 60)], native=native_amp(attack=attack, options=1))
    assert wt_float(r["amp_curve"][0]) == initial


def test_native_amp_fallback_and_mode_changes_do_not_resume_stale_phase(canopy):
    amp = native_amp(attack=1 / 64)
    first = canopy([note(1, 60)], native=amp)
    assert wt_float(voices(first)[1][7]) == 0.5
    legacy = canopy(native=[0] * 8)
    assert legacy["state"][716:] == [0] * 28
    fresh = canopy(native=amp)
    assert wt_float(fresh["amp_curve"][0]) == 0.0
    assert wt_float(voices(fresh)[1][7]) == 0.5
    changed = canopy(native=native_amp(mode=1, attack=1 / 64, hold=40))
    assert wt_float(changed["amp_curve"][0]) == 0.5
    assert wt_float(voices(changed)[1][7]) == 1.0


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
    before = canopy([note(1, 60)], controls=controls, native=amp)
    assert before["state"][136] == 1
    prior_phase, prior_count = before["state"][741:743]
    assert prior_phase == 3 and prior_count == 31
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
    before = canopy([note(i + 1, 60 + 3 * i) for i in range(4)], native=amp)
    source = next(slot for slot in range(4) if before["state"][slot * 8] == 1)
    metadata = before["state"][716 + source * 4 : 720 + source * 4]
    after = canopy([note(5, 74)], native=amp)
    tail = next(slot for slot in (4, 5) if after["state"][slot * 8] == 1)
    assert after["state"][716 + tail * 4 : 720 + tail * 4] == metadata
    assert wt_float(after["state"][tail * 8 + 7]) == 0.5
    after = canopy(native=amp)
    assert after["state"][tail * 8] == 0
    assert set(voices(after)) == {2, 3, 4, 5}


def test_native_amp_rejected_steal_batch_preserves_envelope_metadata(canopy):
    amp = native_amp(attack=1, sustain=1, release=1)
    before = canopy([note(i + 1, 60 + 3 * i) for i in range(4)], native=amp)
    # The first event steals a rendered voice into a tail; duplicate admission
    # fails inside the staged event engine, after that metadata has moved.
    rejected = canopy([note(5, 74), note(5, 76)], native=amp)
    assert rejected["regs"]["R0"] == 0xFFFFFFFF
    assert rejected["state"] == before["state"]
    after = canopy([note(5, 74)], native=amp)
    tail = next(slot for slot in (4, 5) if after["state"][slot * 8] == 1)
    assert after["state"][716 + tail * 4 : 720 + tail * 4] == [1, 1, 0, 2]


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
    assert wt_float(voices(r)[1][7]) == 0.5


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


def test_v5_fine_clock_does_not_schedule_without_phase_provider(canopy):
    controls = presentation_controls(base=FINE)
    for block, boundary in [(0, 1), (188, 2), (376, 3)]:
        r = canopy(
            [note(1, 60)] if not block else [],
            controls=controls,
            edits={
                **position(block, boundary, flags=0x103),
                0: 5,
                1: 96,
                20: (boundary - 1) * 5400000,
                21: (boundary - 1) * 5400000,
            },
        )
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


def test_pattern_identity_change_restarts_local_arp_mid_pattern(canopy):
    controls = STRUM[:6] + (127,) + STRUM[7:14] + (1, 0)
    timing = {**mapped(), 12: 3, 13: 1, 14: 10, 15: 4, 19: 0x43170000}
    canopy([note(1, 60), note(2, 64)], controls=controls, edits=timing)
    canopy(controls=controls, edits=timing, state_edits={146: 89999})
    r = canopy(controls=controls, edits={**timing, 19: 0x43180000})
    assert r["state"][151] == 60


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

    from tools.patchkit.canopy_wavetables import MIPS, frames

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


def test_wavetable_v5_master_requires_fine_clock_capability(canopy):
    controls = wavetable_controls(rate=5, clock=1)
    timing = {0: 5, 1: 96, 12: 3, 13: 1, 14: 1, 15: 0, 16: 16, 20: 1000, 21: 100}
    canopy([note(1, 60)], controls=controls, edits=timing)
    r = canopy(controls=controls, edits={**timing, 20: 217000})
    assert r["state"][239] == 0
    canopy(controls=controls, edits={**timing, 12: 515, 20: 217000})
    r = canopy(controls=controls, edits={**timing, 12: 515, 20: 433000})
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

    from tools.patchkit.canopy_fm_tables import halfband

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
