"""Pin stock DTII 1.17 evidence relevant to a Sound parameter-0 handle.

These are instruction/table checks, not proof that every caller is known or
that stock UI gestures invoke the generic setter for a CANOPY Sound.
"""
import hashlib
from pathlib import Path

from tools.patchkit import port_resolve

ROOT = Path(__file__).resolve().parents[2]
BASE = 0x40000400


def test_dt2_117_parameter_zero_collision_sites():
    fw = ROOT / "firmware/Digitakt_II_OS1.17.syx"
    assert hashlib.sha256(fw.read_bytes()).hexdigest() == (
        "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    main = port_resolve.sections_of(fw)[3]

    def read(addr, size):
        return main[addr - BASE:addr - BASE + size]

    def code(addr, raw):
        expected = bytes.fromhex(raw)
        assert read(addr, len(expected)) == expected

    def word(addr):
        return int.from_bytes(read(addr, 4), "big")

    def string(addr):
        start = addr - BASE
        return main[start:main.index(b"\0", start)].decode("ascii")

    # Descriptor rows have their Sound index at +4 and long name at +0x28.
    table, stride = 0x4020F59C, 0x3C
    rows = [i for i in range(275) if word(table + i * stride + 4) == 0]
    assert rows == [0x13, 0x14]
    assert string(word(table + 0x13 * stride + 0x28)) == "None"
    assert string(word(table + 0x14 * stride + 0x28)) == "Trig Note"
    # The generic Sound setter resolves a descriptor and only rejects a
    # negative index or an index above 70; 0 passes both comparisons.
    code(0x4003BEAA, "2f024eb9400da03c588f26006d00011a206a001022502f08206900284e90588f7046b0836d000102")
    # It obtains the Sound pointer through vtable +0x28, then stores a word
    # at Sound+0x14+index*2. This site accepts index 0 if reached with it.
    code(0x4003BF5E, "206a0010364422502f08206900284e90588f204031843a14")
    # Whole-track refresh copies all 0x8e parameter bytes including p0 to
    # the track mirror (source Sound+0x14, destination 0x80003362+track*0x8e).
    code(0x4002D488, "4878008e260a0683000000142f0347f9401363880680800033622f004e93")
    # Generic incremental updates also write the matching mirror element;
    # this function itself does not write the original Sound.
    code(0x4002D7A4, "2f027410222f0008202f000c226f001041f980003340b480660a43e9048031819a02603674474c00280043f12810243c0000045031819a02")
    # The lock-substitution loop starts at index 0, copies a nonnegative lock
    # value into Sound+0x14+index*2, and loops through indices 0..70.
    code(0x40047E90, "95ca2f0a2f022f0320454e904fef000c4a806d08206efff83180aa14528a7047b08a66de")
    # Lock getter permits parameter index 0 through 70.
    code(0x400405FA, "700fb08465f6727fb28365f0103c0046b08265e8")
    # Trig-event lock collection loops from index 0 to 70 independently of
    # the Sound-copy substitution path, so guarding only that path is not a
    # complete analysis of index-0 lock effects.
    code(0x400D6916, "4fefffe8704748d70c3c262f001c4281226f0020206f00244c0038004200d6882443d5c0752a50a078ffb882672828027affef8cd484d4af002c79702a02ba84671428015281e78c47f02a0245f1481434803553000252807447b48066c2234100084cd70c3c4fef00184e75")
