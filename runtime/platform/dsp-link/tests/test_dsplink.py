"""core/dsp-link (docs/findings/31-dsp-link.md): static checks of the module against the two stock images, the
ColdFire half (dl_send, dl_poll, dl_tick) on the native ColdFire core, and the window dispatch of the DSP half (windows 3
and 4) on the SHARC core. The whole channel runs in the coupled emulator: `uv run python -m tools.patchkit.pk_dsplink
proof` (tools/patchkit/pk_dsplink.py).

Skips what needs a stock firmware or the m68k toolchain when they are absent."""

import hashlib
import re
import struct
import tomllib

import pytest
from support.paths import REPO_ROOT

from tools.firmware import config
from tools.patchkit import patchkit as front_end  # noqa: E402
from tools.patchkit import pk_device, pk_toolchains

ROOT = REPO_ROOT
MOD = ROOT / "runtime" / "platform" / "dsp-link"
IMAGES = {
    "dt2-1.17": "Digitakt_II_OS1.17.syx",
    "dn2-1.12": "Digitone_II_OS1.12.syx",
}


# the DSP's frame-tail geometry per device is the device file's ([digikit] dsp_tail_off, dsp_tail_words; `patchkit gen` writes it into
# build/gen/device-<dev>.inc); the firmware's call site (finding 23, S4b) is checked against it below
def geometry(dev: str) -> tuple[int, int]:
    d = pk_device.load(dev)["digikit"]
    return d["dsp_tail_off"], d["dsp_tail_words"]


def gen_inc(dev: str) -> str:
    return (MOD / "build" / "gen" / f"device-{dev}.inc").read_text()


MAGIC, ACK_MAGIC, ACK_OFF = 0xD15C, 0xA5D1, 0x60
BASE = 0x47E8B000
DATA = 0x47EC0000
needs_gcc = pytest.mark.skipif(
    pk_toolchains.m68k_bin("gcc") is None, reason="m68k toolchain not installed"
)


def define(text: str, name: str) -> int:
    m = re.search(rf"#define\s+{name}\s+(0x[0-9A-Fa-f]+|\d+)u?", text)
    assert m, name
    return int(m.group(1), 0)


def port(name: str) -> dict:
    return tomllib.loads((ROOT / "ports" / f"{name}.toml").read_text())


# The anchors core/dsp-link links against (ports/fw/link.toml and ports/fw/dsp/link.toml).
DSPLINK_ANCHORS = (
    "cf.link.site.block_isr.frame_call",
    "cf.link.fn.frame",
    "cf.link.site.reply_copy_lit",
    "cf.link.site.tx_queue_lit",
    "dsp.link.site.block_tail",
)


def anchors(name: str) -> dict:
    return {a["name"]: a for a in port(name)["anchor"] if a["name"] in DSPLINK_ANCHORS}


def main_os(image: str) -> bytes:
    from tools.patchkit import port_resolve

    return port_resolve.sections_of(config.stock(IMAGES[image]))[3]


def test_objects_are_current():
    front_end.build_module(MOD, check=True)


def test_constants_agree_between_the_two_sides():
    c = (MOD / "src/cf/dsp-link.c").read_text()
    s = (MOD / "src/dsp/dsp-link.inc").read_text()
    for name, want in (
        ("DL_MAGIC", MAGIC),
        ("DL_ACK_MAGIC", ACK_MAGIC),
        ("DL_ACK_OFF", ACK_OFF),
    ):
        assert define(c, name) == want == define(s, name)
    # the area of each device: the DL_AREA_BYTES define, the .PKBSS literal and the word count agree, and the 17-bit offset reaches it
    for dev in pk_device.dsp_devices():
        d = gen_inc(dev)
        area = pk_device.load(dev)["digikit"]["dsp_area_bytes"]
        assert define(d, "DL_AREA_BYTES") == area
        assert define(d, "DL_AREA_WORDS") * 4 == define(d, "DL_AREA_BYTES")
        # the ColdFire side checks against the contract's maximum (generated from sdk.toml), which a device's area does not pass
        h = (MOD / "build/gen/dl-limits.h").read_text()
        assert (
            define(h, "DL_AREA_MAX_BYTES")
            == pk_device.contract_max()["dsp_area_bytes"]
            >= area
        )
        assert define(h, "DL_AREA_MAX_WORDS") * 4 == define(h, "DL_AREA_MAX_BYTES")
        # the DSP source sizes its bss by the generated define
        assert re.search(
            r"\.PKBSS _dl_area, DL_AREA_BYTES;",
            (MOD / "src" / "dsp" / f"{dev}.s").read_text(),
        )
        assert define(d, "DL_AREA_BYTES") <= define(h, "DL_AREA_MAX_BYTES") <= 4 << 17
        # the C file takes its width from the header, not from a literal
        assert re.search(r"#define DL_AREA_BYTES DL_AREA_MAX_BYTES", c)


@pytest.mark.parametrize("image", sorted(IMAGES))
def test_anchors_are_in_the_port(image):
    a = anchors(image)
    assert set(a) == {
        "cf.link.site.block_isr.frame_call",
        "cf.link.fn.frame",
        "cf.link.site.reply_copy_lit",
        "cf.link.site.tx_queue_lit",
        "dsp.link.site.block_tail",
    }


@pytest.mark.parametrize("image", sorted(IMAGES))
def test_call_site_geometry_matches_the_dsp_constants(image):
    if not config.stock(IMAGES[image]).exists():
        pytest.skip("firmware not present")
    dev = image[:3]
    a = anchors(image)
    main = main_os(image)
    base = 0x40000400
    call = a["cf.link.site.block_isr.frame_call"]["addr"] - base
    # `jsr abs.l` to the stock driver, preceded by the four pushes: record copy, frame bytes, data, data bytes
    assert main[call : call + 2] == b"\x4e\xb9"
    assert (
        struct.unpack(">I", main[call + 2 : call + 6])[0]
        == a["cf.link.fn.frame"]["addr"]
    )
    pre = main[call - 20 : call]
    m = re.fullmatch(
        rb"\x48\x79(....)\x48\x78(..)\x48\x79(....)\x48\x78(..)", pre, re.S
    )
    assert m, pre.hex()
    frame_bytes = struct.unpack(">H", m.group(2))[0]
    data_bytes = struct.unpack(">H", m.group(4))[0]
    assert frame_bytes == 0xABC
    e0 = (data_bytes // 2 + 1) & ~1
    tail_off, words = geometry(dev)
    assert 2 * e0 == tail_off
    assert (frame_bytes // 2 - e0) // 2 == words
    for name, want in (("DL_TAIL_OFF", tail_off), ("DL_TAIL_WORDS", words)):
        assert define(gen_inc(dev), name) == want
    # the literal of `lea <tx queue>,a0` in the driver
    # the literal of the first `pea` of the handler: the record copy (dl_diag reads it)
    rl = a["cf.link.site.reply_copy_lit"]["addr"] - base
    assert main[rl - 2 : rl] == b"\x48\x79"
    assert (
        struct.unpack(">I", main[rl : rl + 4])[0] == struct.unpack(">I", m.group(1))[0]
    )
    lit = a["cf.link.site.tx_queue_lit"]["addr"] - base
    assert main[lit - 2 : lit] == b"\x41\xf9"
    assert 0x80000000 <= struct.unpack(">I", main[lit : lit + 4])[0] < 0x80010000


@pytest.mark.parametrize("image", sorted(IMAGES))
def test_dsp_site_is_the_ring_toggle_store(image):
    from tools.patchkit import port_resolve
    from tools.sharc_dev.sharc_disasm import decode_confident_loaded
    from tools.sharc_dev.sharcldr import LoadedMemory

    stock = config.stock(IMAGES[image])
    if not stock.exists():
        pytest.skip("firmware not present")
    pinned = port(image)["port"]
    assert hashlib.sha256(stock.read_bytes()).hexdigest() == pinned["syx_sha256"]
    dsp = port_resolve.sections_of(stock)[7]
    assert hashlib.sha256(dsp).hexdigest() == pinned["dsp_sha256"]
    memory = LoadedMemory.from_stream(dsp)
    anchor = anchors(image)["dsp.link.site.block_tail"]
    assert anchor["addr"] == 0x28000000 + 2 * anchor["pm"]
    raw = memory.read_sw(anchor["pm"], anchor["len"])
    assert raw is not None and len(raw) == anchor["len"] == 6
    assert hashlib.sha256(raw).hexdigest() == anchor["sha256"]
    instruction = decode_confident_loaded(memory, anchor["pm"])
    assert instruction.type_name == "14a" and instruction.length_bytes == 6
    # Type14a: a direct absolute normal-word DM store, with R1 as its source.
    fields = instruction.fields
    assert (fields["g"], fields["d"], fields["l"], fields["ureg[6:0]"]) == (0, 1, 0, 1)
    assert fields["addr[31:16]"] << 16 | fields["addr[15:0]"]


# ---- the ColdFire half on the core -------------------------------------------------------------------------------

AREA = (
    0x2001AB00  # a stand-in for the DSP's reserved area (a DSP address; only compared)
)
TXLIT = DATA + 0x100  # holds the queue's address
RECLIT = DATA + 0x110  # holds the record copy's address (0 until a test sets it)
TXQ = DATA + 0x1000  # the fake queue: 4 bytes per entry
REC = DATA + 0x8000


@pytest.fixture(scope="module")
def built():
    if pk_toolchains.m68k_bin("gcc") is None:
        pytest.skip("m68k toolchain not installed")
    syms = {
        "_dl_area": AREA,
        "dl_tx_lit": TXLIT,
        "dl_rec_lit": RECLIT,
        "dl_stock_send": BASE + 0x100,
        "pp_write_frame": 0,
        "instrument_write_frame": 0,
        "instrument_write_lock_frame": 0,
        "phrase_write_frame": 0,
        "loop_reply_frame": 0,
        "relay_reply_frame": 0,
        "relay_write_frame": 0,
        "tk_pool_link_write": 0,
        "tk_pool_link_reply": 0,
    }
    return front_end.cf_link(
        [MOD / "src/cf/dsp-link.c"], BASE, syms, [MOD / "build/gen"]
    )


@pytest.fixture(scope="module")
def built_position(tmp_path_factory):
    """A bounded test writer marks all 32 data halfwords of the reserved suffix."""
    if pk_toolchains.m68k_bin("gcc") is None:
        pytest.skip("m68k toolchain not installed")
    stub = tmp_path_factory.mktemp("position-writer") / "writer.s"
    stub.write_text("""    .text
    .globl pp_write_frame
pp_write_frame:
    move.l 8(%sp),%d0
    lsl.l #2,%d0
    move.l 4(%sp),%a0
    adda.l %d0,%a0
    moveq #31,%d0
1:
    move.w #0xa55a,2(%a0)
    addq.l #4,%a0
    subq.l #1,%d0
    bpl 1b
    rts
""")
    syms = {
        "_dl_area": AREA,
        "dl_tx_lit": TXLIT,
        "dl_rec_lit": RECLIT,
        "dl_stock_send": BASE + 0x100,
        "instrument_write_frame": 0,
        "instrument_write_lock_frame": 0,
        "phrase_write_frame": 0,
        "loop_reply_frame": 0,
        "relay_reply_frame": 0,
        "relay_write_frame": 0,
        "tk_pool_link_write": 0,
        "tk_pool_link_reply": 0,
    }
    return front_end.cf_link(
        [MOD / "src/cf/dsp-link.c", stub], BASE, syms, [MOD / "build/gen"]
    )


@pytest.fixture(scope="module", params=[(False, False), (True, False), (True, True)])
def built_instrument(request, tmp_path_factory):
    if pk_toolchains.m68k_bin("gcc") is None:
        pytest.skip("m68k toolchain not installed")
    position, locks = request.param
    stub = tmp_path_factory.mktemp("instrument-writer") / "writer.c"
    source = """#include <stdint.h>
static void mark(uint32_t tx, uint32_t entry, uint32_t count, uint16_t value) {
    for (uint32_t i=0; i<count; ++i)
        *(volatile uint16_t *)(tx + 4*(entry+i) + 2) = value;
}
void instrument_write_frame(uint32_t tx, uint32_t entry, const volatile uint16_t *rec,
                            const volatile uint16_t *frame) {
    (void)rec; (void)frame;
    mark(tx, entry, 64, 0xb66b);
}
"""
    if position:
        source += "void pp_write_frame(uint32_t tx, uint32_t entry) { mark(tx, entry, 32, 0xa55a); }\n"
    if locks:
        source += "void instrument_write_lock_frame(uint32_t tx, uint32_t entry) { mark(tx, entry, 36, 0xc77c); }\n"
    stub.write_text(source)
    syms = {
        "_dl_area": AREA,
        "dl_tx_lit": TXLIT,
        "dl_rec_lit": RECLIT,
        "phrase_write_frame": 0,
        "loop_reply_frame": 0,
        "relay_reply_frame": 0,
        "relay_write_frame": 0,
        "tk_pool_link_write": 0,
        "tk_pool_link_reply": 0,
    }
    if not locks:
        syms["instrument_write_lock_frame"] = 0
    if not position:
        syms["pp_write_frame"] = 0
    return (
        front_end.cf_link(
            [MOD / "src/cf/dsp-link.c", stub], BASE, syms, [MOD / "build/gen"]
        ),
        position,
        locks,
    )


@pytest.fixture(scope="module")
def machine():
    from tools.patchkit.pk_cfcall import CfMachine

    syx = config.stock(IMAGES["dt2-1.17"])
    if not syx.exists():
        pytest.skip("firmware not present")
    return CfMachine(syx)


def pokes(linked, extra):
    return {
        linked.base: linked.image,
        linked.base + len(linked.image): b"\0" * linked.bss_size,
        TXLIT: struct.pack(">I", TXQ),
        **extra,
    }


def run(machine, linked, extra, calls):
    return machine.run(pokes(linked, extra), calls)


def call(linked, fn, args, reads=()):
    return {"pc": linked.symbols[fn], "args": list(args), "reads": list(reads)}


LINK_FIELDS = ["state", "err", "buf", "dst", "nwords", "xid", "cap", "nchunks", "acked", "next", "stall", "since", "sent", "resent", "nerr", "ending", "dirty", "win"]  # fmt: skip


def link_of(linked, r):
    b = r.reads[0]
    return dict(zip(LINK_FIELDS, struct.unpack(">18I", b), strict=True))


def tail_words(blob: bytes, e0: int, n: int) -> list[int]:
    """Words of the queue tail from entry e0: entry e has a command halfword and the data halfword at +2."""
    out = []
    for j in range(n):
        lo = struct.unpack(">H", blob[4 * (e0 + 2 * j) + 2 : 4 * (e0 + 2 * j) + 4])[0]
        hi = struct.unpack(
            ">H", blob[4 * (e0 + 2 * j + 1) + 2 : 4 * (e0 + 2 * j + 1) + 4]
        )[0]
        out.append(lo | (hi << 16))
    return out


def check_word(words: list[int]) -> int:
    s1 = s2 = 0
    for w in words:
        s1 = (s1 + w) & 0xFFFFFFFF
        s2 = (s2 + s1) & 0xFFFFFFFF
    return s1 ^ s2


def test_stock_driver_passes_the_full_reply_length_to_memcpy():
    """Pin the copy arguments; hardware DMA and live ingress remain unproved."""
    image = "dt2-1.17"
    main = main_os(image)
    entry = anchors(image)["cf.link.fn.frame"]["addr"]
    off = entry - 0x40000400
    # Extract the driver's ready flag and receive-buffer literal from this image.
    assert main[off + 12 : off + 14] == b"\x4a\xb9"  # tst.l abs.l
    assert main[off + 32 : off + 34] == b"\x48\x79"  # pea abs.l
    # After saving A2/D2, arg3 (reclen) -> D1 and arg4 (dest) -> D0.
    assert main[off : off + 12] == bytes.fromhex("2f0a2f02222f0014202f0018")
    # Positive length and non-null destination: push length, fixed RX buffer,
    # destination, then call memcpy. No mask, truncation or selected-row copy.
    assert main[off + 22 : off + 50] == bytes.fromhex(
        "4a8067184a816f142f014879800010002f004eb9401363884fef000c"
    )


@needs_gcc
def test_position_writer_runs_while_link_idle_and_stays_in_its_suffix(
    machine, built_position
):
    built = built_position
    original = bytes([0xCC]) * (0x55E * 4)
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(TXQ, len(original))])
    result = run(machine, built, {TXQ: original}, [tick])[0].reads[0]
    expected = bytearray(original)
    for entry in range(0xA7C // 2, 0xABC // 2):
        expected[entry * 4 + 2 : entry * 4 + 4] = b"\xa5\x5a"
    assert result == expected


@needs_gcc
def test_instrument_writer_preserves_position_and_every_other_idle_byte(
    machine, built_instrument
):
    built, position, locks = built_instrument
    original = bytes([0xCC]) * (0x55E * 4)
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(TXQ, len(original))])
    result = run(machine, built, {TXQ: original}, [tick])[0].reads[0]
    expected = bytearray(original)
    if position:
        if locks:
            for entry in range(0x9B4 // 2, 0x9FC // 2):
                expected[entry * 4 + 2 : entry * 4 + 4] = b"\xc7\x7c"
        for entry in range(0x9FC // 2, 0xA7C // 2):
            expected[entry * 4 + 2 : entry * 4 + 4] = b"\xb6\x6b"
        for entry in range(0xA7C // 2, 0xABC // 2):
            expected[entry * 4 + 2 : entry * 4 + 4] = b"\xa5\x5a"
    assert result == expected


@needs_gcc
def test_instrument_allocation_reduces_loader_chunks_or_rejects_missing_position(
    machine, built_instrument
):
    built, position, locks = built_instrument
    ln, src = built.symbols["dl_link"], DATA + 0x4000
    data = bytes((i * 7 + 3) & 255 for i in range(1024))
    original = bytes([0xCC]) * (0x55E * 4)
    tick = call(
        built, "dl_tick", [0x802, 0, 0xABC, 0], [(ln, 72), (TXQ, len(original))]
    )
    result = run(
        machine,
        built,
        {src: data, TXQ: original},
        [call(built, "dl_send", [AREA, src, len(data)]), tick],
    )[1]
    status = link_of(built, result)
    if not position:
        assert status["state"] == 3 and status["err"] == 101
        assert result.reads[1] == original
        return
    cap = 105 if locks else 123
    assert status["cap"] == cap
    words = tail_words(result.reads[1], 0x402, 174)
    assert (words[1] >> 17) & 255 == cap
    assert words[2 : 2 + cap] == list(struct.unpack(f"<{cap}I", data[: 4 * cap]))
    assert words[2 + cap] == check_word(words[: 2 + cap])
    if locks:
        assert words[108:126] == [0xC77CC77C] * 18
    assert words[126:158] == [0xB66BB66B] * 32
    assert words[158:] == [0xA55AA55A] * 16
    # Loader and both providers only write data halves, never command halves.
    assert result.reads[1][: 0x402 * 4] == original[: 0x402 * 4]
    for entry in range(0x55E):
        assert result.reads[1][entry * 4 : entry * 4 + 2] == b"\xcc\xcc"


@needs_gcc
def test_position_suffix_is_excluded_from_maximum_link_chunk(machine, built_position):
    built = built_position
    ln, src = built.symbols["dl_link"], DATA + 0x4000
    data = bytes((i * 7 + 3) & 255 for i in range(1024))
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(ln, 72), (TXQ, 0x55E * 4)])
    result = run(
        machine,
        built,
        {src: data, TXQ: bytes(0x55E * 4)},
        [call(built, "dl_send", [AREA, src, len(data)]), tick],
    )[1]
    assert link_of(built, result)["cap"] == 155
    words = tail_words(result.reads[1], 0x402, 174)
    assert (words[1] >> 17) & 255 == 155
    assert words[2:157] == list(struct.unpack("<155I", data[:620]))
    assert words[157] == check_word(words[:157])
    assert words[158:] == [0xA55AA55A] * 16


@needs_gcc
@pytest.mark.parametrize("geometry", [(0xA80, 0xABC), (0x802, 0xAB8), (0, 0xABC)])
def test_position_provider_refuses_non_dt2_geometry_before_any_write(
    machine, built_position, geometry
):
    built = built_position
    ln, src = built.symbols["dl_link"], DATA + 0x4000
    original = bytes([0xCC]) * (0x55E * 4)
    tick = call(
        built,
        "dl_tick",
        [geometry[0], 0, geometry[1], 0],
        [(ln, 72), (TXQ, len(original))],
    )
    result = run(
        machine,
        built,
        {src: bytes(4), TXQ: original},
        [call(built, "dl_send", [AREA, src, 4]), tick],
    )[1]
    status = link_of(built, result)
    assert status["state"] == 3 and status["err"] == 101
    assert result.reads[1] == original


@needs_gcc
def test_dl_send_checks_its_arguments(machine, built):
    ln = built.symbols["dl_link"]
    src = DATA + 0x4000
    ok = call(built, "dl_send", [AREA + 0x40, src, 64], [(ln, 72)])
    cases = [
        ok,
        call(
            built, "dl_send", [AREA + 0x80, src, 64]
        ),  # busy: the first request has not been taken
    ]
    r = run(machine, built, {src: bytes(range(64))}, cases)
    assert [x.d0 & 0xFFFFFFFF for x in r] == [0, 0xFFFFFFFF]
    lk = link_of(built, r[0])
    assert (
        lk["state"] == 4
        and lk["dst"] == 0x10 + 16  # window 0 starts with the 16 slot words
        and lk["nwords"] == 16
        and lk["buf"] == src
    )
    # the slot block lies directly below the area: window 0 words 0 to 15; a write stays inside it
    ok_slots = run(
        machine,
        built,
        {src: bytes(8)},
        [call(built, "dl_send", [AREA - 64 + 56, src, 8])],
    )
    assert ok_slots[0].d0 == 0
    bad = [
        [AREA - 68, src, 4],  # below the slot block
        [AREA - 64 + 60, src, 8],  # leaves the slot block into the area
        [AREA + 1, src, 64],  # unaligned destination
        [AREA, src, 63],  # length not a multiple of 4
        [AREA, src, 0],
        [AREA - 4, src, 64],  # before the area
        [
            AREA + 0x7FFC0,
            src,
            4,
        ],  # after it: the last 64 bytes of the 512 KiB are the slot block's place in window 0
        [AREA + 0x7FFBC, src, 8],  # runs over its end
        [AREA, 0, 64],
    ]
    res = run(machine, built, {}, [call(built, "dl_send", a) for a in bad])
    assert [x.d0 & 0xFFFFFFFF for x in res] == [0xFFFFFFFE] * len(bad)
    last = run(
        machine,
        built,
        {src: bytes(4)},
        [call(built, "dl_send", [AREA + 0x7FFBC, src, 4])],
    )
    assert last[0].d0 == 0


@needs_gcc
def test_first_tick_writes_a_chunk_the_dsp_can_check(machine, built):
    ln = built.symbols["dl_link"]
    src = DATA + 0x4000
    data = bytes((i * 7 + 3) & 0xFF for i in range(1024))
    nwords = len(data) // 4
    words = [int.from_bytes(data[i : i + 4], "little") for i in range(0, len(data), 4)]
    # DT2 geometry: 0x802 data bytes, a 0xabc-byte frame: tail from entry 0x402, 174 words, 171 for the payload
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(ln, 72), (TXQ, 0x55E * 4)])
    r = run(
        machine,
        built,
        {src: data},
        [call(built, "dl_send", [AREA + 0x20, src, len(data)]), tick, tick],
    )
    first = r[1]
    lk = link_of(built, first)
    assert (lk["cap"], lk["nchunks"], lk["xid"], lk["state"], lk["sent"]) == (
        171,
        2,
        1,
        1,
        1,
    )
    w = tail_words(first.reads[1], 0x402, 174)
    assert w[0] == MAGIC | (0 << 16)
    off, n, last, end, xid = (
        w[1] & 0x1FFFF,
        (w[1] >> 17) & 0xFF,
        (w[1] >> 25) & 1,
        (w[1] >> 26) & 1,
        w[1] >> 27,
    )
    assert (off, n, last, end, xid) == (
        24,
        171,
        0,
        0,
        1,
    )  # word 16 is the area start (the slot block is words 0-15), plus 8 words
    assert w[0] >> 30 == 0  # window 0
    assert w[2 : 2 + n] == words[:171]
    assert w[2 + n] == check_word(w[: 2 + n])
    # no acknowledgement came, so the second tick sends chunk 1
    w1 = tail_words(r[2].reads[1], 0x402, 174)
    assert w1[0] == MAGIC | (1 << 16)
    off1, n1, last1 = w1[1] & 0x1FFFF, (w1[1] >> 17) & 0xFF, (w1[1] >> 25) & 1
    assert (off1, n1, last1) == (24 + 171, nwords - 171, 1)
    assert w1[2 : 2 + n1] == words[171:]
    assert w1[2 + n1] == check_word(w1[: 2 + n1])
    # halfwords of the queue outside the tail are untouched (the stock driver writes the command halfwords there)
    blob = first.reads[1]
    assert blob[: 0x402 * 4] == bytes(0x402 * 4)


@needs_gcc
def test_acknowledgements_drive_the_state(machine, built):
    ln = built.symbols["dl_link"]
    src = DATA + 0x4000
    data = (
        bytes(range(256)) * 2
    )  # 128 words: one chunk on DT2 geometry... use DN2 geometry: 12 words per chunk
    n_chunks = 11  # 128 / 12 rounded up

    def ack(cum, xid, status, nerr=0):
        rec = bytearray(0xABC)
        struct.pack_into(
            ">HHHH", rec, ACK_OFF, ACK_MAGIC, cum, xid | (status << 8), nerr
        )
        return bytes(rec)

    def tick(rec_addr):
        return call(built, "dl_tick", [0xA80, 0, 0xABC, rec_addr], [(ln, 72)])

    recs = {
        REC + 0x1000 * i: ack(*a)
        for i, a in enumerate([(0, 1, 0), (5, 1, 0), (5, 2, 0), (11, 1, 1), (11, 1, 1)])
    }
    r0, r1, r2, r3, r4 = recs
    calls = [
        call(built, "dl_send", [AREA, src, len(data)]),
        tick(
            r0
        ),  # starts: DN2 geometry (0xa80 data bytes, 15 tail words), 12 payload words
        tick(r1),  # five chunks taken
        tick(r2),  # an acknowledgement for another transfer id is ignored
        tick(r3),  # all taken, status done
        tick(r4),  # END chunks follow
    ]
    r = run(machine, built, {src: data, **recs}, calls)
    lk = [link_of(built, x) for x in r[1:]]
    assert (lk[0]["cap"], lk[0]["nchunks"], lk[0]["state"]) == (12, n_chunks, 1)
    assert lk[1]["acked"] == 5 and lk[1]["state"] == 1
    assert lk[2]["acked"] == 5 and lk[2]["state"] == 1
    assert (
        lk[3]["acked"] == 11 and lk[3]["state"] == 2 and lk[3]["ending"] == 5
    )  # the first END chunk goes out in the same tick
    assert lk[4]["ending"] == 4
    # polling
    p = run(
        machine,
        built,
        {src: data, **recs},
        [
            call(built, "dl_send", [AREA, src, len(data)]),
            call(built, "dl_poll", [REC + 0x100], [(REC + 0x100, 28)]),
        ],
    )
    assert p[1].d0 == 1  # busy
    assert struct.unpack(">7I", p[1].reads[0])[0] == 1


@needs_gcc
def test_dsp_error_status_and_timeout_stop_the_transfer(machine, built):
    ln = built.symbols["dl_link"]
    src = DATA + 0x4000
    rec = bytearray(0xABC)
    struct.pack_into(
        ">HHHH", rec, ACK_OFF, ACK_MAGIC, 0, 1 | (3 << 8), 0
    )  # status 3: destination refused
    tick = call(built, "dl_tick", [0xA80, 0, 0xABC, REC], [(ln, 72)])
    r = run(
        machine,
        built,
        {src: bytes(64), REC: bytes(rec)},
        [call(built, "dl_send", [AREA, src, 64]), tick],
    )
    lk = link_of(built, r[1])
    assert lk["state"] == 3 and lk["err"] == 3 and lk["ending"] == 5
    # no acknowledgement at all: the timeout is 900 ticks
    ticks = [
        call(built, "dl_tick", [0xA80, 0, 0xABC, 0], [(ln, 72)]) for _ in range(902)
    ]
    r = run(
        machine,
        built,
        {src: bytes(64)},
        [call(built, "dl_send", [AREA, src, 64]), *ticks],
    )
    assert link_of(built, r[-1])["state"] == 3 and link_of(built, r[-1])["err"] == 100
    # a frame with no room in the tail (a data length that fills it) is a geometry error, never a write
    r = run(
        machine,
        built,
        {src: bytes(64)},
        [
            call(built, "dl_send", [AREA, src, 64]),
            call(built, "dl_tick", [0xAB0, 0, 0xABC, 0], [(ln, 72), (TXQ, 64)]),
        ],
    )
    assert link_of(built, r[1])["err"] == 101 and r[1].reads[1] == bytes(64)


@needs_gcc
def test_window_transfers_carry_the_window_and_refuse_bad_arguments(machine, built):
    ln = built.symbols["dl_link"]
    src = DATA + 0x4000
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(ln, 72), (TXQ, 0x55E * 4)])
    data = struct.pack("<2I", 0x11223344, 0x55667788)
    r = run(
        machine,
        built,
        {src: data},
        [call(built, "dl_send_win", [1, 3, src, 8]), tick],
    )
    assert r[0].d0 == 0
    w = tail_words(r[1].reads[1], 0x402, 174)
    assert w[0] == MAGIC | (0 << 16) | (1 << 30)
    assert (w[1] & 0x1FFFF, (w[1] >> 17) & 0xFF, (w[1] >> 25) & 1) == (3, 2, 1)
    assert w[2:4] == [0x11223344, 0x55667788]
    assert w[4] == check_word(w[:4])
    # window 2 (the effect rows of the FX chain) is the same transfer with the window number 2 in the header
    r = run(
        machine,
        built,
        {src: data},
        [call(built, "dl_send_win", [2, 5, src, 8]), tick],
    )
    assert r[0].d0 == 0
    w = tail_words(r[1].reads[1], 0x402, 174)
    assert w[0] == MAGIC | (0 << 16) | (2 << 30)
    assert (w[1] & 0x1FFFF, (w[1] >> 17) & 0xFF, (w[1] >> 25) & 1) == (5, 2, 1)
    bad = [
        [0, 0, src, 8],  # window 0 is dl_send's
        [5, 0, src, 8],  # no window 5 (4, the V2 registration data, is the last)
        [1, 0, src, 0],
        [1, 0, src, 6],
        [1, 0x20000, src, 4],
        [1, 0, 0, 4],
    ]
    res = run(machine, built, {}, [call(built, "dl_send_win", a) for a in bad])
    assert [x.d0 & 0xFFFFFFFF for x in res] == [0xFFFFFFFE] * len(bad)


def wire_window(w0: int) -> int:
    """The window of a chunk header: bits 30-31 its low two bits, bit 29 its third (src/dsp/dsp-link.inc)."""
    return (w0 >> 30) | ((w0 >> 29) & 1) << 2


def test_both_sides_agree_on_the_idx_and_window_bits():
    c = (MOD / "src/cf/dsp-link.c").read_text()
    s = (MOD / "src/dsp/dsp-link.inc").read_text()
    # the sender: 13 idx bits (at most 8192 chunks), windows up to 4, bit 29 the window's third bit
    assert define(c, "DL_MAX_CHUNKS") == 1 << 13 and define(c, "DL_WIN_LAST") == 4
    assert "((l->win & 3u) << 30) | ((l->win >> 2) << 29)" in c
    # the receiver: the same fields
    assert "R13 = FEXT R10 BY 16:13;" in s
    assert re.search(r"R0 = FEXT R10 BY 30:2;[^\n]*\n\s*R3 = FEXT R10 BY 29:1;", s)


@needs_gcc
@pytest.mark.parametrize(
    "window,bits",
    [
        (3, 3 << 30),  # the stats control: upstream's header, bit 29 clear
        (4, 1 << 29),  # the V2 registration data: bit 29 alone
    ],
)
@pytest.mark.parametrize("entry", ["dl_send_win", "dl_send_tagged"])
def test_stats_and_v2_windows_have_headers_of_their_own(
    machine, built, window, bits, entry
):
    """The ColdFire does not know whether the image has a V2 provider; the DSP decides (status 3)."""
    ln, src, ticket = built.symbols["dl_link"], DATA + 0x4000, DATA + 0x5000
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(ln, 72), (TXQ, 0x55E * 4)])
    data = struct.pack("<2I", 0x11223344, 0x55667788)
    args = [window, 1, src, 8] + ([ticket] if entry == "dl_send_tagged" else [])
    r = run(machine, built, {src: data}, [call(built, entry, args), tick])
    assert r[0].d0 == 0
    assert link_of(built, r[1])["win"] == window
    w = tail_words(r[1].reads[1], 0x402, 174)
    assert w[0] == MAGIC | bits
    assert wire_window(w[0]) == window
    assert (w[1] & 0x1FFFF, (w[1] >> 17) & 0xFF, (w[1] >> 25) & 1) == (1, 2, 1)
    assert w[2:4] == [0x11223344, 0x55667788]
    assert w[4] == check_word(w[:4])


@needs_gcc
def test_no_entry_takes_a_window_past_the_v2_window(machine, built):
    src, lease, ticket = DATA + 0x4000, DATA + 0x5000, DATA + 0x5100
    calls = [call(built, "dl_claim", [lease])]
    for win in (5, 6, 7, 8, 0xFFFFFFFF):
        calls += [
            call(built, "dl_send_owned", [1, win, 0, src, 4, ticket], [(ticket, 4)]),
        ]
    calls.append(call(built, "dl_release", [1]))
    for win in (5, 7, 8):
        calls += [
            call(built, "dl_send_win", [win, 0, src, 4]),
            call(built, "dl_send_tagged", [win, 0, src, 4, ticket], [(ticket, 4)]),
        ]
    calls.append(
        call(built, "dl_send_tagged", [4, 0, src, 4, ticket], [(ticket, 4)])
    )  # the last window, so the refusals above consumed no identity
    r = run(machine, built, {src: b"abcd", ticket: b"\xcc" * 4}, calls)
    assert r[0].d0 == 0 and r[6].d0 == 0
    refused = r[1:6] + r[7:-1]
    assert [x.d0 for x in refused] == [0xFFFFFFFE] * len(refused)
    assert all(x.reads[0] == b"\xcc" * 4 for x in refused if x.reads)
    assert r[-1].d0 == 0 and r[-1].reads[0] == struct.pack(">I", 1)


@needs_gcc
@pytest.mark.parametrize("window", [0, 3, 4])
def test_the_last_chunk_number_leaves_bit_29_to_the_window(machine, built, window):
    """idx is 13 bits: chunk 8191, the last one a transfer may have, does not reach bit 29."""
    ln, src = built.symbols["dl_link"], DATA + 0x4000
    # 8192 chunks of 12 words (DN2 geometry), of which the DSP has taken 8191: the sender writes the last one
    fields = dict.fromkeys(LINK_FIELDS, 0) | {
        "state": 1,
        "buf": src,
        "nwords": 12 * 8192,
        "xid": 1,
        "cap": 12,
        "nchunks": 8192,
        "acked": 8191,
        "next": 8191,
        "win": window,
    }
    tick = call(built, "dl_tick", [0xA80, 0, 0xABC, 0], [(ln, 72), (TXQ, 0x55E * 4)])
    r = run(machine, built, {ln: struct.pack(">18I", *fields.values())}, [tick])
    w = tail_words(r[0].reads[1], 0x540, 15)
    assert w[0] & 0xFFFF == MAGIC
    assert (w[0] >> 16) & 0x1FFF == 8191
    assert wire_window(w[0]) == window
    off, n, last = w[1] & 0x1FFFF, (w[1] >> 17) & 0xFF, (w[1] >> 25) & 1
    assert (off, n, last) == (8191 * 12, 12, 1)
    assert w[2 + n] == check_word(w[: 2 + n])


@needs_gcc
def test_a_transfer_the_idx_bits_cannot_number_is_refused_before_a_chunk(
    machine, built
):
    ln, src = built.symbols["dl_link"], DATA + 0x4000
    queue = bytes([0xCC]) * (0x55E * 4)
    tick = call(built, "dl_tick", [0xA80, 0, 0xABC, 0], [(ln, 72), (TXQ, len(queue))])
    most = 4 * 12 * 8192  # 8192 chunks of 12 words (DN2 geometry)
    r = run(
        machine, built, {TXQ: queue}, [call(built, "dl_send", [AREA, src, most]), tick]
    )
    lk = link_of(built, r[1])
    assert (lk["state"], lk["nchunks"], lk["cap"], lk["xid"]) == (1, 8192, 12, 1)
    r = run(
        machine,
        built,
        {TXQ: queue},
        [call(built, "dl_send", [AREA, src, most + 4]), tick, tick],
    )
    assert r[0].d0 == 0
    for x in r[1:]:
        lk = link_of(built, x)
        assert (lk["state"], lk["err"], lk["cap"], lk["xid"]) == (3, 101, 0, 0)
        assert x.reads[1] == queue  # no chunk, and no END chunk after it


DG_MAGIC, DG_WORDS = 0xD1A6, 32


def diag_off(j: int) -> int:
    """Record byte offset of diagnostic word j: slot row 1 + j // 2 (0x1c + 0x54 row), +0x44 or +0x48."""
    return 0x1C + 0x54 * (1 + j // 2) + (0x48 if j & 1 else 0x44)


def diag_record(words: list[int], magic: int = DG_MAGIC) -> bytes:
    rec = bytearray(0xABC)
    for j, w in enumerate([magic | (len(words) + 1) << 16, *words]):
        struct.pack_into(">HH", rec, diag_off(j), w & 0xFFFF, w >> 16)
    return bytes(rec)


def test_diagnostic_words_have_the_same_slots_on_both_sides():
    """The DSP writes word j at word index 7 + 21 (1 + j / 2) + 17 + j % 2 of the record (dsp-link.inc); dl_diag reads the same bytes.
    None of them collides with the acknowledgement (row 0, +0x60 and +0x64) or with the entries the DSP writes (6 to 9 of each row)."""
    inc = (MOD / "src/dsp/dsp-link.inc").read_text()
    assert (
        define(inc, "DL_DG_MAGIC")
        == DG_MAGIC
        == define((MOD / "src/cf/dsp-link.c").read_text(), "DL_DG_MAGIC")
    )
    assert define((MOD / "src/cf/dsp-link.c").read_text(), "DL_DG_WORDS") == DG_WORDS
    # the stores of the DSP routine, as written in the source
    stores = {
        int(m.group(1)): int(m.group(2))
        for m in re.finditer(r"DM\((\d+), I2\) = R0;\s+// word (\d+)", inc)
    }
    # the step is stored by the carry of the static word, which the loop above does not need to see twice
    assert sorted(stores.values()) == list(range(DG_WORDS))
    for widx, j in stores.items():
        assert 4 * widx == diag_off(j), (j, widx)
        row = (diag_off(j) - 0x1C) // 0x54
        entry_off = (diag_off(j) - 0x1C) % 0x54
        assert 1 <= row <= 16
        assert (
            entry_off >= 6 * 11 and entry_off + 4 <= 6 * 13 - 2
        )  # entries 11 and 12 only
    assert define(inc, "DL_ACK_OFF") not in {diag_off(j) for j in range(DG_WORDS)}


@needs_gcc
def test_dl_diag_reads_the_words_from_the_record_copy(machine, built):
    out = DATA + 0x6000
    regs = [
        0,
        1,
        0xFFFFFFFF,
        0,
        1,
        0xFFFFFFFF,
        0x2CB2C8,
        0x2CB2C0,
        0x2CA5A0,
        994,
        0x39011CF8,
    ]
    words = [
        7,
        100,
        *regs,
        0x00120405,
        77,
        76,
        0x01010009,
        0x00020012,
        0x4000,
        0x2000,
        0x7F00,
        0x0100,
        0x12,
    ]  # tail beat, hook beat, M5 .. MODE1, last breadcrumb, dispatches, returns, then the guard's words 17 to 23
    rec = diag_record(words)
    ptr = struct.pack(">I", REC)

    def diag(n):
        return call(built, "dl_diag", [out, n], [(out, 24 * 4)])

    # the literal holds no address (0): -1; then the record
    r = run(machine, built, {out: bytes(0x80)}, [diag(24)])
    assert r[0].d0 & 0xFFFFFFFF == 0xFFFFFFFF
    r = run(
        machine,
        built,
        {RECLIT: ptr, REC: rec, out: bytes(0x80)},
        [diag(24), diag(5)],
    )
    assert r[0].d0 == 24
    assert list(struct.unpack(">24I", r[0].reads[0])) == [DG_MAGIC | 24 << 16, *words]
    assert r[1].d0 == 5  # a shorter buffer
    assert list(struct.unpack(">5I", r[1].reads[0][:20])) == [
        DG_MAGIC | 24 << 16,
        *words[:4],
    ]
    # a record with no diagnostic block (core/dsp-voice absent): -2
    r = run(
        machine, built, {RECLIT: ptr, REC: bytes(0xABC), out: bytes(0x80)}, [diag(24)]
    )
    assert r[0].d0 & 0xFFFFFFFF == 0xFFFFFFFE


@needs_gcc
def test_tick_callback_gets_the_reply_record_and_can_be_cleared(machine, built):
    stub, seen = DATA + 0x7000, DATA + 0x7100
    # move.l 4(sp),d0 ; move.l d0,seen ; rts: the callback stores its argument (the record copy)
    code = struct.pack(">HHHIH", 0x202F, 4, 0x23C0, seen, 0x4E75)
    rec = DATA + 0x8000
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, rec], [(seen, 4)])
    r = run(
        machine,
        built,
        {stub: code, seen: bytes(4)},
        [
            tick,  # no callback: nothing stored
            call(built, "dk_dsp_link_set_tick", [0, stub]),
            tick,  # the callback sees the record
            call(
                built, "dl_tick_cb_drop", [stub + 0x100, stub + 0x200]
            ),  # not in range: kept
            call(
                built, "dl_tick_cb_drop", [stub, stub + 4]
            ),  # the callback's first byte is in range: dropped
            call(built, "dk_dsp_link_set_tick", [0, stub]),
            call(
                built, "dl_tick_cb_drop", [stub - 0x100, stub]
            ),  # [lo, hi): the end is excluded
            tick,
        ],
    )
    assert r[0].reads[0] == bytes(4)
    assert struct.unpack(">I", r[2].reads[0])[0] == rec
    # a dropped callback is not called any more: fresh memory, set, drop, tick
    r = run(
        machine,
        built,
        {stub: code, seen: bytes(4)},
        [
            call(built, "dk_dsp_link_set_tick", [0, stub]),
            call(built, "dl_tick_cb_drop", [stub, stub + 4]),
            tick,
        ],
    )
    assert r[2].reads[0] == bytes(4)


@needs_gcc
@pytest.mark.parametrize("status,terminal", [(1, 2), (3, 3)])
def test_tagged_completion_waits_for_end_frames_and_rejects_legacy_replacement(
    machine, built, status, terminal
):
    src, ticket, output = DATA + 0x4000, DATA + 0x5000, DATA + 0x5100
    record = bytearray(0xABC)
    struct.pack_into(">HHHH", record, ACK_OFF, ACK_MAGIC, 1, 1 | (status << 8), 0)

    def tick(rec):
        return call(built, "dl_tick", [0x802, 0, 0xABC, rec])

    poll = call(built, "dl_poll_tagged", [1, output], [(output, 28)])
    calls = [
        call(built, "dl_send_tagged", [3, 1, src, 4, ticket], [(ticket, 4)]),
        poll,
        tick(0),
        tick(REC),
        poll,
    ]
    for _ in range(5):
        calls += [tick(REC), poll]
    calls += [
        call(built, "dl_send", [AREA, src, 4]),
        poll,
        call(built, "dl_poll_tagged", [2, output], [(output, 28)]),
    ]
    results = run(
        machine, built, {src: b"abcd", REC: bytes(record), output: b"\xcc" * 28}, calls
    )
    assert results[0].d0 == 0
    assert results[0].reads[0] == struct.pack(">I", 1)
    assert results[1].d0 == 0 and struct.unpack(">7I", results[1].reads[0])[0] == 1
    assert results[4].d0 == 0  # terminal status is not yet a drainage proof
    assert struct.unpack(">7I", results[4].reads[0])[0] == terminal
    assert [results[i].d0 for i in (6, 8, 10, 12, 14)] == [0, 0, 0, 0, 1]
    assert results[15].d0 == 0  # legacy transfer consumes the next identity too
    assert results[16].d0 == 0xFFFFFFFF
    assert (
        results[16].reads[0] == results[14].reads[0]
    )  # stale poll cannot overwrite output
    assert results[17].d0 == 0 and struct.unpack(">7I", results[17].reads[0])[0] == 1


@needs_gcc
def test_tagged_rejections_preserve_identity_and_caller_output(machine, built):
    src, ticket, output = DATA + 0x4000, DATA + 0x5000, DATA + 0x5100
    calls = [
        call(built, "dl_send_tagged", [3, 0, src, 4, 0]),
        call(built, "dl_send_tagged", [5, 0, src, 4, ticket], [(ticket, 4)]),
        call(built, "dl_poll_tagged", [0, output], [(output, 28)]),
        call(built, "dl_send_tagged", [0, AREA, src, 4, ticket], [(ticket, 4)]),
        call(built, "dl_send_tagged", [3, 0, src, 4, ticket], [(ticket, 4)]),
        call(built, "dl_poll_tagged", [1, 0]),
    ]
    results = run(
        machine, built, {src: b"abcd", ticket: b"\xcc" * 4, output: b"\xcc" * 28}, calls
    )
    assert [r.d0 for r in results] == [
        0xFFFFFFFE,
        0xFFFFFFFE,
        0xFFFFFFFF,
        0,
        0xFFFFFFFF,
        0xFFFFFFFF,
    ]
    assert results[1].reads[0] == b"\xcc" * 4
    assert results[2].reads[0] == b"\xcc" * 28
    assert results[3].reads[0] == results[4].reads[0] == struct.pack(">I", 1)


@needs_gcc
@pytest.mark.parametrize("serial,expected", [(0xFFFFFFFE, 0), (0xFFFFFFFF, 0xFFFFFFFE)])
def test_transfer_identity_never_wraps(machine, built, serial, expected):
    src, ticket = DATA + 0x4000, DATA + 0x5000
    results = run(
        machine,
        built,
        {
            built.symbols["dl_link"] + 72: struct.pack(">I", serial),
            src: b"abcd",
            ticket: b"\xcc" * 4,
        },
        [call(built, "dl_send_tagged", [3, 0, src, 4, ticket], [(ticket, 4)])],
    )
    assert results[0].d0 == expected
    assert results[0].reads[0] == (
        struct.pack(">I", 0xFFFFFFFF) if expected == 0 else b"\xcc" * 4
    )


@needs_gcc
@pytest.mark.parametrize("status", [1, 3])
def test_lease_survives_terminal_drain_and_registry_reply_gap(machine, built, status):
    src, lease, ticket, output = (DATA + n for n in (0x4000, 0x5000, 0x5100, 0x5200))
    record = bytearray(0xABC)
    struct.pack_into(">HHHH", record, ACK_OFF, ACK_MAGIC, 1, 1 | (status << 8), 0)
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, REC])
    calls = [
        call(built, "dl_claim", [lease], [(lease, 4)]),
        call(built, "dl_send_owned", [1, 3, 0, src, 4, ticket], [(ticket, 4)]),
        call(built, "dl_release", [1]),  # START is not drained
        call(built, "dl_tick", [0x802, 0, 0xABC, 0]),
        call(built, "dl_release", [1]),  # BUSY
        tick,
        call(built, "dl_release", [1]),  # terminal, END still outstanding
        *([tick] * 5),
        call(built, "dl_poll_tagged", [1, output], [(output, 28)]),
        # Byte completion does not end ownership: registry reply may be pending.
        call(built, "dl_send", [AREA, src, 4]),
        call(built, "dl_send_win", [2, 0, src, 4]),
        call(built, "dl_send_tagged", [0, AREA, src, 4, ticket], [(ticket, 4)]),
        call(built, "dl_claim", [lease], [(lease, 4)]),
        call(built, "dl_release", [2]),  # wrong owner cannot release
        call(built, "dl_release", [1]),
        call(built, "dl_send", [AREA, src, 4]),
    ]
    results = run(machine, built, {src: b"abcd", REC: bytes(record)}, calls)
    assert results[0].d0 == results[1].d0 == 0
    assert results[0].reads[0] == results[1].reads[0] == struct.pack(">I", 1)
    assert [results[i].d0 for i in (2, 4, 6)] == [0xFFFFFFFF] * 3
    assert results[12].d0 == 1
    assert struct.unpack(">7I", results[12].reads[0])[0] == (2 if status == 1 else 3)
    assert [r.d0 for r in results[13:17]] == [0xFFFFFFFF] * 4
    assert results[15].reads[0] == results[16].reads[0] == struct.pack(">I", 1)
    assert [r.d0 for r in results[17:]] == [0xFFFFFFFE, 0, 0]


@needs_gcc
@pytest.mark.parametrize("window", [0, 1, 2, 3, 4])
def test_owned_send_requires_current_lease_and_preserves_wire_window(
    machine, built, window
):
    src, lease, ticket = (DATA + n for n in (0x4000, 0x5000, 0x5100))
    dst = AREA if window == 0 else 3
    args = [window, dst, src, 4, ticket]
    results = run(
        machine,
        built,
        {src: b"abcd", ticket: b"\xcc" * 4},
        [
            call(built, "dl_send_owned", [0, *args], [(ticket, 4)]),
            call(built, "dl_send_owned", [1, *args], [(ticket, 4)]),
            call(built, "dl_claim", [lease]),
            call(built, "dl_release", [1]),
            call(built, "dl_claim", [lease], [(lease, 4)]),
            call(built, "dl_release", [1]),
            call(built, "dl_send_owned", [1, *args], [(ticket, 4)]),
            call(built, "dl_send_owned", [2, *args], [(ticket, 4)]),
            call(built, "dl_tick", [0x802, 0, 0xABC, 0], [(TXQ, 0x55E * 4)]),
        ],
    )
    assert [r.d0 for r in results[:8]] == [
        0xFFFFFFFE,
        0xFFFFFFFE,
        0,
        0,
        0,
        0xFFFFFFFE,
        0xFFFFFFFE,
        0,
    ]
    assert results[4].reads[0] == struct.pack(">I", 2)
    for i in (0, 1, 6):
        assert results[i].reads[0] == b"\xcc" * 4
    assert results[7].reads[0] == struct.pack(
        ">I", 1
    )  # claims do not consume transfer IDs
    words = tail_words(results[8].reads[0], 0x402, 174)
    assert wire_window(words[0]) == window
    # window 0: word 16 is the area start (the slot block is words 0-15)
    assert words[1] & 0x1FFFF == (16 if window == 0 else dst)
    assert words[2] == int.from_bytes(b"abcd", "little")
    assert words[3] == check_word(words[:3])


@needs_gcc
@pytest.mark.parametrize("serial,expected", [(0xFFFFFFFE, 0), (0xFFFFFFFF, 0xFFFFFFFE)])
def test_lease_identity_never_wraps_or_changes_output_on_failure(
    machine, built, serial, expected
):
    lease = DATA + 0x5000
    results = run(
        machine,
        built,
        {
            built.symbols["dl_link"] + 80: struct.pack(">I", serial),
            lease: b"\xcc" * 4,
        },
        [
            call(built, "dl_claim", [0]),
            call(built, "dl_claim", [lease], [(lease, 4)]),
            call(built, "dl_release", [0]),
        ],
    )
    assert [r.d0 for r in results] == [0xFFFFFFFE, expected, 0xFFFFFFFE]
    assert results[1].reads[0] == (
        struct.pack(">I", 0xFFFFFFFF) if expected == 0 else b"\xcc" * 4
    )


@needs_gcc
def test_lease_claim_cannot_take_over_legacy_start_or_end_drain(machine, built):
    src, lease = DATA + 0x4000, DATA + 0x5000
    record = bytearray(0xABC)
    struct.pack_into(">HHHH", record, ACK_OFF, ACK_MAGIC, 1, 1 | (1 << 8), 0)
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, REC])
    claim = call(built, "dl_claim", [lease], [(lease, 4)])
    results = run(
        machine,
        built,
        {src: b"abcd", REC: bytes(record), lease: b"\xcc" * 4},
        [
            call(built, "dl_send", [AREA, src, 4]),
            claim,
            call(built, "dl_tick", [0x802, 0, 0xABC, 0]),
            claim,
            tick,
            claim,
            *([tick] * 5),
            claim,
        ],
    )
    for i in (1, 3, 5):
        assert results[i].d0 == 0xFFFFFFFF
        assert results[i].reads[0] == b"\xcc" * 4
    assert results[-1].d0 == 0
    assert results[-1].reads[0] == struct.pack(">I", 1)


@needs_gcc
@pytest.mark.parametrize("phase", ["start", "busy", "ending", "drained"])
def test_reset_retains_owned_transfer_and_rejects_stale_ack(machine, built, phase):
    src, lease, ticket, output = (DATA + n for n in (0x4000, 0x5000, 0x5100, 0x5200))
    record = bytearray(0xABC)
    struct.pack_into(">HHHH", record, ACK_OFF, ACK_MAGIC, 1, 0x101, 0)
    tick = call(built, "dl_tick", [0x802, 0, 0xABC, REC], [(TXQ + 4 * 0x402 + 2, 2)])
    calls = [
        call(built, "dl_reset_begin", []),  # initial boot must allow first use
        call(built, "dl_claim", [lease]),
        call(built, "dl_send_owned", [1, 3, 0, src, 4, ticket]),
    ]
    if phase != "start":
        calls.append(call(built, "dl_tick", [0x802, 0, 0xABC, 0]))
    if phase in ("ending", "drained"):
        calls.append(tick)
    if phase == "drained":
        calls.extend([tick] * 6)
    calls.extend(
        [
            call(built, "dl_reset_begin", []),
            call(built, "dl_reset_begin", []),
        ]
    )
    prefix = len(calls)
    calls.extend([tick] * 8)  # valid old ACK and enough frames to have drained normally
    calls.extend(
        [
            call(
                built,
                "dl_poll_tagged",
                [1, output],
                [(output, 28), (built.symbols["dl_link"], 96)],
            ),
            call(built, "dl_release", [1]),
            call(built, "dl_claim", [lease], [(lease, 4)]),
            call(built, "dl_send_owned", [1, 3, 0, src, 4, ticket], [(ticket, 4)]),
            call(built, "dl_send", [AREA, src, 4]),
            call(built, "dl_send_tagged", [3, 0, src, 4, ticket], [(ticket, 4)]),
        ]
    )
    results = run(machine, built, {src: b"abcd", REC: bytes(record)}, calls)
    assert results[1].d0 == results[2].d0 == 0
    assert all(r.reads[0] == bytes(2) for r in results[prefix : prefix + 8])
    done = results[prefix + 8]
    assert done.d0 == 0  # unresolved, never terminal/drained
    assert struct.unpack(">7I", done.reads[0])[:2] == (1, 102)
    state = struct.unpack(">24I", done.reads[1])
    assert state[2] == src  # source remains owned
    assert state[18:22] == (1, 1, 1, 1)  # serial, lease, lease serial, revoked
    assert all(r.d0 == 0xFFFFFFFE for r in results[prefix + 9 :])
    assert results[prefix + 10].reads[0] == struct.pack(">I", 1)
    assert (
        results[prefix + 11].reads[0]
        == results[prefix + 13].reads[0]
        == struct.pack(">I", 1)
    )


@needs_gcc
@pytest.mark.parametrize(
    "status,csr,minor,expected",
    [
        (0, 0, 0, 1),
        (0x40000000, 0, 0, 2),
        (0, 1, 0, 2),
        (0, 0x40, 0, 2),
        (0, 0x20, 0, 2),
        (0, 0, 0x8000, 2),
    ],
)
def test_reset_hardware_requires_stopped_spi_and_dma(
    machine, built, status, csr, minor, expected
):
    mcr, sr, rx, tx, cerq = (DATA + n for n in (0x6000, 0x6004, 0x6020, 0x6040, 0x6050))
    registers = [mcr, sr, cerq, rx, tx]
    calls = [
        call(built, "dl_reset_quiesce_regs", registers, [(mcr, 4)]),
        call(built, "dl_claim", [DATA + 0x5000]),
        call(built, "dl_reset_begin", []),
        call(
            built,
            "dl_reset_quiesce_regs",
            registers,
            [(built.symbols["dl_reset_hw"], 32), (mcr, 4)],
        ),
        call(
            built,
            "dl_reset_quiesce_regs",
            registers,
            [(built.symbols["dl_reset_hw"], 32)],
        ),
    ]
    for operation in calls:
        operation["max"] = 10_000_000
    results = run(
        machine,
        built,
        {
            mcr: struct.pack(">I", 0x80010000),
            sr: struct.pack(">I", status),
            rx - 10: struct.pack(">H", minor),
            rx - 2: bytes(2),
            tx - 10: bytes(2),
            tx - 2: bytes(2),
            rx: struct.pack(">H", csr),
            tx: bytes(2),
        },
        calls,
    )
    assert results[0].d0 == 0
    assert results[0].reads[0] == struct.pack(">I", 0x80010000)
    assert results[3].d0 == results[4].d0 == expected
    diagnostic = struct.unpack(">8I", results[3].reads[0])
    assert diagnostic == (
        expected,
        1 if expected == 1 else 65536,
        0x80010000,
        status,
        csr,
        0,
        status,
        csr | minor,
    )
    assert results[4].reads[0] == results[3].reads[0]  # never retry after timeout
    assert results[3].reads[1] == struct.pack(
        ">I", 0x80010C01 if expected == 1 else 0x80010001
    )


@needs_gcc
def test_stats_pages_in_the_acknowledgement_slot_go_to_the_runtime_feed(machine, built):
    stub, seen, rec = DATA + 0x7000, DATA + 0x7100, DATA + 0x8000
    # the feed stores its three arguments (tag, a, b) at seen, seen + 4, seen + 8
    code = struct.pack(
        ">HHHIHHHIHHHIH",
        *(0x202F, 4, 0x23C0, seen),
        *(0x202F, 8, 0x23C0, seen + 4),
        *(0x202F, 12, 0x23C0, seen + 8),
        0x4E75,
    )
    tag, a, b = 0x57A7 | 3 << 16 | 9 << 24, 0x11223344, 0x55667788

    def record(words):  # the slot at +0x60: three words, the low half first
        slot = b"".join(struct.pack(">HH", w & 0xFFFF, w >> 16) for w in words)
        return bytes(0x60) + slot

    tick = call(built, "dl_tick", [0x802, 0, 0xABC, rec], [(seen, 12)])
    r = run(
        machine,
        built,
        {stub: code, seen: bytes(12), rec: record([tag, a, b])},
        [
            tick,  # no feed: nothing stored
            call(built, "dl_stats_hook", [stub]),
            tick,  # a page: the feed gets it
        ],
    )
    assert r[0].reads[0] == bytes(12)
    assert struct.unpack(">3I", r[2].reads[0]) == (tag, a, b)
    # an acknowledgement (magic 0xA5D1) in the slot is not a page
    r = run(
        machine,
        built,
        {stub: code, seen: bytes(12), rec: record([0xA5D1 | 5 << 16, 0, 0])},
        [call(built, "dl_stats_hook", [stub]), tick],
    )
    assert r[1].reads[0] == bytes(12)


# ---- the DSP half on the SHARC core: the stats window (3) and the V2 registration window (4) ------------------------

RX, REPLY, FRAME, ROWS, BACK = 0x20010000, 0x20020000, 0x20030000, 0x20040000, 0xB9F000
# the provider's word count constant (`_dl_iwords` holds its address)
COUNT_AT = ROWS + 0x100
SENTINEL = 0xDEADBEEF


@pytest.fixture(scope="module", params=sorted(pk_device.dsp_devices()))
def dsp_tail(request):
    """The block tail of a device (src/dsp/<dev>.s) assembled, its data placed from 0x20050000, and the externs its object declares."""
    from tools.patchkit import pk_sharc

    dev = request.param
    source = pk_sharc.expand_includes(
        (MOD / "src/dsp" / f"{dev}.s").read_text(),
        [MOD / "src/dsp", *pk_sharc.sharc_include_dirs(MOD)],
    )
    obj = pk_sharc.assemble(source)
    sections, address, initial = {}, 0x20050000, {}
    for name, (blob, align, _bss) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        for i in range(len(blob) // 4):
            value = int.from_bytes(blob[4 * i : 4 * i + 4], "little")
            if value or len(blob) < 4096:
                initial[address // 4 + i] = value
        address += len(blob)
    syms = {
        name: sections[sec] + offset
        for name, (sec, offset, _) in obj.data_symbols.items()
    }
    manifest = tomllib.loads((MOD / "module.toml").read_text())
    (decl,) = [o for o in manifest["object"] if o.get("devices") == [dev]]
    externs = dict.fromkeys(decl["optional"], 0) | {"_dl_back": BACK}
    return dev, obj, syms, initial, externs


def dsp_chunk(dsp_tail, win, off, payload, rows=ROWS, count=16):
    """One block of the tail with a single chunk (transfer 1, chunk 0, not the last) into window `win` in the ring. The image has a V2 provider:
    its rows at `rows` (0: none), its word count `count` (None: `_dl_iwords` is 0). Returns the reads and the data symbols."""
    from tools.patchkit import pk_sharc

    dev, obj, syms, initial, externs = dsp_tail
    linked = pk_sharc.link(
        obj,
        0xB90000,
        externs
        | {"_dl_irows": rows, "_dl_iwords": 0 if count is None else COUNT_AT}
        | syms,
    )
    w0 = MAGIC | (win & 3) << 30 | (win >> 2) << 29
    words = [w0, 1 << 27 | len(payload) << 17 | off, *payload]
    words.append(check_word(words))
    tail = RX + define(gen_inc(dev), "DL_TAIL_OFF")
    dm = dict(initial)
    dm.update({tail // 4 + i: w for i, w in enumerate(words)})
    dm[FRAME // 4 - 3], dm[FRAME // 4 - 2] = RX, REPLY
    dm.update({ROWS // 4 - 1 + i: SENTINEL for i in range(18)})
    if count is not None:
        dm[COUNT_AT // 4] = count
    word = {
        n: syms[n] // 4 for n in ("_dl_stctl", "_dl_stptr", "_dl_status", "_dl_cum")
    }
    result = pk_sharc.run_call(
        linked,
        "_dl_hook",
        regs={"I4": REPLY, "I6": FRAME},
        dm_words=dm,
        reads={
            # the word before the rows, the 16 rows, the word after
            "rows": (ROWS // 4 - 1, 18),
            "stctl": (word["_dl_stctl"], 3),
            "stptr": (word["_dl_stptr"], 1),
            "status": (word["_dl_status"], 1),
            "cum": (word["_dl_cum"], 1),
        },
        return_sw=BACK,
        max_steps=5000,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    return result, syms


def area_words(dsp_tail) -> int:
    return define(gen_inc(dsp_tail[0]), "DL_AREA_WORDS")


@pytest.mark.parametrize(
    "rows,count,off,n,accepted",
    [
        (ROWS, 8, 6, 2, True),  # the last two words fit exactly
        (ROWS, 8, 7, 2, False),  # one word past its end
        (ROWS, 8, 8, 1, False),  # at its end
        (ROWS, "area", 14, 2, True),  # the largest provider: as many words as the area
        (ROWS, "area+1", 0, 1, False),  # larger than the area
        (0, 8, 0, 1, False),  # no rows: no provider
        (ROWS, None, 0, 1, False),  # no word count: no provider
        (ROWS, 0, 0, 1, False),  # no words
        (ROWS, 0xFFFFFFFF, 0, 1, False),  # a negative count
        (ROWS + 1, 8, 0, 1, False),  # rows not 4-aligned
    ],
)
def test_v2_window_writes_only_inside_its_provider(
    dsp_tail, rows, count, off, n, accepted
):
    if isinstance(count, str):
        count = area_words(dsp_tail) + (count == "area+1")
    payload = [0x11223344 + i for i in range(n)]
    r, _ = dsp_chunk(dsp_tail, 4, off, payload, rows=rows, count=count)
    expected = [SENTINEL] * 18
    if accepted:
        expected[1 + off : 1 + off + n] = payload
    assert r["rows"] == expected
    assert r["status"] == [0 if accepted else 3]
    assert r["cum"] == [1 if accepted else 0]
    # the stats window is not touched by a V2 write
    assert r["stctl"] == [0, 0, 0] and r["stptr"] == [0]


@pytest.mark.parametrize(
    "off,payload,accepted",
    [
        (0, [1, 7, 0], True),  # stats on, clear generation 7, no period
        (2, [1500], True),  # its last word
        (1, [1, 7, 0], False),  # one word past its three
        (3, [1], False),  # at its end
    ],
)
def test_stats_window_keeps_its_three_control_words_beside_a_v2_provider(
    dsp_tail, off, payload, accepted
):
    """Window 3 with a valid V2 provider in the image: upstream's stats control (`_dl_stctl`, DL_STATS_CTL_WORDS), the rows untouched."""
    r, syms = dsp_chunk(dsp_tail, 3, off, payload)
    assert r["rows"] == [SENTINEL] * 18
    assert r["status"] == [0 if accepted else 3]
    assert r["cum"] == [1 if accepted else 0]
    expected = [0, 0, 0]
    if accepted:
        expected[off : off + len(payload)] = payload
    assert r["stctl"] == expected
    # the window pointer the services test: the stats window while word 0 says on
    assert r["stptr"] == [syms["_dk_stats"] if expected[0] & 1 else 0]


@pytest.mark.parametrize("win", [5, 6, 7])
def test_windows_past_the_v2_window_refuse_every_write(dsp_tail, win):
    """Window 7 has bits 30-31 of window 3: it is neither the stats control nor the V2 rows."""
    r, _ = dsp_chunk(dsp_tail, win, 0, [1, 7, 0])
    assert r["status"] == [3] and r["cum"] == [0]
    assert r["rows"] == [SENTINEL] * 18
    assert r["stctl"] == [0, 0, 0] and r["stptr"] == [0]
