"""Bounded DT2 1.17 carrier investigation using the supplied firmware's instructions.

This configures and runs the stock envelope through an in-memory entry patch and
the V2 transition policy. It does not write a patched SYX or enable MIDI routes.
It is a component experiment, not a booted-device or live-frame delivery proof.
Run from the repository root with python -m tools.sharc_dev.canopy_carrier_probe.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import struct
from pathlib import Path

from tools.patchkit import pk_sharc
from tools.patchkit.port_resolve import sections_of
from tools.sharc_dev.sharc_run import Runner, st
from tools.sharc_dev.sharcldr import LoadedMemory, parse_blocks

# Refuse other images: these are investigation addresses, not SDK declarations.
FIRMWARE_SHA = "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
BLOB_SHA = "0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2"
ARM, ENVELOPE = 0x1C4EAF, 0x1CBB57
CONFIGURE = 0x1CBA9E
WORKSPACE = 0x1C648A
STACK, RECORD, BUFFER, SAMPLE_RECORD = (
    0x20090000,
    0x20091000,
    0x20092000,
    0x20093000,
)
END = 0xB9F100


def carrier_policy(blob):
    """Execute the adapter policy, keeping its per-track state between calls."""
    path = (
        Path(__file__).resolve().parents[2]
        / "modules/core/adapters/midi-events/src/dsp/carrier.s"
    )
    obj = pk_sharc.assemble(
        "\n".join(
            path.with_name(name).read_text()
            for name in ("carrier.s", "envelope.s", "workspace.s", "carrier-frame.s")
        )
        + "\n"
        + "\n".join(
            (path.parents[5] / "machines/canopy/src/dsp" / name).read_text()
            for name in ("voice.s", "tuning.s", "instrument.s", "render.s")
        )
        + "\n.PKDATA _mi_epoch, 4;\n"
    )
    address = 0x20094000
    sections, initial = {}, {}
    for name, (data, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        for i in range(0, len(data), 4):
            initial[address + i] = int.from_bytes(data[i : i + 4], "little")
        address += len(data)
    symbols = {
        name: sections[section] + offset
        for name, (section, offset, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_envelope_next": ENVELOPE + 3,
            "_mi_workspace_next": WORKSPACE + 3,
        },
    )
    jump = pk_sharc.link(
        pk_sharc.assemble(
            ".SECTION/PM seg_pmco;\n.EXTERN _mi_envelope_entry;\nJUMP _mi_envelope_entry;\n"
        ),
        ENVELOPE,
        linked.symbols,
    )
    workspace_jump = pk_sharc.link(
        pk_sharc.assemble(
            ".SECTION/PM seg_pmco;\n.EXTERN _mi_workspace_entry;\nJUMP _mi_workspace_entry;\n"
        ),
        WORKSPACE,
        linked.symbols,
    )
    blocks = list(parse_blocks(blob))
    image = blob
    for code in (linked, jump, workspace_jump):
        blocks.append(
            {
                "target_address": 0x28000000 + 2 * code.base_sw,
                "byte_count": len(code.code_boot),
                "fill": False,
                "payload_offset": len(image),
                "payload_len": len(code.code_boot),
                "index": len(blocks),
            }
        )
        image += code.code_boot
    memory = LoadedMemory(image, blocks)
    session = pk_sharc.CallSession([linked], END)
    for address, value in initial.items():
        session.poke(address // 4, value)
    stamp = 0

    def step(active, trigger, release, runner):
        nonlocal stamp
        result = session.call(
            "_mi_carrier_publish",
            {
                "R4": 0,
                "R8": active,
                "R10": 1,
                "R11": stamp,
                "R12": trigger,
                "R9": release,
                "M5": 0,
                "L0": 0,
                "MODE1": 0x1011800,
            },
            max_steps=150,
            read_regs=("R0", "R1", "R2", "R3", "I0", "M5"),
        )
        if result["halt"]["reason"] != "breakpoint":
            raise RuntimeError(f"carrier policy failed: {result}")
        pair = result["regs"]["R0"], result["regs"]["R1"]
        for voice in (0, 1):
            delivered = session.call(
                "_mi_carrier_flags",
                {
                    "R4": voice,
                    "R11": stamp,
                    "R12": trigger,
                    "R9": release,
                    "M5": 0,
                    "L0": 0,
                    "MODE1": 0x1011800,
                },
                max_steps=100,
                read_regs=("R0", "R1", "R2"),
            )
            if delivered["halt"]["reason"] != "breakpoint" or tuple(
                delivered["regs"][r] for r in ("R0", "R1", "R2")
            ) != (*pair, 1):
                raise RuntimeError(f"stereo carrier delivery failed: {delivered}")
        # Copy the actual policy publication into the firmware core. The two
        # cores are a component harness, not live frame delivery.
        for name, count in (("_mi_carrier_active", 16), ("_mi_carrier_pairs", 48)):
            for i in range(count):
                address = symbols[name] + 4 * i
                put(runner, address, session.peek(address // 4))
        put(runner, symbols["_mi_carrier_stamp"], stamp)
        stamp += 1
        return pair

    return step, memory, initial, {**symbols, **linked.symbols}


def bind_workspace(runner, workspace):
    """Run the original dispatcher's pointer setup, including our capture hook."""
    stop = 0x1C649F  # after stock stores its computed bank at I6-25
    runner = runner.fresh_call(
        0x1C6479,
        regs={
            "R4": workspace,
            "R8": BUFFER,
            "I6": STACK,
            "L1": 0,
            "L2": 0,
            "L3": 0,
            "MODE1": 0x1011800,
        },
        return_address=END,
    )
    runner.breakpoints |= {stop}
    result = runner.run(100)
    if result.halt.reason != "breakpoint" or result.final_pc_sw != stop:
        raise RuntimeError(f"workspace capture failed: {result.halt.to_json()}")
    runner.breakpoints -= {stop}
    return runner


def put(runner, address, value):
    if not st._dm_write(
        runner.state, address, 4, st.Const(value & 0xFFFFFFFF), normal_word=False
    ):
        raise RuntimeError(f"write failed at {address:#x}")


def get(runner, address):
    value = st._dm_read(runner.state, address, 4, normal_word=False)
    if value is None:
        raise RuntimeError(f"unknown read at {address:#x}")
    return value.value & 0xFFFFFFFF


def bits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


def configure_from_dispatcher(runner):
    """Execute the actual argument-loading caller with sustained proof settings."""
    params = 0x2008F000
    # I5-relative mode, attack, hold, decay, sustain, release and final flag.
    values = (1, bits(0.0), bits(1.0), bits(0.0), bits(1.0), bits(0.0), 0)
    for offset, value in zip(range(-7, 0), values, strict=True):
        put(runner, params + offset * 4, value)
    stop = 0x1C6A28
    runner = runner.fresh_call(
        0x1C6A0A,
        regs={
            "R2": 0,
            "R6": values[-2],
            "R7": values[-1],
            "I5": params,
            "I8": RECORD,
            "I6": STACK,
            "I7": STACK - 256,
            "M5": 0,
            "M6": 1,
            "M7": 0xFFFFFFFF,
            "MODE1": 0x1011800,
            **{f"L{i}": 0 for i in range(16)},
        },
        return_address=END,
    )
    runner.breakpoints |= {stop}
    result = runner.run(1000)
    if result.halt.reason != "breakpoint" or result.final_pc_sw != stop:
        raise RuntimeError(
            f"stock configuration caller failed: {result.halt.to_json()}"
        )
    runner.breakpoints -= {stop}
    return runner


def musical_chain(memory, initial, symbols):
    """One core: kernel -> carrier publication -> patched stock envelope."""
    runner = Runner(memory, ENVELOPE, breakpoints=[END])
    for address, value in initial.items():
        put(runner, address, value)
    for i in range(11):
        put(runner, RECORD + 4 * i, 0)
    runner = bind_workspace(runner, RECORD - 0xD604)
    runner = configure_from_dispatcher(runner)
    put(runner, symbols["_cp_ready"], 1)

    def internal(entry, **args):
        nonlocal runner
        runner = runner.fresh_call(
            symbols[entry],
            regs={
                "R4": symbols["_cp_banks"],
                "M5": 0,
                "M6": 1,
                "M13": 0,
                "I13": END,
                "MODE1": 0x1011800,
                **{f"L{i}": 0 for i in range(16)},
                **args,
            },
            return_address=END,
        )
        result = runner.run(12000)
        if result.halt.reason != "breakpoint" or result.final_pc_sw != END:
            raise RuntimeError(f"{entry} failed: {result.halt.to_json()}")

    unchanged, counters, releases, audio = True, [], [], []

    def render():
        nonlocal runner, unchanged
        internal("_mi_carrier_begin")
        internal("_cp_voice_render", R2=0, R8=BUFFER)
        before = [get(runner, BUFFER + 4 * i) for i in range(32)]
        pair = get(runner, symbols["_mi_carrier_pairs"] + 4)
        owned = get(runner, symbols["_mi_carrier_pairs"] + 8)
        runner = call(runner, ENVELOPE, trigger=True, release=True)
        after = [get(runner, BUFFER + 4 * i) for i in range(32)]
        audio.extend(struct.unpack("<f", struct.pack("<I", word))[0] for word in after)
        if owned and pair == 0:
            unchanged &= before == after
        if owned and pair == 2:
            releases.append(before == [0] * 32)
        counters.append(record(runner)[9])

    internal("_cp_note_on", R8=1, R12=60, R9=20)
    internal("_cp_note_on", R8=2, R12=64, R9=100)
    for _ in range(16):
        render()
    internal("_cp_note_on", R8=3, R12=67, R9=127)
    internal("_cp_note_off", R8=2)
    for _ in range(240):
        render()
    bank = symbols["_cp_banks"]
    tokens = [get(runner, bank + 32 * i) for i in range(6)]
    independent = 1 in tokens and 3 in tokens and 2 not in tokens
    gains = {
        token: struct.unpack("<f", struct.pack("<I", get(runner, bank + 32 * i + 24)))[
            0
        ]
        for i, token in enumerate(tokens)
        if token
    }
    # Reset uses the production bounded fade. It must hold the envelope through
    # its last nonempty block, then emit exactly one release on an empty block.
    packet = 0x2008E000
    put(runner, packet + 8, 0)
    put(runner, packet + 32, 0)
    internal("_instrument_reset", R4=packet)
    for _ in range(4):
        render()
    return {
        "independent_note_release_through_stock_envelope": independent,
        "independent_low_then_high_velocity_survives": abs(gains.get(1, -1) - 20 / 127)
        < 1e-6
        and abs(gains.get(3, -1) - 1) < 1e-6,
        "post_envelope_audio_is_finite_and_nonzero": all(
            math.isfinite(x) for x in audio
        )
        and 0.01 < max(abs(x) for x in audio) < 0.375,
        "held_audio_is_bit_identical_through_stock_envelope": unchanged,
        "stock_envelope_counter_never_restarts_while_held": all(
            b == a + 32 for a, b in zip(counters[:255], counters[1:256], strict=True)
        ),
        "reset_releases_only_after_last_audio_block": releases == [True],
    }


def record(runner):
    return [get(runner, RECORD + 4 * i) for i in range(11)]


def call(runner, entry, *, sample_record=False, trigger=False, release=False):
    regs = {
        "R0": 0,
        "R4": SAMPLE_RECORD if sample_record else RECORD,
        "R8": BUFFER,
        "R12": BUFFER,
        "I6": STACK,
        "I7": STACK,
        "M5": 0,
        "M6": 1,
        "M7": 0xFFFFFFFF,
        "M13": 0,
        "M14": 1,
        "M15": 0xFFFFFFFF,
        "MODE1": 0x1011800,
        **{f"L{i}": 0 for i in range(16)},
    }
    put(runner, STACK - 4, END - 1)
    put(runner, STACK, STACK + 256)
    # Stock caller passes two flags followed by the 32-sample count. Their
    # relation to live CF note/gate fields is not established by this probe.
    put(runner, STACK + 4, 1 if trigger else 0)
    put(runner, STACK + 8, int(release))
    put(runner, STACK + 12, 32)
    if entry == CONFIGURE:
        regs["R8"] = 1
        regs["R12"] = bits(0.0)
        for i, value in enumerate((bits(1.0), bits(0.0), bits(1.0), bits(0.0), 0), 1):
            put(runner, STACK + 4 * i, value)
    runner = runner.fresh_call(entry, regs=regs, return_address=END)
    result = runner.run(10000)
    if result.halt.reason != "breakpoint" or result.final_pc_sw != END:
        raise RuntimeError(f"firmware call failed: {result.halt.to_json()}")
    return runner


def probe(firmware: Path, blocks: int):
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != FIRMWARE_SHA:
        raise ValueError("probe requires the pinned DT2 OS 1.17 firmware")
    blob = sections_of(firmware)[7]
    if hashlib.sha256(blob).hexdigest() != BLOB_SHA:
        raise ValueError("unexpected DT2 1.17 DSP blob")
    policy, memory, initial, symbols = carrier_policy(blob)
    runner = Runner(memory, ENVELOPE, breakpoints=[END])
    for address, value in initial.items():
        put(runner, address, value)
    for base, words in ((STACK - 256, 128), (RECORD, 11), (SAMPLE_RECORD, 118)):
        for i in range(words):
            put(runner, base + 4 * i, 0)
    # Exercise relocation as well as initial capture; no direct write to the
    # hook's envelope-bank pointer is allowed anywhere in this probe.
    runner = bind_workspace(runner, RECORD - 0xD604 + 0x1000)
    moved_base = get(runner, symbols["_mi_envelope_records"])
    runner = bind_workspace(runner, RECORD - 0xD604)
    captured_base = get(runner, symbols["_mi_envelope_records"])
    stock_base = get(runner, STACK - 25 * 4)
    runner = configure_from_dispatcher(runner)
    configured = record(runner)
    before_arm = record(runner)
    runner = call(runner, ARM, sample_record=True)
    arm_record = get(runner, SAMPLE_RECORD + 0x1B8)
    arm_preserves_envelope = record(runner) == before_arm

    edges = []

    def render(current, *, active=1, stock_trigger=0, stock_release=0):
        trigger, release = policy(active, stock_trigger, stock_release, current)
        edges.append((trigger, release))
        for i in range(32):
            put(current, BUFFER + 4 * i, bits(0.25))
        # Enter at the patched stock address with unmodified STOCK arguments:
        # only the new entry hook is allowed to replace them with owned flags.
        current = call(current, ENVELOPE, trigger=stock_trigger, release=stock_release)
        output = [get(current, BUFFER + 4 * i) for i in range(32)]
        return current, output

    runner, onset = render(runner)
    counter_at_onset = record(runner)[9]
    sustained = True
    for _ in range(blocks):
        runner, output = render(runner, stock_trigger=1, stock_release=1)
        sustained &= output == [bits(0.25)] * 32
    held = record(runner)
    runner, released = render(runner, active=0)
    release_state = record(runner)
    release_peak = max(
        abs(struct.unpack("<f", struct.pack("<I", value))[0]) for value in released[1:]
    )
    # Trigger branch must restore a nonzero block after the release experiment.
    runner, retriggered = render(runner)
    retrigger_state = record(runner)
    runner, settled = render(runner)
    recovered = retriggered[-1] == bits(0.25) and settled == [bits(0.25)] * 32
    checks = {
        "workspace_capture_matches_stock_and_rebinds": moved_base == RECORD + 0x1000
        and captured_base == stock_base == RECORD,
        "arm_sets_active_and_seed": arm_record == 0x00010001,
        "sample_arm_leaves_separate_envelope_unchanged": arm_preserves_envelope,
        "configured_envelope_wakes": onset[-1] == bits(0.25),
        "configured_hold_passes_input_unchanged": sustained,
        "hold_state_persists": held[7] == 3,
        "hold_counter_advances_without_retrigger": held[9]
        == counter_at_onset + blocks * 32,
        "quiet_edge_releases_envelope": release_peak < 0.25,
        "trigger_recovers_output": recovered,
        "policy_emits_only_two_wakes_and_one_release": edges.count((1, 0)) == 2
        and edges.count((0, 1)) == 1
        and edges.count((0, 0)) == blocks + 1,
    }
    checks.update(musical_chain(memory, initial, symbols))
    return {
        "firmware_sha256": FIRMWARE_SHA,
        "dsp_sha256": BLOB_SHA,
        "scope": "in-memory patched stock envelope entry and adapter policy; no live frame or final mix proof",
        "blocks": blocks,
        "samples": blocks * 32,
        "checks": checks,
        "hold_counter": held[9],
        "configured": configured,
        "release_state": release_state,
        "retrigger_state": retrigger_state,
        "release_peak_after_first_sample": release_peak,
        "passed": all(checks.values()),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--firmware", type=Path, default=Path("firmware/Digitakt_II_OS1.17.syx")
    )
    parser.add_argument("--blocks", type=int, default=128)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()
    if not 1 <= args.blocks <= 1500:
        parser.error("--blocks must be between 1 and 1500")
    report = probe(args.firmware, args.blocks)
    output = json.dumps(report, indent=2) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(output)
    print(output, end="")
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
