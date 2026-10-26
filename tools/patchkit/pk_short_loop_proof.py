"""Bounded, one-boot DT2 1.17 firmware proof of michael/short-loop.

Run in the Linux emulator environment, with prepared tools and ready inputs::

    python -m tools.patchkit.pk_short_loop_proof --work out/runs/short-loop-firmware \
        --patchkit PATH --plusdrive PATH --cf-script PATH --timeout 900

The existing ``DIGI_EMUD_DT2`` and ``DIGI_PK_READY_DT2_117`` overrides select the
daemon and ready inputs. This tool never builds a compiler or an emulator. It
patches the stock image with existing core objects, packs the two modules, makes
a private card, and boots once. Generated files default to ``out/``. On a host
where that filesystem cannot make sparse files, ``--card-directory`` can put
the disposable card on a sparse-capable scratch filesystem.

The actual firmware receives encoder events and PTN/TRIG presses. Test/sine is
auditioned on A01; A02 is deliberately silent. Transport stays stopped: this
proves a real pattern/kit replacement, not sequencer-boundary synchronization.
No state, parameter, or kit memory is poked. No save, restart, or hardware access
is performed. The JSON distinguishes immutable capture storage from metadata
that must advance, and records the unprepared-kit FADE reset before testing the
matching-controls workaround.
"""

from __future__ import annotations

import argparse
import contextlib
import csv
import hashlib
import json
import math
import os
import signal
import struct
import time
import tomllib
import uuid
from collections.abc import Iterator
from dataclasses import asdict, dataclass
from pathlib import Path
from typing import Any

from tools.patchkit.short_loop_ref import (
    BLOCK,
    FRAMES,
    FROZEN,
    META_WORDS,
    STATE_BYTES,
    start_frame,
)

ROOT = Path(__file__).resolve().parents[2]
MODULE = "michael/short-loop"
SINE = "test/sine"
CORE = "core/runtime,core/dynreg,core/dyndsp,core/dynfx,core/fxpages"
RAW_MAX = 0x7F00
ONE = 0x3F800000


class ProofFailure(RuntimeError):
    """A failed prerequisite or measured invariant, retained in the report."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode_dsp_bytes(reply: dict[str, Any], length: int) -> bytes:
    """Reject unknown or incomplete memory; never turn missing evidence into zero."""
    encoded = reply.get("hex")
    if not isinstance(encoded, str):
        raise ProofFailure("DSP peek did not return hex bytes")
    if reply.get("unknown", 0) or "?" in encoded:
        raise ProofFailure("DSP peek contains unknown bytes")
    try:
        data = bytes.fromhex(encoded)
    except ValueError as exc:
        raise ProofFailure("DSP peek did not return valid hex bytes") from exc
    if len(data) != length:
        raise ProofFailure(f"DSP peek returned {len(data)} bytes, expected {length}")
    return data


def pcm_payload(wav: bytes) -> bytes:
    """Read a complete 48 kHz stereo float32 WAV without trimming damaged data."""
    if len(wav) < 12 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ProofFailure("PCM capture is not a RIFF/WAVE file")
    if int.from_bytes(wav[4:8], "little") + 8 != len(wav):
        raise ProofFailure("PCM capture has a truncated or inconsistent RIFF length")
    position, format_ok, payload = 12, False, None
    while position < len(wav):
        if position + 8 > len(wav):
            raise ProofFailure("PCM capture has a truncated chunk header")
        tag = wav[position : position + 4]
        size = int.from_bytes(wav[position + 4 : position + 8], "little")
        start, end = position + 8, position + 8 + size
        if end + (size & 1) > len(wav):
            raise ProofFailure("PCM capture has a truncated chunk body")
        if tag == b"fmt ":
            if size < 16:
                raise ProofFailure("PCM capture has a truncated format chunk")
            fmt, channels, rate, byte_rate, align, bits = struct.unpack_from(
                "<HHIIHH", wav, start
            )
            format_ok = (fmt, channels, rate, byte_rate, align, bits) == (
                3,
                2,
                48000,
                384000,
                8,
                32,
            )
        elif tag == b"data":
            if payload is not None:
                raise ProofFailure("PCM capture has more than one data chunk")
            payload = wav[start:end]
        position = end + (size & 1)
    if not format_ok or payload is None:
        raise ProofFailure("PCM capture must contain 48 kHz stereo float32 data")
    if len(payload) % 8:
        raise ProofFailure("PCM capture ends inside a stereo frame")
    return payload


def pcm_count_ok(samples: int, milliseconds: int) -> bool:
    """Allow frame-boundary/timing slack, but reject empty or shortened captures."""
    expected = milliseconds * 96  # 48 stereo frames per millisecond
    return (
        milliseconds > 0
        and samples > 0
        and samples % 2 == 0
        and expected * 9 // 10 <= samples <= expected * 11 // 10
    )


def sample_stats(data: bytes) -> dict[str, Any]:
    """Finite float32 signal statistics without changing the captured bytes."""
    values = struct.unpack(f"<{len(data) // 4}f", data)
    finite = all(math.isfinite(x) for x in values)
    return {
        "samples": len(values),
        "sha256": sha256(data),
        "finite": finite,
        "peak": max(map(abs, values), default=0.0) if finite else None,
        "rms": math.sqrt(sum(x * x for x in values) / len(values))
        if values and finite
        else None,
    }


def dsp_watch_evidence(
    path: Path, counter: int, snapshots: dict[str, Any]
) -> dict[str, Any]:
    """Match checkpoints to post-period PC observations from the DSP worker.

    A DSP period need not finish in idle if the firmware overruns its budget.
    Keep the exact phase oracle; these observations explain a failure instead
    of hiding it behind a one-block tolerance.
    """
    device = tomllib.loads((ROOT / "devices/digitakt-ii.toml").read_text())
    dsp = next(fw["dsp"] for fw in device["firmware"] if fw["version"] == "1.17")
    idle = (dsp["idle_start"], dsp["idle_end"])
    wanted = {snapshot["blocks"] for snapshot in snapshots.values()}
    matched: dict[int, dict[str, Any]] = {}
    rows, unknown = 0, 0
    with path.open(newline="") as stream:
        for row in csv.DictReader(stream):
            rows += 1
            raw = row[f"0x{counter:x}"].split("|", 1)[0]
            if raw == "?":
                unknown += 1
                continue
            block = int(raw, 0)
            if block in wanted:
                pc = int(row["pc"], 0)
                matched[block] = {
                    "frame": int(row["frame"]),
                    "pc": pc,
                    "busy": int(row["busy"]),
                    "pc_at_rx": int(row["pc_at_rx"], 0),
                    # DT2 1.17 idle_end is the closing jump to idle_head.
                    # Include it, matching the SHARC engine's idle-skip guard.
                    "at_idle": idle[0] <= pc <= idle[1],
                }
    checkpoints = {
        name: {
            "blocks": snapshot["blocks"],
            "observation": matched.get(snapshot["blocks"]),
        }
        for name, snapshot in snapshots.items()
    }
    return {
        "file": path.name,
        "rows": rows,
        "unknown_counter_rows": unknown,
        "idle_range": list(idle),
        "checkpoints": checkpoints,
        "all_checkpoints_at_idle": bool(checkpoints)
        and all(
            item["observation"] is not None and item["observation"]["at_idle"]
            for item in checkpoints.values()
        ),
    }


@dataclass(frozen=True)
class Snapshot:
    state_address: int
    effect_pc: int
    descriptor: int
    blocks: int
    metadata: tuple[int, ...]
    row: tuple[int, ...]
    left_sha256: str
    right_sha256: str
    metadata_sha256: str


def continuity(before: Snapshot, after: Snapshot) -> dict[str, bool]:
    """Storage stays fixed; phase advances by exactly one block per FX call.

    START, pending START, gain, and REC are deliberately not called immutable:
    the actual kit can change all four. The metadata hash is evidence, not an
    assertion that a running looper's entire metadata remains unchanged.
    """
    delta = (after.blocks - before.blocks) & 0xFFFFFFFF
    return {
        "same_instance": (
            before.state_address,
            before.effect_pc,
            before.descriptor,
        )
        == (after.state_address, after.effect_pc, after.descriptor),
        "left_unchanged": before.left_sha256 == after.left_sha256,
        "right_unchanged": before.right_sha256 == after.right_sha256,
        "still_frozen": before.metadata[0] == after.metadata[0] == FROZEN,
        "capture_length_unchanged": before.metadata[1] == after.metadata[1] == FRAMES,
        "reserved_unchanged": before.metadata[7:] == after.metadata[7:],
        "phase_continues": delta > 0
        and after.metadata[2] == (before.metadata[2] + delta * BLOCK) % FRAMES,
        "enabled_without_fault": after.row[3] == 1
        and after.row[14] == 0
        and after.row[17] == 0,
    }


@contextlib.contextmanager
def deadline(seconds: int) -> Iterator[None]:
    """Bound even a stuck subprocess/socket call; the daemon context cleans up."""
    if not hasattr(signal, "SIGALRM"):
        raise ProofFailure("Run this firmware proof in Linux/WSL (SIGALRM required)")

    def expired(_signum: int, _frame: Any) -> None:
        raise TimeoutError(f"firmware proof exceeded its {seconds}s wall limit")

    previous = signal.signal(signal.SIGALRM, expired)
    signal.alarm(seconds)
    try:
        yield
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)


def prepare(args: argparse.Namespace) -> tuple[Any, Any, Path]:
    from tools.patchkit import pk_dkrt_boot as B
    from tools.patchkit.pk_dkrt import Tools

    class PreparedTools(Tools):
        def __init__(self) -> None:
            # Bypass Tools' implicit cargo build: reuse the runner's binaries.
            self.patchkit = args.patchkit.resolve()
            self.plusdrive = args.plusdrive.resolve()
            self.script = args.cf_script.resolve()
            for path in (self.patchkit, self.plusdrive, self.script):
                if not path.is_file():
                    raise ProofFailure(f"prepared binary is missing: {path}")

    env = B.Env("dt2-1.17", args.work, PreparedTools())
    image = env.build_image(CORE, "short-loop")
    env.pack(SINE, "sine.dkm")
    env.pack(MODULE, "short-loop.dkm")
    selection = ["modules/sine.dkm", "modules/short-loop.dkm"]
    if args.card_directory is None:
        card = env.card(selection, tag="proof")
    else:
        # The card has a large logical address space but little written data.
        # WSL-mounted NTFS may allocate its holes; Linux scratch avoids that.
        args.card_directory.mkdir(parents=True, exist_ok=True)
        card = args.card_directory / f"short-loop-{uuid.uuid4().hex}.img"
        B.build_card(env.tools, env.folder, card, env.stock, env.device)
        record = B.make_record(selection)
        B.write_record_sectors(card, record, record, env.device)
    return env, image, card


class Proof:
    def __init__(self, rig: Any, work: Path, report: dict[str, Any]) -> None:
        from tools.patchkit.pk_digikit_fxpage import Kit, Shots, profile

        self.rig, self.work, self.report = rig, work, report
        self.cfg = profile("dt2")
        self.kit = Kit(rig, self.cfg)
        self.shots = Shots(work, "short-loop")
        self.snapshots: dict[str, Snapshot] = {}

    def check(self, name: str, ok: bool, detail: Any = None) -> None:
        self.report["checks"].append({"name": name, "ok": bool(ok), "detail": detail})
        print(f"{'ok' if ok else 'FAIL'} {name}", flush=True)
        if not ok:
            raise ProofFailure(name)

    def row(self) -> tuple[int, ...]:
        from tools.patchkit import pk_dkrt_machine as M
        from tools.patchkit.pk_digikit_fx import ROW_WORDS

        address = M.sym(self.rig.plan, "core/fx", "fx.rows")
        return struct.unpack(f"<{ROW_WORDS}I", self.dsp_bytes(address, ROW_WORDS * 4))

    def dsp_bytes(self, address: int, length: int) -> bytes:
        # The first peek synchronizes outstanding DSP frames. The daemon is
        # paused between jobs, so all chunks describe the same stopped state.
        out = bytearray()
        for offset in range(0, length, 4096):
            count = min(4096, length - offset)
            reply = self.rig.emu.dsp_peek(address + offset, count)
            out.extend(decode_dsp_bytes(reply, count))
        return bytes(out)

    def metadata(self) -> tuple[int, ...]:
        return struct.unpack(
            f"<{META_WORDS}I", self.dsp_bytes(self.row()[2], META_WORDS * 4)
        )

    def sound_address(self) -> int:
        # DT2 kit layout: docs/findings/02-machines-and-parameters.md,
        # "Sound array, track j". Track 1 Sound is embedded at kit + 0x34.
        # The separate Sound cache can be null during page changes; a fixed
        # boot-object address becomes stale after PTN/TRIG selection.
        kit = self.rig.emu.peek32(self.cfg["kit_ptr"])
        if not kit:
            raise ProofFailure("live kit pointer could not be read")
        return kit + 0x34

    def track_machine(self) -> int:
        # The machine type is the byte at Sound + 0xa2; ColdFire words are BE.
        address = self.sound_address() + 0xA2
        word = self.rig.emu.peek32(address & ~3)
        if word is None:
            raise ProofFailure("live track 1 machine type could not be read")
        return (word >> (8 * (3 - address % 4))) & 0xFF

    def controls(self) -> dict[str, Any]:
        from tools.patchkit.pk_digikit_fxpage import fxp_state

        return {
            "kit_address": self.rig.w(self.cfg["kit_ptr"]),
            "sound_address": self.sound_address(),
            "track_machine": self.track_machine(),
            "raw": [self.kit.raw(0, j) for j in (0, 1, 2, 7)],
            "dsp_words": [self.row()[j] for j in (4, 5, 6, 13)],
            "runtime": fxp_state(self.rig),
        }

    def snapshot(self, name: str) -> Snapshot:
        from tools.patchkit import pk_dkrt_machine as M

        row = self.row()
        data = self.dsp_bytes(row[2], STATE_BYTES)
        metadata = data[: META_WORDS * 4]
        left = data[META_WORDS * 4 : META_WORDS * 4 + FRAMES * 4]
        right = data[META_WORDS * 4 + FRAMES * 4 :]
        counter = M.sym(self.rig.plan, "core/fx", "_fx_blk")
        snap = Snapshot(
            row[2],
            row[0],
            row[1],
            struct.unpack("<I", self.dsp_bytes(counter, 4))[0],
            struct.unpack(f"<{META_WORDS}I", metadata),
            row,
            sha256(left),
            sha256(right),
            sha256(metadata),
        )
        self.snapshots[name] = snap
        self.report["snapshots"][name] = {
            **asdict(snap),
            "left": sample_stats(left),
            "right": sample_stats(right),
            "controls": self.controls(),
        }
        return snap

    def continued(self, first: str, last: str) -> None:
        result = continuity(self.snapshots[first], self.snapshots[last])
        self.check(
            f"capture and phase survive {first} -> {last}", all(result.values()), result
        )

    def tap(self, key: Any, settle_ms: int = 100) -> None:
        self.rig.s.do_tap(key, 20)
        self.rig.s.run_ms(settle_ms)

    def page(self) -> None:
        from tools.patchkit.pk_digikit_fxpage import open_send_fx, page_down

        open_send_fx(self.rig)
        for _ in range(5):
            self.tap("UP", 80)
        for _ in range(3):
            page_down(self.rig)

    def endpoint(self, encoder: str, index: int, high: bool) -> None:
        target = RAW_MAX if high else 0
        # Acceleration and input delivery can make one fixed burst fall short.
        # Read back every burst; retain an exact endpoint and a finite bound.
        for events in (80, 40, 40, 40):
            if self.kit.raw(0, index) == target:
                break
            self.rig.s.turn_burst(encoder, 8 if high else -8, events)
        self.rig.s.run_ms(100)
        self.check(
            f"{encoder} reaches {'maximum' if high else 'minimum'}",
            self.kit.raw(0, index) == target,
            self.kit.raw(0, index),
        )

    def switch(self, pattern: int, name: str) -> None:
        """Use the firmware UI; observe the actual kit pointer, not a mock source."""
        from tools.patchkit.pk_digikit_fxpage import fxp_state

        old = self.rig.w(self.cfg["kit_ptr"])
        self.tap("NO", 300)
        self.tap("PTN", 800)
        self.tap(f"TRIG {pattern}", 100)
        for _ in range(80):
            current = self.rig.w(self.cfg["kit_ptr"])
            if current != old and fxp_state(self.rig)["kit"] == current:
                break
            self.rig.s.run_ms(100)
        self.rig.s.run_ms(100)
        self.check(
            f"{name}: PTN/TRIG {pattern} selected another firmware kit",
            self.rig.w(self.cfg["kit_ptr"]) != old
            and fxp_state(self.rig)["kit"] == self.rig.w(self.cfg["kit_ptr"]),
            self.controls(),
        )
        self.snapshot(name)

    def pcm(self, name: str, milliseconds: int = 500) -> dict[str, Any]:
        path = self.work / f"{name}.wav"
        self.rig.emu.call("pcm.tap", action="capture", path=str(path))
        try:
            self.rig.s.run_ms(milliseconds)
        finally:
            self.rig.emu.call("pcm.tap", action="capture_stop")
        result = sample_stats(pcm_payload(path.read_bytes()))
        self.report["pcm"][name] = {"file": path.name, **result}
        self.check(
            f"{name}: PCM capture has a complete plausible duration",
            pcm_count_ok(result["samples"], milliseconds),
            {"samples": result["samples"], "requested_ms": milliseconds},
        )
        return result

    def run(self) -> None:
        from tools.patchkit.pk_digikit_fxpage import bits, knob_float
        from tools.patchkit.pk_harness import button

        boot = self.rig.boot_info()
        self.report["boot"] = boot
        self.check(
            "sine and short-loop load without safe mode",
            boot["results"] == [0, 0] and boot["safe"] == 0,
            boot,
        )
        row = self.row()
        desc = struct.unpack("<7I", self.dsp_bytes(row[1], 28))
        self.check(
            "loaded descriptor matches the 250 ms stereo proof",
            desc[:4] == (0x444B4658, 1, 3, STATE_BYTES),
            list(desc),
        )
        self.tap("STOP", 300)
        self.report["scope"]["transport_setup"] = (
            "explicit STOP before audition; no PLAY inputs"
        )
        sine_type = self.rig.dyn_type(SINE, "sine")
        self.rig.pick(sine_type, 7)
        self.page()
        self.check(
            "live kit Sound reports the selected sine machine",
            self.track_machine() == sine_type,
            {"expected": sine_type, "actual": self.track_machine()},
        )
        self.check(
            "source starts with REC/START/FADE zero and MIX maximum",
            self.controls()["raw"] == [0, 0, 0, RAW_MAX],
            self.controls(),
        )

        # Trigger while the FX page is open; the note lasts two seconds. Even
        # all bounded REC retries leave room for the 250 ms capture.
        self.rig.emu.button(button("TRIG 1"), True)
        try:
            self.rig.s.run_ms(100)
            self.endpoint("ENC A", 0, True)
            for _ in range(30):
                if self.metadata()[0] == FROZEN:
                    break
                self.rig.s.run_ms(50)
        finally:
            self.rig.emu.button(button("TRIG 1"), False)
        frozen = self.snapshot("captured")
        self.check(
            "exactly 12000 stereo frames freeze",
            frozen.metadata[:2] == (FROZEN, FRAMES),
            list(frozen.metadata),
        )
        for channel in ("left", "right"):
            stat = self.report["snapshots"]["captured"][channel]
            self.check(
                f"captured {channel} contains finite audible input",
                stat["finite"] and stat["peak"] > 0.001,
                stat,
            )

        self.endpoint("ENC C", 2, True)
        source = self.pcm("source-loop")
        self.check(
            "FADE maximum produces audible loop PCM",
            source["finite"] and source["peak"] > 0.001,
            source,
        )
        raw_start = self.kit.set_raw("ENC B", 0, 1, 0x2000)
        self.rig.s.run_ms(300)  # at least one full loop for the deferred latch
        selected_start = start_frame(knob_float(0.0, 1.0, raw_start))
        self.endpoint("ENC A", 0, False)
        self.shots.take(self.rig, "source-frozen")
        ready = self.snapshot("source-ready")
        self.check(
            "START encoder reaches DSP and latches a nonzero loop offset",
            ready.metadata[3] == ready.metadata[4] == selected_start
            and selected_start > 0
            and ready.row[5] == bits(knob_float(0.0, 1.0, raw_start)),
            {
                "raw": raw_start,
                "frame": selected_start,
                "metadata": list(ready.metadata),
            },
        )
        self.check(
            "FADE reaches exactly the loop endpoint",
            ready.metadata[5] == ONE and ready.row[6] == ONE and ready.row[13] == ONE,
        )
        self.continued("captured", "source-ready")

        self.switch(2, "unprepared-A02")
        self.continued("source-ready", "unprepared-A02")
        self.check(
            "an unprepared target recalls kit-local REC/START/FADE defaults",
            self.controls()["raw"] == [0, 0, 0, RAW_MAX],
            self.controls(),
        )
        self.check(
            "target has a different live machine",
            self.track_machine() != sine_type,
            {"source_type": sine_type, "target_type": self.track_machine()},
        )
        self.rig.s.run_ms(1200)
        live = self.pcm("target-live")
        self.check(
            "target live output is quiet after the old note expires",
            live["finite"] and live["peak"] < source["peak"] * 0.01,
            {"live": live, "loop": source},
        )

        # Prepare this destination through its real encoders, then leave and
        # revisit it. Matching kit controls are a workaround, not global knobs.
        self.page()
        target_start = self.kit.set_raw("ENC B", 0, 1, raw_start, tol=0)
        self.endpoint("ENC C", 2, True)
        self.snapshot("prepared-A02")
        self.continued("unprepared-A02", "prepared-A02")
        self.switch(1, "returned-A01")
        self.continued("prepared-A02", "returned-A01")
        self.check(
            "A01 recalls its own prepared controls",
            self.controls()["raw"] == [0, raw_start, RAW_MAX, RAW_MAX],
            self.controls(),
        )
        self.switch(2, "prepared-transition-A02")
        self.continued("returned-A01", "prepared-transition-A02")
        final = self.snapshots["prepared-transition-A02"]
        self.check(
            "prepared A02 recalls matching START/FADE/MIX",
            self.controls()["raw"] == [0, target_start, RAW_MAX, RAW_MAX]
            and final.metadata[3] == selected_start
            and final.metadata[5] == ONE
            and final.row[13] == ONE,
            self.controls(),
        )
        loop = self.pcm("target-loop")
        self.check(
            "frozen source remains audible over the silent target",
            loop["finite"]
            and loop["peak"] > 0.001
            and abs(loop["rms"] / source["rms"] - 1.0) < 0.02,
            {"source": source, "target": loop},
        )
        self.page()
        self.endpoint("ENC C", 2, False)
        faded = self.pcm("target-faded-live")
        self.check(
            "FADE encoder returns to the new live output",
            faded["finite"] and faded["peak"] < source["peak"] * 0.01,
            faded,
        )
        self.snapshot("finished")
        self.continued("captured", "finished")
        self.shots.take(self.rig, "target-live")
        self.rig.s.health("short-loop proof complete")
        self.report["shots"] = self.shots.hashes


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--work", type=Path, required=True)
    parser.add_argument("--patchkit", type=Path, required=True)
    parser.add_argument("--plusdrive", type=Path, required=True)
    parser.add_argument("--cf-script", type=Path, required=True)
    parser.add_argument(
        "--card-directory",
        type=Path,
        help="optional sparse-capable scratch directory for the disposable card",
    )
    parser.add_argument(
        "--timeout",
        type=int,
        default=900,
        help="total wall-clock seconds, including fixture generation",
    )
    args = parser.parse_args(argv)
    args.work = args.work.resolve()
    if args.card_directory is not None:
        args.card_directory = args.card_directory.resolve()
    if args.timeout < 1:
        parser.error("--timeout must be positive")
    if not args.work.is_relative_to((ROOT / "out").resolve()):
        parser.error("--work must be under the ignored out/ directory")
    args.work.mkdir(parents=True, exist_ok=True)
    report: dict[str, Any] = {
        "schema": 1,
        "status": "running",
        "image": "dt2-1.17",
        "module": MODULE,
        "execution": {
            name: os.environ.get(name)
            for name in (
                "SHARC_FAST_AUTO",
                "SHARC_FAST_REGIONS",
                "SHARC_BLOCKTRANS",
                "DIGI_EMUD_DT2",
            )
        },
        "checks": [],
        "snapshots": {},
        "pcm": {},
        "scope": {
            "actual_firmware": True,
            "one_boot": True,
            "transport": "stopped",
            "source": "test/sine audition on A01",
            "destination": "silent A02",
            "memory_pokes": False,
            "hardware_tested": False,
            "not_proved": [
                "running-sequencer timing",
                "quantized capture",
                "long-loop memory",
                "global controls",
                "transient-free cross-kit control delivery",
            ],
        },
    }
    started = time.monotonic()
    run_tag = f"short-loop-{uuid.uuid4().hex}"
    report["run_tag"] = run_tag
    dsp_watch_path = args.work / f"{run_tag}.dspwatch.csv"
    dsp_counter: int | None = None
    try:
        with deadline(args.timeout):
            from tools.patchkit import pk_dkrt_machine as M

            env, image, card = prepare(args)
            dsp_counter = M.sym(image.plan, "core/fx", "_fx_blk")
            report["inputs"] = {
                "stock_sha256": sha256(env.stock.read_bytes()),
                "patched_sha256": sha256(image.syx.read_bytes()),
                "module_sha256": sha256(
                    (env.folder / "digikit/modules/short-loop.dkm").read_bytes()
                ),
                "card": str(card),
                "modules": CORE,
            }
            with env.boot(
                image,
                card,
                tag=run_tag,
                watch=True,
                extra_env={
                    "DIGI_DSP_WATCH": f"0x{dsp_counter:x}",
                    "DIGI_DSP_WATCH_OUT": str(dsp_watch_path),
                },
            ) as rig:
                Proof(rig, args.work, report).run()
                report["session_checks"] = [asdict(check) for check in rig.s.checks]
                if not all(check.ok for check in rig.s.checks):
                    raise ProofFailure("the emulator session reported a failed check")
                report["store_sector_writes"] = rig.card_store_writes()
                report["write_watch"] = rig.watch_summaries()
                if (
                    report["write_watch"].get("emulators", 0) < 1
                    or report["write_watch"].get("write_events") != 0
                    or report["write_watch"].get("flush_hits") != 0
                ):
                    raise ProofFailure("missing or failed write-watch/flush summary")
            report["status"] = "passed"
    except Exception as exc:  # preserve bounded failures as reviewable evidence
        report["status"] = "failed"
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        if dsp_counter is not None and dsp_watch_path.is_file():
            try:
                report["dsp_watch"] = dsp_watch_evidence(
                    dsp_watch_path, dsp_counter, report["snapshots"]
                )
                if (
                    report["status"] == "passed"
                    and not report["dsp_watch"]["all_checkpoints_at_idle"]
                ):
                    report["status"] = "failed"
                    report["error"] = (
                        "DSP watch did not verify idle at every phase checkpoint"
                    )
            except (OSError, KeyError, ValueError) as exc:
                report["dsp_watch_error"] = f"{type(exc).__name__}: {exc}"
                if report["status"] == "passed":
                    report["status"] = "failed"
                    report["error"] = "DSP watch evidence could not be read"
        elif report["status"] == "passed":
            report["status"] = "failed"
            report["error"] = "DSP watch evidence is missing"
        report["wall_seconds"] = round(time.monotonic() - started, 3)
        destination = args.work / "report.json"
        contents = json.dumps(report, indent=2, allow_nan=False) + "\n"
        (args.work / f"{run_tag}.report.json").write_text(contents)
        destination.write_text(contents)
        print(
            json.dumps(
                {
                    "status": report["status"],
                    "report": str(destination),
                    "error": report.get("error"),
                }
            ),
            flush=True,
        )
    return 0 if report["status"] == "passed" else 1


if __name__ == "__main__":
    raise SystemExit(main())
