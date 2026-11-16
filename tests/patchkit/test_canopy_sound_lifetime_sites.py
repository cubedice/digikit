"""Check the fixture's Sound-handle invalidation prototypes.

Matching these bytes is not proof that the two sites cover all Sound lifetime
operations or production safety. Only the disposable emulator fixture links it.
"""

import hashlib
import tomllib
from pathlib import Path

from tools.patchkit import port_resolve


def test_sound_lifetime_prototype_sites_match_dt2_117():
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    base = 0x40000400

    def code(address, length):
        return main[address - base : address - base + length]

    # Non-null Sound is loaded into a2 and checked at the initializer entry.
    assert code(0x400E64E4, 18).hex() == "246f002c242f0034262f00384a8a670000fa"
    # Both complete instructions are replayed by ip_sound_init_gap.
    assert code(0x400E65C8, 8).hex() == "42aa013c45ea0140"
    # Getter result and sign-extended old type are retained in a0 and d6.
    assert code(0x4005178A, 6).hex() == "20407d2800a2"
    assert code(0x4005179A, 10).hex() == "508f2040114200a2b486"


def test_proof_invalidation_patches_match_stock_instructions():
    """The fixture must patch the audited sites, not adjacent instructions."""
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    fixture = tomllib.loads(
        (root / "tests/regression/fixtures/canopy-sound-proof/module.toml").read_text()
    )
    ops = fixture["op"]
    for addr, symbol in (
        (0x400E65C8, "ip_sound_init_gap"),
        (0x4005179E, "ip_sound_type_gap"),
    ):
        offset = addr - 0x400DB3B2
        branch = next(op for op in ops if op["offset"] == offset)
        destination = next(op for op in ops if op["offset"] == offset + 2)
        assert branch["value"] == 0x4EF9 and branch["width"] == 2
        assert destination["value"] == symbol and destination["width"] == 4
        for op in (branch, destination):
            start = 0x400DB3B2 + op["offset"] - 0x40000400
            assert (
                hashlib.sha256(main[start : start + op["width"]]).hexdigest()
                == op["expect_sha256"]
            )
