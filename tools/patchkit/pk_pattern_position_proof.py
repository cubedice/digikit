"""Bounded DT2 1.17 running-sequencer comparison for a pattern-position hook.

Use prepared baseline/hook images and one private card containing test/sine::

    python -m tools.patchkit.pk_pattern_position_proof \
        --baseline BASE.syx --hook HOOK.syx --card PRIVATE.img --work out/runs/position

Both images must include the same runtime/framework modules; the hook image adds
the position provider. Each image's adjacent .plan.json is required. Existing
DIGI_EMUD_DT2 and DIGI_PK_READY_DT2_117 overrides select prepared binaries and
ready inputs. This runner never builds tools or writes guest memory. The card's
guest writes remain in each emulator's disposable overlay.

Grid-recorded panel trigs, DSP-bound note-on frames, and complete stereo PCM are
independent evidence. A baseline run before entering trigs must contain no track-1
note-ons. Positive runs must repeat the sequence and match after one constant
capture offset; individual notes are never realigned. Provider-specific assertions
are separate from these audio and sequencer preservation checks.
"""

from __future__ import annotations

import argparse
import csv
import json
import os
import struct
import time
import tomllib
import uuid
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from tools.emulator.dspi_log import Frame, read_log
from tools.patchkit.pk_short_loop_proof import (
    ROOT,
    SINE,
    ProofFailure,
    deadline,
    pcm_payload,
    sample_stats,
    sha256,
)

RATE = 48000
BLOCK = 32
DT2_117_SHA256 = "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
NOTE_ON = 0x22
NOTE_WORDS = 2
POSITION_OFFSET = 0xA7C
POSITION_WORDS = 16
POSITION_MAGIC = 0x50504F53
POSITION_VERSION = 1
POSITION_XOR = 0x7B91D6E3
MAX_SNAPSHOT_AGE_BLOCKS = 64  # 42.7 ms; this bounded proof uses the internal clock.


@dataclass(frozen=True)
class NoteOn:
    frame: int
    note: int


def position_words(frame: Frame) -> tuple[int, ...]:
    """Each DSP word is two big-endian wire halfwords, low halfword first."""
    end = POSITION_OFFSET + POSITION_WORDS * 4
    if not frame.attached or len(frame.tx) < end:
        raise ProofFailure("missing pattern-position frame suffix")
    halves = struct.unpack_from(">32H", frame.tx, POSITION_OFFSET)
    words = tuple(halves[i] | (halves[i + 1] << 16) for i in range(0, 32, 2))
    checksum = POSITION_XOR
    for word in words[:15]:
        checksum ^= word
    if words[:2] != (POSITION_MAGIC, POSITION_VERSION) or words[15] != checksum:
        raise ProofFailure("invalid pattern-position magic/version/checksum")
    if words[5] & ~3 or bool(words[5] & 2) != (words[14] == 1):
        raise ProofFailure("unknown or inconsistent pattern-position transport flags")
    return words


def position_timing_diagnostics(
    frames: list[Frame], events: list[NoteOn], trigs: tuple[int, ...]
) -> dict[str, Any]:
    """Describe observed phase; these measurements are not capture acceptance rules."""
    decoded = [(frame.index, position_words(frame)) for frame in frames]
    by_frame = dict(decoded)

    def sample(index: int, words: tuple[int, ...]) -> dict[str, int]:
        return {
            "frame": index,
            "current_step": words[7],
            "next_step": words[13],
            "substep": words[9],
            "event_sequence": words[3],
            "snapshot_cf_block": words[10],
            "transmit_cf_block": words[12],
            "snapshot_age_blocks": (words[12] - words[10]) & 0xFFFFFFFF,
        }

    step_edges, substep_edges = [], []
    # An initial value, a gap, or entry into the valid interval is not an edge.
    for (before_index, before), (index, words) in zip(
        decoded, decoded[1:], strict=False
    ):
        if index != before_index + 1 or before[5] != 3 or words[5] != 3:
            continue
        event_delta = (words[3] - before[3]) & 0xFFFFFFFF
        block_delta = (words[10] - before[10]) & 0xFFFFFFFF
        fresh = 0 < event_delta < 0x80000000 and 0 < block_delta < 0x80000000
        edge = {**sample(index, words), "fresh_snapshot": fresh}
        if words[7] != before[7]:
            step_edges.append(edge)
        if fresh and words[9] == 1 and (words[7], words[9]) != (before[7], before[9]):
            substep_edges.append(edge)

    def preceding(
        edges: list[dict[str, Any]], index: int, step: int
    ) -> dict[str, Any] | None:
        for edge in reversed(edges):
            if edge["frame"] <= index and edge["current_step"] == step:
                delay = index - edge["frame"]
                return {
                    **edge,
                    "note_delay_blocks": delay,
                    "note_delay_stereo_frames": delay * BLOCK,
                    "note_delay_ms": delay * BLOCK * 1000 / RATE,
                }
        return None

    notes = []
    for number, event in enumerate(events):
        words = by_frame[event.frame]
        notes.append(
            {
                **sample(event.frame, words),
                "grid_step_label": trigs[number % len(trigs)],
                "current_step_edge": preceding(step_edges, event.frame, words[7]),
                "fresh_substep1_edge": preceding(substep_edges, event.frame, words[7]),
            }
        )
    gaps = [b - a for a, b in zip(trigs, trigs[1:], strict=False)] + [
        16 - trigs[-1] + trigs[0]
    ]
    return {
        "descriptive_only": True,
        "initial_observation_is_edge": False,
        "edge_search": "latest matching observed edge at or before the note-on; null if absent",
        "current_step_edges": step_edges,
        "fresh_substep1_edges": substep_edges,
        "note_timings": notes,
        "uniform_trig_spacing": len(set(gaps)) == 1,
        "grid_label_assumption": (
            "The first observed note-on is assigned the first programmed trig, then labels repeat. "
            "Uniform trig spacing cannot independently distinguish a cyclic phase relabeling."
        ),
        "arbitrary_step_boundary_alignment_verified": False,
        "sample_accurate_capture_verified": False,
    }


def position_evidence(
    frames: list[Frame], events: list[NoteOn], trigs: tuple[int, ...]
) -> dict[str, Any]:
    """Measure native step/trigger mapping without assuming a lookahead offset."""
    decoded = [(frame.index, position_words(frame)) for frame in frames]
    for (_, before), (_, after) in zip(decoded, decoded[1:], strict=False):
        if (after[2] - before[2]) & 0xFFFFFFFF != 1:
            raise ProofFailure("pattern-position TX sequence skips or stalls")
    running = [(index, words) for index, words in decoded if words[5] & 2]
    if len(running) < 1500 or any(words[5] != 3 for _, words in running):
        raise ProofFailure("pattern-position has no complete valid running interval")
    if running[-1][0] - running[0][0] + 1 != len(running):
        raise ProofFailure("pattern-position running flag drops inside playback")
    initial = running[0][1]
    if initial[6] == 0 or initial[8] != 16:
        raise ProofFailure("pattern-position does not describe a 16-step pattern")
    for _, words in running:
        if (words[4], words[6], words[8]) != (initial[4], initial[6], initial[8]):
            raise ProofFailure(
                "pattern/length/epoch changed during the isolated sequence"
            )
        if words[7] > words[8] or words[13] > words[8]:
            raise ProofFailure("native pattern step is outside the pattern length")
        if ((words[12] - words[10]) & 0xFFFFFFFF) > MAX_SNAPSHOT_AGE_BLOCKS:
            raise ProofFailure("pattern-position snapshot is stale while playing")
    for (_, before), (_, after) in zip(running, running[1:], strict=False):
        event_delta = (after[3] - before[3]) & 0xFFFFFFFF
        snapshot_delta = (after[10] - before[10]) & 0xFFFFFFFF
        if ((after[12] - before[12]) & 0xFFFFFFFF) != 1:
            raise ProofFailure("transmit CF block skips or stalls")
        if event_delta >= 0x80000000 or snapshot_delta >= 0x80000000:
            raise ProofFailure("snapshot event/block progression moves backward")
        if bool(event_delta) != bool(snapshot_delta):
            raise ProofFailure(
                "snapshot event and source block disagree about progress"
            )
    by_frame = dict(decoded)
    observations = []
    for index, event in enumerate(events):
        words = by_frame.get(event.frame)
        if words is None or words[5] != 3:
            raise ProofFailure("note-on has no valid running position record")
        observations.append(
            {
                "frame": event.frame,
                "grid_step": trigs[index % len(trigs)],
                "current_step": words[7],
                "next_step": words[13],
                "substep": words[9],
                "event_sequence": words[3],
                "raw_transport": words[14],
                "snapshot_cf_block": words[10],
                "transmit_cf_block": words[12],
                "snapshot_age_blocks": (words[12] - words[10]) & 0xFFFFFFFF,
            }
        )
    if len(observations) < 2 * len(trigs):
        raise ProofFailure("position mapping has fewer than two observed passes")
    current_offsets = {
        (item["current_step"] - (item["grid_step"] - 1)) % 16 for item in observations
    }
    next_offsets = {
        (item["next_step"] - (item["grid_step"] - 1)) % 16 for item in observations
    }
    if len(current_offsets) != 1 or len(next_offsets) != 1:
        raise ProofFailure("native step-to-note mapping changes across the sequence")
    transitions = []
    for index, words in running:
        if not transitions or transitions[-1]["step"] != words[7]:
            transitions.append(
                {"frame": index, "step": words[7], "event_sequence": words[3]}
            )
    if len(transitions) < 17:
        raise ProofFailure("native playhead did not traverse a complete pattern")
    for before, after in zip(transitions, transitions[1:], strict=False):
        if (after["step"] - before["step"]) % 16 != 1:
            raise ProofFailure("native playhead skips or reorders steps between notes")
        if ((after["event_sequence"] - before["event_sequence"]) & 0xFFFFFFFF) == 0:
            raise ProofFailure("native playhead moved without a fresh snapshot event")
    return {
        "valid_running_frames": len(running),
        "epoch": initial[4],
        "pattern_pointer": initial[6],
        "length": initial[8],
        "current_step_offset_from_grid_zero": next(iter(current_offsets)),
        "next_step_offset_from_grid_zero": next(iter(next_offsets)),
        "note_positions": observations,
        "transitions": transitions,
        "snapshot_age_max_blocks": max(
            (words[12] - words[10]) & 0xFFFFFFFF for _, words in running
        ),
        "snapshot_age_limit_blocks": MAX_SNAPSHOT_AGE_BLOCKS,
        "epoch_semantics": "successful start/resume generation",
        "mapping_measured_not_assumed": True,
        "timing_diagnostics": position_timing_diagnostics(frames, events, trigs),
    }


def receiver_evidence(
    frames: list[Frame],
    path: Path,
    address: int,
    fx_counter: int,
    idle: tuple[int, int],
) -> dict[str, Any]:
    """Match every wire record to its post-render DSP mirror, never to a later row."""
    wanted = {frame.index: position_words(frame) for frame in frames}
    observed: dict[int, tuple[int, ...]] = {}
    fx_blocks = []
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            index = int(row["frame"])
            if index not in wanted:
                continue
            if index in observed:
                raise ProofFailure("duplicate DSP receiver watch frame")
            try:
                words = tuple(
                    int(row[f"0x{address + word * 4:x}"].split("|", 1)[0], 0)
                    for word in range(20)
                )
                fx_block = int(row[f"0x{fx_counter:x}"].split("|", 1)[0], 0)
                pc = int(row["pc"], 0)
            except (KeyError, ValueError) as exc:
                raise ProofFailure("unknown DSP receiver/counter watch data") from exc
            if words[:16] != wanted[index]:
                raise ProofFailure("DSP receiver does not mirror this exact wire frame")
            if words[19] != 1:
                raise ProofFailure("DSP receiver transmit-sequence delta is not one")
            if not idle[0] <= pc <= idle[1]:
                raise ProofFailure("DSP did not reach idle after the observed frame")
            observed[index] = words
            fx_blocks.append(fx_block)
    if not wanted or set(observed) != set(wanted):
        raise ProofFailure("DSP receiver watch does not cover every capture frame")
    ordered = [observed[frame.index] for frame in frames]
    for before, after in zip(ordered, ordered[1:], strict=False):
        if ((after[16] - before[16]) & 0xFFFFFFFF) != 1 or (
            (after[18] - before[18]) & 0xFFFFFFFF
        ) != 1:
            raise ProofFailure("DSP receiver/accept counter skips or stalls")
        if after[17] != before[17]:
            raise ProofFailure("DSP receiver rejected a capture frame")
    return {
        "file": str(path),
        "matched_frames": len(ordered),
        "idle_range": idle,
        "first_counters": list(ordered[0][16:]),
        "last_counters": list(ordered[-1][16:]),
        "fx_counter_first": fx_blocks[0],
        "fx_counter_last": fx_blocks[-1],
        "exact_same_frame_mirror": True,
        "observed_after_render": True,
        "before_fx_consumer_proved": False,
    }


def note_ons(frames: list[Frame], track: int = 0) -> list[NoteOn]:
    """Rising track bits in actual DT2 TX frames; held bits are one event."""
    if not 0 <= track < 16:
        raise ValueError("track must be 0..15")
    previous, result = False, []
    for frame in frames:
        if not frame.attached or len(frame.tx) < NOTE_ON + 2:
            raise ProofFailure("missing or short attached DSP frame")
        high = bool(
            int.from_bytes(frame.tx[NOTE_ON : NOTE_ON + 2], "big") & (1 << track)
        )
        if high and not previous:
            at = NOTE_WORDS + 2 * track
            result.append(
                NoteOn(frame.index, int.from_bytes(frame.tx[at : at + 2], "big"))
            )
        previous = high
    return result


def sequence_evidence(events: list[NoteOn], trigs: tuple[int, ...]) -> dict[str, Any]:
    """Reject silence/one audition and require at least two programmed passes."""
    if not trigs or len(events) < 2 * len(trigs):
        raise ProofFailure("fewer than two passes of the programmed trigs")
    gaps = [b.frame - a.frame for a, b in zip(events, events[1:], strict=False)]
    if min(gaps) <= 0 or events[-1].frame - events[0].frame < 1500:
        raise ProofFailure("note-ons do not span a running sequence")
    steps = [b - a for a, b in zip(trigs, trigs[1:], strict=False)] + [
        16 - trigs[-1] + trigs[0]
    ]
    # A block quantizes a step boundary; allow two blocks for rounding. This is
    # a nonuniform-trig spacing check, not an assumption that the tempo is 120.
    units = [gap / steps[i % len(steps)] for i, gap in enumerate(gaps)]
    step_frames = sum(units) / len(units)
    residual = max(
        abs(gap - step_frames * steps[i % len(steps)]) for i, gap in enumerate(gaps)
    )
    if residual > 2:
        raise ProofFailure("note-on spacing does not match the entered trig steps")
    return {
        "events": [asdict(event) for event in events],
        "relative_frames": [event.frame - events[0].frame for event in events],
        "gaps": gaps,
        "step_frames": step_frames,
        "spacing_max_error_blocks": residual,
    }


def compare_events(baseline: list[NoteOn], hook: list[NoteOn]) -> dict[str, Any]:
    """One start offset is allowed; every later onset must keep the same offset."""
    if not baseline or len(baseline) != len(hook):
        raise ProofFailure("baseline and hook note-on counts differ or are empty")
    shift = hook[0].frame - baseline[0].frame
    for index, (left, right) in enumerate(zip(baseline, hook, strict=True)):
        if left.note != right.note or right.frame - left.frame != shift:
            raise ProofFailure(f"note-on drift or pitch difference at event {index}")
    return {"constant_offset_blocks": shift, "events": len(baseline), "drift_blocks": 0}


def compare_pcm(baseline: bytes, hook: bytes, max_shift: int = 256) -> dict[str, Any]:
    """Exact complete common capture, with only one stereo-frame offset."""
    if max_shift < 0 or len(baseline) % 8 or len(hook) % 8:
        raise ProofFailure("invalid PCM alignment inputs")
    count_a, count_b = len(baseline) // 8, len(hook) // 8
    if min(count_a, count_b) < RATE or abs(count_a - count_b) > max_shift:
        raise ProofFailure("PCM capture is short or the lengths differ too much")
    for label, payload in (("baseline", baseline), ("hook", hook)):
        stats = sample_stats(payload)
        if not stats["finite"] or stats["peak"] <= 1e-4:
            raise ProofFailure(f"{label} PCM is nonfinite or silent")
    for shift in sorted(range(-max_shift, max_shift + 1), key=abs):
        a, b = (shift * 8, 0) if shift >= 0 else (0, -shift * 8)
        length = min(len(baseline) - a, len(hook) - b)
        if length < (min(count_a, count_b) - max_shift) * 8:
            continue
        if baseline[a : a + length] == hook[b : b + length]:
            return {
                "constant_offset_stereo_frames": shift,
                "compared_stereo_frames": length // 8,
                "sha256": sha256(baseline[a : a + length]),
                "exact": True,
            }
    raise ProofFailure("complete stereo PCM differs at every allowed constant offset")


def window_frames(
    frames: list[Frame], first: int, last: int, final_frames: int
) -> tuple[list[Frame], int]:
    """Log and runtime worker counters both start at zero, outside guest state.

    status.dsp_frames is ThreadedHandle.frames_sent, not a restored guest clock.
    sharc_peer creates it and Diag.log_index at zero; each Msg::Frame increments
    both once. Require the independent final runtime count instead of deriving
    an offset from the log tail, which could hide a truncated capture.
    """
    if not frames or any(
        b.index != a.index + 1 for a, b in zip(frames, frames[1:], strict=False)
    ):
        raise ProofFailure("DSP log is empty or has missing frame indices")
    if (
        frames[0].index != 0
        or frames[-1].index + 1 != final_frames
        or len(frames) != final_frames
    ):
        raise ProofFailure(
            "DSP log is truncated relative to the independent runtime frame count"
        )
    if not 0 <= first < last <= final_frames:
        raise ProofFailure("invalid runtime frame window")
    found = frames[first:last]
    if len(found) != last - first:
        raise ProofFailure("DSP log does not cover the complete capture window")
    return found, 0


def pcm_frame_evidence(payload: bytes, first: int, last: int) -> dict[str, int]:
    """PCM capture can straddle at most one 32-frame block at its boundaries."""
    if len(payload) % 8 or first < 0 or last <= first:
        raise ProofFailure("invalid measured PCM frame window")
    actual, expected = len(payload) // 8, (last - first) * BLOCK
    if actual < RATE or abs(actual - expected) > BLOCK:
        raise ProofFailure("PCM length differs from the measured DSP frame interval")
    return {
        "stereo_frames": actual,
        "expected_stereo_frames": expected,
        "boundary_slack_stereo_frames": BLOCK,
        "difference_stereo_frames": actual - expected,
    }


def parse_trigs(value: str) -> tuple[int, ...]:
    try:
        result = tuple(int(part) for part in value.split(","))
    except ValueError as exc:
        raise argparse.ArgumentTypeError("trigs must be comma-separated steps") from exc
    if (
        not result
        or result != tuple(sorted(set(result)))
        or not all(1 <= n <= 16 for n in result)
    ):
        raise argparse.ArgumentTypeError(
            "trigs must be distinct increasing steps in 1..16"
        )
    return result


def read_image(path: Path) -> Any:
    from tools.patchkit.pk_dkrt_boot import Img

    plan = json.loads(path.with_suffix(".plan.json").read_text())
    base = plan.get("base", {})
    if (base.get("device"), base.get("version"), base.get("syx_sha256")) != (
        "dt2",
        "1.17",
        DT2_117_SHA256,
    ):
        raise ProofFailure(f"{path}: plan is not based on stock DT2 OS 1.17")
    modules = ",".join(value.rsplit("@", 1)[0] for value in plan["modules"])
    return Img(path, plan, modules)


def capture(rig: Any, path: Path, duration_ms: int) -> dict[str, Any]:
    """Capture before PLAY and end before STOP; no release timing in the PCM."""
    # Match pk_dkrt_machine.Rig.note: inject halfway through an audio block,
    # where small differences in the CF hook's cost cannot cross a boundary.
    rig.emu.call("run_until", frames=rig.frames() + 20)
    rig.emu.step(44_000)
    rig.emu.call("pcm.tap", action="capture", path=str(path))
    first = rig.frames()
    try:
        rig.s.do_tap("PLAY")
        rig.s.run_ms(duration_ms)
    finally:
        rig.emu.call("pcm.tap", action="capture_stop")
    last = rig.frames()
    rig.s.do_tap("STOP")
    rig.s.run_ms(300)
    rig.s.health("running sequencer")
    payload = pcm_payload(path.read_bytes())
    timing = pcm_frame_evidence(payload, first, last)
    stats = sample_stats(payload)
    if not stats["finite"]:
        raise ProofFailure("PCM contains a nonfinite value")
    return {
        "path": str(path),
        "first_frame": first,
        "last_frame": last,
        "pcm": stats,
        "timing": timing,
    }


def run_image(
    env: Any,
    image: Any,
    args: argparse.Namespace,
    label: str,
    run_tag: str,
    result: dict[str, Any],
) -> None:
    from tools.patchkit import pk_dkrt_machine as M

    log = args.work / f"{run_tag}-{label}.dspi.bin"
    result.update(
        {
            "dspi_log": str(log),
            "log_origin_frame": 0,
            "runtime_counter": "status.dsp_frames = live worker.frames_sent (not restored guest state)",
            "windows": {},
        }
    )
    extra_env = {"DIGI_INTC_FORCE": "1", "DIGI_DSPI_LOG": str(log)}
    watch = args.work / f"{run_tag}-{label}.dspwatch.csv"
    provider_address, fx_counter = 0, 0
    if label == "hook":
        provider_address = M.sym(
            image.plan, "michael/pattern-position", "_pattern_position_info"
        )
        fx_counter = M.sym(image.plan, "core/fx", "_fx_blk")
        extra_env.update(
            {
                "DIGI_DSP_WATCH": f"0x{provider_address:x}:20,0x{fx_counter:x}",
                "DIGI_DSP_WATCH_OUT": str(watch),
            }
        )
        result["provider_address"] = provider_address
        result["dsp_watch"] = str(watch)
    with env.boot(
        image, args.card, tag=f"{run_tag}-{label}", extra_env=extra_env
    ) as rig:
        result["runtime_frames_after_boot_setup"] = rig.frames()
        boot = rig.boot_info()
        result["boot"] = boot
        if boot["safe"] or boot["results"] != [0]:
            raise ProofFailure(
                "the private card must load exactly test/sine without safe mode"
            )
        rig.s.do_tap("STOP")
        rig.s.run_ms(300)
        sine_type = rig.dyn_type(SINE, "sine")
        rig.pick(sine_type, 7)
        if rig.type_now() != sine_type:
            raise ProofFailure("track 1 did not select the sine machine")
        result["sine_type"] = sine_type
        if label == "baseline":
            rig.s.run_ms(2200)
            result["windows"]["no-trig"] = capture(
                rig, args.work / f"{run_tag}-no-trig.wav", 2600
            )
        rig.s.do_tap("RECORD")
        rig.s.run_ms(300)
        for step in args.trigs:
            rig.s.do_tap(f"TRIG {step}")
            rig.s.run_ms(100)
        rig.s.do_tap("RECORD")
        rig.s.run_ms(2200)
        result["windows"]["sequence"] = capture(
            rig, args.work / f"{run_tag}-{label}.wav", args.duration_ms
        )
        result["final_frames"] = rig.frames()
        result["session_checks"] = [asdict(check) for check in rig.s.checks]
        if not all(check.ok for check in rig.s.checks):
            raise ProofFailure("the emulator session reported a failed check")
    # Closing the daemon flushes the diagnostic streams.
    frames = list(read_log(log))
    for name, window in result["windows"].items():
        rows, offset = window_frames(
            frames, window["first_frame"], window["last_frame"], result["final_frames"]
        )
        events = note_ons(rows)
        window["log_frame_offset"] = offset
        window["note_ons"] = [asdict(event) for event in events]
        if name == "no-trig":
            if events:
                raise ProofFailure("no-trig negative control produced track-1 note-ons")
            window["positive_oracle_rejected"] = False
            try:
                sequence_evidence(events, args.trigs)
            except ProofFailure:
                window["positive_oracle_rejected"] = True
            if not window["positive_oracle_rejected"]:
                raise ProofFailure(
                    "no-trig negative control was accepted as a sequence"
                )
        else:
            window["sequence"] = sequence_evidence(events, args.trigs)
            if label == "hook":
                window["position"] = position_evidence(rows, events, args.trigs)
                device = tomllib.loads((ROOT / "devices/digitakt-ii.toml").read_text())
                dsp = next(
                    item["dsp"]
                    for item in device["firmware"]
                    if item["version"] == "1.17"
                )
                window["receiver"] = receiver_evidence(
                    rows,
                    watch,
                    provider_address,
                    fx_counter,
                    (dsp["idle_start"], dsp["idle_end"]),
                )
    if label == "hook":
        stopped, _ = window_frames(
            frames,
            result["final_frames"] - 100,
            result["final_frames"],
            result["final_frames"],
        )
        records = [position_words(frame) for frame in stopped]
        if any(words[5] & 2 for words in records):
            raise ProofFailure("pattern-position still reports running after STOP")
        if any(words[5] & 1 for words in records):
            raise ProofFailure("pattern-position still reports valid after STOP")
        result["position_stopped"] = {
            "frames": len(records),
            "last_record": records[-1],
        }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--hook", type=Path, required=True)
    parser.add_argument("--card", type=Path, required=True)
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--trigs", type=parse_trigs, default=(1, 5, 9, 13))
    parser.add_argument("--duration-ms", type=int, default=5000)
    parser.add_argument("--timeout", type=int, default=1200)
    args = parser.parse_args(argv)
    if os.name != "posix":
        parser.error("run the bounded firmware proof in Linux/WSL")
    if args.timeout < 1 or not 4000 <= args.duration_ms <= 12000:
        parser.error("timeout must be positive; duration must be 4000..12000 ms")
    for field in ("baseline", "hook", "card", "work"):
        setattr(args, field, getattr(args, field).resolve())
    if not args.work.is_relative_to((ROOT / "out").resolve()):
        parser.error("--work must be under the ignored out/ directory")
    args.work.mkdir(parents=True, exist_ok=True)
    run_tag = "position-" + uuid.uuid4().hex
    started = time.monotonic()
    report: dict[str, Any] = {
        "schema": 1,
        "status": "running",
        "run_tag": run_tag,
        "image": "dt2-1.17",
        "trigs": args.trigs,
        "duration_ms": args.duration_ms,
        "execution": {
            "DIGI_INTC_FORCE": "1",
            **{
                key: os.environ.get(key)
                for key in (
                    "DIGI_EMUD_DT2",
                    "DIGI_PK_READY_DT2_117",
                    "SHARC_FAST_AUTO",
                    "SHARC_FAST_REGIONS",
                    "SHARC_BLOCKTRANS",
                )
            },
        },
        "scope": {
            "memory_pokes": False,
            "hardware_tested": False,
            "hook_record_verified": False,
            "arbitrary_step_boundary_alignment_verified": False,
            "sample_accurate_capture_verified": False,
            "transport": "running",
        },
        "runs": {},
    }
    try:
        with deadline(args.timeout):
            from tools.patchkit import pk_dkrt_boot as B
            from tools.patchkit import pk_harness as H

            binary = Path(os.environ.get("DIGI_EMUD_DT2", ""))
            if not binary.is_file():
                raise ProofFailure(
                    "DIGI_EMUD_DT2 must name an existing daemon; implicit builds are disabled"
                )
            baseline, hook = read_image(args.baseline), read_image(args.hook)
            if not args.card.is_file():
                raise ProofFailure("the prepared private card is missing")
            # Only boot helpers are used; no compiler/tool constructor is needed.
            env = B.Env("dt2-1.17", args.work, tools=object())
            stock, ready = H.firmware_inputs(H.Cell("dt2", "1.17", []))
            source = json.loads((ready / "SOURCE.json").read_text())
            if sha256(stock.read_bytes()) != DT2_117_SHA256:
                raise ProofFailure("firmware base is not stock DT2 OS 1.17")
            report["inputs"] = {
                "stock_sha256": DT2_117_SHA256,
                "baseline_sha256": sha256(args.baseline.read_bytes()),
                "hook_sha256": sha256(args.hook.read_bytes()),
                "baseline_plan_sha256": sha256(
                    args.baseline.with_suffix(".plan.json").read_bytes()
                ),
                "hook_plan_sha256": sha256(
                    args.hook.with_suffix(".plan.json").read_bytes()
                ),
                "daemon_sha256": sha256(binary.read_bytes()),
                "ready_source": source,
                "card": str(args.card),
                "card_logical_bytes": args.card.stat().st_size,
                "baseline_modules": baseline.modules,
                "hook_modules": hook.modules,
            }
            for label, image in (("baseline", baseline), ("hook", hook)):
                result: dict[str, Any] = {}
                report["runs"][label] = result
                run_image(env, image, args, label, run_tag, result)
            a, b = (
                report["runs"][label]["windows"]["sequence"]
                for label in ("baseline", "hook")
            )
            report["note_comparison"] = compare_events(
                [NoteOn(**event) for event in a["note_ons"]],
                [NoteOn(**event) for event in b["note_ons"]],
            )
            report["pcm_comparison"] = compare_pcm(
                pcm_payload(Path(a["path"]).read_bytes()),
                pcm_payload(Path(b["path"]).read_bytes()),
            )
            report["scope"]["hook_record_verified"] = True
            report["scope"]["dsp_receiver_after_render_verified"] = True
            report["scope"]["dsp_receiver_before_fx_verified"] = False
            report["status"] = "passed"
    except Exception as exc:
        report["status"] = "failed"
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        report["wall_seconds"] = round(time.monotonic() - started, 3)
        content = json.dumps(report, indent=2, allow_nan=False) + "\n"
        path = args.work / "report.json"
        path.write_text(content)
        (args.work / f"{run_tag}.report.json").write_text(content)
        print(
            json.dumps(
                {
                    "status": report["status"],
                    "report": str(path),
                    "error": report.get("error"),
                }
            ),
            flush=True,
        )
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
