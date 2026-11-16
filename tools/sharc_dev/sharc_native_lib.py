#!/usr/bin/env python3
"""Build the firmware-free native SHARC library (crate packages/emulator/sharc).

    uv run python -m tools.sharc_dev.sharc_native_lib [--ensure]

Builds the cdylib ``sharc_native`` against the shared core in
``out/native/shared-core/gen`` (``mise run gen:core``) into
``out/native/shared-core/target``. The tests in tests/sharc/semantics and
tests/sharc/generation load it, and pk_sharc runs calls on it; no firmware is
needed. --ensure first regenerates the shared core when it is missing or its
CORE_FINGERPRINT differs from the sources (otherwise it is left alone, so cargo
rebuilds only what changed): the python-tests gate target runs it.
"""

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys

import tools

ROOT = pathlib.Path(__file__).resolve().parents[2]

from tools.emulator import digi_env  # noqa: E402

SHARED = ROOT / "out/native/shared-core"


def library_path() -> pathlib.Path:
    if sys.platform == "win32":  # cargo names a Windows cdylib without the lib prefix
        return SHARED / "target/release/sharc_native.dll"
    suffix = "dylib" if sys.platform == "darwin" else "so"
    return SHARED / "target/release" / f"libsharc_native.{suffix}"


def generated_current(gen: pathlib.Path) -> bool:
    """Whether GEN holds a shared core made from the current sources."""
    try:
        have = (gen / digi_env.CORE_FINGERPRINT_FILE).read_text().strip()
    except OSError:
        return False
    return have == digi_env.core_fingerprint()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument(
        "--ensure",
        action="store_true",
        help="regenerate the shared core first when it is missing or stale (as mise run gen:core)",
    )
    args = ap.parse_args(argv)
    gen = SHARED / "gen"
    if args.ensure and not generated_current(gen):
        print("sharc_native_lib: regenerating the shared core", file=sys.stderr)
        code = subprocess.run(
            [
                sys.executable,
                "-m",
                "tools.sharc_dev.sharc_transpile",
                "--strict",
                "--out",
                str(gen),
                "--work",
                str(SHARED / "work"),
            ],
            cwd=ROOT,
            env=tools.python_env(),
            check=False,
        ).returncode
        if code:
            return code
    if not gen.is_dir():
        print(
            f"sharc_native_lib: shared core is missing: {gen}\n"
            "run `mise run gen:core` first",
            file=sys.stderr,
        )
        return 1
    digi_env.check_generated(gen)
    env = digi_env.rust_env()
    env["SHARC_GEN_DIR"] = str(gen)
    command = [
        "rustup",
        "run",
        digi_env.TOOLCHAIN,
        "cargo",
        "build",
        "--release",
        "--locked",
        "--manifest-path",
        "packages/emulator/sharc/Cargo.toml",
        "--target-dir",
        str(SHARED / "target"),
    ]
    code = subprocess.run(command, cwd=ROOT, env=env, check=False).returncode
    if code:
        return code
    print(library_path())
    print("Tests find it automatically; SHARC_NATIVE_LIB overrides.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
