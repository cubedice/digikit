"""Real DT2 Sound deserializer with the fixture lifetime wrapper on ColdFire.
Synthetic RAM records; this does not prove browser/kit/paste lifecycle coverage.
"""

import hashlib
import json
import os
import subprocess
import tomllib
from pathlib import Path

import pytest

from tools.patchkit import patchkit, port_resolve

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
MOD = ROOT / "tests/fixtures/modules/canopy-carrier/src"


def test_sound_loader_window_matches_image():
    main = port_resolve.sections_of(FW)[3]
    assert (
        hashlib.sha256(main).hexdigest()
        == "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    decl = tomllib.loads((ROOT / "devices/fw/machine.toml").read_text())
    site = next(
        s for s in decl["site"] if s["name"] == "cf.machine.site.sound_deserialize"
    )
    actual = port_resolve.resolve_cf_window_hash(
        main, 0x40000400, dict(site["rule"], name=site["name"])
    )
    anchors = tomllib.loads((ROOT / "devices/ports/dt2-1.17.toml").read_text())[
        "anchor"
    ]
    pinned = next(a for a in anchors if a["name"] == site["name"])
    assert all(actual[k] == pinned[k] for k in actual)


def test_sound_load_and_pattern_admission_lifetimes(tmp_path):
    runner = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not runner.exists():
        pytest.skip("cf_script must be built explicitly")
    assert (
        hashlib.sha256(FW.read_bytes()).hexdigest()
        == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    linked = patchkit.cf_link(
        [
            MOD / "admission.c",
            MOD / "sound-load.s",
            ROOT / "tests/support/canopy_sound_load_probe.c",
        ],
        0x47E00000,
        {"cp_sound_resume": 0x400DB3BA},
    )
    ops = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]
    for case in range(4):
        ops.append(
            {
                "op": "call",
                "pc": linked.symbols["cp_sound_case"],
                "args": [case],
                "max": 100000,
                "tag": str(case),
            }
        )
    for case in range(4):
        ops.append(
            {
                "op": "call",
                "pc": linked.symbols["cp_pattern_case"],
                "args": [case],
                "max": 100000,
                "tag": f"pattern-{case}",
            }
        )
    script = tmp_path / "script.json"
    script.write_text(json.dumps(ops))
    result = subprocess.run(
        [str(runner), str(FW), str(script)], capture_output=True, text=True, timeout=60
    )
    assert result.returncode == 0, result.stderr + result.stdout[-3000:]
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [r for r in rows if r.get("op") == "call"]
    assert len(calls) == 8
    for row in calls:
        assert row["ok"] and not row.get("error") and row["d0"] == 0, json.dumps(calls)
