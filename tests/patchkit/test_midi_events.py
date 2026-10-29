"""Execute production MIDI ownership and dispatch code on the ColdFire core.

This is a component check, not a USB/DIN hardware or final-audio proof.
"""

import hashlib
import json
import os
import subprocess
import tomllib
from pathlib import Path

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit, pk_toolchains, port_resolve

ROOT = REPO_ROOT
MOD = ROOT / "modules/core/adapters/midi-events"
FIRMWARE = ROOT / "firmware/Digitakt_II_OS1.17.syx"


def test_din_hook_resolves_uniquely_on_pinned_image():
    if not FIRMWARE.exists():
        pytest.skip("DTII 1.17 firmware required")
    declarations = tomllib.loads((ROOT / "devices/fw/midi.toml").read_text())
    site = next(s for s in declarations["site"] if s["name"].endswith(".din_rx"))
    port = tomllib.loads((ROOT / "devices/ports/dt2-1.17.toml").read_text())
    anchor = next(a for a in port["anchor"] if a["name"] == site["name"])
    main = port_resolve.sections_of(FIRMWARE)[3]
    rule = site["rule"]
    size = rule["search_len"]
    matches = [
        at
        for at in range(0, len(main) - size + 1, 2)
        if hashlib.sha256(main[at : at + size]).hexdigest() == rule["sha256"]
    ]
    assert len(matches) == 1
    assert matches[0] + 0x40000400 + rule["offset"] == anchor["addr"]
    start = anchor["addr"] - 0x40000400
    assert (
        hashlib.sha256(main[start : start + anchor["len"]]).hexdigest()
        == anchor["sha256"]
    )


def test_ownership_and_dispatch_on_coldfire(tmp_path):
    runner = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not FIRMWARE.exists() or not runner.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("DTII firmware, m68k compiler and cf_script required")
    memory = tmp_path / "memory.c"
    memory.write_text("""
void *memcpy(void *d, const void *s, unsigned n) {
    volatile unsigned char *p=d; const volatile unsigned char *q=s;
    while(n--) { *p++=*q++; }
    return d;
}
void *memset(void *d, int c, unsigned n) {
    volatile unsigned char *p=d;
    while(n--) { *p++=(unsigned char)c; }
    return d;
}
""")
    linked = patchkit.cf_link(
        [
            MOD / "src/cf/ownership.c",
            MOD / "src/cf/adapter.c",
            MOD / "host/tests/ownership.c",
            MOD / "host/tests/dispatch.c",
            ROOT / "modules/core/runtime/src/cf/midi_dispatch.c",
            memory,
        ],
        0x47E00000,
        {},
        [MOD / "src/cf"],
    )
    names = [
        "fifo",
        "reroute",
        "overflow",
        "messages",
        "panic",
        "reset_presses",
        "token_wrap",
        "ring_wrap",
        "dispatch",
    ]
    script = tmp_path / "calls.json"
    script.write_text(
        json.dumps(
            [
                {
                    "op": "poke",
                    "addr": linked.base,
                    "hex": (linked.image + bytes(linked.bss_size)).hex(),
                },
                *[
                    {
                        "op": "call",
                        "tag": name,
                        "pc": linked.symbols["mi_test_" + name],
                        "args": [],
                        "max": 5_000_000,
                    }
                    for name in names
                ],
            ]
        )
    )
    result = subprocess.run(
        [str(runner), str(FIRMWARE), str(script)],
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [row for row in rows if row.get("op") == "call"]
    assert [row["tag"] for row in calls] == names
    for row in calls:
        assert row["ok"] and not row["error"] and row["d0"] == 0, row
