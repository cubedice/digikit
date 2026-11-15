"""Pin two distinct DTII project-load caller behaviors for the host proof.

These assertions do not prove that either call site is exhaustive or safe to
hook. In particular, the second caller does not inspect the load result.
"""
import hashlib
import tomllib
from pathlib import Path

from tools.patchkit import port_resolve


def test_project_load_callers_handle_results_differently():
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )

    def code(address, length):
        return main[address - 0x40000400:address - 0x40000400 + length].hex()

    # Card wrapper calls its loader, saves d0's low byte, then branches on it.
    assert code(0x40046C62, 4) == "4ebafca2"
    assert code(0x40046C6E, 14) == "18004e924fef00184a0466044204"
    # The memory-project caller calls the separate loader, then immediately
    # calls another function instead of testing the returned d0 byte.
    assert code(0x400D5232, 8) == "4eb940043f744e94"

    # The opt-in fixture replaces exactly the call and the next instruction.
    fixture = tomllib.loads(
        (root / "tests/regression/fixtures/canopy-load-preflight/module.toml").read_text()
    )
    for op in fixture["op"]:
        address = 0x400DB3B2 + op["offset"]
        data = bytes.fromhex(code(address, op["width"]))
        assert hashlib.sha256(data).hexdigest() == op["expect_sha256"]
    assert fixture["op"][0]["value"] == 0x4EF9
    assert fixture["op"][1]["value"] == "canopy_proof_card_load_guard"


def test_memory_project_command_does_not_offer_checked_refusal():
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )

    def code(address, length):
        return main[address - 0x40000400:address - 0x40000400 + length].hex()

    # Dispatcher compares command 0x1d before preparing a 0xdd9604-byte buffer.
    assert code(0x400D5198, 8) == "701db0816600080c"
    assert code(0x400D51A0, 6) == "2f3c00dd9604"
    # The return from the load is ignored; the next call retrieves the state
    # object and then invokes the stock state transition unconditionally.
    assert code(0x400D5232, 16) == "4eb940043f744e942f004eb940135b34"
    # The state helper specifically checks state 11 and requests state 2.
    assert code(0x40135B36, 18) == "720b246f000871aa001cb280661e1f3c0002"
    # Both the project buffer and an outer temporary are released at the
    # dispatcher's common cleanup, with no observed returned status contract.
    assert code(0x400D5384, 6) == "2f026000047c"
    assert code(0x400D5804, 6) == "4e92600001a0"
    assert code(0x400D59AA, 14) == "486ebcec60022f044eb9401c6256"
    assert code(0x401C6256, 20) == "206f00042010670a2f4000044ef94011ebdc4e75"
