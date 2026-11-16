"""Pin the synthetic memory-recall cancellation fixture to DTII 1.17."""

import hashlib
import tomllib
from pathlib import Path

from tools.patchkit import port_resolve


def test_memory_cancel_fixture_pins_loader_and_cleanup_sites():
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    manifest = tomllib.loads(
        (
            root / "tests/regression/fixtures/canopy-memory-cancel/module.toml"
        ).read_text()
    )
    for op in manifest["op"]:
        address = 0x400DB3B2 + op["offset"]
        data = main[address - 0x40000400 : address - 0x40000400 + op["width"]]
        assert hashlib.sha256(data).hexdigest() == op["expect_sha256"]
    assert manifest["op"][0]["value"] == 0x4EF9
    assert manifest["op"][1]["value"] == "canopy_memory_cancel_guard"
    assert manifest["op"][2]["value"] == 0x4EF9
    assert manifest["op"][3]["value"] == "canopy_memory_cleanup_probe"
    for address, expected in (
        (0x402469FF, b"ACCEPT NEW RECALL\0"),
        (0x40246A11, b"CANCEL RECALL\0"),
    ):
        assert (
            main[address - 0x40000400 : address - 0x40000400 + len(expected)]
            == expected
        )
