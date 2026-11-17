"""Isolated ColdFire model, not a firmware load or hardware proof."""

import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest

from tools.patchkit import patchkit

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
FIXTURE = ROOT / "tests/regression/fixtures/canopy-detach"
ADAPTER = ROOT / "modules/core/adapters/instrument-play/src/cf"


def test_detach_model_full_and_partial_overwrite_on_coldfire(tmp_path):
    runner = Path(
        os.environ.get(
            "DIGIKIT_CF_SCRIPT",
            ROOT / "out/native/target/nogen/release/examples/cf_script",
        )
    )
    if not runner.exists():
        pytest.skip("cf_script must be built explicitly")
    assert (
        hashlib.sha256(FW.read_bytes()).hexdigest()
        == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    linked = patchkit.cf_link(
        [
            FIXTURE / "detach.c",
            FIXTURE / "cf-probe.c",
            ROOT / "tests/regression/fixtures/canopy-two-phase/cf-probe.s",
            ADAPTER / "preset.c",
            ADAPTER / "sound-codec.c",
            ADAPTER / "sound-pool.c",
        ],
        0x47E00000,
        {"memset": 0x401363D0, "memcpy": 0x40136388},
    )
    ops = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        },
        {"op": "call", "pc": linked.symbols["canopy_detach_cf_run"], "max": 1000000},
    ]
    script = tmp_path / "detach.json"
    script.write_text(json.dumps(ops))
    proc = subprocess.run(
        [str(runner), str(FW), str(script)], text=True, capture_output=True, timeout=90
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout[-3000:]
    row = next(
        json.loads(line)
        for line in proc.stdout.splitlines()
        if line.startswith("{") and json.loads(line).get("op") == "call"
    )
    assert row["ok"] and row["d0"] == 0, row
    print("ColdFire whole-model instruction count:", row["insns"])
