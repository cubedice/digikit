"""Execute destination control handoffs on the ColdFire core (synthetic kits)."""

import json
import os
import struct
import subprocess
from pathlib import Path

import pytest

from tools.patchkit import patchkit

ROOT = Path(__file__).resolve().parents[2]
BASE, SOURCE, DEST, INDICES = 0x47E00000, 0x47F00000, 0x47F01000, 0x47F02000


@pytest.fixture(scope="module")
def linked():
    return patchkit.cf_link(
        [ROOT / "modules/core/adapters/loop-controls/src/controls.c"],
        BASE,
        {
            "fxpages_info": 0x47F03000,
            "pp_position": 0x47F04000,
            "pp_current_pattern": 0,
        },
    )


@pytest.mark.parametrize("rec", [0, 0x3F7F, 0x3F80, 0x7F00])
@pytest.mark.parametrize("late_load", [False, True])
@pytest.mark.parametrize("in_place", [False, True])
def test_destination_receives_window_fade_and_consumed_cue(
    linked, tmp_path, rec, late_load, in_place
):
    binary = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not binary.exists():
        pytest.skip("build cf_script explicitly")
    destination = SOURCE if in_place else DEST
    ops = []

    def poke(addr, data):
        ops.append({"op": "poke", "addr": addr, "hex": data.hex()})

    def poll(selected, committed, array, load, tag):
        ops.append(
            {
                "op": "call",
                "pc": linked.symbols["lc_poll"],
                "args": [selected, committed, array, load, 0, INDICES, 10],
                "max": 10000,
                "sr": 0x2500,
                "tag": tag,
            }
        )

    def peek(addr, tag):
        ops.append({"op": "peek", "addr": addr, "len": 16, "tag": tag})

    poke(BASE, linked.image + bytes(linked.bss_size))
    poke(INDICES, bytes([0, 2, 4, 6]))  # non-contiguous kit indices
    original = [rec, 0x1111, 4 * 256, 0x2222, 0x3200, 0x3333, 7 * 256, 0x4444]
    loaded = [0x7F00, 0x1111, 99 * 256, 0x2222, 0x2100, 0x3333, 66 * 256, 0x4444]
    poke(SOURCE, struct.pack(">8H", *original))
    poll(100, 100, SOURCE, 1, "source")
    if late_load:
        poll(200, 200, SOURCE, 1, "commit")
        poke(destination, struct.pack(">8H", *loaded))
        poll(200, 200, destination, 2, "loaded")
    else:
        poke(destination, struct.pack(">8H", *loaded))
        poll(200, 100, destination, 2, "scheduled")
        poll(200, 200, destination, 2, "commit")
    peek(destination, "destination")
    if not in_place:
        peek(SOURCE, "source_after")
    # A second switch with the consumed cue must choose live, carrying the window.
    poll(300, 300, destination, 2, "next_commit")
    peek(destination, "next")
    script = tmp_path / "controls.json"
    script.write_text(json.dumps(ops))
    result = subprocess.run(
        [str(binary), str(ROOT / "firmware/Digitakt_II_OS1.17.syx"), str(script)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stderr + result.stdout[-3000:]
    rows = {
        v["tag"]: v
        for line in result.stdout.splitlines()
        if line.startswith("{")
        for v in [json.loads(line)]
        if "tag" in v
    }
    for v in rows.values():
        if v["op"] == "call":
            assert v["ok"] and not v.get("error"), v
    expected = [
        0,
        0x1111,
        4 * 256,
        0x2222,
        0x7F00 if rec >= 0x3F80 else 0,
        0x3333,
        7 * 256,
        0x4444,
    ]
    assert struct.unpack(">8H", bytes.fromhex(rows["destination"]["hex"])) == tuple(
        expected
    )
    expected[4] = 0
    assert struct.unpack(">8H", bytes.fromhex(rows["next"]["hex"])) == tuple(expected)
    if not in_place and not late_load:
        assert struct.unpack(
            ">8H", bytes.fromhex(rows["source_after"]["hex"])
        ) == tuple(original)
