"""Reset hook ABI and exact current-firmware anchor; not a hardware reset proof."""

import json
import os
import subprocess
import tomllib
from pathlib import Path

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit, port_resolve


def test_reset_window_matches_pinned_firmware():
    firmware = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
    if not firmware.exists():
        pytest.skip("DTII 1.17 firmware required")
    main = port_resolve.sections_of(firmware)[3]
    decl = tomllib.loads((REPO_ROOT / "devices/fw/rtos.toml").read_text())
    site = next(s for s in decl["site"] if s["name"] == "cf.rtos.site.dsp_reset")
    actual = port_resolve.resolve_cf_window_hash(
        main, 0x40000400, dict(site["rule"], name=site["name"])
    )
    port = tomllib.loads((REPO_ROOT / "devices/ports/dt2-1.17.toml").read_text())
    pinned = next(a for a in port["anchor"] if a["name"] == site["name"])
    assert all(actual[k] == pinned[k] for k in actual)
    assert main[
        actual["addr"] - 0x40000400 : actual["addr"] - 0x40000400 + 6
    ] == bytes.fromhex("2f02102f000b")


def test_reset_hook_preserves_call_and_uses_low_argument_byte(tmp_path):
    firmware = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
    runner = Path(
        os.environ.get(
            "DIGIKIT_CF_SCRIPT", REPO_ROOT / "target/release/examples/cf_script"
        )
    )
    if not firmware.exists() or not runner.exists():
        pytest.skip("DTII firmware and cf_script required")
    probe = tmp_path / "probe.s"
    probe.write_text("""
.text
.globl reset_probe, mi_dsp_reset_begin, mi_dsp_reset_next
reset_probe:
    move.l %d2,-(%sp)
    move.l 8(%sp),-(%sp)
    moveq #17,%d0
    moveq #34,%d1
    movea.l #51,%a0
    movea.l #68,%a1
    moveq #85,%d2
    jsr mi_dsp_reset_hook
    addq.l #4,%sp
    move.l (%sp)+,%d2
    rts
mi_dsp_reset_begin:
    addq.l #1,notifications
    moveq #-1,%d0
    moveq #-2,%d1
    movea.l #-3,%a0
    movea.l #-4,%a1
    rts
mi_dsp_reset_next:
    cmpi.l #17,%d0
    bne.s bad
    cmpi.l #34,%d1
    bne.s bad
    cmpa.l #51,%a0
    bne.s bad
    cmpa.l #68,%a1
    bne.s bad
    cmpi.l #85,%d2
    bne.s bad
    /* Execute exactly the displaced stock prologue and check argument access. */
    move.l %d2,-(%sp)
    move.b 11(%sp),%d0
    andi.l #255,%d0
    move.l %d0,argument
    move.l (%sp)+,%d2
    move.l notifications,%d0
    lsl.l #8,%d0
    or.l argument,%d0
    rts
bad:
    moveq #-1,%d0
    rts
.data
notifications: .long 0
argument: .long 0
""")
    linked = patchkit.cf_link(
        [REPO_ROOT / "modules/core/adapters/midi-events/src/cf/reset.s", probe],
        0x47E00000,
        {},
    )
    ops = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]
    values = [0, 0x100, 1, 0, 0x12340080, 0xFF000000, 0xFFFFFFFF]
    expected = [0, 0, 0x101, 0x100, 0x280, 0x200, 0x3FF]
    for i, value in enumerate(values):
        ops.append(
            {
                "op": "call",
                "tag": str(i),
                "pc": linked.symbols["reset_probe"],
                "args": [value],
                "max": 10000,
            }
        )
    script = tmp_path / "reset.json"
    script.write_text(json.dumps(ops))
    result = subprocess.run(
        [str(runner), str(firmware), str(script)],
        capture_output=True,
        text=True,
        timeout=90,
        check=True,
    )
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [r for r in rows if r.get("op") == "call"]
    assert len(calls) == len(values)
    for row, value in zip(calls, expected, strict=True):
        assert row["ok"] and not row["error"] and row["d0"] == value, row
