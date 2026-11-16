"""Early reset-marker candidate in a disposable DTII 1.17 emulator.

This checks output equivalence, not atomic admission or production safety.
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
ASM = ROOT / "tests/regression/fixtures/canopy-lifetime-compare/early-init.s"
BASE = 0x40000400
SIZE = 0x450
HANDLE = 0x13A


def test_initializer_entry_marker_is_partially_overwritten_by_stock(tmp_path):
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
    assert main[0x400E64DC - BASE : 0x400E64E4 - BASE].hex() == "4fefffd848d77c0c"
    linked = patchkit.cf_link([ASM], 0x47E00000, {})
    source = bytearray((i * 17 + 37) & 0xFF for i in range(SIZE))
    source[HANDLE : HANDLE + 2] = b"\x45\x67"
    a, b = 0x47E10000, 0x47E11000
    jump = bytes.fromhex("4ef9") + linked.symbols["canopy_early_init_entry"].to_bytes(
        4, "big"
    )
    ops = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        },
        {"op": "poke", "addr": a, "hex": source.hex()},
        {"op": "poke", "addr": b, "hex": source.hex()},
        {
            "op": "call",
            "pc": 0x400E64DC,
            "args": [a, 0, 0, 0],
            "max": 1000000,
            "tag": "stock",
        },
        {"op": "poke", "addr": 0x400E64DC, "hex": jump.hex()},
        {
            "op": "call",
            "pc": 0x400E64DC,
            "args": [b, 0, 0, 0],
            "max": 1000000,
            "tag": "hooked",
        },
        {
            "op": "call",
            "pc": 0x400E64DC,
            "args": [0, 0, 0, 0],
            "max": 10000,
            "tag": "null",
        },
        {"op": "peek", "addr": a, "len": SIZE, "tag": "original"},
        {"op": "peek", "addr": b, "len": SIZE, "tag": "patched"},
    ]
    script = tmp_path / "early-reset.json"
    script.write_text(json.dumps(ops))
    proc = subprocess.run(
        [str(runner), str(FW), str(script)], text=True, capture_output=True, timeout=90
    )
    assert proc.returncode == 0, proc.stderr + proc.stdout[-3000:]
    rows = [
        json.loads(line) for line in proc.stdout.splitlines() if line.startswith("{")
    ]
    assert all(row["ok"] for row in rows if row.get("op") == "call"), rows
    peeks = {
        row["tag"]: bytearray.fromhex(row["hex"])
        for row in rows
        if row.get("op") == "peek"
    }
    stock, patched = peeks["original"], peeks["patched"]
    # The stock helper writes 1 to Sound+0x13a *after* the early hook.
    # Thus the early 0xffff marker becomes 0x01ff, a potentially valid
    # pool handle; it is not a safe invalidation mechanism.
    assert stock[HANDLE : HANDLE + 2] == b"\x01\x67"
    assert patched[HANDLE : HANDLE + 2] == b"\x01\xff"
    patched[HANDLE : HANDLE + 2] = stock[HANDLE : HANDLE + 2]
    assert patched == stock
