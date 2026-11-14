"""Check the experimental inline Sound payload against the stock DTII 1.17 map."""

import hashlib
import struct
from pathlib import Path

from tools.patchkit import port_resolve


def test_sound_padding_is_unused_by_the_stock_save_and_load_maps():
    root = Path(__file__).resolve().parents[2]
    main = port_resolve.sections_of(root / "firmware/Digitakt_II_OS1.17.syx")[3]
    assert hashlib.sha256(main).hexdigest() == (
        "a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964"
    )
    base = 0x40000400
    save = struct.unpack_from(">69I", main, 0x40213A50 - base)
    load = struct.unpack_from(">77I", main, 0x40213B64 - base)
    padding = list(range(4, 33, 4)) + list(range(77, 90))
    assert len(set(save)) == 69
    assert [i for i in range(90) if i not in save] == padding
    assert all(load[i] == 0 for i in padding if i < len(load))
