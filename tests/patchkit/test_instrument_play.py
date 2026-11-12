"""Execute the admitted instance/transport/carrier boundary on the SHARC core."""

import json
import os
import struct
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
        for n in (
            "voice",
            "tuning",
            "events",
            "blend",
            "arp",
            "strum",
            "status",
            "v2",
            "wavetable",
            "wavetables",
            "fm",
            "fm-tables",
        )
    )
    for module, names in (
        ("instrument-registry", ("process", "process-protected", "input", "lifecycle")),
        ("instrument-play", ("play", "modulation", "reset-protected")),
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
.PKDATA _mi_envelope_records, 4;
.PKDATA _iv_amp_attack_table, 512;
.PKDATA _iv_amp_decay_table, 512;
.PKDATA _iv_amp_hold_table, 512;
.PKDATA _test_amp_record, 44;
.PKDATA _iv_mod_frame, 2048; // sixteen 0x60-byte parameter rows follow byte 0xda
.PKDATA _test_state, 11904; // four 2976-byte instances
.PKDATA _test_packet, 128;
.PKDATA _test_metadata, 24;
.PKDATA _test_values, 128;
.PKDATA _test_output, 128;
.PKDATA _test_post_gains, 128;
.PKDATA _test_post_samples, 128;
.PKDATA _test_post_count, 4;
.PKDATA _test_post_result, 4;
.SECTION/PM seg_pmco;
.GLOBAL _test_post_process;
_test_post_process:
    I0 = R8;
    R0 = DM(32, I0);
    I1 = R0;
    I2 = _test_post_gains;
    R2 = DM(_test_post_count);
    R2 = PASS R2;
    IF EQ JUMP _test_post_pcm;
_test_post_gain:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _test_post_gain;
_test_post_pcm:
    R0 = DM(5, I0);
    I1 = R0;
    I2 = _test_post_samples;
    R2 = 32;
_test_post_copy:
    R0 = DM(I2, M6);
    DM(I1, M6) = R0;
    R2 = R2 - 1;
    IF NE JUMP _test_post_copy;
    R0 = DM(_test_post_result);
    JUMP (M13, I13);
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
        "_cu_publish",
        "_iv_modulate",
        "_iv_reset_row",
        "_iv_mod_status",
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
    symbols["_test_post_process"] = linked.symbols["_test_post_process"]
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
            744,
            state,
            state + 2976,
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
        [2, 9, 20, linked.symbols["_canopy_process"], 0xB90000, 0xBC0000, 8, 32],
    )
    put("_instrument_session", [1])
    put("_mi_epoch", [1])
    frame = [0] * 256
    frame[37] = 20
    for halfword in (0x6E, 0x6F, 0x70, 0x73, 0x74, 0x75):
        frame[halfword // 2] |= 0x4000 << (16 * (halfword & 1))
    put("_iv_mod_frame", frame)
    # Direct overlay units start with an established owner. Production resets
    # below clear this flag and must warm the previous-frame copy themselves.
    put("_iv_mod_ready", [1])

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
            return_sw=0xBCF100,
            read_regs=tuple(
                f"{bank}{i}" for bank in ("I", "L", "B") for i in range(3, 8)
            )
            + ("M5", "M6", "MODE1"),
        )
        assert result["halt"]["reason"] == "breakpoint", result
        for name, (base, _) in reads.items():
            memory.update({base + i: value for i, value in enumerate(result[name])})
        call.last_regs = result["regs"]
        call.last_instructions = result["instructions"]
        return result["regs"]["R0"]

    def reset(generation=1, track=0, slot=None, epoch=1):
        packet = [0] * 32
        packet[2], packet[8], packet[9] = epoch, track, generation
        if slot is not None:
            packet[10] = slot + 1  # the CF's explicit instance slot hint
        put("_test_packet", packet)
        return call(1, R4=symbols["_test_packet"])

    def configure(generation=1, values=(1,), revision=1, schema=None, track=0, **regs):
        put(
            "_test_metadata",
            [
                track,
                generation,
                revision,
                0,
                len(values) * 4,
                schema
                if schema is not None
                else {
                    1: 0x43505331,
                    6: 0x43505333,
                    10: 0x43505334,
                    16: 0x43505335,
                    32: {
                        6: 0x43505336,
                        7: 0x43505337,
                        8: 0x43505338,
                        9: 0x43505339,
                        10: 0x43505341,
                        11: 0x43505342,
                    }.get(values[0] & 255, 0),
                }[len(values)],
            ],
        )
        put("_test_values", values)
        return call(2, R4=symbols["_test_values"], R8=symbols["_test_metadata"], **regs)

    def events(batch, generation=1, revision=1, track=0):
        put(
            "_test_packet",
            wire_packet(batch, generation=generation, revision=revision, track=track),
        )
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
        [2, 9, 20, symbols["_test_status_process"], 0xB90000, 0xBC0000, 8, 32],
    )
    call(0)
    assert get("_iv_input", 24)[:2] == [5, 96]
    assert get("_iv_input", 24)[22:] == [symbols["_iv_ui_sink"], 13]
    status = get("_instrument_ui_status", 13)
    assert status[:2] == [1, 5] and status[12] == 13
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    put("_instrument_session", [0])
    call(0)
    assert get("_instrument_ui_status") == [0]


def _native_amp_fixture(playback):
    call, put, get, reset, configure, _, symbols = playback
    assert reset() == 1
    call(0)
    assert configure() == 1
    put("_ir_process_flags", [3])
    put(
        "_ir_process_rows",
        [2, 9, 20, symbols["_test_status_process"], 0xB90000, 0xBC0000, 8, 32],
    )

    def bits(value):
        return int.from_bytes(struct.pack("<f", value), "little")

    put("_iv_amp_attack_table", [bits(-0.125)] * 128)
    put("_iv_amp_decay_table", [bits(-0.75)] * 127 + [bits(-1.0)])
    put("_iv_amp_hold_table", [29] * 128)
    put("_mi_envelope_records", [symbols["_test_amp_record"]])
    record = [3, 7, 11, bits(0.25), 127, 9, 2, 4, 17, 31, bits(-0.5)]
    put("_test_amp_record", record)
    put("_mi_carrier_stamp", [41])
    return call, put, get, bits, record


def test_native_amp_snapshot_uses_stock_configuration_without_mutating_it(playback):
    call, put, get, bits, record = _native_amp_fixture(playback)
    call(0)
    assert get("_iv_input", 32)[:2] == [6, 128]
    assert get("_iv_input", 32)[24:] == [
        2,
        bits(0.125),
        29,
        bits(0.75),
        bits(0.25),
        bits(1.0),
        1,
        0,
    ]
    assert get("_test_amp_record", 11) == record
    assert get("_mi_native_owner") == [41], {
        name: get(name, count)
        for name, count in (
            ("_iv_native_valid", 1),
            ("_iv_result", 1),
            ("_ir_rows", 16),
            ("_ir_process_flags", 1),
            ("_instrument_ui_status", 13),
        )
    }
    assert get("_mi_native_owner") == [41]
    record[1], record[3], record[4], record[5], record[6] = 127, 0, 11, 0, 1
    put("_test_amp_record", record)
    call(0)
    assert get("_iv_input", 32)[24:] == [
        1,
        bits(0.125),
        0xFFFFFFFF,
        bits(0.75),
        0,
        bits(0.75),
        0,
        0,
    ]
    assert get("_test_amp_record", 11) == record
    put("_ir_process_flags", [1])
    call(0)
    assert get("_iv_input", 2) == [5, 96]
    assert get("_mi_native_owner") == [0]


@pytest.mark.parametrize(
    "field,value",
    [
        (0, 128),
        (1, 0xFFFFFFFF),
        (2, 128),
        (3, 0x7FC00000),
        (3, 0xBF800000),
        (3, 0x40000000),
        (4, 128),
        (6, 0),
        (6, 3),
    ],
)
def test_invalid_native_amp_snapshot_cannot_enable_shared_bypass(
    playback, field, value
):
    call, put, get, _, record = _native_amp_fixture(playback)
    call(0)
    assert get("_mi_native_owner") == [41]
    record[field] = value
    put("_test_amp_record", record)
    call(0)
    assert get("_iv_input", 32)[24] == 0
    assert get("_mi_native_owner") == [0]


def test_missing_native_snapshot_and_lost_session_revoke_shared_bypass(playback):
    call, put, get, _, _ = _native_amp_fixture(playback)
    call(0)
    assert get("_mi_native_owner") == [41]
    put("_mi_envelope_records", [0])
    call(0)
    assert get("_iv_input", 32)[24] == 0
    assert get("_mi_native_owner") == [0]
    put("_mi_native_owner", [41])
    put("_instrument_session", [0])
    call(0)
    assert get("_mi_native_owner") == [0]


def _post_amp_fixture(playback):
    call, put, get, bits, record = _native_amp_fixture(playback)
    symbols = playback[-1]
    put("_ir_process_flags", [7])
    put(
        "_ir_process_rows",
        [2, 9, 20, symbols["_test_post_process"], 0xB90000, 0xBC0000, 8, 32],
    )
    put("_test_post_gains", [bits(0.25)] * 32)
    put("_test_post_samples", [bits(-0.125)] * 32)
    put("_test_post_count", [32])
    put("_test_post_result", [0])
    return call, put, get, bits, record


@pytest.mark.parametrize("result", [0, 1])
@pytest.mark.parametrize("gain", [1.0, 0.25, 0.1, 1e-20, 1e-38, 1e-44])
def test_post_amp_negotiates_and_normalizes_completed_blocks(playback, result, gain):
    call, put, get, bits, record = _post_amp_fixture(playback)
    symbols = playback[-1]
    gains = [0.0, -0.0, gain, gain] * 8
    put("_test_post_gains", [bits(g) for g in gains])
    # Quantize the divisor first, so even the smallest subnormal ratio is exact.
    pcm = [0 if not g else bits(g) ^ 0x80000000 for g in gains]
    put("_test_post_samples", pcm)
    put("_test_post_result", [result])
    call(0)
    packet = get("_iv_input", 34)
    assert packet[:2] == [7, 136]
    assert packet[32:] == [symbols["_iv_post_slots"], 32]
    assert get("_iv_post_slots", 32) == [bits(g) for g in gains]
    output = [
        struct.unpack("<f", x.to_bytes(4, "little"))[0] for x in get("_iv_output", 32)
    ]
    assert output == pytest.approx([0 if not g else -1 for g in gains], abs=2e-7)
    assert get("_mi_post_ptr") == [playback[-1]["_iv_post_slots"]]
    assert get("_mi_native_owner") == [41]
    assert get("_mi_native_owner") == [41]
    assert get("_test_amp_record", 11) == record


@pytest.mark.parametrize("sample", [-8.0, 8.0])
def test_post_amp_accepts_existing_normalized_output_bound(playback, sample):
    call, put, get, bits, _ = _post_amp_fixture(playback)
    put("_test_post_gains", [bits(0.1)] * 32)
    put("_test_post_samples", [bits(sample * 0.1)] * 32)
    call(0)
    assert get("_mi_post_ptr") == [playback[-1]["_iv_post_slots"]]
    assert get("_iv_output", 32) == [bits(sample)] * 32


def test_post_amp_normalizes_subnormal_pcm_with_normal_gain(playback):
    call, put, get, bits, _ = _post_amp_fixture(playback)
    put("_test_post_gains", [0x00800000] * 32)
    put("_test_post_samples", [0x80400000] * 32)
    call(0)
    assert get("_mi_post_ptr") == [playback[-1]["_iv_post_slots"]]
    assert get("_iv_output", 32) == [bits(-0.5)] * 32


@pytest.mark.parametrize(
    "kind",
    [
        "partial",
        "missing",
        "nan",
        "inf",
        "negative",
        "above_one",
        "pcm_nan",
        "quotient",
        "failed",
    ],
)
def test_post_amp_rejects_bad_or_incomplete_outputs_and_revokes_owner(playback, kind):
    call, put, get, bits, _ = _post_amp_fixture(playback)
    call(0)
    assert get("_mi_post_ptr") == [playback[-1]["_iv_post_slots"]]
    if kind in ("partial", "missing"):
        put("_test_post_count", [31 if kind == "partial" else 0])
    elif kind == "failed":
        put("_test_post_result", [0xFFFFFFFF])
    elif kind in ("pcm_nan", "quotient"):
        put("_test_post_samples", [0x7FC00000 if kind == "pcm_nan" else bits(3.0)] * 32)
    else:
        bad = {
            "nan": 0x7FC00000,
            "inf": 0x7F800000,
            "negative": bits(-0.1),
            "above_one": bits(1.01),
        }[kind]
        put("_test_post_gains", [bits(0.25)] * 31 + [bad])
    call(0)
    assert get("_mi_post_ptr") == [0]
    assert get("_mi_native_owner") == [0]
    assert get("_iv_output", 32) == [0] * 32
    assert get("_ir_rows")[0] == 5


@pytest.mark.parametrize("kind", ["snapshot", "session", "reset", "v6"])
def test_post_amp_ownership_expires_without_valid_native_completion(playback, kind):
    call, put, get, bits, _ = _post_amp_fixture(playback)
    call(0)
    assert get("_mi_post_ptr") == [playback[-1]["_iv_post_slots"]]
    if kind == "snapshot":
        put("_mi_envelope_records", [0])
        put("_test_post_gains", [bits(1.0)] * 32)
    elif kind == "session":
        put("_instrument_session", [0])
    elif kind == "reset":
        assert playback[3]() == 1
        assert get("_mi_post_ptr") == [0]
        return
    else:
        put("_ir_process_flags", [3])
        symbols = playback[-1]
        put(
            "_ir_process_rows",
            [2, 9, 20, symbols["_test_status_process"], 0xB90000, 0xBC0000, 8, 32],
        )
    call(0)
    assert get("_mi_post_ptr") == [0]
    if kind == "snapshot":
        assert get("_iv_output", 32) == [bits(-0.125)] * 32


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
    state = get("_test_state", 200)
    voices = {state[i]: state[i : i + 8] for i in range(0, 32, 8) if state[i]}
    assert set(voices) == {1, 2}
    assert voices[1][6] < voices[2][6]
    call(4, R2=0, R8=symbols["_test_output"])
    assert get("_test_state", 200) == state
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
    before = get("_test_state", 200)
    assert configure(values=(3, 1, 127, 127, 0, 127), revision=2) == 1
    assert get("_test_state", 200) == before
    call(0)
    after = get("_test_state", 200)
    assert [after[i] for i in range(0, 48, 8)] == [before[i] for i in range(0, 48, 8)]
    assert get("_iv_revision") == [2]
    assert configure(values=(3, 1, 128, 0, 5, 15), revision=3) == 0
    assert get("_iv_revision") == [2]
    assert get("_test_state", 200) == after
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
    assert get("_test_state", 200)[136] == 1
    assert get("_iv_controls", 10) == list(values)
    before = get("_test_state", 200)
    assert configure(values=values[:7] + (301, 4, 50), revision=2) == 0
    assert get("_test_state", 200) == before
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
    assert get("_test_state", 200)[151] == 84
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


def test_v5_timing_metadata_distinguishes_stop_loss_and_missing_provider(playback):
    call, put, get, reset, configure, _, _ = playback
    reset()
    call(0)
    assert configure() == 1
    put("_ir_process_flags", [1])
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
    assert get("_iv_input", 24)[12] == 0x103
    put("_pattern_clock_info", [0x50434C4B, 1, 10, 5, 3, 101, 9, 7])
    call(0)
    assert get("_iv_input", 24)[12] == 0x303
    put("_pattern_clock_info", [0x50434C4B, 1, 10, 5, 0, 101, 9, 7])
    call(0)
    assert get("_iv_input", 24)[12] == 0x300  # Lost sync, transport still running.
    position[5], position[14] = 0, 0
    put("_pattern_position_info", position)
    call(0)
    assert get("_iv_input", 24)[12] == 0x700  # Explicit STOP, providers still exist.
    position[19] = 2
    put("_pattern_position_info", position)
    call(0)
    assert get("_iv_input", 24)[12] == 0  # Missing frame, not an explicit STOP.


@pytest.mark.parametrize("version", [6, 7, 8])
def test_configuration_copies_one_complete_presentation_revision(playback, version):
    from test_canopy_v2 import presentation_controls

    call, _, get, reset, configure, _, _ = playback
    reset()
    call(0)
    controls = presentation_controls(mode=1)
    if version >= 7:
        controls[0] = version | (1 << 8) | (0x4C80 << 16)
    assert configure(values=controls, revision=7) == 1
    call(0)
    assert get("_iv_controls", 32) == controls
    assert get("_iv_input", 12)[10:] == [32, 7]
    assert get("_iv_result")[0] in (0, 1)
    bad = controls.copy()
    bad[18] = 0
    assert configure(values=bad, revision=8) == 0
    assert get("_iv_controls", 32) == controls and get("_iv_revision") == [7]


def test_configuration_exceeding_installed_capacity_preserves_previous_bank(playback):
    from test_canopy_v2 import presentation_controls

    call, put, get, reset, configure, _, _ = playback
    reset()
    call(0)
    controls = presentation_controls()
    old = [5, *controls[1:16]]
    assert configure(values=old, revision=7) == 1
    row = get("_ir_process_rows", 8)
    row[7] = 16
    put("_ir_process_rows", row)
    assert configure(values=controls, revision=8) == 0
    assert get("_iv_control_count") == [16]
    assert get("_iv_revision") == [7]
    assert get("_iv_controls", 16) == old
    call(0)
    assert get("_iv_result")[0] in (0, 1)


def wavetable_bank(*, family=2, mode=0, tune=0x4000, clock=0, rate=10):
    from test_canopy_v2 import presentation_controls

    values = presentation_controls(mode=mode)
    values[0] = 9 | (mode << 8) | (tune << 16)
    values[1] = family
    extra = [0, 0, 0, rate, 0, 0, clock, 100, 5, 15, 100, 100]
    for index, value in enumerate(extra, 16):
        values[index] |= value << 16
    return values


def fm_bank():
    values = wavetable_bank(family=3, mode=2, tune=0x4C80)
    values[0] = (values[0] & ~255) | 10
    extra = [0, 50, 3, 8, 0, 5, 40, 50, 15, 100, 100, 100]
    for index, value in enumerate(extra, 16):
        values[index] = (values[index] & 65535) | (value << 16)
    return values


def wavetable_fm_bank(family=2):
    values = fm_bank() if family == 3 else wavetable_bank(family=family)
    values[0] = (values[0] & ~255) | 11
    for index, value in enumerate([60, 8, 150, 175], 28):
        values[index] = (values[index] & 65535) | (value << 16)
    return values


@pytest.mark.parametrize("family", [0, 1, 2, 3])
def test_cpsb_publishes_wavetable_fm_with_each_source_family(playback, family):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = wavetable_fm_bank(family)
    assert configure(values=values, revision=20) == 1
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [20]
    call(0)
    assert get("_iv_result")[0] in (0, 1)


@pytest.mark.parametrize("index,value", [(28, 101), (29, 16), (30, 201), (31, 201)])
def test_cpsb_rejects_invalid_extension_without_partial_publication(
    playback, index, value
):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = wavetable_fm_bank()
    assert configure(values=values, revision=21) == 1
    bad = values.copy()
    bad[index] = (bad[index] & 65535) | (value << 16)
    assert configure(values=bad, revision=22) == 0
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [21]


@pytest.mark.parametrize("version,schema", [(9, 0x43505339), (10, 0x43505341)])
def test_cpsb_extension_cannot_be_smuggled_through_old_schema(
    playback, version, schema
):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    old = wavetable_bank()
    assert configure(values=old, revision=23) == 1
    bad = wavetable_fm_bank()
    assert configure(values=bad, schema=schema, revision=24) == 0
    bad[0] = (bad[0] & ~255) | version
    assert configure(values=bad, schema=schema, revision=24) == 0
    assert get("_iv_controls", 32) == old
    assert get("_iv_revision") == [23]


@pytest.mark.parametrize("family", [0, 1, 2, 3])
def test_cpsa_publishes_complete_source_specific_bank(playback, family):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = fm_bank() if family == 3 else wavetable_bank(family=family)
    values[0] = (values[0] & ~255) | 10
    assert configure(values=values, revision=19) == 1
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [19]
    assert get("_iv_control_count") == [32]


@pytest.mark.parametrize(
    "index,value",
    [
        (1, 4),
        (2, 128),
        (16, 4 << 16),
        (17, 101 << 16),
        (18, 16 << 16),
        (19, 16 << 16),
        (20, 101 << 16),
        (21, 128 << 16),
        (22, 128 << 16),
        (23, 101 << 16),
        (24, 128 << 16),
        (25, 201 << 16),
        (26, 201 << 16),
        (27, 201 << 16),
        (28, 1 << 16),
        (29, 1 << 16),
        (30, 1 << 16),
        (31, 1 << 16),
    ],
)
def test_invalid_fm_revision_never_partially_publishes(playback, index, value):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    old = wavetable_bank()
    assert configure(values=old, revision=41) == 1
    bad = fm_bank()
    bad[index] = value if index < 16 else (bad[index] & 65535) | value
    assert configure(values=bad, revision=42) == 0
    assert get("_iv_controls", 32) == old
    assert get("_iv_revision") == [41]
    assert get("_iv_control_count") == [32]


@pytest.mark.parametrize("index", [18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 30, 31])
def test_fm_low_half_duplicates_remain_atomic(playback, index):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = fm_bank()
    assert configure(values=values, revision=7) == 1
    bad = values.copy()
    bad[index] ^= 1
    assert configure(values=bad, revision=8) == 0
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [7]


def test_fm_full_ranges_and_schema_identity_are_enforced(playback):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = fm_bank()
    maxima = [3, 100, 15, 15, 100, 127, 127, 100, 127, 200, 200, 200]
    for extensions in ([0] * 12, maxima):
        for index, value in enumerate(extensions, 16):
            values[index] = (values[index] & 65535) | (value << 16)
        assert configure(values=values, revision=7) == 1
        assert get("_iv_controls", 32) == values
    # Older schemas must never reinterpret an FM bank, even if its upper
    # fields happen to lie within the wavetable ranges.
    assert configure(values=values, schema=0x43505339, revision=8) == 0
    bad = wavetable_bank(family=3)
    assert configure(values=bad, revision=8) == 0
    bad = wavetable_bank()
    assert configure(values=bad, schema=0x43505341, revision=8) == 0
    # A CPSA legacy-family bank retains the wavetable extension semantics.
    bad = wavetable_bank(rate=0)
    bad[0] = (bad[0] & ~255) | 10
    assert configure(values=bad, revision=8) == 0
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [7]


@pytest.mark.parametrize("bytes_count", [0, 124, 132])
def test_partial_fm_transport_bank_cannot_publish(playback, bytes_count):
    call, put, get, reset, configure, _, symbols = playback
    assert reset() == 1
    call(0)
    old = fm_bank()
    assert configure(values=old, revision=7) == 1
    put("_test_values", wavetable_bank())
    put("_test_metadata", [0, 1, 8, 0, bytes_count, 0x43505341])
    assert call(2, R4=symbols["_test_values"], R8=symbols["_test_metadata"]) == 0
    assert get("_iv_controls", 32) == old
    assert get("_iv_revision") == [7]
    assert get("_iv_control_count") == [32]


def test_fm_source_switch_reaches_live_callback_without_replacing_note_owner(playback):
    call, _, get, reset, configure, events, _ = playback
    assert reset() == 1
    call(0)
    values = wavetable_bank(mode=2, tune=0x4C80)
    assert configure(values=values, revision=7) == 1
    call(0)
    assert events([(1, 1, 60, 100, 0, 0)], revision=7) == 1
    call(0)
    before = get("_test_state", 744)
    tokens = [before[i] for i in range(0, 32, 8)]
    assert 1 in tokens, (get("_iv_result"), before[:48], before[180:200])
    assert configure(values=fm_bank(), revision=8) == 1
    assert get("_test_state", 744) == before
    # Cross the bounded old-source fade and the new-source fade-in.
    transition = []
    for _ in range(8):
        call(0)
        assert get("_iv_result") == [0]
        transition.append((get("_test_state", 744)[712:716], get("_fm_fade_step")))
    assert get("_iv_input", 12)[10:] == [32, 8]
    assert [get("_test_state", 32)[i] for i in range(0, 32, 8)] == tokens
    assert get("_test_state", 744)[712:716] == [3, 0x3F800000, 0, 1], transition
    assert any(get("_iv_output", 32))
    assert configure(values=values, revision=9) == 1
    for _ in range(8):
        call(0)
        assert get("_iv_result") == [0]
    assert get("_test_state", 744)[712:716] == [2, 0x3F800000, 0, 1]
    assert [get("_test_state", 32)[i] for i in range(0, 32, 8)] == tokens
    assert events([(1, 2, 60, 0, 0, 0)], revision=9) == 1
    call(0)
    assert get("_iv_result")[0] in (0, 1)


@pytest.mark.parametrize("family", [0, 1, 2])
def test_wavetable_configuration_preserves_all_32_packed_words(playback, family):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = wavetable_bank(family=family, mode=2, tune=0x4C80, clock=1, rate=7)
    values[16] |= 4 << 16
    values[17] |= 700 << 16
    values[18] |= 100 << 16
    assert configure(values=values, revision=19) == 1
    assert get("_iv_controls", 32) == values
    assert get("_iv_revision") == [19]
    assert get("_iv_control_count") == [32]


@pytest.mark.parametrize(
    "index,value",
    [
        (0, 9 | (3 << 8) | (0x4000 << 16)),
        (0, 9 | (1 << 10) | (0x4000 << 16)),
        (0, 9 | (0x3FF << 16)),
        (0, 9 | (0x7C01 << 16)),
        (1, 3),
        (7, 29),
        (7, 301),
        (16, 5 << 16),
        (16, 12),
        (17, 701 << 16),
        (18, 101 << 16),
        (19, 1001 << 16),
        (19, 0),
        (20, 3 << 16),
        (21, 3 << 16),
        (22, 2 << 16),
        (23, 201 << 16),
        (24, 128 << 16),
        (25, 128 << 16),
        (26, 201 << 16),
        (27, 201 << 16),
        (28, 1 << 16),
        (29, 1 << 16),
        (30, 1 << 16),
        (31, 1 << 16),
    ],
)
def test_invalid_wavetable_revision_never_replaces_the_applied_bank(
    playback, index, value
):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    old = wavetable_bank()
    assert configure(values=old, revision=41) == 1
    bad = old.copy()
    bad[index] = value
    assert configure(values=bad, revision=42) == 0
    assert get("_iv_controls", 32) == old
    assert get("_iv_revision") == [41]


def test_wavetable_clock_and_duplicate_validation_is_atomic(playback):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    old = wavetable_bank()
    assert configure(values=old, revision=6) == 1
    bad_values = [wavetable_bank(clock=1, rate=8)]
    for index in [18, 19, 21, 22, 23, 24, 25, 26, 27, 30, 31]:
        bad = old.copy()
        bad[index] ^= 1
        bad_values.append(bad)
    bad = old.copy()
    bad[20] |= 1  # Strum presentation no longer matches its audio duration.
    bad_values.append(bad)
    bad = old.copy()
    bad[12], bad[13], bad[26] = 1, 1, (100 << 16) | 1
    bad_values.append(bad)  # Master grid requires zero arp swing.
    for bad in bad_values:
        assert configure(values=bad, revision=7) == 0
        assert get("_iv_controls", 32) == old
        assert get("_iv_revision") == [6]


@pytest.mark.parametrize("invalid", [None, 1, 16, 19, 31])
@pytest.mark.parametrize("circular", ["linear", "all", "duplicate"])
@pytest.mark.parametrize("fm", [False, True])
def test_wavetable_config_preserves_midi_consumer_address_registers(
    playback, invalid, circular, fm
):
    call, _, get, reset, configure, _, _ = playback
    assert reset() == 1
    call(0)
    values = fm_bank() if fm else wavetable_bank()
    if invalid is not None:
        values[invalid] = 0xFFFFFFFF
    # Stock firmware can enter with circular DAGs outside our lookup tables.
    # All seven validation-table pointers must advance linearly; saved index,
    # length and base registers must survive both accepted and rejected banks.
    preserved = {f"I{i}": 0x20100000 + 4 * i for i in range(3, 8)}
    preserved.update({f"B{i}": 0x21000000 + 256 * i for i in range(3, 8)})
    for i in range(3, 8):
        active = circular == "all" or (circular == "duplicate" and i == 6)
        preserved[f"L{i}"] = 16 + i if active else 0
    preserved.update(M5=0, M6=1, MODE1=0x1011800)  # CBUFEN remains enabled.
    assert configure(values=values, **preserved) == int(invalid is None)
    assert {name: call.last_regs[name] for name in preserved} == preserved


@pytest.mark.parametrize("reject_duplicate", [False, True])
def test_wavetable_config_never_borrows_the_interrupt_stack(
    playback, playback_object, monkeypatch, reject_duplicate
):
    from tools.sharc_dev import sharc_run

    call, _, get, reset, configure, _, _ = playback
    linked, _, _, _ = playback_object
    assert reset() == 1
    call(0)
    old = wavetable_bank()
    assert configure(values=old, revision=41) == 1
    values = old.copy()
    values[17] |= 250 << 16
    if reject_duplicate:
        values[19] ^= 1  # Valid range, but disagrees with audio control word 3.

    # Final restoration is insufficient: a real interrupt writes below the
    # current I7. Observe the executed code at every instruction boundary.
    stack = {"I7": 0x20100200, "B7": 0x20100000, "L7": 256}
    codes = {name: sharc_run.st.UREG_CODES[name] for name in stack}
    step = sharc_run.Runner.step
    visited = set()

    def check_stack(runner):
        pc = runner.state.pc_sw
        before = {name: runner.state.uregs[code] for name, code in codes.items()}
        step(runner)
        visited.add(pc)
        for name, code in codes.items():
            assert runner.state.uregs[code] is before[name], (hex(pc), name)
            assert runner.state.uregs[code].value == stack[name], (hex(pc), name)

    monkeypatch.setattr(sharc_run.Runner, "step", check_stack)
    assert configure(values=values, revision=42, **stack) == int(not reject_duplicate)
    assert linked.symbols["_iv_wt_presentation"] in visited
    assert get("_iv_controls", 32) == (old if reject_duplicate else values)
    assert get("_iv_revision") == [41 if reject_duplicate else 42]


@pytest.mark.parametrize("schema", [8, 9, 10])
def test_applied_status_unpacks_wavetable_values_without_losing_legacy_tune(
    playback, schema
):
    call, put, get, _, _, _, symbols = playback
    values = fm_bank() if schema == 10 else wavetable_bank(mode=1, tune=0x4C80)
    if schema != 10:
        values[17] |= 327 << 16
    if schema == 8:
        values = [v if i < 16 else v & 65535 for i, v in enumerate(values)]
        values[0] = 8 | (1 << 8) | (0x4C80 << 16)
        values[1] = 1
    put("_test_values", values)
    source = [
        5,
        96,
        0,
        1,
        7,
        symbols["_test_output"],
        0,
        0,
        0,
        symbols["_test_values"],
        32,
        9,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        symbols["_instrument_ui_status"],
        13,
    ]
    put("_iv_input", source)
    detail = []
    for part in range(11):
        state = [0] * 744
        state[188], state[193], state[194] = 4 + part * 8, 1, 9
        put("_test_state", state)
        call(5, R4=symbols["_test_state"], R8=symbols["_iv_input"])
        status = get("_instrument_ui_status", 13)
        assert status[0] == 1
        detail_schema = {8: 2, 9: 3, 10: 4}[schema]
        assert status[5] == (detail_schema << 16) | 0xB00 | part
        assert status[6] == 9
        detail.extend(status[7:13])
    assert detail[:16] == [v & 65535 for v in values[16:32]]
    if schema >= 9:
        assert detail[16:28] == [v >> 16 for v in values[16:28]]
        assert detail[28] == 0x4C80
        assert not any(detail[29:])
    else:
        assert detail[16] == 0x4C80
        assert not any(detail[17:])


def test_canopy_status_output_flows_through_protected_carrier(playback):
    from test_canopy_v2 import presentation_controls

    call, put, get, reset, configure, events, _ = playback
    assert reset() == 1
    assert configure(values=presentation_controls(), revision=1) == 1
    # Exercise the opt-in without publishing the incomplete production feature.
    put("_ir_process_flags", [1])
    call(0)
    assert get("_iv_input", 2) == [5, 96]
    assert get("_iv_result")[0] in (0, 1)
    status = get("_instrument_ui_status", 13)
    assert status[0] == 1 and status[3:5] == [1, 1]
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    call(0)
    status = get("_instrument_ui_status", 13)
    assert status[0] == 1 and (status[2] >> 20) & 7 == 1
    put("_instrument_session", [0])
    call(0)
    assert get("_instrument_ui_status")[0] == 0


def test_instance_binds_to_the_reset_track_and_renders_only_there(playback):
    call, put, get, reset, configure, events, symbols = playback
    # The first reset while unconfigured binds the instance to its track.
    assert reset(track=11, slot=0) == 1
    assert get("_iv_track") == [11]
    call(0)
    assert configure(track=0) == 0  # configuration names the bound track only
    assert configure(track=11) == 1
    call(0)
    assert get("_iv_input", 3)[2] == 11  # status word 1 and packets carry the track
    assert events([(1, 1, 60, 100, 0, 0)]) == 0
    assert events([(1, 1, 60, 100, 0, 0)], track=11) == 1
    call(0)
    rendered = get("_iv_output", 32)
    assert any(rendered)
    put("_test_output", [7] * 32)
    call(4, R2=0, R8=symbols["_test_output"])
    assert get("_test_output", 32) == [0] * 32  # the same type elsewhere is silent
    call(4, R2=11, R8=symbols["_test_output"])
    assert get("_test_output", 32) == rendered
    owners = get("_mi_velocity_owners", 32)
    assert owners[22] == get("_mi_carrier_stamp")[0] and owners[23] == 1
    # Another track's reset is only acknowledged while this one is configured.
    assert reset(track=3) == 1
    assert get("_iv_track") == [11] and get("_iv_revision") == [1]
    assert configure(track=3, revision=2) == 0


def test_instance_rebinds_after_its_own_reset_once_silent(playback):
    call, put, get, reset, configure, events, _ = playback
    assert reset(track=11, slot=0) == 1
    call(0)
    assert configure(track=11) == 1
    call(0)
    assert events([(1, 1, 60, 100, 0, 0)], track=11) == 1
    call(0)
    assert reset(track=11) == 1  # the CF resets the old track first
    assert get("_iv_revision") == [0]
    for _ in range(400):
        call(0)
        if get("_iv_active") == [0]:
            break
    assert get("_iv_active") == [0]
    assert reset(track=2, slot=0) == 1  # then binds the new one
    assert get("_iv_track") == [2]
    assert configure(track=2) == 1
    call(0)
    assert get("_iv_input", 3)[2] == 2


SLOT_WORDS = 160  # 8 scalars, input 34, output 32, events 48, controls 32, padding
SLOT_OUTPUT, SLOT_CONTROLS = 42, 122


def _four_instances(playback):
    """Widen the admitted row to four registry instances (one state block each)."""
    _, put, get, _, _, _, symbols = playback
    row = get("_ir_rows", 16)
    row[3], row[7] = 4, symbols["_test_state"] + 4 * 2976
    put("_ir_rows", row)


def _slot(get, index):
    words = get("_iv_slots", 4 * SLOT_WORDS)
    return words[index * SLOT_WORDS : (index + 1) * SLOT_WORDS]


def test_slots_bind_by_hint_and_render_only_their_own_tracks(playback):
    call, put, get, reset, configure, events, symbols = playback
    _four_instances(playback)
    assert reset(track=3, slot=0) == 1
    assert reset(track=9, slot=2) == 1
    assert get("_iv_slot_tracks", 4) == [3, 0xFFFFFFFF, 9, 0xFFFFFFFF]
    call(0)
    assert configure(track=3) == 1
    assert configure(track=9) == 1
    call(0)
    assert events([(1, 1, 60, 100, 0, 0)], track=3) == 1
    assert events([(1, 1, 67, 100, 0, 0)], track=9) == 1
    call(0)
    first, third = _slot(get, 0), _slot(get, 2)
    out3 = first[SLOT_OUTPUT : SLOT_OUTPUT + 32]
    out9 = third[SLOT_OUTPUT : SLOT_OUTPUT + 32]
    assert any(out3) and any(out9) and out3 != out9
    # Each slot drives its own registry instance: state base + slot * 2976.
    state = get("_test_state", 4 * 744)
    tokens = [
        {state[744 * k + i] for i in range(0, 32, 8) if state[744 * k + i]}
        for k in range(4)
    ]
    assert tokens == [{1}, set(), {1}, set()]
    for track, expected in ((3, out3), (9, out9), (5, [0] * 32)):
        put("_test_output", [7] * 32)
        call(4, R2=track, R8=symbols["_test_output"])
        assert get("_test_output", 32) == expected, track
    owners = get("_mi_velocity_owners", 32)
    assert owners[6] == owners[18] == get("_mi_carrier_stamp")[0]
    assert owners[7] == owners[19] == 1


def test_unbound_tracks_reject_configuration_and_events(playback):
    call, _, get, reset, configure, events, _ = playback
    _four_instances(playback)
    assert reset(track=3, slot=1) == 1
    call(0)
    assert configure(track=5) == 0
    assert configure(track=3) == 1
    assert events([(1, 1, 60, 100, 0, 0)], track=5) == 0
    assert events([(1, 1, 60, 100, 0, 0)], track=3) == 1


def test_hint_beyond_the_admitted_instances_binds_nothing(playback):
    _, _, get, reset, _, _, _ = playback
    assert reset(track=5, slot=1) == 1  # the row admits one instance
    assert get("_iv_slot_tracks", 4) == [0, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF]


def test_new_epoch_reset_unconfigures_every_slot(playback):
    call, _, get, reset, configure, events, _ = playback
    _four_instances(playback)
    assert reset(track=3, slot=0) == 1
    assert reset(track=9, slot=1) == 1
    call(0)
    assert configure(track=3) == 1 and configure(track=9) == 1
    call(0)
    assert [_slot(get, k)[1] for k in (0, 1)] == [1, 1]
    assert reset(track=3, epoch=2, generation=2) == 1
    assert [_slot(get, k)[1] for k in (0, 1)] == [0, 0]
    assert [_slot(get, k)[0] for k in (0, 1)] == [2, 1]


def test_focus_flag_selects_the_status_slot_and_never_reaches_the_module(playback):
    from test_canopy_v2 import wavetable_fm_controls

    call, put, get, reset, configure, _, _ = playback
    _four_instances(playback)
    put("_ir_process_flags", [1])
    assert reset(track=3, slot=0) == 1
    assert reset(track=9, slot=1) == 1
    call(0)
    values = wavetable_fm_controls()
    assert configure(track=3, values=values) == 1
    flagged = list(values)
    flagged[0] |= 0x8000
    assert configure(track=9, values=flagged) == 1
    assert get("_iv_focus") == [1]
    assert _slot(get, 1)[SLOT_CONTROLS] == values[0]  # flag stripped before the module
    call(0)
    status = get("_instrument_ui_status", 13)
    assert status[0] == 1 and status[2] & 15 == 9, status  # ready, block, kind/track
    assert configure(track=3, values=flagged, revision=2) == 1
    call(0)
    status = get("_instrument_ui_status", 13)
    assert status[0] == 1 and status[2] & 15 == 3, status
    # An unflagged configuration of the focused slot keeps the focus.
    assert configure(track=3, values=values, revision=3) == 1
    assert get("_iv_focus") == [0]


def test_binding_a_track_unbinds_its_previous_slot(playback):
    call, _, get, reset, configure, _, _ = playback
    _four_instances(playback)
    assert reset(track=3, slot=0) == 1
    assert reset(track=3, slot=2) == 1  # the CF moved the track to another instance
    assert get("_iv_slot_tracks", 4) == [0xFFFFFFFF, 0xFFFFFFFF, 3, 0xFFFFFFFF]
    call(0)
    assert configure(track=3) == 1
    assert _slot(get, 2)[1] == 1 and _slot(get, 0)[1] == 0


def test_unbound_slots_are_never_processed_or_published(playback):
    """An unbound slot holds track 0xffffffff, which must not pass a signed
    track check: it is neither processed nor allowed to touch owner tables."""
    call, put, get, reset, configure, _, _ = playback
    _four_instances(playback)
    assert reset(track=3, slot=0) == 1
    call(0)
    assert configure(track=3) == 1
    assert reset(track=3, slot=1) == 1  # slot 0 keeps generation 1 but is unbound
    put("_mi_native_owner", [0] * 16)
    unbound = _slot(get, 0)
    call(0)
    assert get("_ir_rows")[0] == 3  # no callback fault quarantined the row
    assert get("_iv_slot_tracks", 4) == [0xFFFFFFFF, 3, 0xFFFFFFFF, 0xFFFFFFFF]
    assert _slot(get, 0) == unbound  # never loaded, processed or stored
