"""DT2 1.17 CF position hooks, executed with synthetic sequencer state.

Requires the stock image, m68k assembler, and an already built cf_script;
this test never builds a native emulator. DIGIKIT_CF_SCRIPT overrides its path.
The proof covers the provider's ABI and wire packet, not musical/audio timing.
"""

import hashlib
import json
import os
import re
import struct
import subprocess
import tomllib
from pathlib import Path

import pytest

from tools.patchkit import pk_link, pk_toolchains
from tools.patchkit.pk_loop_storage_proof import MAIN_BASE, MAIN_SHA, main_image

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "modules/tools/pattern-position"
SOURCE = MODULE / "src/cf/pattern_position.s"
FIRMWARE = ROOT / "firmware/Digitakt_II_OS1.17.syx"
BASE, WRAPPER, QUEUE, REGISTERS = 0x47E00000, 0x47E01000, 0x47E10000, 0x47E11000
PATTERN, SUFFIX, ENTRIES = 0x47F00000, 19, 56
SALT, MAGIC = 0x7B91D6E3, 0x50504F53
SEEDS = tuple(0x10203040 + index * 0x01010101 for index in range(15))
FW = {
    "FW_COUNTDOWN": 0x80004888,
    "FW_TRANSPORT": 0x43164F94,
    "FW_MASTER_NEXT": 0x43164F98,
    "FW_SUBSTEP": 0x43164F9A,
    "FW_MASTER_CURRENT": 0x43164F9C,
    "FW_PATTERN": 0x4316501C,
    "FW_LENGTH": 0x43165130,
    "FW_SCHED_CLOCK": 0x43158F7C,
    "FW_BLOCKS": 0x40966840,
}
# name, pointer, master override, fallback, expected length, valid snapshot
LENGTH_CASES = (
    ("override", PATTERN, 64, 32, 64, True),
    ("fallback-zero", PATTERN, 0, 32, 32, True),
    ("fallback-one", PATTERN, 1, 32, 32, True),
    ("fallback-negative", PATTERN, 0xFFFF, 32, 32, True),
    ("unknown-zero", PATTERN, 0, 0, 0, True),
    ("unknown-one", PATTERN, 0, 1, 0, True),
    ("unknown-negative", PATTERN, 0, 0xFFFF, 0, True),
    ("signed-max", PATTERN, 0x7FFF, 32, 0x7FFF, True),
    ("last-safe-pointer", 0x47FE248A, 0, 32, 32, True),
    ("null-pointer", 0, 64, None, 0, False),
    ("below-ddr", 0x3FFFFFFE, 64, None, 0, False),
    ("odd-pointer", PATTERN + 1, 64, None, 0, False),
    ("past-safe-pointer", 0x47FE248C, 64, None, 0, False),
    ("outside-ddr", 0x48000000, 64, None, 0, False),
)


@pytest.fixture(scope="module")
def stock_main():
    if not FIRMWARE.exists():
        pytest.skip("DT2 1.17 stock firmware is not installed")
    image = main_image(FIRMWARE)
    assert hashlib.sha256(image).hexdigest() == MAIN_SHA
    return image


def test_hook_windows_and_position_getter_match_the_pinned_image(stock_main):
    manifest = tomllib.loads((MODULE / "module.toml").read_text(encoding="utf-8"))
    rules = {rule["name"]: rule for rule in manifest["anchor_rule"]}
    for name, address in (("pp.cf.start", 0x400D66D8), ("pp.cf.tick", 0x400D8DCA)):
        rule = rules[name]
        offset = address - MAIN_BASE
        assert rule["kind"] == "cf_window_hash" and rule["len"] == 6
        window = stock_main[offset : offset + rule["search_len"]]
        assert hashlib.sha256(window).hexdigest() == rule["sha256"]
        # Both instructions are the displaced longword countdown store.
        assert hashlib.sha256(window[:6]).hexdigest() == (
            "a1b93eb1870f22fc4b72f3e7ceb0a5e95169fd08394878509249c2c58ef30aeb"
        )
    # Native track/master position accessor: master mode reads 0x43164f9c,
    # distinct from the advance/scheduling counter at 0x43164f98.
    start = 0x400D67FE - MAIN_BASE
    assert hashlib.sha256(stock_main[start : start + 30]).hexdigest() == (
        "0c400342e4f6780e77328eff69836796eb76129b2985ba05459dc721221d4b0a"
    )
    # The actual master-next wrap uses the signed override when >1, falling
    # back to pattern+0x1db74 otherwise. Both paths share the comparison.
    start = 0x400D8820 - MAIN_BASE
    assert hashlib.sha256(stock_main[start : start + 50]).hexdigest() == (
        "8d68c36f75d8b8069f952b85aeaf4bc9742cd5ee254d011d5cbc541ee034ebd7"
    )
    constants = {
        name: int(value, 16)
        for name, value in re.findall(
            r"\.equ\s+(FW_\w+),\s*(0x[0-9a-fA-F]+)", SOURCE.read_text()
        )
    }
    assert constants == FW


def _compile(source, destination, base, externs=None):
    pk_link.compile_source(source, destination)
    obj = pk_link.object_from_elf(destination.read_bytes(), [source])
    linked = pk_link.link(obj, base, externs)
    end = max(linked.placement[sec["name"]] + sec["size"] for sec in obj["sections"])
    # Include the alignment gap before BSS, as well as the BSS itself.
    image = linked.image + bytes(end - base - len(linked.image))
    return linked, image


def _wrapper_source():
    bodies = []
    for name, callee in (
        ("start", "pp_start_hook"),
        ("tick", "pp_seq_hook"),
        ("write", "pp_write_frame"),
    ):
        args = (
            f"move.l #{SUFFIX},-(%sp)\nmove.l #{QUEUE},-(%sp)"
            if name == "write"
            else ""
        )
        cleanup = "lea 8(%sp),%sp" if name == "write" else ""
        bodies.append(f"""
            .globl probe_{name}
        probe_{name}:
            {args}
            moveq #-1,%d0
            move.l %d0,{FW["FW_COUNTDOWN"]}
            lea register_seeds,%a0
            movem.l (%a0),%d0-%d7/%a0-%a6
            move.l %sp,{REGISTERS + 68}
            jsr {callee}
            move.l %sp,{REGISTERS + 60}
            lea -60(%sp),%sp
            movem.l %d0-%d7/%a0-%a6,(%sp)
            move.w %sr,%d0
            move.w %d0,{REGISTERS + 64}
            move.l %sp,%a0
            lea {REGISTERS},%a1
            moveq #15,%d0
        copy_{name}:
            move.l (%a0)+,%d1
            move.l %d1,(%a1)+
            subq.l #1,%d0
            bne.s copy_{name}
            move.l {FW["FW_COUNTDOWN"]},%d0
            move.l %d0,{REGISTERS + 72}
            lea 60(%sp),%sp
            {cleanup}
            rts
        """)
    seeds = ",".join(str(value) for value in SEEDS)
    return ".text\n.even\n" + "\n".join(bodies) + f"\nregister_seeds:\n.long {seeds}\n"


@pytest.fixture(scope="module")
def executed(stock_main, tmp_path_factory):
    del stock_main  # Establish the firmware hash before executing it.
    binary = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not binary.is_file():
        pytest.skip("Build cf_script or set DIGIKIT_CF_SCRIPT to an existing binary")
    if pk_toolchains.m68k_bin("as") is None:
        pytest.skip("m68k assembler is not installed")
    work = tmp_path_factory.mktemp("pattern-position-cf")
    provider, provider_image = _compile(SOURCE, work / "provider.o", BASE)
    wrapper_source = work / "probe.s"
    wrapper_source.write_text(_wrapper_source(), encoding="utf-8")
    wrapper, wrapper_image = _compile(
        wrapper_source, work / "probe.o", WRAPPER, provider.symbols
    )
    position = provider.symbols["pp_position"]
    valid = provider.symbols["pp_snapshot_valid"]
    ops = []

    def poke(address, data):
        ops.append({"op": "poke", "addr": address, "hex": data.hex()})

    def word(address, value):
        poke(address, struct.pack(">I", value))

    def call(tag, function, args=()):
        pc = (
            wrapper.symbols[function]
            if function.startswith("probe_")
            else provider.symbols[function]
        )
        ops.append(
            {
                "op": "call",
                "pc": pc,
                "args": list(args),
                "max": 1000,
                "sr": 0x2500,
                "tag": tag,
            }
        )

    def peek(tag, address, size):
        ops.append({"op": "peek", "addr": address, "len": size, "tag": tag})

    def sequencer(pointer=PATTERN, override=64, fallback=32):
        word(FW["FW_PATTERN"], pointer)
        poke(FW["FW_LENGTH"], struct.pack(">H", override))
        if fallback is not None:
            poke(pointer + 0x1DB74, struct.pack(">H", fallback))
        poke(FW["FW_MASTER_CURRENT"], struct.pack(">H", 17))
        poke(FW["FW_MASTER_NEXT"], struct.pack(">H", 18))
        poke(FW["FW_SUBSTEP"], b"\x05")
        word(FW["FW_SCHED_CLOCK"], 0x89ABCDEF)
        word(FW["FW_BLOCKS"], 1234)
        word(FW["FW_TRANSPORT"], 1)

    queue = b"".join(struct.pack(">HH", 0xA000 + i, 0x5555) for i in range(ENTRIES))

    def write(tag, transport=1):
        word(FW["FW_TRANSPORT"], transport)
        word(FW["FW_BLOCKS"], 1235)
        poke(QUEUE, queue)
        call(tag, "probe_write")
        peek(tag + "-queue", QUEUE, len(queue))
        peek(tag + "-registers", REGISTERS, 72)
        peek(tag + "-state", position, 64)

    poke(BASE, provider_image)
    poke(WRAPPER, wrapper_image)
    sequencer()
    for name in ("start", "tick"):
        call(name, "probe_" + name)
        peek(name + "-registers", REGISTERS, 72)
        peek(name + "-countdown", REGISTERS + 72, 4)
        peek(name + "-state", position, 64)
    for name, transport in (("running", 1), ("paused", 2), ("stopped", 0)):
        write(name, transport)
    call("resume", "probe_start")
    write("resumed")

    for name, pointer, override, fallback, _, _ in LENGTH_CASES:
        poke(position, bytes(64))
        word(valid, 0)
        sequencer(pointer, override, fallback)
        call(name + "-snapshot", "pp_snapshot")
        write(name)

    # Counters are unsigned serial numbers; zero is a legal wrapped value.
    sequencer()
    poke(position, bytes(64))
    for index in (2, 3, 4):
        word(position + 4 * index, 0xFFFFFFFF)
    call("wrap-start", "probe_start")
    write("wrap")
    script = work / "script.json"
    script.write_text(json.dumps(ops), encoding="utf-8")
    result = subprocess.run(
        [str(binary.resolve()), str(FIRMWARE), str(script)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stdout[-3000:] + result.stderr[-3000:]
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    assert any(row.get("op") == "ready" for row in rows)
    tagged = {row["tag"]: row for row in rows if row.get("tag")}
    wanted = [op["tag"] for op in ops if op.get("tag")]
    assert len(tagged) == len(wanted) and set(tagged) == set(wanted)
    for row in tagged.values():
        if row["op"] == "call":
            assert row["ok"] and not row.get("error"), row
            assert row["insns"] < 1000, row
    return tagged, queue


def _bytes(executed, tag):
    return bytes.fromhex(executed[0][tag]["hex"])


def _words(executed, tag):
    data = _bytes(executed, tag)
    return struct.unpack(f">{len(data) // 4}I", data)


def _packet(executed, tag):
    queue = _bytes(executed, tag + "-queue")
    original = executed[1]
    words = []
    allowed = set()
    for index in range(16):
        offset = (SUFFIX + index * 2) * 4 + 2
        low = int.from_bytes(queue[offset : offset + 2], "big")
        high = int.from_bytes(queue[offset + 4 : offset + 6], "big")
        words.append(low | high << 16)
        allowed.update((offset, offset + 1, offset + 4, offset + 5))
    assert all(
        value == original[i] for i, value in enumerate(queue) if i not in allowed
    )
    assert tuple(words) == _words(executed, tag + "-state")
    checksum = SALT
    for value in words[:15]:
        checksum ^= value
    assert words[15] == checksum
    assert words[:2] == [MAGIC, 1]
    return words


def test_hooks_replay_store_preserve_registers_and_count_events(executed):
    for tag in ("start", "tick"):
        registers = _words(executed, tag + "-registers")
        assert registers[:15] == SEEDS
        assert registers[15] == registers[17]  # SP before/after call.
        assert registers[16] >> 24 == 0x25  # Saved interrupt mask unchanged.
        assert _words(executed, tag + "-countdown") == (SEEDS[2],)
    start = _words(executed, "start-state")
    tick = _words(executed, "tick-state")
    assert start[2:5] == (0, 1, 1)
    assert tick[2:5] == (0, 2, 1)
    assert tick[6:12] == (PATTERN, 17, 64, 5, 1234, 0x89ABCDEF)
    assert tick[13] == 18


def test_wire_order_checksum_queue_ownership_and_writer_abi(executed):
    for tag in ("running", "paused", "stopped", "resumed", "wrap"):
        _packet(executed, tag)
        registers = _words(executed, tag + "-registers")
        for index in (*range(2, 8), *range(10, 15)):
            assert registers[index] == SEEDS[index]
        assert registers[15] == registers[17]
        assert registers[16] >> 24 == 0x25
    for tx, tag, transport in (
        (1, "running", 1),
        (2, "paused", 2),
        (3, "stopped", 0),
        (4, "resumed", 1),
    ):
        packet = _packet(executed, tag)
        assert packet[2] == tx and packet[12] == 1235 and packet[14] == transport
        assert packet[5] == (3 if transport == 1 else 0)
        assert packet[3:5] == ([3, 2] if tag == "resumed" else [2, 1])
    assert _packet(executed, "wrap")[2:5] == [0, 0, 0]


@pytest.mark.parametrize("case", LENGTH_CASES, ids=lambda case: case[0])
def test_signed_length_fallback_and_pointer_bounds(executed, case):
    name, pointer, _, _, length, valid = case
    packet = _packet(executed, name)
    assert packet[2:5] == [1, 1, 0]
    assert packet[5] == (3 if valid else 2)
    assert packet[6:12] == [pointer, 17, length, 5, 1234, 0x89ABCDEF]
    assert packet[12:15] == [1235, 18, 1]
