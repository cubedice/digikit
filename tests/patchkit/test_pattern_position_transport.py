"""Bounded per-frame position receiver, optional imports, and tail ownership."""

import json
import struct
import tomllib
from functools import reduce
from operator import xor
from pathlib import Path

import pytest

from tools.patchkit import pk_sharc, pk_toolchains

ROOT = Path(__file__).resolve().parents[2]
PROVIDER = ROOT / "modules/core/adapters/pattern-position"
LINK = ROOT / "modules/core/adapters/dsp-link"
MAGIC, SALT = 0x50504F53, 0x7B91D6E3
RX, DATA, BASE, BACK = 0x200A0000, 0x20090000, 0xB90000, 0xB9F000
SUFFIX = 0xA7C
SAVED = tuple(
    [f"R{i}" for i in range(16)]
    + [f"S{i}" for i in range(16)]
    + [
        "I0",
        "I1",
        "I2",
        "I3",
        "M0",
        "M1",
        "L0",
        "L1",
        "MODE1",
        "ASTATX",
        "ASTATY",
        "STKYX",
        "STKYY",
    ]
)


def wire(sequence=7):
    words = [MAGIC, 2, sequence, 4, 2, 3, 0x47123456, 5, 0, 11, 0, 37, 50, 6, 1]
    return words + [reduce(xor, words, SALT)]


def test_tail_ranges_do_not_overlap():
    link_start, link_words, position_words = 0x804, 174 - 16, 16
    assert link_start + link_words * 4 == SUFFIX
    assert SUFFIX + position_words * 4 == 0xABC
    assert link_words - 3 == 155
    # PUSHR entry coordinates are halfwords, with data at entry*4+2.
    assert (0xABC // 2 - 2 * position_words) * 2 == SUFFIX


def test_direct_optional_symbols_are_consistent():
    manifest = tomllib.loads((LINK / "module.toml").read_text())
    cf = next(o for o in manifest["object"] if o["place"] == "cf.expansion")
    dt2 = next(o for o in manifest["object"] if o.get("devices") == ["dt2"])
    dn2 = next(o for o in manifest["object"] if o.get("devices") == ["dn2"])
    assert "pp_write_frame" in cf["optional"]
    assert "pp_write_frame" not in cf.get("externs", {})
    assert "_pattern_position_info" in dt2["optional"]
    assert "_pattern_position_info" not in dn2["optional"]
    dyndsp = tomllib.loads(
        (ROOT / "modules/core/adapters/dyndsp/module.toml").read_text()
    )
    obj = next(o for o in dyndsp["object"] if o["devices"] == ["dt2"])
    assert "_pattern_position_info" in obj["optional"]
    assert "_pattern_position_info" not in obj.get("externs", {})
    provider = tomllib.loads((PROVIDER / "module.toml").read_text())
    assert provider["module"]["ports"] == ["dt2-1.17"]
    decls = tomllib.loads((ROOT / "devices/fw/dsp/pattern.toml").read_text())
    rule = next(
        r["rule"] for r in decls["site"] if r["name"] == "dsp.pattern.site.block_entry"
    )
    assert rule["offset"] == 0x34 and rule["len"] == 6


@pytest.mark.parametrize(
    "name", ["_pattern_position_info", "_loop_controls_info", "_loop_ui_status"]
)
def test_built_import_uses_the_provider_name(name):
    path = ROOT / "modules/core/adapters/dyndsp/build/dyndsp-dt2.dko.json"
    obj = json.loads(path.read_text())
    data = bytes.fromhex(
        next(s["hex"] for s in obj["sections"] if s["name"] == ".rodata")
    )
    count = struct.unpack_from(">I", data, 36)[0]
    offsets = [
        68 + i * 32
        for i in range(count)
        if data[40 + i * 32 : 68 + i * 32].split(b"\0", 1)[0] == name.encode()
    ]
    assert len(offsets) == 1, "rebuild core/dyndsp after adding a provider import"
    assert any(
        r["section"] == ".rodata" and r["offset"] == offsets[0] and r["symbol"] == name
        for r in obj["relocs"]
    )


@pytest.fixture(scope="module")
def receiver():
    if pk_toolchains.selache_dir() is None:
        pytest.skip("Selache is not installed")
    obj = pk_sharc.assemble((PROVIDER / "src/dsp/pattern_position.s").read_text())
    sections, address, dm = {}, DATA, {}
    for name, (blob, align, _bss) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        dm.update(
            {
                address // 4 + i: int.from_bytes(blob[4 * i : 4 * i + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob)
    syms = {
        name: sections[sec] + offset
        for name, (sec, offset, _global) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(obj, BASE, {"_pp_block_back": BACK, **syms})
    return linked, syms, dm


def receive(receiver, words, *, previous=None, seen=False, mode=0x1011800):
    linked, syms, initial = receiver
    dm = dict(initial)
    record = syms["_pattern_position_info"] // 4
    dm.update({(RX + SUFFIX) // 4 + i: value for i, value in enumerate(words)})
    dm[(RX + SUFFIX) // 4 - 1] = 0x99118822
    dm[(RX + SUFFIX) // 4 + 16] = 0x33774466
    if previous is not None:
        dm.update({record + i: value for i, value in enumerate(previous)})
    dm[syms["_pp_seen"] // 4] = int(seen)
    regs = {f"R{i}": 0x12340000 + i for i in range(16)}
    regs.update({f"S{i}": 0x76540000 + i for i in range(16)})
    regs.update(
        I0=0x20080000,
        I1=0x20081000,
        I2=0x20082000,
        I3=RX,
        M0=7,
        M1=0xFFFFFFFD,
        L0=64,
        L1=128,
        MODE1=mode,
        ASTATX=0,
        ASTATY=0,
        STKYX=0,
        STKYY=0,
    )
    out = pk_sharc.run_call(
        linked,
        "_pp_block_entry",
        regs=regs,
        dm_words=dm,
        reads={"record": (record, 20), "wire": ((RX + SUFFIX) // 4 - 1, 18)},
        read_regs=SAVED,
        max_steps=500,
        return_sw=BACK,
    )
    assert out["halt"]["reason"] == "breakpoint" and out["halt"]["pc_sw"] == BACK, out
    assert out["instructions"] < 400
    for name in SAVED:
        assert out["regs"][name] == regs[name], (name, out["regs"][name], regs[name])
    assert out["wire"] == [0x99118822, *words, 0x33774466]
    return out["record"]


@pytest.mark.parametrize("mode", [0x1011800, 0x1013800, 0x1213800])
def test_receiver_publishes_before_return_and_preserves_context(receiver, mode):
    words = wire()
    record = receive(receiver, words, mode=mode)
    assert record == words + [1, 0, 1, 0]


@pytest.mark.parametrize("index", [0, 1, 5, 14, 15])
def test_bad_header_or_checksum_clears_validity(receiver, index):
    previous = wire() + [30, 2, 28, 1]
    bad = wire(8)
    bad[index] ^= 1
    record = receive(receiver, bad, previous=previous, seen=True)
    assert record[0] == record[5] == 0
    assert record[16:19] == [31, 3, 28]


def test_duplicate_rejected_and_counter_wrap_accepted(receiver):
    previous = wire(0xFFFFFFFF) + [0xFFFFFFFF, 2, 5, 1]
    duplicate = receive(receiver, wire(0xFFFFFFFF), previous=previous, seen=True)
    assert duplicate[0] == duplicate[5] == 0
    assert duplicate[16:19] == [0, 3, 5]
    wrapped = receive(receiver, wire(0), previous=previous, seen=True)
    assert wrapped[:16] == wire(0)
    assert wrapped[16:] == [0, 2, 6, 1]


def test_backward_transmit_sequence_is_rejected(receiver):
    previous = wire(10) + [42, 2, 40, 1]
    record = receive(receiver, wire(9), previous=previous, seen=True)
    assert record[0] == record[5] == 0
    assert record[16:19] == [43, 3, 40]


def test_gap_count_and_semantically_invalid_snapshot_are_explicit(receiver):
    previous = wire(10) + [42, 0, 40, 1]
    words = wire(13)
    words[5] = 0
    words[15] = reduce(xor, words[:15], SALT)
    record = receive(receiver, words, previous=previous, seen=True)
    assert record[:16] == words
    assert record[16:] == [43, 0, 41, 3]
    assert record[5] == 0  # valid wire does not fabricate valid sequencer semantics
