"""Pin representative Sound/kit copy-site evidence in the DTII 1.17 image.

This is a regression for the documented classification, not a completeness
proof: other paths may use register-sized or inlined copies.  In particular,
matching a size alone never establishes the copied object's type.
"""

import hashlib
from pathlib import Path

import pytest

from tools.patchkit import port_resolve

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
BASE = 0x40000400

# Windows include the relevant getter, length and call.  The Sound getter is
# track vtable +0x28; the project kit getter is +0x30.  Two indirect calls
# additionally pin where their callee register receives stock memcpy.
WINDOWS = [
    (
        "sound-track-to-scratch",
        0x4001A7FA,
        "224020512f00206800284e90487804502f002f0b4eb9401363884fef0018",
    ),
    (
        "sound-to-track",
        0x40052192,
        "20522f0a206800284e9048780450222efff058812f012f004eb940136388205242a7",
    ),
    (
        "kit-to-kit",
        0x40035E22,
        "2052487800802f0a206800304e9020522f032f0a2068003024004e90487855742f002f024eb940136388205242a7",
    ),
    ("sound-indirect-callee", 0x40045666, "47f940136388"),
    (
        "sound-indirect-call",
        0x40045672,
        "224020512f00260f068300000248206800284e90487804502f002f034e93",
    ),
    ("kit-indirect-callee", 0x400479E6, "47f9401363884e93"),
    (
        "kit-indirect-call",
        0x400479EE,
        "206a00f42f02486a00f420680030282efff006840001db9045eefffc4e90487855742f002f044e934fef0020206efff0",
    ),
    # Same 0x450 length, but target is stock memset, not stock memcpy.
    (
        "sound-sized-memset-is-not-copy",
        0x400533F6,
        "248c2548000c4878045042a7486a00104eb9401363d04fef000c",
    ),
]


@pytest.mark.parametrize("name,address,expected", WINDOWS, ids=[w[0] for w in WINDOWS])
def test_pinned_copy_site_windows(name, address, expected):
    if not FW.exists():
        pytest.skip("the official DTII 1.17 firmware fixture is not installed")
    assert hashlib.sha256(FW.read_bytes()).hexdigest() == (
        "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    main = port_resolve.sections_of(FW)[3]
    actual = main[address - BASE : address - BASE + len(bytes.fromhex(expected))]
    assert actual.hex() == expected, name
