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
.GLOBAL _test_many;
_test_many:
    DM(_test_loop_left) = R10;
    DM(_test_loop_state) = R4;
    DM(_test_loop_input) = R8;
_test_loop:
    R4 = DM(_test_loop_state);
    R8 = DM(_test_loop_input);
    CALL _test_process;
    R0 = PASS R0;
    IF LT RTS;
    R1 = DM(_test_loop_input);
    I0 = R1;
    R1 = 0;
    DM(8, I0) = R1;
    R1 = DM(_test_loop_left);
    R1 = R1 - 1;
    DM(_test_loop_left) = R1;
    IF NE JUMP _test_loop;
    RTS;
.PKBSS _test_loop_left, 4;
.PKBSS _test_loop_state, 4;
.PKBSS _test_loop_input, 4;
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
    for base, size in [(0x20050000, 200), (0x20054000, 200), (0x20013000, 32)]:
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
        reads.update({"state": (state // 4, 200), "out": (0x20013000 // 4, 32)})
        reads["status"] = (0x20015000 // 4, 13)
        reads["status_before"] = (0x20015000 // 4 - 1, 1)
        reads["status_after"] = (0x20015000 // 4 + 13, 1)
        reads.update(
            {"guard_before": (state // 4 - 1, 1), "guard_after": (state // 4 + 200, 1)}
        )
        if protected and reset is None:
            dm[symbols["_ir_process_flags"] // 4] = 1 if words[0] == 5 else 0
            row = [3, 2, 9, 1, state, 200, state, state + 800, 0, 0, 0, 0, 0, 0, 0, 0]
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
        result = pk_sharc.run_call(
            linked,
            "_test_packet"
            if packet is not None
            else (
                ("_test_many" if blocks > 1 else "_test_process")
                if reset is None
                else "_test_reset"
            ),
            regs={
                "R4": 0 if protected and reset is None else state,
                "R5": 0,
                "R9": 1,
                "R10": blocks,
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


def test_v2_chord_independent_release_and_state_isolation(canopy):
    assert canopy(reset=1)["state"] == [0] * 200
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
    assert canopy(reset=3)["state"] == [0] * 200
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
    assert r["state"][146:] == [0] * 54
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


def test_local_arp_live_bend_preserves_identity(canopy):
    a = canopy([note(1, 60)], controls=LOCAL)
    b = canopy([[4, 0, 0, 0, 16383, 0]], controls=LOCAL)
    assert a["state"][136] == b["state"][136] == 1
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
    assert new["out"] == old["out"] and new["state"] == old["state"]


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


def test_native_tune_reaches_running_arp_and_survives_legacy_controls(canopy):
    base = list(STRUM)
    base[6] = 127
    first = canopy([note(1, 60)], controls=native_controls(base=base), blocks=3)
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


def test_native_tune_busy_callback_stays_within_declared_instruction_bound(canopy):
    base = (5, 1, 127, 127, 0, 127, 127, 300, 8, 100, 4, 3, 75, 0, 0, 750)
    controls = native_controls(0x4080, base)
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
    changed = native_controls(0x4C80, base)
    measured.append(
        canopy(
            [[4, 0, 0, 0, 16383 - i, 0] for i in range(8)],
            controls=changed,
            edits={11: 2},
        )["instructions"]
    )
    assert max(measured) < 100000
