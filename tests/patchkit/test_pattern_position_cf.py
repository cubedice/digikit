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
# cf.pattern.site.playing_lit in DT2 1.17 (devices/ports/dt2-1.17.toml): the literal of `lea playing,a6`
PLAYING_LIT, PLAYING = 0x400D84AE, 0x44F37BD0
SCHEDULED, OVERRIDE, STEP, SUBSTEP, CLOCK, BLOCK = (
    0x4316501C,
    0x43165130,
    0x43164F9C,
    0x43164F9A,
    0x43158F7C,
    0x40966840,
)


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
            # the literal itself, in the firmware image cf_script loads
            "pp_playing_lit": PLAYING_LIT,
        },
    )


def execute(linked, tmp_path, events, *, tempo=0, transport=1):
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

    ops += [word(0x43164F94, transport), word(0x40966840, 100), word(0x402B67FC, tempo)]
    original = b"".join(struct.pack(">HH", 0xA000 + i, 0x5555) for i in range(64))
    ops += [{"op": "poke", "addr": QUEUE, "hex": original.hex()}]
    for index, (function, args) in enumerate(events):
        if function == "poke":
            ops.append({"op": "poke", "addr": args[0], "hex": args[1]})
            continue
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
        {"op": "peek", "addr": 0x80004880, "len": 4, "tag": "phase"},
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
    assert words[9] == int(rows["phase"]["hex"], 16)
    assert words[15] == checksum and words[:2] == [0x50504F53, 4]
    return words, rows


def observe(clock, step, start=0):
    return ("pp_observe", [start, clock, PATTERN, step, 16])


def due(clock, kind=5, block=9):
    return ("pp_commit", [clock, kind, block])


@pytest.mark.parametrize("tempo", [0, 3599, 3600, 12240, 36000, 36001, 0xFFFFFFFF])
@pytest.mark.parametrize("transport", [0, 1])
def test_selected_tempo_is_sampled_while_running_and_stopped(
    linked, tmp_path, tempo, transport
):
    words, _ = execute(linked, tmp_path, [], tempo=tempo, transport=transport)
    expected = tempo if 3600 <= tempo <= 36000 else 0
    assert words[5] == (expected << 16) | (2 if transport else 0)
    assert words[14] == transport


def test_external_clock_expiry_latches_until_stop_and_handles_wrap(linked, tmp_path):
    cases = [
        ([1, 1, 1, 100000000, 0, 44000000, 24], 1),  # internal
        ([1, 1, 3, 0xFFF00000, 0xFFF00000, 44000000, 24], 1),
        ([1, 1, 1, 0x500000, 0xFFF00000, 44000000, 24], 0),  # wrap + stock fallback
        ([1, 1, 1, 0xFFF00000, 0xFFF00000, 44000000, 24], 0),  # full counter wrap
        ([1, 1, 3, 0x600000, 0x600000, 44000000, 24], 1),  # next pulse
        ([1, 1, 3, 100000000, 0, 0xFFFFFFFF, 24], 0),  # bounded timeout
        ([0, 1, 1, 100000000, 0, 44000000, 24], 1),
        ([1, 1, 1, 100000000, 0, 44000000, 24], 1),  # new internal run
    ]
    _, rows = execute(linked, tmp_path, [("pp_clock_fresh", args) for args, _ in cases])
    assert [rows[f"call{i}"]["d0"] for i in range(len(cases))] == [v for _, v in cases]


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


def poke(addr, data):
    return ("poke", [addr, data.hex()])


def poke32(addr, value):
    return poke(addr, struct.pack(">I", value))


def pattern_at(addr, steps):
    """A pattern as the snapshot reads it: its length at +0x1db74, common mode and scale 1x at +0x1db79."""
    return [
        poke(addr + 0x1DB74, struct.pack(">h", steps)),
        poke(addr + 0x1DB79, bytes([0, 2])),
    ]


def tick(scheduled, playing, step, substep, clock, start=0):
    """The stock tick's state at its tail, then the snapshot it hooks."""
    return [
        poke32(SCHEDULED, scheduled),
        poke32(PLAYING, playing),
        poke(STEP, struct.pack(">H", step)),
        poke(SUBSTEP, bytes([substep])),
        poke32(CLOCK, clock),
        ("pp_snapshot", [start]),
    ]


def commit(clock, block, kind=5):
    return [poke32(BLOCK, block), ("pp_due", [clock, kind])]


def test_snapshot_carries_the_playing_pattern_at_a_change_at_the_end(linked, tmp_path):
    """A change from A (16 steps) to B (8 steps) at A's end: the stock tick moves the scheduled pointer to B at the edge
    of A's last step, while track 1's current step is still A's. The boundary of that edge is A's step 15 of 16; B's first
    step comes at the next edge, and a track that takes B between the edges does not move it (finding 365)."""
    a, b = PATTERN, PATTERN + 0x40000
    events = [
        poke(OVERRIDE, struct.pack(">h", 0)),
        *pattern_at(a, 16),
        *pattern_at(b, 8),
    ]
    events += tick(a, a, 0, 0, 1000, start=1) + commit(1000, 10, kind=7)
    events += tick(a, a, 14, 0, 2000) + commit(2000, 11)
    events += tick(b, a, 15, 0, 3000) + commit(3000, 12)
    events += tick(b, b, 15, 1, 3100)
    events += tick(b, b, 0, 0, 4000) + commit(4000, 13)
    words, rows = execute(linked, tmp_path, events)
    dues = [i for i, (function, _) in enumerate(events) if function == "pp_due"]
    # after each commit: the boundary count, the pattern, the step and the length
    committed = [
        struct.unpack(">16I", bytes.fromhex(rows[f"state{i}"]["hex"]))[3:9]
        for i in dues
    ]
    assert [(c[0], c[3], c[4], c[5]) for c in committed] == [
        (1, a, 0, 16),
        (2, a, 14, 16),
        (3, a, 15, 16),  # the last step of A is A's, with A's length
        (4, b, 0, 8),  # the snapshot between the edges queued nothing
    ]
    assert words[5] & 3 == 3 and words[6:9] == [b, 0, 8]
