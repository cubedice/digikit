"""Pinned DTII memcpy instruction budget for an isolated copy-guard experiment.

ColdFire component execution is not a hardware latency or lifecycle proof.
"""
import hashlib
import json
import os
import subprocess
from pathlib import Path

import pytest

from tools.patchkit import patchkit, port_resolve

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
FIXTURE = ROOT / "tests/regression/fixtures/canopy-copy-guard"
ADAPTER = ROOT / "modules/core/adapters/instrument-play/src/cf"


def test_copy_guard_uses_pinned_stock_memcpy_with_bounded_guest_instructions(tmp_path):
    runner = Path(os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "out/native/target/nogen/release/examples/cf_script"))
    if not runner.exists():
        pytest.skip("cf_script must be built explicitly")
    assert hashlib.sha256(FW.read_bytes()).hexdigest() == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    main = port_resolve.sections_of(FW)[3]
    assert main[0x40136388 - 0x40000400:0x40136390 - 0x40000400].hex() == "226f0004206f0008"
    linked = patchkit.cf_link(
        [FIXTURE / "copy-guard.c", FIXTURE / "cf-probe.c", FIXTURE / "cf-probe.s", ADAPTER / "sound-share-pool.c"],
        0x47E00000,
        {"cg_probe_stock_memcpy": 0x40136388},
    )
    ops = [{"op": "poke", "addr": linked.base, "hex": (linked.image + bytes(linked.bss_size)).hex()}]
    for kit in (0, 1):
        for name in ("setup", "run", "verify"):
            ops.append({"op": "call", "pc": linked.symbols[f"cg_probe_{name}"],
                        "args": [kit], "max": 100000, "tag": f"{kit}-{name}"})
    script = tmp_path / "copy-guard.json"
    script.write_text(json.dumps(ops))
    result = subprocess.run([str(runner), str(FW), str(script)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stderr + result.stdout[-3000:]
    calls = [json.loads(line) for line in result.stdout.splitlines() if line.startswith('{') and json.loads(line).get("op") == "call"]
    assert len(calls) == 6
    for row in calls:
        assert row["ok"] and row["d0"] == 0, json.dumps(calls)
    budgets = {row["tag"]: row["insns"] for row in calls}
    # Limits are component-instruction ceilings, not physical time estimates.
    assert budgets["0-run"] < 6000, budgets
    assert budgets["1-run"] < 30000, budgets
    print("ColdFire instruction counts:", budgets["0-run"], budgets["1-run"])
