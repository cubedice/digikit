"""CANOPY normalized V2 callbacks executed on the SHARC instruction core."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module", params=[False, True], ids=["direct", "protected"])
def canopy_object(request):
    root = REPO_ROOT / "modules/machines/canopy/src/dsp"
    source = "\n".join(
        (root / f"{name}.s").read_text()
        for name in ["voice", "tuning", "events", "instrument", "v2"]
    )
    if request.param:
        registry = REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp"
        source += "\n" + "\n".join(
            (registry / name).read_text()
            for name in ["process.s", "process-protected.s", "input.s"]
        )
        source += "\n.PKDATA _ir_rows, 512;\n.PKDATA _ir_process_rows, 256;\n"
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
    for base, size in [(0x20050000, 136), (0x20054000, 136), (0x20013000, 32)]:
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
        ]
        for index, value in (edits or {}).items():
            words[index] = value
        dm.update({0x20011000 // 4 + i: value for i, value in enumerate(words)})
        dm.update({0x20012000 // 4 + i: v for i, v in enumerate(controls)})
        for i, event in enumerate(events):
            dm.update(
                {0x20012100 // 4 + i * 6 + j: value for j, value in enumerate(event)}
            )
        reads = {
            name: (sections[name] // 4, len(blob) // 4)
            for name, (blob, _, _) in obj.data.items()
        }
        reads.update({"state": (state // 4, 136), "out": (0x20013000 // 4, 32)})
        reads.update(
            {"guard_before": (state // 4 - 1, 1), "guard_after": (state // 4 + 136, 1)}
        )
        if protected and reset is None:
            row = [3, 2, 9, 1, state, 136, state, state + 544, 0, 0, 0, 0, 0, 0, 0, 0]
            meta = [
                2,
                9,
                20,
                linked.symbols["_canopy_process"],
                0xB90000,
                0xBC0000,
                8,
                6,
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
        result = pk_sharc.run_call(
            linked,
            "_test_packet"
            if packet is not None
            else ("_test_process" if reset is None else "_test_reset"),
            regs={
                "R4": 0 if protected and reset is None else state,
                "R5": 0,
                "R9": 1,
                "R12": 0x20014000 if packet else 0,
                "R8": 0x20011000 if reset is None else reset,
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
            max_steps=100000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["guard_before"] == result["guard_after"] == [0xDEADBEEF]
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
        if result["state"][i]
    }


def test_v2_chord_independent_release_and_state_isolation(canopy):
    assert canopy(reset=1)["state"] == [0] * 136
    result = canopy([note(1, 60), note(2, 64)])
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1, 2}
    result = canopy([note(3, 67, owner=0x100)])
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
        [1, 0x200, 2, 64 << 8, 100 << 8, 0],
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
    assert canopy(reset=3)["state"] == [0] * 136
    assert canopy(generation=2)["regs"]["R0"] == 1
    assert set(voices(canopy(state=0x20054000, track=1))) == {2}


def test_velocity_is_per_note_and_other_route_bend_isolated(canopy):
    result = canopy([note(1, 60, 20), note(2, 67, 127, owner=0x100)])
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


def wire_packet(events, *, track=0, generation=1, revision=1, epoch=1):
    words = [0x494E0101, 1 | (len(events) << 8), epoch, 1, 1, revision, 0, 0] + [0] * 24
    for i, (token, kind, pitch, value, port, channel) in enumerate(events):
        words[8 + 3 * i : 11 + 3 * i] = [
            token,
            generation,
            kind | port << 4 | channel << 6 | track << 10 | pitch << 14 | value << 21,
        ]
    a = b = 0
    for i, word in enumerate(words):
        if i != 7:
            a = (a + word) & 0xFFFFFFFF
            b = (b + a) & 0xFFFFFFFF
    words[7] = a ^ b
    return words


@pytest.mark.parametrize("canopy_object", [True], indirect=True)
def test_transport_packet_reaches_instance_and_preserves_other_note(canopy):
    result = canopy(packet=wire_packet([(1, 1, 60, 24, 0, 0), (2, 1, 64, 127, 1, 3)]))
    assert result["regs"]["R0"] == 0
    assert set(voices(result)) == {1, 2}
    assert voices(result)[1][6] < voices(result)[2][6]
    canopy(packet=wire_packet([(0, 3, 64, 127, 0, 0)]))
    result = canopy(packet=wire_packet([(1, 2, 60, 0, 0, 0)]))
    assert voices(result)[1][1] == 1
    result = canopy(packet=wire_packet([(0, 3, 64, 0, 0, 0)]))
    assert voices(result)[1][1] == 0
    assert voices(result)[2][1] == 1


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
    assert reset["state"][132:] == before["state"][132:]
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


@pytest.mark.parametrize("controls", [(1,), (3, 1, 127, 127, 0, 127)])
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
    assert max(measured[k] for k in ("create", "replace_reset", "unload")) < 1000
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
    assert reset["state"][132:] == released["state"][132:]
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
