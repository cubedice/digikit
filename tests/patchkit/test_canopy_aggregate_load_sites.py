"""Pin the DTII 1.17 aggregate-load failure evidence, not all callers.

Static instruction windows cannot prove reachability, transaction rollback or
coverage of indirect calls. Refusing a Sound entry alone is unsafe because
the observed containing loops replace failed elements with defaults.
"""

import hashlib
from pathlib import Path

import pytest

from tools.patchkit import port_resolve

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
BASE = 0x40000400

# Loop-bound arithmetic comes directly from the pinned comparison/stride:
# bank 0x82 Sounds; kit 0x10 Sounds; project 0x2c5800/0x5800=129 kits and
# 0x22a80/0x455=128 pool Sounds (129*16+128 = 2192 total).
WINDOWS = [
    (
        "bank-130-defaults-failed-sounds",
        0x400DB854,
        "45fafb5c47f9400e65f82f042f034e92508f4a0066082f022f034e93508f52820683000004500684000004550c820000008266d67001",
    ),
    (
        "kit-16-defaults-failed-sounds",
        0x400DB932,
        "49fafa7e4bf9400e65f82f042f034e94508f4a0066082f022f034e95508f52820683000004500684000004557010b08266d8",
    ),
    (
        "project-129-kits-default-on-kit-failure",
        0x400DFABC,
        "4dfabdda2a3c400e5700487538002f044e96508f4a0066082f0420454e90588f0684000055740683000058000c83002c580066d6",
    ),
    (
        "project-128-sounds-default-on-sound-failure",
        0x400DFBE2,
        "4bfab7ce4df9400e64dc487338002f0c4e95508f4a0066144878ffff4878ffff4878ffff2f0c4e964fef001006830000045549ec04500c8300022a8066cc",
    ),
    # A containing pattern load writes the pattern before checking kit load.
    (
        "pattern-mutates-before-kit-load",
        0x400E1558,
        "2f2f001c2f022f044ebab1a0068200015c002f022f0318004ebaa3264fef00144a00",
    ),
    # The tail branch returns Sound loader status to each caller.
    (
        "sound-container-tail",
        0x400DB80C,
        "202f0004206f00084a8867144a8067104a280004660a5a882f4800086000fb8842004e75",
    ),
    ("sound-conversion-tail", 0x400DFEEE, "4a0067102f4200102f43000c241f261f6000"),
    (
        "sound-file-checks-status",
        0x40131014,
        "486b00052f2efff84eb9400db3b2508f4a0067ca",
    ),
    (
        "first-conversion-caller-checks-status",
        0x400D2BFC,
        "42a72f022f004eb9400dfecc4fef00181400679e",
    ),
    (
        "second-conversion-caller-checks-status",
        0x400D2CE4,
        "42a72f022f004eb9400dfecc4fef001c14006796",
    ),
    # No D0 status check between the load and the subsequent 0x53 notification.
    (
        "remote-conversion-caller-ignores-status",
        0x40122892,
        "42a72f032f004eb9400dfecc4878ffff2f0b1f3c0053578f4ebaeb884fef0024",
    ),
]


@pytest.mark.parametrize("name,address,expected", WINDOWS, ids=[x[0] for x in WINDOWS])
def test_aggregate_load_evidence(name, address, expected):
    if not FW.exists():
        pytest.skip("official DTII 1.17 firmware fixture not installed")
    assert (
        hashlib.sha256(FW.read_bytes()).hexdigest()
        == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    main = port_resolve.sections_of(FW)[3]
    raw = bytes.fromhex(expected)
    assert main[address - BASE : address - BASE + len(raw)] == raw, name
