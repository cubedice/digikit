"""Real in-place-load bracket and DTII deserializer, with modeled admission.

The forced failure is injected after a successful stock write; it checks the
test transaction's rollback, not an observed stock partial-write failure.
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
FIXTURE = ROOT / "tests/regression/fixtures/canopy-load-bracket"
ADAPTER = ROOT / "modules/core/adapters/instrument-play/src/cf"


def test_actual_load_bracket_captures_and_rolls_back(tmp_path):
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
    main = port_resolve.sections_of(FW)[3]
    assert (
        main[0x400DB3B2 - 0x40000400 : 0x400DB3BA - 0x40000400].hex()
        == "4fefffe048d77c1c"
    )
    linked = patchkit.cf_link(
        [
            FIXTURE / "sound-proof-build.c",
            FIXTURE / "proof-entry.s",
            FIXTURE / "fixture.c",
            ROOT / "tests/regression/fixtures/canopy-detach/detach.c",
            ROOT / "tests/regression/fixtures/canopy-two-phase/cf-probe.s",
            ADAPTER / "preset.c",
            ADAPTER / "sound-codec.c",
            ADAPTER / "sound-pool.c",
        ],
        0x47E00000,
        {
            "canopy_bracket_resume": 0x400DB3BA,
            "memset": 0x401363D0,
            "memcpy": 0x40136388,
        },
    )
    ops = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]
    for case in range(3):
        ops.append(
            {
                "op": "call",
                "pc": linked.symbols["canopy_bracket_case"],
                "args": [case],
                "max": 1000000,
                "tag": str(case),
            }
        )
    script = tmp_path / "load-bracket.json"
    script.write_text(json.dumps(ops))
    proc = subprocess.run(
        [str(runner), str(FW), str(script)], text=True, capture_output=True, timeout=90
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout[-3000:]
    calls = [
        json.loads(line)
        for line in proc.stdout.splitlines()
        if line.startswith("{") and json.loads(line).get("op") == "call"
    ]
    assert len(calls) == 3
    assert all(row["ok"] and row["d0"] == 0 for row in calls), calls
