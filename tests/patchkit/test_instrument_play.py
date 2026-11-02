"""Execute the admitted instance/transport/carrier boundary on the SHARC core."""

import json
import os
import subprocess
from pathlib import Path

import pytest
from support.paths import REPO_ROOT
from test_canopy_v2 import wire_packet

from tools.patchkit import patchkit, pk_sharc, pk_toolchains


@pytest.fixture(scope="module")
def playback_object():
    adapters = REPO_ROOT / "modules/core/adapters"
    canopy = REPO_ROOT / "modules/machines/canopy/src/dsp"
    source = "\n".join(
        (canopy / f"{n}.s").read_text()
        for n in ("voice", "tuning", "events", "blend", "arp", "strum", "v2")
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
.PKDATA _pattern_position_info, 80;
.PKDATA _pattern_clock_info, 32;
.PKDATA _ir_rows, 512;
.PKDATA _ir_process_rows, 256;
.PKDATA _ir_process_flags, 32;
.PKDATA _ir_lifecycle_ready, 4;
.PKDATA _mi_epoch, 4;
.PKDATA _instrument_session, 4;
.PKDATA _mi_carrier_stamp, 4;
.PKDATA _test_state, 768;
.PKDATA _test_packet, 128;
.PKDATA _test_metadata, 24;
.PKDATA _test_values, 64;
.PKDATA _test_output, 128;
.SECTION/PM seg_pmco;
.GLOBAL _test_status_process;
_test_status_process:
    I0 = R8;
    R0 = DM(22, I0);
    I1 = R0;
    R0 = DM(0, I0);
    DM(1, I1) = R0;
    R0 = DM(23, I0);
    DM(12, I1) = R0;
    R0 = 1;
    DM(0, I1) = R0;
    JUMP (M13, I13);
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
    symbols["_test_status_process"] = linked.symbols["_test_status_process"]
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
            192,
            state,
            state + 768,
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
        [2, 9, 20, linked.symbols["_canopy_process"], 0xB90000, 0xBC0000, 8, 16],
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

    def configure(generation=1, values=(1,), revision=1):
        put(
            "_test_metadata",
            [
                0,
                generation,
                revision,
                0,
                len(values) * 4,
                {1: 0x43505331, 6: 0x43505333, 10: 0x43505334, 16: 0x43505335}[
                    len(values)
                ],
            ],
        )
        put("_test_values", values)
        return call(2, R4=symbols["_test_values"], R8=symbols["_test_metadata"])

    def events(batch, generation=1):
        put("_test_packet", wire_packet(batch, generation=generation))
        return call(3, R4=symbols["_test_packet"])

    return call, put, get, reset, configure, events, symbols


def test_opted_in_output_is_borrowed_per_block_and_revoked_with_session(playback):
    call, put, get, reset, configure, events, symbols = playback
    assert reset() == 1
    call(0)
    assert configure() == 1
    # Installed capability and callback are owned by the same row identity.
    put("_ir_process_flags", [1])
    put(
        "_ir_process_rows",
        [2, 9, 20, symbols["_test_status_process"], 0xB90000, 0xBC0000, 8, 16],
    )
    call(0)
    assert get("_iv_input", 24)[:2] == [5, 96]
    assert get("_iv_input", 24)[22:] == [symbols["_instrument_ui_status"], 13]
    status = get("_instrument_ui_status", 13)
    assert status[:2] == [1, 5] and status[12] == 13
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    put("_instrument_session", [0])
    call(0)
    assert get("_instrument_ui_status") == [0]


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
    state = get("_test_state", 192)
    voices = {state[i]: state[i : i + 8] for i in range(0, 32, 8) if state[i]}
    assert set(voices) == {1, 2}
    assert voices[1][6] < voices[2][6]
    call(4, R2=0, R8=symbols["_test_output"])
    assert get("_test_state", 192) == state
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


def test_keyboard_coldfire_instruction_ceiling(tmp_path):
    """Bound actual CF work: full physical pool, eight notes, then idle."""
    runner = Path(
        os.environ.get(
            "DIGIKIT_CF_SCRIPT", REPO_ROOT / "target/release/examples/cf_script"
        )
    )
    firmware = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
    if not runner.exists() or not firmware.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("ColdFire runner, compiler and DTII 1.17 required")
    owner = REPO_ROOT / "modules/core/adapters/instrument-play/src/cf"
    probe = tmp_path / "keyboard.c"
    probe.write_text("""
#include "keyboard.h"
static KbState state;
static MiEvent input[8], output[8];
int prepare(void) {
    kb_init(&state); kb_reset(&state,1,1);
    for(unsigned i=0;i<120;++i) {
        state.keys[i].token=i+1; state.keys[i].note=60;
        state.keys[i].held=1; state.keys[i].velocity=90;
        state.keys[i].stolen=i<116;
    }
    for(unsigned i=0;i<8;++i) {
        input[i].epoch=1; input[i].generation=1; input[i].token=121+i;
        input[i].kind=MI_NOTE_ON; input[i].note=60+i; input[i].value=100;
    }
    state.update=1;
    return kb_process(&state,input,0,output);
}
int batch(void) { return kb_process(&state,input,8,output); }
int idle(void) { return kb_process(&state,input,0,output); }
""")
    linked = patchkit.cf_link([owner / "keyboard.c", probe], 0x47E00000, {}, [owner])
    script = tmp_path / "calls.json"
    script.write_text(
        json.dumps(
            [
                {
                    "op": "poke",
                    "addr": linked.base,
                    "hex": (linked.image + bytes(linked.bss_size)).hex(),
                },
                *[
                    {
                        "op": "call",
                        "tag": name,
                        "pc": linked.symbols[name],
                        "args": [],
                        "max": 100_000,
                    }
                    for name in ("prepare", "batch", "idle")
                ],
            ]
        )
    )
    result = subprocess.run(
        [str(runner), str(firmware), str(script)],
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    calls = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [row for row in calls if row.get("op") == "call"]
    assert [row["tag"] for row in calls] == ["prepare", "batch", "idle"]
    for row, expected in zip(calls, (4, 8, 0), strict=True):
        assert row["ok"] and not row["error"] and row["d0"] == expected, row
    assert calls[1]["insns"] < 100_000, calls
    assert calls[2]["insns"] < 100, calls
    print("keyboard CF instructions:", [(r["tag"], r["insns"]) for r in calls])


def test_pad_configuration_is_atomic_and_does_not_reset_a_held_note(playback):
    call, _, get, reset, configure, events, _ = playback
    reset()
    call(0)
    assert configure(values=(3, 0, 64, 0, 5, 15)) == 1
    call(0)
    assert events([(1, 1, 60, 127, 0, 0)]) == 1
    call(0)
    before = get("_test_state", 192)
    assert configure(values=(3, 1, 127, 127, 0, 127), revision=2) == 1
    assert get("_test_state", 192) == before
    call(0)
    after = get("_test_state", 192)
    assert [after[i] for i in range(0, 48, 8)] == [before[i] for i in range(0, 48, 8)]
    assert get("_iv_revision") == [2]
    assert configure(values=(3, 1, 128, 0, 5, 15), revision=3) == 0
    assert get("_iv_revision") == [2]
    assert get("_test_state", 192) == after
    call(0)
    assert get("_iv_result") == [0]


def test_local_arp_configuration_reaches_the_process_callback(playback):
    call, _, get, reset, configure, events, _ = playback
    reset()
    call(0)
    values = (4, 0, 64, 0, 0, 0, 127, 120, 4, 50)
    assert configure(values=values) == 1
    call(0)
    assert events([(1, 1, 60, 127, 0, 0), (2, 1, 64, 127, 0, 0)]) == 1
    call(0)
    assert get("_test_state", 192)[136] == 1
    assert get("_iv_controls", 10) == list(values)
    before = get("_test_state", 192)
    assert configure(values=values[:7] + (301, 4, 50), revision=2) == 0
    assert get("_test_state", 192) == before
    assert get("_iv_revision") == [1]


def test_extended_movement_configuration_is_applied_atomically(playback):
    call, _, get, reset, configure, events, _ = playback
    reset()
    call(0)
    values = (5, 0, 64, 0, 0, 0, 127, 120, 4, 50, 1, 3, 50, 0, 0, 0)
    assert configure(values=values) == 1
    call(0)
    assert events([(1, 1, 60, 127, 0, 0)]) == 1
    call(0)
    assert get("_test_state", 192)[151] == 84
    assert get("_iv_controls", 16) == list(values)
    assert configure(values=values[:12] + (76,) + values[13:], revision=2) == 0
    assert get("_iv_revision") == [1]
    fine = values[:13] + (2,) + values[14:]
    assert configure(values=fine, revision=2) == 1
    assert get("_iv_controls", 16) == list(fine)
    assert configure(values=values[:13] + (1,) + values[14:], revision=3) == 0
    assert configure(values=values[:13] + (3,) + values[14:], revision=3) == 0
    assert get("_iv_revision") == [2]


def test_position_snapshot_reaches_input_and_rejects_gaps(playback):
    call, put, get, reset, configure, _, _ = playback
    reset()
    call(0)
    assert configure() == 1
    position = [
        0x50504F53,
        2,
        30,
        9,
        7,
        3,
        0x43170000,
        4,
        16,
        0,
        100,
        0,
        100,
        5,
        1,
        0,
        101,
        0,
        30,
        1,
    ]
    put("_pattern_position_info", position)
    call(0)
    assert get("_iv_input", 18)[4] == 101
    assert get("_iv_input", 18)[12:17] == [3, 7, 9, 4, 16]
    put("_pattern_clock_info", [0x50434C4B, 1, 0xFFFFFF10, 0xFFFF0000, 3, 101, 9, 7])
    call(0)
    assert get("_iv_input", 22)[:2] == [4, 88]
    assert get("_iv_input", 22)[20:] == [0xFFFFFF10, 0xFFFF0000]
    put("_pattern_clock_info", [0x50434C4B, 1, 0xFFFFFF10, 0xFFFF0000, 0, 101, 9, 7])
    call(0)
    assert get("_iv_input", 22)[12] == 0  # GRID and PHASE both pause on expired clock
    put("_pattern_clock_info", [0x50434C4B, 99, 0xFFFFFF10, 0xFFFF0000, 3, 101, 9, 7])
    call(0)
    assert get("_iv_input", 22)[:2] == [3, 80]
    assert get("_iv_input", 22)[20:] == [0, 0]
    position[19] = 2
    put("_pattern_position_info", position)
    call(0)
    assert get("_iv_input", 18)[12:17] == [0] * 5
    position[19], position[1] = 1, 99
    put("_pattern_position_info", position)
    call(0)
    assert get("_iv_input", 18)[12:17] == [0] * 5
