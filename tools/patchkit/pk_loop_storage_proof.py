"""Bounded stock-versus-reserved DT2 1.17 sample allocator proof.

Requires a provider-only image patched with michael/loop-storage and an existing
cf_script binary. Firmware bytes, generated scripts and reports stay under out/.
The real firmware helper runs against synthetic pool records, not a loaded
400 MiB sample set. This is an allocator boundary proof, not full integration.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import subprocess
from pathlib import Path

from tools.firmware.container import sections
from tools.firmware.elz import depack_section
from tools.firmware.extract import classify

ROOT = Path(__file__).resolve().parents[2]
STOCK_SHA = "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
MAIN_SHA = "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
MAIN_BASE = 0x40000400
CF_BIAS = 0x400
STOCK_BYTES = 0x19000000
POOL_BYTES = 0x18800000
BUFFER_BYTES = STOCK_BYTES - POOL_BYTES
DSP_SAMPLE_BASE = 0x8422B9C8
BUFFER_BASE = DSP_SAMPLE_BASE + POOL_BYTES
PREVIEW_BASE = DSP_SAMPLE_BASE + STOCK_BYTES
PATCH_SITES = {
    0x40153AE2: (STOCK_BYTES + CF_BIAS, POOL_BYTES + CF_BIAS),
    0x40153AF2: (STOCK_BYTES, POOL_BYTES),
    0x40153AFC: (STOCK_BYTES, POOL_BYTES),
    0x4015470C: (STOCK_BYTES + CF_BIAS, POOL_BYTES + CF_BIAS),
    0x40154764: (STOCK_BYTES, POOL_BYTES),
    0x40154AEC: (STOCK_BYTES, POOL_BYTES),
}
# These matching literals have different meanings and must never be patched.
EXCLUDED_SITES = (0x401541FC, 0x40154228, 0x401E9C74)
VECTOR_BEGIN = 0x405A0F64
VECTOR_END = 0x405A0F68
RECORD = 0x47F00000
OUTPUT = 0x47F01000
SENTINEL = 0xA55AA55A


def cf_offset_to_dsp(offset: int) -> int:
    """CF logical sample offset, including its 0x400 bias, to DSP bytes."""
    if offset < CF_BIAS:
        raise ValueError("CF sample offsets start at 0x400")
    return DSP_SAMPLE_BASE + offset - CF_BIAS


def main_image(syx: Path) -> bytes:
    container, table = sections(str(syx))
    _, offset, size, base = next(row for row in table if row[0] == 3)
    if base != MAIN_BASE:
        raise ValueError("Unexpected MAIN load address")
    kind, data = classify(container[offset : offset + size])
    return depack_section(data) if kind == "packed" else data


def audit_main(stock: bytes, patched: bytes) -> None:
    """Require exactly the six reviewed writes; reject preview/table edits."""
    if hashlib.sha256(stock).hexdigest() != MAIN_SHA:
        raise ValueError("Stock MAIN SHA-256 is not DT2 1.17")
    expected = bytearray(stock)
    for address, (before, after) in PATCH_SITES.items():
        offset = address - MAIN_BASE
        if struct.unpack_from(">I", stock, offset)[0] != before:
            raise ValueError(f"Unexpected allocator operand at {address:#x}")
        struct.pack_into(">I", expected, offset, after)
    if patched != expected:
        raise ValueError("Patched MAIN differs beyond the six reservation operands")


def allocation_record(start: int, size: int, gap: int = 0) -> bytes:
    """Stock 0x34-byte record with an empty reference tree.

    Fields +0x10/+0x14/+0x18 are start, allocated bytes, preceding free gap.
    The tree at +0x1c has its empty header at +0x20, null parent, left/right
    pointing to that header, and zero count. The real helper copies/destroys
    this empty tree; it does not allocate a tree node.
    """
    words = [0] * 13
    words[4:7] = [start, size, gap]
    words[10:12] = [RECORD + 0x20, RECORD + 0x20]
    return struct.pack(">13I", *words)


def allocator_script(capacity: int) -> tuple[list[dict], dict[str, tuple[int, int]]]:
    """Operations plus expected (mask, value) for call/peek result tags."""
    ops: list[dict] = []
    expected: dict[str, tuple[int, int]] = {}

    def poke(address: int, value: int) -> None:
        ops.append({"op": "poke32", "addr": address, "value": value})

    def call(tag: str, pc: int, args: list[int], value: int, mask: int) -> None:
        ops.append({"op": "call", "tag": tag, "pc": pc, "args": args, "max": 50_000})
        expected[tag] = (mask, value)

    def check_alloc(tag: str, file_size: int, end: int) -> None:
        rounded = (file_size + 0x200F) & ~0x1FFF
        accepted = end + rounded <= CF_BIAS + capacity
        poke(OUTPUT, SENTINEL)
        call(tag, 0x40154682, [0, 0, file_size, 0, OUTPUT], int(accepted), 0xFF)
        out_tag = tag + "-output"
        ops.append({"op": "peek", "addr": OUTPUT, "len": 4, "tag": out_tag})
        expected[out_tag] = (0xFFFFFFFF, end if accepted else SENTINEL)

    # Empty-vector branch never calls the tree helpers.
    poke(VECTOR_BEGIN, RECORD)
    poke(VECTOR_END, RECORD)
    call("total-capacity", 0x40153AFA, [], capacity, 0xFFFFFFFF)
    call("empty-free", 0x40153ABC, [], capacity, 0xFFFFFFFF)
    for tag, file_size in (
        ("new-capacity-exact", POOL_BYTES - 0x10),
        ("new-capacity-over", POOL_BYTES - 0x0F),
        ("old-capacity-exact", STOCK_BYTES - 0x10),
        ("old-capacity-over", STOCK_BYTES - 0x0F),
    ):
        check_alloc(tag, file_size, CF_BIAS)

    # The real helper reads the last live allocation and copies its empty
    # reference tree. No huge backing sample buffer is created or touched.
    size = POOL_BYTES - 0x2000
    ops.append(
        {"op": "poke", "addr": RECORD, "hex": allocation_record(CF_BIAS, size).hex()}
    )
    poke(VECTOR_END, RECORD + 0x34)
    call("tail-free", 0x40153ABC, [], capacity - size, 0xFFFFFFFF)
    check_alloc("tail-exact", 1, CF_BIAS + size)
    check_alloc("tail-over", 0x2000, CF_BIAS + size)
    # Existing holes must contribute to free space without extending the end.
    gap = 0x2000
    ops.append(
        {
            "op": "poke",
            "addr": RECORD,
            "hex": allocation_record(CF_BIAS + gap, size - gap, gap).hex(),
        }
    )
    call("gap-free", 0x40153ABC, [], capacity - size + gap, 0xFFFFFFFF)
    return ops, expected


def validate_results(output: str, expected: dict[str, tuple[int, int]]) -> dict:
    rows = [json.loads(line) for line in output.splitlines() if line.startswith("{")]
    if not any(row.get("op") == "ready" for row in rows):
        raise ValueError("Missing ready result")
    found = {}
    for row in rows:
        tag = row.get("tag")
        if tag not in expected:
            continue
        if tag in found:
            raise ValueError(f"Duplicate result {tag}")
        if row["op"] == "call":
            if not row.get("ok") or row.get("error"):
                raise ValueError(f"Firmware helper failed: {tag}: {row}")
            value = row["d0"]
        elif row["op"] == "peek":
            raw = bytes.fromhex(row["hex"])
            if len(raw) != 4:
                raise ValueError(f"Incomplete result {tag}")
            value = int.from_bytes(raw, "big")
        else:
            raise ValueError(f"Unexpected result operation for {tag}")
        mask, wanted = expected[tag]
        if value & mask != wanted:
            raise ValueError(f"{tag}: {value & mask:#x}, expected {wanted:#x}")
        found[tag] = row
    if set(found) != set(expected):
        raise ValueError(f"Missing results: {set(expected) - set(found)}")
    return found


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--stock", type=Path, default=ROOT / "firmware/Digitakt_II_OS1.17.syx"
    )
    parser.add_argument("--patched", type=Path, required=True)
    parser.add_argument("--cf-script", type=Path, required=True)
    parser.add_argument("--work", type=Path, default=ROOT / "out/runs/loop-storage")
    parser.add_argument("--timeout", type=float, default=120)
    args = parser.parse_args()
    if args.timeout <= 0 or args.timeout > 900:
        parser.error("--timeout must be in (0, 900] seconds per process")
    if hashlib.sha256(args.stock.read_bytes()).hexdigest() != STOCK_SHA:
        raise ValueError("Stock firmware SHA-256 is not DT2 1.17")
    audit_main(main_image(args.stock), main_image(args.patched))
    args.work.mkdir(parents=True, exist_ok=True)
    report = {
        "status": "failed",
        "stock_sha256": STOCK_SHA,
        "patched_sha256": hashlib.sha256(args.patched.read_bytes()).hexdigest(),
        "buffer_base": BUFFER_BASE,
        "buffer_bytes": BUFFER_BYTES,
        "scope": "Real ColdFire allocator helpers with synthetic records; no sample-load integration",
        "runs": {},
    }
    try:
        for name, syx, capacity in (
            ("stock", args.stock, STOCK_BYTES),
            ("reserved", args.patched, POOL_BYTES),
        ):
            ops, expected = allocator_script(capacity)
            script = args.work / f"{name}.json"
            script.write_text(json.dumps(ops, indent=2) + "\n", encoding="utf-8")
            command = [
                str(args.cf_script.resolve()),
                str(syx.resolve()),
                str(script.resolve()),
            ]
            if name == "reserved":
                command += ["--base-sha", STOCK_SHA]
            result = subprocess.run(
                command,
                capture_output=True,
                text=True,
                timeout=args.timeout,
                check=False,
            )
            (args.work / f"{name}.log").write_text(
                result.stdout + result.stderr, encoding="utf-8"
            )
            if result.returncode:
                raise ValueError(f"{name}: cf_script exit {result.returncode}")
            report["runs"][name] = validate_results(result.stdout, expected)
        report["status"] = "passed"
    except (ValueError, subprocess.TimeoutExpired) as error:
        report["error"] = str(error)
    (args.work / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8"
    )
    print(
        json.dumps(
            {"status": report["status"], "report": str(args.work / "report.json")}
        )
    )
    return int(report["status"] != "passed")


if __name__ == "__main__":
    raise SystemExit(main())
