"""Reservation coordinates, reviewed firmware patch scope, and proof oracles."""

import json
import struct
import tomllib
from pathlib import Path

import pytest

from tools.patchkit import pk_loop_storage_proof as proof

ROOT = Path(__file__).resolve().parents[2]
MODULE = ROOT / "modules/tools/loop-storage"


@pytest.fixture(scope="module")
def stock_main():
    firmware = ROOT / "firmware/Digitakt_II_OS1.17.syx"
    if not firmware.exists():
        pytest.skip("DT2 1.17 firmware is not installed")
    return proof.main_image(firmware)


def patched_from_manifest(stock):
    manifest = tomllib.loads((MODULE / "module.toml").read_text(encoding="utf-8"))
    patched = bytearray(stock)
    for op in manifest["op"]:
        assert op["op"] == "write" and op["anchor"] == "cf.main"
        assert op["width"] == 4
        struct.pack_into(">I", patched, op["offset"], op["value"])
    return bytes(patched)


def test_sample_offset_bias_and_preview_boundary():
    assert proof.cf_offset_to_dsp(0x400) == 0x8422B9C8
    assert proof.cf_offset_to_dsp(0x18800400) == 0x9CA2B9C8
    assert proof.cf_offset_to_dsp(0x19000400) == 0x9D22B9C8
    assert proof.BUFFER_BASE + proof.BUFFER_BYTES == proof.PREVIEW_BASE
    assert proof.BUFFER_BYTES == 8 * 1024 * 1024
    assert proof.BUFFER_BYTES >= 16 * 48_000 * 2 * 4  # four bars at 60 BPM
    # Relative pages, not absolute CF offsets: the last legal pool page ends
    # exactly at the reservation, and the reservation ends before preview.
    assert proof.DSP_SAMPLE_BASE + (0x187FF + 1) * 4096 == proof.BUFFER_BASE
    assert proof.DSP_SAMPLE_BASE + 0x19000 * 4096 == proof.PREVIEW_BASE
    with pytest.raises(ValueError, match="start at"):
        proof.cf_offset_to_dsp(0x3FF)


def test_manifest_changes_only_reviewed_six_operands(stock_main):
    manifest = tomllib.loads((MODULE / "module.toml").read_text(encoding="utf-8"))
    assert manifest["module"]["ports"] == ["dt2-1.17"]
    assert len(manifest["op"]) == 6
    assert {proof.MAIN_BASE + op["offset"] for op in manifest["op"]} == set(
        proof.PATCH_SITES
    )
    patched = patched_from_manifest(stock_main)
    proof.audit_main(stock_main, patched)
    for address in proof.EXCLUDED_SITES:
        offset = address - proof.MAIN_BASE
        assert patched[offset : offset + 4] == stock_main[offset : offset + 4]


def test_unrelated_matching_capacity_literal_is_rejected(stock_main):
    patched = bytearray(patched_from_manifest(stock_main))
    # A blind literal replacement also changes this unrelated numeric table.
    offset = 0x401E9C74 - proof.MAIN_BASE
    assert struct.unpack_from(">I", patched, offset)[0] == proof.STOCK_BYTES
    struct.pack_into(">I", patched, offset, proof.POOL_BYTES)
    with pytest.raises(ValueError, match="beyond the six"):
        proof.audit_main(stock_main, bytes(patched))


def test_preview_movement_and_wrong_stock_are_rejected(stock_main):
    patched = bytearray(patched_from_manifest(stock_main))
    struct.pack_into(
        ">I", patched, 0x401541FC - proof.MAIN_BASE, proof.POOL_BYTES + 0x400
    )
    with pytest.raises(ValueError, match="beyond the six"):
        proof.audit_main(stock_main, bytes(patched))
    wrong_stock = bytearray(stock_main)
    wrong_stock[0] ^= 1
    with pytest.raises(ValueError, match="SHA-256"):
        proof.audit_main(bytes(wrong_stock), bytes(patched))


def test_packed_provider_descriptor_matches_reserved_interval():
    path = MODULE / "build/loop-storage-dsp.dko.json"
    if not path.exists():
        pytest.skip("Build the provider object first")
    obj = json.loads(path.read_text(encoding="utf-8"))
    symbol = next(s for s in obj["symbols"] if s["name"] == "_loop_buffer_info")
    section = next(s for s in obj["sections"] if s["name"] == symbol["section"])
    words = struct.unpack_from("<4I", bytes.fromhex(section["hex"]), symbol["value"])
    assert words == (0x4C425546, 1, proof.BUFFER_BASE, proof.BUFFER_BYTES)


def test_optional_storage_export_uses_direct_global_name():
    # object.externs aliases resolve primitives/anchors, not another module's
    # global symbol. An optional alias silently became zero even with the
    # provider selected, causing the real runtime to return E_DSP_IMPORT.
    folder = ROOT / "modules/core/dyndsp"
    manifest = tomllib.loads((folder / "module.toml").read_text(encoding="utf-8"))
    obj = next(item for item in manifest["object"] if item["devices"] == ["dt2"])
    assert obj["optional"] == ["_loop_buffer_info"]
    assert "_loop_buffer_info" not in obj["externs"]
    assert "_dd_loop_buffer_info" not in obj["externs"]
    source = (folder / "src/cf/dyndsp-dt2.s").read_text(encoding="utf-8")
    assert ".long   _loop_buffer_info" in source
    assert "_dd_loop_buffer_info" not in source


def test_built_optional_export_relocates_the_actual_provider_symbol():
    folder = ROOT / "modules/core/dyndsp"
    path = folder / "build/dyndsp-dt2.dko.json"
    provider_path = MODULE / "build/loop-storage-dsp.dko.json"
    if not path.exists() or not provider_path.exists():
        pytest.skip("Build dyndsp and the provider object first")
    obj = json.loads(path.read_text(encoding="utf-8"))
    provider = json.loads(provider_path.read_text(encoding="utf-8"))
    exported = next(
        item for item in provider["symbols"] if item["name"] == "_loop_buffer_info"
    )
    assert exported["global"] and exported["section"] is not None
    imported = next(item for item in obj["symbols"] if item["name"] == exported["name"])
    assert imported["section"] is None
    section = next(item for item in obj["sections"] if item["name"] == ".rodata")
    data = bytes.fromhex(section["hex"])
    count = struct.unpack_from(">I", data, 36)[0]
    entries = [
        index
        for index in range(count)
        if data[40 + index * 32 : 68 + index * 32].split(b"\0", 1)[0]
        == b"_loop_buffer_info"
    ]
    assert len(entries) == 1
    relocation = next(
        item
        for item in obj["relocs"]
        if item["section"] == ".rodata" and item["offset"] == 68 + entries[0] * 32
    )
    assert relocation["symbol"] == exported["name"]


def test_execution_probe_has_both_sides_of_capacity_and_tail():
    stock_ops, stock = proof.allocator_script(proof.STOCK_BYTES)
    reserved_ops, reserved = proof.allocator_script(proof.POOL_BYTES)
    # Both images receive identical operations; only their expected answers
    # differ. This prevents a comparison that silently uses different inputs.
    assert stock_ops == reserved_ops
    assert stock["new-capacity-exact"] == reserved["new-capacity-exact"] == (0xFF, 1)
    assert stock["new-capacity-over"] == (0xFF, 1)
    assert reserved["new-capacity-over"] == (0xFF, 0)
    assert stock["tail-over"] == (0xFF, 1)
    assert reserved["tail-over"] == (0xFF, 0)
    assert stock["old-capacity-over"] == reserved["old-capacity-over"] == (0xFF, 0)
    assert all(op["max"] == 50_000 for op in stock_ops if op["op"] == "call")


def test_failed_missing_and_duplicate_probe_results_cannot_pass():
    ready = {"op": "ready", "icount": 1}
    good = {"op": "call", "tag": "boundary", "ok": True, "d0": 1, "error": None}
    expected = {"boundary": (0xFF, 1)}

    def output(*rows):
        return "\n".join(json.dumps(row) for row in rows)

    assert proof.validate_results(output(ready, good), expected)["boundary"]["d0"] == 1
    for rows in (
        (ready,),
        (good,),
        (ready, good, good),
        (ready, {**good, "ok": False}),
        (ready, {**good, "d0": 0}),
        (ready, {**good, "op": "peek", "hex": "00"}),
    ):
        with pytest.raises(ValueError):
            proof.validate_results(output(*rows), expected)
