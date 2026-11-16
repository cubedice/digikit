"""Pin the DTII 1.17 facts that make the CANOPY record extension invisible to stock.

The extension (modules/core/adapters/instrument-play/src/cf/sound-record.c) uses
record positions 4..28 step 4 and 77..89. These checks read the image bytes; they
do not run the firmware.
"""

import hashlib
import struct
from pathlib import Path

from tools.patchkit import port_resolve

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x40000400
EXTENSION = list(range(4, 29, 4)) + list(range(77, 90))


def main():
    fw = ROOT / "firmware/Digitakt_II_OS1.17.syx"
    assert hashlib.sha256(fw.read_bytes()).hexdigest() == (
        "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    return port_resolve.sections_of(fw)[3]


def test_extension_positions_are_zero_filled_and_never_loaded_into_a_live_parameter():
    m = main()

    def code(addr, raw):
        expected = bytes.fromhex(raw)
        assert m[addr - BASE : addr - BASE + len(expected)] == expected

    # Writer: memset(record + 0x1c, 0, 0xb4) clears positions 0..89, then the
    # 69-entry save permutation writes parameters 0..68.
    code(0x400DBADA, "487800b442a7486a001c4eb9401363d0")
    code(0x400DBAEE, "43f940213a50")
    code(0x400DBB0A, "0c800000008a")
    save = struct.unpack_from(">69I", m, 0x40213A50 - BASE)
    assert set(save).isdisjoint(EXTENSION) and 32 not in save
    # Loader: positions 0..76 in ascending order (d0 += 2 up to 0x9a), each to
    # Sound + 0x14 + 2 * load[position]; positions 77..89 are never read.
    code(0x400DB426, "43f940213b644280204949f3081c")
    code(0x400DB43C, "548035941a000c800000009a66e6")
    load = struct.unpack_from(">77I", m, 0x40213B64 - BASE)
    to_param0 = [p for p, v in enumerate(load) if v == 0]
    assert to_param0 == [0, 4, 8, 12, 16, 20, 24, 28, 32]
    # Position 32 is the last write to parameter 0 and stays zero, so stock
    # ends a load exactly as with a stock record; the extension never uses it.
    assert all(p < 32 for p in EXTENSION if p < 77)
    assert all(p in to_param0 or p >= len(load) for p in EXTENSION)


def test_kit_table_and_sound_pool_literals():
    m = main()

    def word(addr):
        return int.from_bytes(m[addr - BASE : addr - BASE + 4], "big")

    # muls.l #0x5574 then addi.l #kits; muls.l #0x450 then addi.l #pool with
    # the pool index bounded to 127 (cmp.l d5 against 0x7f).
    def code(addr, raw):
        expected = bytes.fromhex(raw)
        assert m[addr - BASE : addr - BASE + len(expected)] == expected

    code(0x4003406E, "223c000055744c010800")
    code(0x4003407A, "0680")
    assert word(0x4003407C) == 0x426532B8
    code(0x40120288, "707f")
    code(0x4012028E, "b0856512303c04504c050800")
    code(0x4012029A, "0680")
    assert word(0x4012029C) == 0x4291377A
