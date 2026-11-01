"""Execute the admitted instance/transport/carrier boundary on the SHARC core."""

import pytest
from support.paths import REPO_ROOT
from test_canopy_v2 import wire_packet

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module")
def playback_object():
    adapters = REPO_ROOT / "modules/core/adapters"
    canopy = REPO_ROOT / "modules/machines/canopy/src/dsp"
    source = "\n".join(
        (canopy / f"{n}.s").read_text() for n in ("voice", "tuning", "events", "v2")
    )
    for module, names in (
        ("instrument-registry", ("process", "process-protected", "input", "lifecycle")),
        ("instrument-play", ("play", "reset-protected")),
        ("midi-events", ("packet", "carrier", "carrier-frame", "velocity")),
    ):
        source += "\n" + "\n".join(
            (adapters / module / "src/dsp" / f"{n}.s").read_text() for n in names
        )
    source += """
.PKDATA _ir_rows, 512;
.PKDATA _ir_process_rows, 256;
.PKDATA _ir_lifecycle_ready, 4;
.PKDATA _mi_epoch, 4;
.PKDATA _instrument_session, 4;
.PKDATA _mi_carrier_stamp, 4;
.PKDATA _test_state, 528;
.PKDATA _test_packet, 128;
.PKDATA _test_metadata, 24;
.PKDATA _test_values, 4;
.PKDATA _test_output, 128;
.SECTION/PM seg_pmco;
"""
    entries = [
        "_instrument_carrier_prepare",
        "_instrument_reset",
        "_instrument_configure",
        "_instrument_events",
        "_iv_voice_render",
    ]
    for i, entry in enumerate(entries):
        source += f"""
.GLOBAL _test_{i};
_test_{i}:
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    I13 = _test_return;
    {"JUMP" if entry == "_iv_voice_render" else "CALL"} {entry};
    RTS;
"""
    source += "_test_return:\n    RTS;\n"
    obj = pk_sharc.assemble(source)
    sections, initial, at = {}, {}, 0x20020000
    for name, (blob, align, _) in obj.data.items():
        at = (at + align - 1) // align * align
        sections[name] = at
        initial.update(
            {
                at // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        at += len(blob) + 64
    symbols = {n: sections[sec] + off for n, (sec, off, _) in obj.data_symbols.items()}
    linked = pk_sharc.link(obj, 0xB90000, symbols)
    reads = {
        n: (sections[n] // 4, len(blob) // 4) for n, (blob, _, _) in obj.data.items()
    }
    return linked, symbols, initial, reads


@pytest.fixture
def playback(playback_object):
    linked, symbols, initial, reads = playback_object
    memory = dict(initial)

    def put(name, words):
        memory.update({symbols[name] // 4 + i: value for i, value in enumerate(words)})

    def get(name, count=1):
        return [memory[symbols[name] // 4 + i] for i in range(count)]

    state = symbols["_test_state"]
    put(
        "_ir_rows",
        [
            3,
            2,
            9,
            1,
            state,
            132,
            state,
            state + 528,
            linked.symbols["_canopy_reset"],
            0xB90000,
            0xBC0000,
            0,
            0,
            0,
            0,
            0,
        ],
    )
    put(
        "_ir_process_rows",
        [2, 9, 20, linked.symbols["_canopy_process"], 0xB90000, 0xBC0000, 8, 1],
    )
    put("_instrument_session", [1])
    put("_mi_epoch", [1])

    def call(index, **regs):
        result = pk_sharc.run_call(
            linked,
            f"_test_{index}",
            regs={
                "M5": 0,
                "M6": 1,
                "M13": 0,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
                **regs,
            },
            dm_words=memory,
            reads=reads,
            max_steps=100000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        for name, (base, _) in reads.items():
            memory.update({base + i: value for i, value in enumerate(result[name])})
        return result["regs"]["R0"]

    def reset(generation=1, track=0):
        packet = [0] * 32
        packet[2], packet[8], packet[9] = 1, track, generation
        put("_test_packet", packet)
        return call(1, R4=symbols["_test_packet"])

    def configure(generation=1):
        put("_test_metadata", [0, generation, 1, 0, 4, 0x43505331])
        put("_test_values", [1])
        return call(2, R4=symbols["_test_values"], R8=symbols["_test_metadata"])

    def events(batch, generation=1):
        put("_test_packet", wire_packet(batch, generation=generation))
        return call(3, R4=symbols["_test_packet"])

    return call, put, get, reset, configure, events, symbols


def test_admitted_render_consumes_batch_once_and_raw_carrier_only_copies(playback):
    call, _, get, reset, configure, events, symbols = playback
    assert reset() == 1
    call(0)
    assert get("_instrument_voice_rows", 2)[0] == 20
    assert configure() == 1
    call(0)
    assert events([(1, 1, 60, 30, 0, 0), (2, 1, 64, 120, 0, 0)]) == 1
    assert events([(3, 1, 67, 100, 0, 0)]) == 0
    call(0)
    assert get("_iv_result") == [0]
    assert get("_iv_input", 18)[8] == 0
    state = get("_test_state", 132)
    voices = {state[i]: state[i : i + 8] for i in range(0, 32, 8) if state[i]}
    assert set(voices) == {1, 2}
    assert voices[1][6] < voices[2][6]
    call(4, R2=0, R8=symbols["_test_output"])
    assert get("_test_state", 132) == state
    assert get("_test_output", 32) == get("_iv_output", 32)
    call(0)
    assert get("_iv_result") == [0]  # replaying the ON would reject its duplicate token
    assert get("_instrument_voice_rows", 2)[0] == 20


def test_reset_retires_tail_before_new_configuration_and_acks_inactive_tracks(playback):
    call, _, get, reset, configure, events, _ = playback
    assert reset() == 1
    call(0)
    assert configure() == 1
    call(0)
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    call(0)
    assert reset(2) == 1
    assert configure(2) == 0
    call(0)
    call(0)
    call(0)
    assert get("_iv_active") == [0]
    assert configure(2) == 1
    assert reset(2, track=1) == 1
    assert get("_iv_revision") == [1]


def test_lost_row_identity_closes_events_and_silences_stale_output(playback):
    call, put, get, reset, configure, events, _ = playback
    reset()
    call(0)
    configure()
    call(0)
    events([(1, 1, 60, 100, 0, 0)])
    call(0)
    assert any(get("_iv_output", 32))
    put("_ir_process_rows", [2, 10])
    assert events([(2, 1, 67, 100, 0, 0)]) == 0
    call(0)
    assert get("_iv_output", 32) == [0] * 32
    assert get("_iv_revision") == [0]
