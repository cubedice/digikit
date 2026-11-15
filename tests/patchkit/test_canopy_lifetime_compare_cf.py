"""Stock-vs-prototype DTII 1.17 Sound initializer component experiment.

Tests the exact stock initializer and the isolated type-site trampoline in a
disposable emulator; it does not prove complete lifecycle or concurrency.
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
LIFETIME = ROOT / "modules/core/adapters/instrument-play/src/cf/sound-lifetime.s"
PROBE = ROOT / "tests/regression/fixtures/canopy-lifetime-compare/type-probe.s"
BASE = 0x40000400
SIZE = 0x450
HANDLE = 0x13A
TYPE = 0xA2


def test_stock_initializer_exposes_late_marker_collision(tmp_path):
    runner = Path(os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "out/native/target/nogen/release/examples/cf_script"))
    if not runner.exists():
        pytest.skip("cf_script must be built explicitly")
    assert hashlib.sha256(FW.read_bytes()).hexdigest() == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    main = port_resolve.sections_of(FW)[3]
    assert main[0x400E65C8 - BASE:0x400E65D0 - BASE].hex() == "42aa013c45ea0140"
    assert main[0x4005179E - BASE:0x400517A4 - BASE].hex() == "114200a2b486"
    linked = patchkit.cf_link([LIFETIME, PROBE], 0x47E00000, {})
    original = bytearray((i * 17 + 37) & 0xFF for i in range(SIZE))
    original[HANDLE:HANDLE + 2] = b"\x45\x67"
    original[TYPE] = 3
    a, b, c, d = 0x47E10000, 0x47E11000, 0x47E12000, 0x47E13000
    init_jump = bytes.fromhex("4ef9") + linked.symbols["ip_sound_init_gap"].to_bytes(4, "big")
    ops = [
        {"op": "poke", "addr": linked.base, "hex": (linked.image + bytes(linked.bss_size)).hex()},
        *[{"op": "poke", "addr": addr, "hex": original.hex()} for addr in (a, b, c, d)],
        {"op": "call", "pc": 0x400E64DC, "args": [a, 0, 0, 0], "max": 1000000, "tag": "stock-init"},
        {"op": "poke", "addr": 0x400E65C8, "hex": init_jump.hex()},
        {"op": "call", "pc": 0x400E64DC, "args": [b, 0, 0, 0], "max": 1000000, "tag": "patched-init"},
        {"op": "peek", "addr": a, "len": SIZE, "tag": "stock"},
        {"op": "peek", "addr": b, "len": SIZE, "tag": "patched"},
        # Only in this component fixture, replace the real continuation by
        # RTS to inspect the exact displaced type instruction and CCR.
        {"op": "poke", "addr": 0x400517A4, "hex": "4e75"},
        {"op": "call", "pc": linked.symbols["canopy_type_probe"], "args": [c, 3, 3], "max": 10000, "tag": "same-type"},
        {"op": "call", "pc": linked.symbols["canopy_type_probe"], "args": [d, 5, 3], "max": 10000, "tag": "changed-type"},
        {"op": "peek", "addr": c, "len": SIZE, "tag": "same"},
        {"op": "peek", "addr": d, "len": SIZE, "tag": "changed"},
    ]
    script = tmp_path / "lifetime.json"
    script.write_text(json.dumps(ops))
    proc = subprocess.run([str(runner), str(FW), str(script)], text=True, capture_output=True, timeout=90)
    assert proc.returncode == 0, proc.stderr + proc.stdout[-3000:]
    rows = [json.loads(line) for line in proc.stdout.splitlines() if line.startswith("{")]
    calls = {row["tag"]: row for row in rows if row.get("op") == "call"}
    assert all(row["ok"] for row in calls.values()), calls
    assert calls["same-type"]["d0"] == 0x14 and calls["changed-type"]["d0"] == 0x10, calls
    peeks = {row["tag"]: bytes.fromhex(row["hex"]) for row in rows if row.get("op") == "peek"}
    baseline, patched = bytearray(peeks["stock"]), bytearray(peeks["patched"])
    # Stock helper 0x400e6464 writes byte 1 at Sound+0x13a; the late
    # marker changes this real stock byte to 0xff. This is evidence that
    # the full halfword is unsafe for a handle, not a passing safety proof.
    assert baseline[HANDLE:HANDLE + 2] == b"\x01\x67"
    assert patched[HANDLE:HANDLE + 2] == b"\xff\xff"
    patched[HANDLE:HANDLE + 2] = baseline[HANDLE:HANDLE + 2]
    assert patched == baseline, "initializer changed fields outside the candidate gap"
    assert peeks["same"] == original
    expected = bytearray(original)
    expected[TYPE] = 5
    expected[HANDLE:HANDLE + 2] = b"\xff\xff"
    assert peeks["changed"] == expected
