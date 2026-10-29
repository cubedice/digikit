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

from tools.patchkit import patchkit, pk_sharc, pk_toolchains, port_resolve

ROOT = REPO_ROOT
MOD = ROOT / "modules/core/adapters/midi-events"
FIRMWARE = ROOT / "firmware/Digitakt_II_OS1.17.syx"


@pytest.fixture(scope="module")
def link_receiver_object():
    link = ROOT / "modules/core/adapters/dsp-link"
    source = pk_sharc.expand_includes(
        (link / "src/dsp/dt2.s").read_text(),
        [link / "src/dsp", *pk_sharc.sharc_include_dirs(link)],
    )
    obj = pk_sharc.assemble(source)
    sections, address, initial = {}, 0x20050000, {}
    for name, (blob, align, _bss) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        for i in range(len(blob) // 4):
            value = int.from_bytes(blob[4 * i : 4 * i + 4], "little")
            if value or len(blob) < 4096:
                initial[address // 4 + i] = value
        address += len(blob)
    syms = {
        name: sections[sec] + offset
        for name, (sec, offset, _) in obj.data_symbols.items()
    }
    return obj, syms, initial


@pytest.mark.parametrize(
    "position,instrument,capacity",
    [(False, False, 171), (True, False, 155), (True, True, 123)],
)
@pytest.mark.parametrize("oversize", [False, True])
def test_dsp_loader_enforces_each_optional_tail_boundary(
    link_receiver_object, position, instrument, capacity, oversize
):
    obj, syms, initial = link_receiver_object
    rx, reply, frame, mailbox, back = (
        0x20010000,
        0x20020000,
        0x20030000,
        0x20040000,
        0xB9F000,
    )
    externs = {
        "_dl_back": back,
        "_dl_vrows": 0,
        "_dl_vcount": 0,
        "_dl_frows": 0,
        "_dl_fcount": 0,
        "_dv_diag": 0,
        "_dv_rec": 0,
        "_loop_ui_status": 0,
        "_pattern_position_info": mailbox if position else 0,
        "_instrument_reply": mailbox if instrument else 0,
        **syms,
    }
    linked = pk_sharc.link(obj, 0xB90000, externs)
    n = capacity + int(oversize)
    words = [0xD15C, (1 << 27) | (n << 17), *range(1, n + 1)]
    s1 = s2 = 0
    for value in words:
        s1 = (s1 + value) & 0xFFFFFFFF
        s2 = (s2 + s1) & 0xFFFFFFFF
    words.append(s1 ^ s2)
    dm = dict(initial)
    dm.update({mailbox // 4 + i: 0 for i in range(16)})
    dm.update({(rx + 0x804) // 4 + i: value for i, value in enumerate(words)})
    dm[frame // 4 - 3] = rx
    dm[frame // 4 - 2] = reply
    area = syms["_dl_area"] // 4
    dm.update({area + i: 0xDEADBEEF for i in range(n + 1)})
    result = pk_sharc.run_call(
        linked,
        "_dl_hook",
        regs={"I4": reply, "I6": frame},
        dm_words=dm,
        reads={"area": (area, n + 1), "status": (syms["_dl_status"] // 4, 1)},
        return_sw=back,
        max_steps=6000,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    if oversize:
        assert result["status"] == [3]
        assert result["area"] == [0xDEADBEEF] * (n + 1)
    else:
        assert result["status"] == [0]
        assert result["area"] == [*range(1, n + 1), 0xDEADBEEF]


@pytest.mark.parametrize("present", [False, True])
def test_instrument_reply_preserves_overloop_and_every_other_record_word(present):
    source = (
        ROOT / "modules/core/adapters/dsp-link/src/dsp/instrument-status.inc"
    ).read_text()
    obj = pk_sharc.assemble(
        ".EXTERN _instrument_reply;\n.EXTERN _dl_instrument_status_return;\n"
        ".GLOBAL _dl_instrument_status;\n" + source
    )
    mailbox, reply, back = 0x20090000, 0x200A0000, 0xB9F000
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            "_instrument_reply": mailbox if present else 0,
            "_dl_instrument_status_return": back,
        },
    )
    words = [0x494E5331, *range(1, 16)]
    original = [0xA5000000 + i for i in range(0xABC // 4)]
    dm = {reply // 4 + i: value for i, value in enumerate(original)}
    dm.update({mailbox // 4 + i: value for i, value in enumerate(words)})
    result = pk_sharc.run_call(
        linked,
        "_dl_instrument_status",
        regs={"I4": reply},
        dm_words=dm,
        reads={"reply": (reply // 4, len(original))},
        return_sw=back,
        max_steps=100,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    expected = original.copy()
    if present:
        for j, value in enumerate(words):
            offset = 0x1C + 0x54 * (21 + j // 2) + 0x44 + (j % 2) * 4
            expected[offset // 4] = value
    assert result["reply"] == expected


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
