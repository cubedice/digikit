"""Disposable Sound-copy observation point, never a production hook.

The image-byte test is static. The opt-in ColdFire execution test compares the
unpatched stock entry with our trampoline; it is not a hardware timing claim or
a proof that all Sound-copy paths pass through this function.
"""

import hashlib
import os
import tomllib
from pathlib import Path

import pytest

from tools.patchkit import patchkit, pk_cfcall, pk_toolchains, port_resolve

ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / "tests/regression/fixtures/canopy-copy-probe"
FIRMWARE = ROOT / "firmware/Digitakt_II_OS1.17.syx"
BASE = 0x47E8B000
SOURCE = 0x47EC0004
DESTINATION = 0x47ED0004
STOCK = 0x40136388


def test_observation_patch_replaces_the_pinned_complete_stock_entry():
    main = port_resolve.sections_of(FIRMWARE)[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    assert main[STOCK - 0x40000400 : STOCK - 0x40000400 + 12].hex() == (
        "226f0004206f0008202f000c"
    )
    ops = tomllib.loads((FIXTURE / "module.toml").read_text())["op"]
    assert [(op["offset"], op["width"], op["value"]) for op in ops] == [
        (0x5AFD6, 2, 0x4EF9),
        (0x5AFD8, 4, "canopy_copy_probe_entry"),
        (0x5AFDC, 2, 0x4E71),
    ]
    for op in ops:
        start = 0x400DB3B2 + op["offset"] - 0x40000400
        assert (
            hashlib.sha256(main[start : start + op["width"]]).hexdigest()
            == op["expect_sha256"]
        )


@pytest.mark.skipif(
    not os.environ.get("DIGI_CANOPY_COPY_CF_CALL")
    or pk_toolchains.m68k_bin("gcc") is None
    or not FIRMWARE.exists(),
    reason="set DIGI_CANOPY_COPY_CF_CALL to the prebuilt runner and provide the m68k toolchain",
)
def test_trampoline_matches_stock_and_has_bounded_instruction_overhead(monkeypatch):
    runner = Path(os.environ["DIGI_CANOPY_COPY_CF_CALL"])
    assert runner.is_file(), "the explicit runner must already exist"
    monkeypatch.setattr(pk_cfcall, "EXAMPLE", runner)
    monkeypatch.setattr(pk_cfcall, "_built", True)
    linked = patchkit.cf_link([FIXTURE / "src/cf/copy-probe.s"], BASE, {})
    probe = linked.symbols["canopy_copy_probe_entry"]
    lengths = (0, 1, 15, 16, 0x44F, 0x450, 0x451, 0x5574)
    data = bytes((n * 37 + 11) & 0xFF for n in range(max(lengths)))
    calls = [
        {
            "pc": pc,
            "args": [DESTINATION, SOURCE, length],
            "reads": [(DESTINATION - 4, length + 8), (SOURCE - 4, length + 8)],
        }
        for length in lengths
        for pc in (STOCK, probe)
    ]
    initial = {
        linked.base: linked.image,
        linked.base + len(linked.image): bytes(linked.bss_size),
        SOURCE - 4: b"\x91" * 4 + data + b"\x92" * 4,
        DESTINATION - 4: b"\xa5" * (max(lengths) + 8),
    }
    results = pk_cfcall.CfMachine(FIRMWARE).run(initial, calls)
    for length, stock, hooked in zip(lengths, results[::2], results[1::2], strict=True):
        assert stock.ok and hooked.ok and not stock.clobbered and not hooked.clobbered
        assert stock.reads == hooked.reads
        assert stock.d == hooked.d and stock.a == hooked.a
        # Direct probe entry excludes the patched firmware entry jump. With the
        # actual jump add one instruction: five ordinary, six matching.
        assert hooked.insns - stock.insns == (5 if length == 0x450 else 4)
