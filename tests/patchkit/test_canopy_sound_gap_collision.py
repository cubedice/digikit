"""Pin evidence disproving the proposed 16-bit Sound+0x13a gap.

The stock initializer writes Sound+0x13a and stock refresh copies both bytes
into the machine row. This is not a complete inventory of their consumers.
"""
import hashlib
from pathlib import Path

from tools.patchkit import port_resolve

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x40000400


def test_initializer_and_refresh_use_candidate_bytes_on_dt2_117():
    fw = ROOT / "firmware/Digitakt_II_OS1.17.syx"
    assert hashlib.sha256(fw.read_bytes()).hexdigest() == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    main = port_resolve.sections_of(fw)[3]

    def code(addr, expected):
        raw = bytes.fromhex(expected)
        assert main[addr - BASE:addr - BASE + len(raw)] == raw

    # 0x400e65c0 passes Sound+0xa2 to the helper at 0x400e6464.
    code(0x400E65B8, "486a01274ebafe82486a00a24ebafe9e")
    # Helper ends `moveq #1,d0; move.b d0,0x98(a0)`, so the write is
    # Sound+0xa2+0x98 = Sound+0x13a. Initial value is not preserved.
    code(0x400E6464, "4200206f000411400081700d11400082420011400083700e114000847001114000984e75")
    # The stock row refresh copies Sound+0xa2 for 0x9a bytes, including
    # both candidate bytes at +0x13a and +0x13b.
    code(0x4002D4A6, "4878009a486a00a2203c0000009a4c020800068080003cd02f004e93")
    # The loader's last metadata memcpy stops at Sound+0x139, which explains
    # why serialized mapping alone did not reveal the live initializer write.
    code(0x400DB7E6, "48780010486b0440486a012a4eb9401363884fef000c")
