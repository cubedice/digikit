"""Check shipped DSP initialization, not a simulated hardware reset."""

import json
import subprocess

import pytest
from support.paths import REPO_ROOT

from tools import python_env
from tools.patchkit import dsp_patch, patchkit, port_resolve


def test_registry_boot_stream_explicitly_clears_boot_owned_state(tmp_path):
    firmware = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
    if not firmware.exists():
        pytest.skip("DTII 1.17 firmware required")
    output = tmp_path / "registry.syx"
    plan_path = output.with_suffix(".plan.json")
    subprocess.run(
        [
            str(patchkit.binary()),
            "--root",
            str(REPO_ROOT),
            "apply",
            "--firmware",
            str(firmware),
            "--modules",
            "core/instrument-registry",
            "-o",
            str(output),
            "--plan",
            str(plan_path),
        ],
        check=True,
        capture_output=True,
        text=True,
        env=python_env(),
    )
    plan = json.loads(plan_path.read_text())
    stream = dsp_patch.Stream(port_resolve.sections_of(output)[7])
    # Every byte must be written by the actual final loader stream. Allocator
    # reservations and the component fixture's zero-filled memory are not enough.
    # In particular, PKBSS regressions must fail rather than silently read zero.
    regions = {
        "_instrument_session": 4,
        "_ir_sequence": 4,
        "_ir_lifecycle_ready": 4,
        "_ir_pending": 4,
        "_dl_irows": 64,
        "_instrument_registry_reply": 32,
        "_ir_rows": 8 * 16 * 4,
        "_mi_epoch": 4,
        "_mi_message": 4,
        "_mi_heartbeat": 4,
    }
    for symbol, size in regions.items():
        matches = [v for k, v in plan["symbols"].items() if k.endswith(":" + symbol)]
        assert len(matches) == 1, (symbol, matches)
        address = matches[0]
        assert all(stream.present(address + i) for i in range(size)), symbol
        assert stream.read(address, size) == bytes(size), symbol
    # Scratch written before use is deliberately excluded. This proves boot
    # image contents only, not GPIO reset delivery or in-flight transfer aborts.
