"""Overloop UI transport and rendering on the real CF/SHARC instruction cores."""

import json
import os
import struct
import subprocess
from pathlib import Path

import pytest

from tools.patchkit import patchkit, pk_sharc

ROOT = Path(__file__).resolve().parents[2]
MOD = ROOT / "modules/core/adapters/loop-display"
BASE, DATA, REPLY, BITMAP = 0x47E00000, 0x47F00000, 0x47F01000, 0x47F10000


@pytest.mark.parametrize("present", [False, True])
def test_dsp_reply_preserves_every_byte_outside_status_slots(present):
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
    words = [0x4F564C31, *range(1, 16)]
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
    expected = original.copy()
    if present:
        for j, value in enumerate(words):
            expected[(0x1C + 0x54 * (13 + j // 2) + 0x44 + (j % 2) * 4) // 4] = value
    assert out["reply"] == expected


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
    for j, value in enumerate(words):
        offset = 0x1C + 0x54 * (13 + j // 2) + 0x44 + (j % 2) * 4
        struct.pack_into(">HH", reply, offset, value & 0xFFFF, value >> 16)
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
            poke(REPLY + 0x1C + 0x54 * 13 + 0x44, bytes(4)),
            call(display, "loop_ui_tick"),
            peek(display.symbols["loop_ui_snapshot"], 64, "missing"),
        ],
    )
    assert struct.unpack(">16I", result["fresh"]) == tuple(words)
    assert struct.unpack(">I", result["age"])[0] >= 60
    assert result["missing"] == result["fresh"]  # retained data is explicitly stale


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
