"""Bounded DT2 1.17 carrier investigation using the supplied firmware's instructions.

This is a component experiment with synthetic envelope state, not a booted-device
or MIDI proof. It does not patch firmware, initialize a carrier, or enable routes.
Run from the repository root with python -m tools.sharc_dev.canopy_carrier_probe.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
from pathlib import Path

from tools.patchkit.port_resolve import sections_of
from tools.sharc_dev.sharc_run import Runner, st
from tools.sharc_dev.sharcldr import LoadedMemory

# Refuse other images: these are investigation addresses, not SDK declarations.
FIRMWARE_SHA = "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
BLOB_SHA = "0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2"
ARM, ENVELOPE = 0x1C4EAF, 0x1CBB57
STACK, RECORD, BUFFER, SAMPLE_RECORD = (
    0x20090000,
    0x20091000,
    0x20092000,
    0x20093000,
)
END = 0xB9F100


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


def record(runner):
    return [get(runner, RECORD + 4 * i) for i in range(11)]


def call(runner, entry, *, sample_record=False, trigger=False):
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
    put(runner, STACK + 8, 0)
    put(runner, STACK + 12, 32)
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
    runner = Runner(LoadedMemory.from_stream(blob), ENVELOPE, breakpoints=[END])
    for base, words in ((STACK - 256, 128), (RECORD, 11), (SAMPLE_RECORD, 118)):
        for i in range(words):
            put(runner, base + 4 * i, 0)
    # Synthetic sustained envelope: HOLD=127, mode flags=1, state=3 and
    # internal negative unity amplitude. This does not replace normal setup.
    for offset, value in ((4, 127), (24, 1), (28, 3), (40, bits(-1.0))):
        put(runner, RECORD + offset, value)
    before_arm = record(runner)
    runner = call(runner, ARM, sample_record=True)
    arm_record = get(runner, SAMPLE_RECORD + 0x1B8)
    arm_preserves_envelope = record(runner) == before_arm

    def render(current, *, trigger=False):
        for i in range(32):
            put(current, BUFFER + 4 * i, bits(0.25))
        current = call(current, ENVELOPE, trigger=trigger)
        output = [get(current, BUFFER + 4 * i) for i in range(32)]
        return current, output

    sustained = True
    for _ in range(blocks):
        runner, output = render(runner)
        sustained &= output == [bits(0.25)] * 32
    held = record(runner)
    # Enter the routine's release state explicitly; live note-off ownership
    # remains a separate hook investigation.
    put(runner, RECORD + 28, 4)
    runner, released = render(runner)
    release_state = record(runner)
    release_peak = max(
        abs(struct.unpack("<f", struct.pack("<I", value))[0]) for value in released[1:]
    )
    # Trigger branch must restore a nonzero block after the release experiment.
    runner, retriggered = render(runner, trigger=True)
    retrigger_state = record(runner)
    runner, settled = render(runner)
    recovered = retriggered[-1] == bits(0.25) and settled == [bits(0.25)] * 32
    checks = {
        "arm_sets_active_and_seed": arm_record == 0x00010001,
        "sample_arm_leaves_separate_envelope_unchanged": arm_preserves_envelope,
        "synthetic_hold_passes_input_unchanged": sustained,
        "hold_state_persists": held[7] == 3,
        "hold_counter_advances_without_retrigger": held[9] == blocks * 32,
        "explicit_release_attenuates": release_peak < 0.25,
        "trigger_recovers_output": recovered,
    }
    return {
        "firmware_sha256": FIRMWARE_SHA,
        "dsp_sha256": BLOB_SHA,
        "scope": "synthetic-state firmware component; no live carrier or final mix proof",
        "blocks": blocks,
        "samples": blocks * 32,
        "checks": checks,
        "hold_counter": held[9],
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
