"""Execute the committed-boundary queue and packet writer on the ColdFire core.
The inputs are synthetic scheduler events; these tests do not establish that
firmware hooks coincide with rendered audio.
"""

import hashlib
import json
import os
import struct
import subprocess
import tomllib
from pathlib import Path

import pytest

from tools.patchkit import patchkit, port_resolve

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "modules/core/adapters/pattern-position"
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
BASE, QUEUE = 0x47E00000, 0x47E10000
PATTERN = 0x47F00000


def test_all_cf_windows_match_pinned_image():
    main = port_resolve.sections_of(FW)[3]
    assert (
        hashlib.sha256(main).hexdigest()
        == "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    manifest = tomllib.loads((ROOT / "devices/fw/pattern.toml").read_text())
    port = tomllib.loads((ROOT / "devices/ports/dt2-1.17.toml").read_text())
    anchors = {a["name"]: a for a in port["anchor"]}
    for site in manifest["site"]:
        rule = dict(site["rule"], name=site["name"])
        if rule["kind"] != "cf_window_hash":
            continue
        actual = port_resolve.resolve_cf_window_hash(main, 0x40000400, rule)
        assert all(actual[k] == anchors[rule["name"]][k] for k in actual)


@pytest.fixture(scope="module")
def linked():
    return patchkit.cf_link(
        [MOD / "src/cf/pattern_position.s", MOD / "src/cf/boundary.c"],
        BASE,
        {
            "pp_stock_step": 0x4012BFF0,
            "pp_start_back": 0,
            "pp_tick_back": 0,
            "pp_start_due_back": 0,
        },
    )


def execute(linked, tmp_path, events):
    binary = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not binary.exists():
        pytest.skip("cf_script must be built explicitly")
    ops = [
        {
            "op": "poke",
            "addr": BASE,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]

    def word(addr, n):
        return {"op": "poke", "addr": addr, "hex": struct.pack(">I", n).hex()}

    ops += [word(0x43164F94, 1), word(0x40966840, 100)]
    original = b"".join(struct.pack(">HH", 0xA000 + i, 0x5555) for i in range(64))
    ops += [{"op": "poke", "addr": QUEUE, "hex": original.hex()}]
    for index, (function, args) in enumerate(events):
        ops += [
            {
                "op": "call",
                "pc": linked.symbols[function],
                "args": args,
                "max": 10000,
                "sr": 0x2700,
                "tag": f"call{index}",
            },
            {
                "op": "peek",
                "addr": linked.symbols["pp_position"],
                "len": 64,
                "tag": f"state{index}",
            },
        ]
    ops += [
        {
            "op": "call",
            "pc": linked.symbols["pp_write_frame"],
            "args": [QUEUE, 8],
            "max": 10000,
            "sr": 0x2500,
            "tag": "write",
        },
        {"op": "peek", "addr": QUEUE, "len": len(original), "tag": "wire"},
    ]
    script = tmp_path / "script.json"
    script.write_text(json.dumps(ops))
    r = subprocess.run(
        [str(binary), str(FW), str(script)], capture_output=True, text=True, timeout=60
    )
    assert r.returncode == 0, r.stderr + r.stdout[-3000:]
    rows = {
        v["tag"]: v
        for line in r.stdout.splitlines()
        if line.startswith("{")
        for v in [json.loads(line)]
        if "tag" in v
    }
    for v in rows.values():
        if v["op"] == "call":
            assert v["ok"] and not v.get("error"), v
    wire = bytes.fromhex(rows["wire"]["hex"])
    assert wire[:32] == original[:32] and wire[160:] == original[160:]
    assert all(wire[i : i + 2] == original[i : i + 2] for i in range(32, 160, 4))
    words = [
        int.from_bytes(wire[32 + 8 * i + 2 : 32 + 8 * i + 4], "big")
        | int.from_bytes(wire[32 + 8 * i + 6 : 32 + 8 * i + 8], "big") << 16
        for i in range(16)
    ]
    checksum = 0x7B91D6E3
    for w in words[:15]:
        checksum ^= w
    assert words[15] == checksum and words[:2] == [0x50504F53, 2]
    return words, rows


def observe(clock, step, start=0):
    return ("pp_observe", [start, clock, PATTERN, step, 16])


def due(clock, kind=5, block=9):
    return ("pp_commit", [clock, kind, block])


def test_snapshot_does_not_publish_early_and_commit_freezes_context(linked, tmp_path):
    words, rows = execute(
        linked,
        tmp_path,
        [
            observe(1000, 0, 1),
            due(999, 7),
            due(1000, 7),
            observe(2000, 1),
            observe(3000, 2),
            due(2000, block=10),
        ],
    )
    for i in (0, 1):
        state = struct.unpack(">16I", bytes.fromhex(rows[f"state{i}"]["hex"]))
        assert state[3] == 0
    assert words[3:9] == [2, 1, 3, PATTERN, 1, 16]
    assert words[10:12] == [10, 2000]


@pytest.mark.parametrize(
    "events",
    [
        [observe(1000, 0, 1), due(1001, 7)],  # missed deadline
        [observe(1000, 0, 1), due(1000, 5)],  # wrong kind cannot start
        [
            observe(1000, 0, 1),
            due(1000, 7),
            observe(2000, 1),
            due(2000, block=9),
        ],  # two in same frame
        [observe(1000, 0, 1)]
        + [observe(2000 + i * 1000, (i + 1) % 16) for i in range(16)],  # queue full
    ],
)
def test_queue_failures_clear_validity(linked, tmp_path, events):
    words, _ = execute(linked, tmp_path, events)
    assert words[5] == 2


def test_unsigned_deadline_wrap_and_duplicate_substeps(linked, tmp_path):
    words, _ = execute(
        linked,
        tmp_path,
        [
            observe(0xFFFFFFF0, 0, 1),
            due(0xFFFFFFF0, 7),
            observe(0x20, 1),
            observe(0x30, 1),
            due(0x10, block=10),
            due(0x20, block=11),
        ],
    )
    assert words[3] == 2 and words[7] == 1 and words[11] == 0x20 and words[5] == 3
