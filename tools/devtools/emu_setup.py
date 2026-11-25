"""Prepare a worktree for emulator device tests.

    mise run emu:setup [-- --device dt2,dn2] [--check]

A new worktree has none of the untracked inputs that device tests read, and some of them take minutes, a card
image or another lane's tools to make. Each step reuses what another worktree of the same clone already has
when it is the same thing, and makes it otherwise:

1. `out/refs` (the ADSP-2156x hardware reference that the SHARC core generator reads): copied.
2. `out/tools` (the toolchains): a link to another worktree's when missing.
3. The shared SHARC core (`gen:core`) when missing or stale, then the native library (`gen:native-lib`).
4. The ready snapshot of each device (its `[acceptance] ready_dir`): copied from another worktree when it
   belongs to the same `.syx`, else built (`emu:ready`). A snapshot rebuilt with today's tools differs from
   the one the stock goldens were recorded against, so a copy is preferred.
5. The generated audio core of each device (`out/native/<device>-audio/gen`): copied when its
   CORE_FINGERPRINT is the current one, else made (`perf:aot`; the DT2 one needs a card image, so it is only
   copied), and registered for the snapshot's DSP image (`out/native/gen-index.json`).
6. The DT2 sample folder of live sessions (`out/runs/patchkit/samples`).
7. The core libraries and the daemons (`verify_build cores`, `verify_build daemons`).

`--check` reports what is missing or stale and changes nothing. The exit status is 1 when something is still
missing afterwards.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from collections.abc import Callable
from pathlib import Path

from tools.emulator import card_check, digi_env
from tools.firmware import config
from tools.patchkit import pk_device, pk_harness

ROOT = Path(__file__).resolve().parents[2]
REFS = Path("out/refs")
REFS_FILE = REFS / "adsp-2156x-hwr" / "all.txt"
TOOLS = Path("out/tools")
SHARED_CORE = Path("out/native/shared-core/gen")
SAMPLES = Path("out/runs/patchkit/samples")


def worktrees() -> list[Path]:
    """The other worktrees of this clone, as `git worktree list` names them, the ones that exist."""
    out = subprocess.run(
        ["git", "worktree", "list", "--porcelain"],
        cwd=ROOT,
        capture_output=True,
        text=True,
        check=True,
    ).stdout
    found = []
    for line in out.splitlines():
        if line.startswith("worktree "):
            path = Path(line[len("worktree ") :])
            if path.resolve() != ROOT.resolve() and path.is_dir():
                found.append(path)
    return found


def source_of(rel: Path, fits: Callable[[Path], bool]) -> Path | None:
    """`rel` in the first other worktree where it exists and `fits` accepts it."""
    for tree in worktrees():
        candidate = tree / rel
        try:
            if candidate.exists() and fits(candidate):
                return candidate
        except OSError:
            continue
    return None


def copy_dir(src: Path, dst: Path) -> None:
    """Replace `dst` with a copy of `src` (links are kept as links)."""
    if dst.is_symlink() or dst.is_file():
        dst.unlink()
    elif dst.exists():
        shutil.rmtree(dst)
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(src, dst, symlinks=True)


def run(*args: str) -> bool:
    """Run a module of this repository with this interpreter; True when it succeeded."""
    print("  $", " ".join(args), flush=True)
    return subprocess.run([sys.executable, *args], cwd=ROOT).returncode == 0


def fingerprint_of(gen: Path) -> str | None:
    try:
        return (gen / digi_env.CORE_FINGERPRINT_FILE).read_text().strip()
    except OSError:
        return None


def ready_source(ready: Path) -> dict[str, object] | None:
    try:
        source = json.loads((ready / "SOURCE.json").read_text())
    except (OSError, ValueError):
        return None
    return source if isinstance(source, dict) else None


class Setup:
    def __init__(self, check: bool) -> None:
        self.check = check
        self.missing: list[str] = []

    def note(self, what: str, state: str) -> None:
        print(f"{what}: {state}", flush=True)

    def lack(self, what: str, how: str) -> None:
        self.missing.append(what)
        self.note(what, f"MISSING ({how})")

    def refs(self) -> None:
        if (ROOT / REFS_FILE).is_file():
            return self.note("out/refs", "present")
        src = source_of(REFS, lambda p: (p.parent.parent / REFS_FILE).is_file())
        if src is None:
            return self.lack("out/refs", "no worktree of this clone has it")
        if self.check:
            return self.lack("out/refs", f"copy from {src}")
        copy_dir(src, ROOT / REFS)
        self.note("out/refs", f"copied from {src}")

    def tools(self) -> None:
        if (ROOT / TOOLS).exists():
            return self.note("out/tools", "present")
        src = source_of(TOOLS, lambda p: p.is_dir())
        if src is None:
            return self.lack("out/tools", "no worktree of this clone has it")
        if self.check:
            return self.lack("out/tools", f"link to {src.resolve()}")
        (ROOT / TOOLS).parent.mkdir(parents=True, exist_ok=True)
        os.symlink(src.resolve(), ROOT / TOOLS)
        self.note("out/tools", f"linked to {src.resolve()}")

    def shared_core(self) -> None:
        current = digi_env.core_fingerprint(ROOT)
        if fingerprint_of(ROOT / SHARED_CORE) == current:
            self.note("shared SHARC core", "current")
        elif self.check:
            return self.lack("shared SHARC core", "mise run gen:core")
        elif not run(
            "-m",
            "tools.sharc_dev.sharc_transpile",
            "--strict",
            "--out",
            str(SHARED_CORE),
            "--work",
            "out/native/shared-core/work",
        ):
            return self.lack("shared SHARC core", "gen:core failed")
        if not self.check and not run("-m", "tools.sharc_dev.sharc_native_lib"):
            self.lack("native SHARC library", "gen:native-lib failed")

    def ready(self, device: str) -> None:
        what = f"{device} ready snapshot"
        version = pk_device.acceptance_firmware()[device]
        spec = pk_harness.FIRMWARE[(device, version)]
        syx = config.stock(spec["syx"])
        if not syx.is_file():
            return self.lack(what, f"{syx} is missing")
        ready = ROOT / spec["ready"]
        paths = digi_env.ready_inputs(device=device)
        source = ready_source(ready)
        if source and all(p.is_file() for k, p in paths.items() if k != "firmware"):
            return self.note(what, f"present ({ready.relative_to(ROOT)})")
        with syx.open("rb") as f:
            want = hashlib.file_digest(f, "sha256").hexdigest()

        def same_syx(p: Path) -> bool:
            s = ready_source(p)
            return (
                bool(s)
                and s.get("syx_sha256") == want
                and (p / "digi-audio-m5.snap").is_file()
            )

        src = source_of(Path(spec["ready"]), same_syx)
        if self.check:
            how = (
                f"copy from {src}"
                if src
                else f"mise run emu:ready -- {device} --syx {syx}"
            )
            return self.lack(what, how)
        if src is not None:
            copy_dir(src, ready)
            return self.note(what, f"copied from {src}")
        print(
            f"{what}: building (the stock goldens may differ from a rebuilt snapshot)"
        )
        if not run(
            "-m",
            "tools.emulator.ready",
            device,
            "--syx",
            str(syx),
            "--out",
            str(ready),
            "--force",
        ):
            self.lack(what, "emu:ready failed")

    def audio_core(self, device: str) -> None:
        what = f"{device} audio core"
        gen = ROOT / "out" / "native" / f"{device}-audio" / "gen"
        current = digi_env.core_fingerprint(ROOT)
        if fingerprint_of(gen) != current:
            rel = gen.relative_to(ROOT)
            src = source_of(rel, lambda p: fingerprint_of(p) == current)
            if self.check:
                how = f"copy from {src}" if src else "mise run perf:aot"
                return self.lack(what, how)
            if src is not None:
                copy_dir(src, gen)
                self.note(what, f"copied from {src}")
            elif device == "dn2":
                if not run("-m", "tools.sharc_dev.aot_gen", "dn2"):
                    return self.lack(what, "perf:aot failed")
            else:
                return self.lack(
                    what,
                    f"no worktree has a current one: mise run perf:aot -- {device} --card-image IMG",
                )
        else:
            self.note(what, "current")
        image = digi_env.ready_inputs(device=device)["DSP image"]
        if not image.is_file():
            return self.lack(f"{device} core registration", "no ready snapshot")
        registered = digi_env.gen_for_image(image)
        if registered is not None and registered.resolve() == gen.resolve():
            return self.note(f"{device} core registration", "present")
        if self.check:
            return self.lack(f"{device} core registration", "out/native/gen-index.json")
        digi_env.register_gen(image, gen)
        self.note(f"{device} core registration", "written")

    def samples(self) -> None:
        if (ROOT / SAMPLES).is_dir():
            return self.note("sample folder", "present")
        if self.check:
            return self.lack("sample folder", str(SAMPLES))
        card_check.make_samples(ROOT / SAMPLES)
        self.note("sample folder", "written")

    def builds(self, devices: list[str]) -> None:
        if self.check:
            return
        listed = ",".join(devices)
        if not run("-m", "tools.devtools.verify_build", "cores", "--device", listed):
            self.lack("core libraries", "verify_build cores failed")
        if not run(
            "-m",
            "tools.devtools.verify_build",
            "daemons",
            "--device",
            listed,
            "--env-out",
            "dist/daemons.json",
        ):
            self.lack("daemons", "verify_build daemons failed")


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=(__doc__ or "").splitlines()[0])
    ap.add_argument(
        "--device",
        default="",
        help="comma-separated devices with a DSP (default: dt2 and dn2 when their firmware is present)",
    )
    ap.add_argument("--check", action="store_true", help="report only; change nothing")
    args = ap.parse_args(argv)
    devices = [d for d in args.device.split(",") if d]
    if not devices:
        versions = pk_device.acceptance_firmware()
        devices = [
            d
            for d in ("dt2", "dn2")
            if d in versions
            and config.stock(pk_harness.FIRMWARE[(d, versions[d])]["syx"]).is_file()
        ]
    s = Setup(args.check)
    s.refs()
    s.tools()
    s.shared_core()
    for device in devices:
        s.ready(device)
        s.audio_core(device)
    s.samples()
    if not s.missing:
        s.builds(devices)
    if s.missing:
        print(f"missing: {', '.join(s.missing)}", file=sys.stderr)
        return 1
    print(f"emulator inputs ready for {', '.join(devices)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
