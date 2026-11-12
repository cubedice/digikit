"""Overloop UI transport and rendering on the real CF/SHARC instruction cores."""

import json
import os
import struct
import subprocess
from pathlib import Path

import pytest
from support.fx_rig import DATA_END, FxRig, Row, nw

from tools.patchkit import patchkit, pk_sharc

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "modules/core/adapters/loop-display"
BASE, DATA, REPLY, BITMAP = 0x47E00000, 0x47F00000, 0x47F01000, 0x47F10000


def carrier_indices():
    return [
        (0x1C + 0x54 * row + offset) // 4
        for row in (17, 18, 19, 20, 29, 30, 31)
        for offset in (0x44, 0x48)
    ]


def carrier_words(words):
    packed = words[:14]
    packed[0] = 0x4F564C32
    packed[7] = words[7] | words[14] << 8 | words[15] << 16
    return packed


def transport(words, present=True):
    source = (
        ROOT / "modules/core/adapters/dsp-link/src/dsp/loop-status.inc"
    ).read_text()
    obj = pk_sharc.assemble(
        ".EXTERN _dl_loop_status_return;\n.GLOBAL _dl_loop_status;\n" + source
    )
    mailbox, reply, back = 0x20090000, 0x200A0000, 0xB9F000
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            "_loop_ui_status": mailbox if present else 0,
            "_dl_loop_status_return": back,
        },
    )
    original = [0xA5000000 + i for i in range(0xABC // 4)]
    dm = {reply // 4 + i: value for i, value in enumerate(original)}
    dm.update({mailbox // 4 + i: value for i, value in enumerate(words)})
    out = pk_sharc.run_call(
        linked,
        "_dl_loop_status",
        regs={"I4": reply},
        dm_words=dm,
        reads={"reply": (reply // 4, len(original))},
        return_sw=back,
        max_steps=100,
    )
    assert out["halt"]["reason"] == "breakpoint", out
    return original, out["reply"]


@pytest.mark.parametrize("present", [False, True])
def test_dsp_reply_preserves_every_byte_outside_status_slots(present):
    words = [0x4F564C31, *range(1, 16)]
    original, result = transport(words, present)
    expected = original.copy()
    if present:
        for index, value in zip(carrier_indices(), carrier_words(words), strict=True):
            expected[index] = value
    assert result == expected


@pytest.mark.parametrize("control", [7, 14, 15])
@pytest.mark.parametrize("invalid", [128, 256, 0xFFFFFFFF])
def test_dsp_rejects_unrepresentable_step_controls(control, invalid):
    words = [0x4F564C31, *range(1, 16)]
    words[control] = invalid
    original, result = transport(words)
    assert result == original


def test_immediate_fx_breadcrumbs_cannot_destroy_a_published_loop_reply():
    """The old row13 magic was overwritten before CF copied the previous ring.
    Run the real FX call/return markers after the loop tail has published both
    records: guard evidence must remain immediate and the full carrier intact.
    """
    words = [0x4F564C31, *range(1, 16)]
    _, published = transport(words)
    rig = FxRig([Row("_te_half", "_te_state_a")])
    # dsp_voice_rig's reply addresses overlap FxRig's DATA_BASE. Reserve two
    # whole records beyond this rig's initialized adapter/module/state storage.
    used_end = max(address + len(raw) for address, raw, _ in rig.built.inits)
    ring_a = (used_end + 0xFFF) & ~0xFFF
    rings = (ring_a, ring_a + 0x1000)
    assert rings[-1] + len(published) * 4 <= DATA_END
    rec = rig.built.data["_fx_rec"]
    rig.sess.poke(nw(rec), rings[0])
    rig.sess.poke(nw(rec) + 1, rings[1])
    rig.sess.poke(nw(rec) + 2, rig.built.data["_dk_step"])
    for ring in rings:
        for i, value in enumerate(published):
            rig.sess.poke(nw(ring) + i, value)
    result = rig.block([0.125] * 64, ctx="friendly")
    assert result.returned, result.halt
    for ring in rings:
        assert rig.sess.peek(nw(ring) + 297) == 0x205  # immediate master return
        assert rig.sess.peek(nw(ring) + 45) == 0x20D1A6
        assert [
            rig.sess.peek(nw(ring) + i) for i in carrier_indices()
        ] == carrier_words(words)


@pytest.fixture(scope="module")
def display(tmp_path_factory):
    folder = tmp_path_factory.mktemp("loop-display")
    stub = folder / "draw.c"
    stub.write_text("""#include <stdint.h>
#include "dk_fw_draw.h"
uint32_t fxpages_info[64], loop_reply_literal, calls, outside, stock_calls, page;
uint32_t get_page(uint8_t *v) { (void)v; return page; }
uint32_t dkrt_irq_mask(void) { return 0x2700; }
void dkrt_irq_restore(uint32_t sr) { (void)sr; }
void loop_stock_update(uint8_t *v) { (void)v; ++stock_calls; }
void invalidate(uint8_t *v) __asm__("cf.view.fn.invalidate");
void invalidate(uint8_t *v) { (void)v; ++calls; }
void dk_fw_draw_fill_rect(uint8_t *b, int32_t x, int32_t y, int32_t ex, int32_t ey, int32_t ink) {
    if (x<0 || y<0 || ex>=128 || ey>=64) { ++outside; return; }
    for (int j=y;j<=ey;++j) for(int i=x;i<=ex;++i) b[(63-j)*128+i]=(uint8_t)ink;
}
""")
    return patchkit.cf_link(
        [MOD / "src/display.c", stub], BASE, {}, [ROOT / "modules/core/fw/include"]
    )


def execute(display, tmp_path, ops):
    binary = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not binary.exists():
        pytest.skip("build cf_script explicitly")
    script = tmp_path / "display.json"
    script.write_text(
        json.dumps(
            [
                {
                    "op": "poke",
                    "addr": BASE,
                    "hex": (display.image + bytes(display.bss_size)).hex(),
                },
                *ops,
            ]
        )
    )
    result = subprocess.run(
        [str(binary), str(ROOT / "firmware/Digitakt_II_OS1.17.syx"), str(script)],
        capture_output=True,
        text=True,
        timeout=60,
    )
    assert result.returncode == 0, result.stderr + result.stdout[-2000:]
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    for row in rows:
        if row.get("op") == "call":
            assert row["ok"] and not row.get("error"), row
    return {
        row["tag"]: bytes.fromhex(row["hex"]) for row in rows if row.get("op") == "peek"
    }


def poke(addr, blob):
    return {"op": "poke", "addr": addr, "hex": blob.hex()}


def call(display, name, args=()):
    return {
        "op": "call",
        "pc": display.symbols[name],
        "args": list(args),
        "max": 2000000,
    }


def peek(addr, count, tag):
    return {"op": "peek", "addr": addr, "len": count, "tag": tag}


def test_cf_status_endianness_and_missing_or_stalled_sender(display, tmp_path):
    words = [
        0x4F564C31,
        70,
        2,
        384000,
        384000,
        1600,
        0x3F800000,
        0,
        64,
        64,
        0,
        64,
        96000,
        48000,
        16,
        8,
    ]
    reply = bytearray(0xABC)
    # Use the executed SHARC producer, not a separately emulated byte encoder.
    _, produced = transport(words)
    for i, value in enumerate(produced):
        struct.pack_into(">HH", reply, i * 4, value & 0xFFFF, value >> 16)
    result = execute(
        display,
        tmp_path,
        [
            poke(display.symbols["loop_reply_literal"], struct.pack(">I", REPLY)),
            poke(REPLY, reply),
            call(display, "loop_ui_tick"),
            peek(display.symbols["loop_ui_snapshot"], 64, "fresh"),
            *[call(display, "loop_ui_tick") for _ in range(65)],
            peek(display.symbols["loop_ui_age"], 4, "age"),
            poke(REPLY + 4 * carrier_indices()[0], bytes(4)),
            call(display, "loop_ui_tick"),
            peek(display.symbols["loop_ui_snapshot"], 64, "missing"),
        ],
    )
    assert struct.unpack(">16I", result["fresh"]) == tuple(words)
    assert struct.unpack(">I", result["age"])[0] >= 60
    assert result["missing"] == result["fresh"]  # retained data is explicitly stale


@pytest.mark.parametrize("bad", [0, 0x80, 0x8000, 0x800000, 0x1000000])
def test_cf_accepts_step_limits_but_refuses_reserved_packed_bits(
    display, tmp_path, bad
):
    words = [0x4F564C31, 1, 0, 0, 0, 0, 0, 127, 128, 128, 0, 128, 0, 0, 127, 127]
    _, produced = transport(words)
    reply = bytearray(0xABC)
    produced[carrier_indices()[7]] |= bad
    for i, value in enumerate(produced):
        struct.pack_into(">HH", reply, i * 4, value & 0xFFFF, value >> 16)
    result = execute(
        display,
        tmp_path,
        [
            poke(display.symbols["loop_reply_literal"], struct.pack(">I", REPLY)),
            poke(REPLY, reply),
            call(display, "loop_ui_tick"),
            peek(display.symbols["loop_ui_updates"], 4, "updates"),
            peek(display.symbols["loop_ui_snapshot"], 64, "snapshot"),
        ],
    )
    assert result["updates"] == struct.pack(">I", 0 if bad else 1)
    if not bad:
        assert struct.unpack(">16I", result["snapshot"]) == tuple(words)


@pytest.mark.parametrize(
    "fresh,frames,gain,side",
    [(1, 384000, 0x3F800000, 104), (1, 0, 0x3F800000, 2), (0, 384000, 0x3F800000, 2)],
)
def test_fader_shows_actual_available_audio_and_pixels_stay_in_bounds(
    display, tmp_path, fresh, frames, gain, side
):
    words = [
        0x4F564C31,
        100,
        2,
        frames,
        0,
        8000,
        gain,
        0,
        64,
        64,
        0,
        64,
        360000,
        48000,
        60,
        8,
    ]
    result = execute(
        display,
        tmp_path,
        [
            poke(DATA, struct.pack(">16I", *words)),
            call(display, "loop_ui_render", [BITMAP, DATA, fresh, 1, 60, 8]),
            peek(BITMAP, 8192, "pixels"),
            peek(display.symbols["outside"], 4, "outside"),
        ],
    )
    pixels = result["pixels"]
    assert pixels[63 * 128 + side : 63 * 128 + side + 23] == bytes([1]) * 23
    assert result["outside"] == bytes(4)
    if fresh and frames:
        assert pixels[36 * 128 + 4] == pixels[36 * 128 + 123] == 1  # wrapped window


def test_updates_only_invalidate_visible_overloop_and_preserve_stock_update(
    display, tmp_path
):
    view, manager, node, vtable, ids = (
        DATA,
        DATA + 0x200,
        DATA + 0x300,
        DATA + 0x400,
        DATA + 0x500,
    )
    info = [0] * 64
    info[0], info[2], info[5], info[6], info[28] = 0x46585047, 1, 16, 1, ids

    def word(value):
        return struct.pack(">I", value)

    result = execute(
        display,
        tmp_path,
        [
            poke(display.symbols["fxpages_info"], struct.pack(">64I", *info)),
            poke(ids, struct.pack(">H", 0xCA1B)),
            poke(view, word(vtable)),
            poke(view + 0x2C, word(manager)),
            poke(vtable + 0x88, word(display.symbols["get_page"])),
            poke(manager + 0x1C, word(node)),
            poke(node, word(manager + 0x14)),
            poke(node + 8, word(view)),
            poke(display.symbols["page"], word(16)),
            poke(display.symbols["loop_ui_age"], word(0)),
            poke(display.symbols["loop_ui_snapshot"] + 4, word(60)),
            call(display, "loop_view_update", [view]),
            peek(display.symbols["calls"], 4, "visible"),
            poke(manager + 0x1C, word(manager + 0x14)),  # opaque modal hides it
            call(display, "loop_view_update", [view]),
            peek(display.symbols["calls"], 4, "hidden"),
            poke(manager + 0x1C, word(node)),
            poke(display.symbols["page"], word(15)),
            call(display, "loop_view_update", [view]),
            peek(display.symbols["calls"], 4, "stock"),
            peek(display.symbols["stock_calls"], 4, "updates"),
        ],
    )
    assert result["visible"] == result["hidden"] == result["stock"] == word(1)
    assert result["updates"] == word(3)
