"""CANOPY normalized V2 callbacks executed on the SHARC instruction core."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module", params=[False, True], ids=["direct", "protected"])
def canopy_object(request):
    root = REPO_ROOT / "modules/machines/canopy/src/dsp"
    source = "\n".join(
        (root / f"{name}.s").read_text()
        for name in ["voice", "tuning", "instrument", "v2"]
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
    for base, size in [(0x20050000, 132), (0x20054000, 132), (0x20013000, 32)]:
        dm.update({base // 4 + i: 0 for i in range(size)})

    def call(
        events=(),
        state=0x20050000,
        track=0,
        generation=1,
        reset=None,
        edits=None,
        packet=None,
    ):
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
            1,
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
        dm[0x20012000 // 4] = 1
        for i, event in enumerate(events):
            dm.update(
                {0x20012100 // 4 + i * 6 + j: value for j, value in enumerate(event)}
            )
        reads = {
            name: (sections[name] // 4, len(blob) // 4)
            for name, (blob, _, _) in obj.data.items()
        }
        reads.update({"state": (state // 4, 132), "out": (0x20013000 // 4, 32)})
        if protected and reset is None:
            row = [3, 2, 9, 1, state, 132, state, state + 528, 0, 0, 0, 0, 0, 0, 0, 0]
            meta = [
                2,
                9,
                20,
                linked.symbols["_canopy_process"],
                0xB90000,
                0xBC0000,
                8,
                1,
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
    assert canopy(reset=1)["state"] == [0] * 132
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
    assert canopy(reset=3)["state"] == [0] * 132
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
