"""SHARC+ patchkit toolchain: a module source built by `patchkit sharc` (Selache as a library, our decoder re-decodes) runs on the core.

The guards, the fix-ups and the decoder checks of the build are tested in packages/patchkit/src/build/sharc_obj.rs. Skips when Selache's
C compiler is not built (uv run python -m tools.patchkit.pk_toolchains selache)."""

import struct

import pytest
from support.paths import REPO_ROOT

ROOT = REPO_ROOT

from tools.patchkit import (  # noqa: E402
    pk_sharc,  # noqa: E402
    pk_toolchains,  # noqa: E402
)

SRC = ROOT / "tests" / "patchkit" / "pk_src"
BASE = 0xB90000  # short-word (PC) address in L2 code space
BUF = 0x08020000  # normal-word DM address in L2
HOOK = 0xB9F000
RET = 0xB9E000
FRAME = 0x08030100  # a C frame: [FRAME] = caller frame pointer, [FRAME-1] = return address - 1


def bits(x: float) -> int:
    return struct.unpack("<I", struct.pack("<f", x))[0]


def val(x: int) -> float:
    return struct.unpack("<f", struct.pack("<I", x))[0]


def test_scale_buffer_runs_and_every_word_redecodes():
    obj, linked = pk_sharc.build(
        (SRC / "scale.s").read_text(), BASE, {"_fw_hook": HOOK}
    )
    assert linked.checked == len(obj.rows) == 12
    vals = [1.0, 2.0, -3.0, 0.5]
    out = pk_sharc.run_call(
        linked,
        "_scale_buf",
        regs={"R4": BUF, "R8": 4},
        dm_words={BUF + i: bits(v) for i, v in enumerate(vals)},
        reads={"out": (BUF, 5)},
        return_sw=HOOK,
    )
    assert out["halt"]["reason"] == "breakpoint" and out["halt"]["pc_sw"] == HOOK
    assert [val(w) for w in out["out"][:4]] == [v * 2.5 for v in vals]
    assert out["out"][4] is None  # one past the buffer untouched
    assert out["regs"]["R0"] == 7  # the local call `_bias` returned


# --- selcc evaluation: its output decodes and runs correctly on the core (byte-address pointers) ---


@pytest.fixture(scope="module")
def selcc_module():
    if pk_toolchains.selache_dir() is None:
        pytest.skip("selache not built (tools/patchkit/pk_toolchains.py selache)")
    if pk_toolchains.selcc_bin() is None:
        pytest.skip("selcc not built (not part of pk_toolchains selache)")
    return pk_sharc.link(pk_sharc.compile_c((SRC / "scale_c.c").read_text()), BASE)


def c_call(linked, entry, regs, dm=None, reads=None):
    mem = {FRAME: 0, FRAME - 1: RET - 1, **(dm or {})}
    regs = {**regs, "I6": FRAME, "I7": FRAME - 2}
    return pk_sharc.run_call(
        linked,
        entry,
        regs=regs,
        dm_words=mem,
        reads=reads,
        return_sw=RET,
        max_steps=20000,
    )


def s32(x):
    return x - (1 << 32) if x & 0x80000000 else x


def test_selcc_integer_functions(selcc_module):
    assert (
        s32(c_call(selcc_module, "add3.", {"R4": 10, "R8": 3, "R12": 7})["regs"]["R0"])
        == 31
    )
    byte_buf = 0x20080000
    r = c_call(
        selcc_module,
        "sum.",
        {"R4": byte_buf, "R8": 5},
        {byte_buf + 4 * i: v & 0xFFFFFFFF for i, v in enumerate([1, 2, 3, 40, -5])},
    )
    assert s32(r["regs"]["R0"]) == 41
    for x, want in ((0, 11), (1, 22), (2, 33), (5, 44), (9, -1)):
        assert s32(c_call(selcc_module, "sw.", {"R4": x})["regs"]["R0"]) == want
    for x in (0, 0xF0F0, 0xFFFFFFFF):
        assert c_call(selcc_module, "popc.", {"R4": x})["regs"]["R0"] == bin(x).count(
            "1"
        )


def test_selcc_float_loop(selcc_module):
    byte_buf = 0x20080000
    vals = [1.0, 2.0, -3.0, 0.5]
    r = c_call(
        selcc_module,
        "scale.",
        {"R4": byte_buf, "R8": 4, "R12": bits(1.5)},
        {byte_buf + 4 * i: bits(v) for i, v in enumerate(vals)},
        {f"o{i}": (byte_buf + 4 * i, 1) for i in range(4)},
    )
    assert [val(r[f"o{i}"][0]) for i in range(4)] == [v * 1.5 for v in vals]


@pytest.mark.parametrize("image", ["dt2-1.17", "dn2-1.12"])
def test_every_stock_shift_immediate_passes_the_check(image, tmp_path):
    """The shift-immediate rule of the build (`patchkit sharc shift-check`) accepts every 48-bit shift instruction in the functions of the
    stock image."""
    import sqlite3

    db = ROOT / "out" / "sharcdb" / f"{image}.sqlite"
    if not db.is_file():
        pytest.skip(f"no {db} (tools/sharc_dev/sharcdb build)")
    with sqlite3.connect(db) as c:
        rows = c.execute(
            "select sw, raw from insn where form in ('6a_mem', '6a_nomem', '6b_shiftimm')"
            " and aligned = 1 and function_sw is not null"
        ).fetchall()
    words = tmp_path / "words.txt"
    words.write_text("".join(f"{raw}\n" for _, raw in rows))
    verdicts = pk_sharc._sharc(["shift-check", str(words)]).splitlines()
    bad = [(hex(sw), v) for (sw, _), v in zip(rows, verdicts, strict=True) if v != "ok"]
    assert len(rows) > 1000 and bad == []


# --- CallSession: fresh registers per call and forks of a shared image ---

BUMP = """
.SECTION/PM seg_pmco;
.GLOBAL _bump;
_bump:
    R1 = R1 + 1;
    I0 = R4;
    DM(0, I0) = R1;
    R0 = R1;
    RTS;
"""


def test_call_session_fresh_registers_match_run_call():
    _, linked = pk_sharc.build(BUMP, BASE, {})
    first = {"R4": BUF, "R1": 5}
    carried = pk_sharc.CallSession([linked], HOOK)
    fresh = pk_sharc.CallSession([linked], HOOK, fresh_registers=True)
    for sess in (carried, fresh):
        assert sess.call("_bump", first)["regs"]["R0"] == 6
    # R1 is not given: a session keeps the last call's, a fresh core does not.
    assert carried.call("_bump", {"R4": BUF})["regs"]["R0"] == 7
    alone = pk_sharc.run_call(
        linked, "_bump", regs={"R4": BUF}, reads={"out": (BUF, 1)}, return_sw=HOOK
    )
    again = fresh.call("_bump", {"R4": BUF})
    assert again["regs"] == alone["regs"] and again["halt"] == alone["halt"]
    assert again["regs"]["R0"] != 7
    # DM still carries over: the word the call stored stays.
    assert fresh.peek(BUF) == alone["out"][0]


def test_call_session_forks_do_not_share_memory():
    _, linked = pk_sharc.build(BUMP, BASE, {})
    image = pk_sharc.CallSession([linked], HOOK, fresh_registers=True)
    image.poke(BUF + 1, 123)
    a, b = image.fork(), image.fork()
    assert a.peek(BUF + 1) == b.peek(BUF + 1) == 123
    assert a.call("_bump", {"R4": BUF, "R1": 1})["regs"]["R0"] == 2
    b.poke(BUF + 1, 7)
    assert (a.peek(BUF), a.peek(BUF + 1)) == (2, 123)
    assert (b.peek(BUF), b.peek(BUF + 1)) == (None, 7)
    assert image.peek(BUF) is None and image.peek(BUF + 1) == 123


# --- the native core behind run_call and CallSession (skipped without the native library) ---

needs_native = pytest.mark.skipif(
    pk_sharc.native_library() is None,
    reason=f"native SHARC library not built: {pk_sharc.NATIVE_HINT}",
)


@needs_native
def test_native_run_call_matches_python():
    _, linked = pk_sharc.build(BUMP, BASE, {})
    args = dict(
        regs={"R4": BUF, "R1": 41},
        dm_words={BUF + 1: 7},
        reads={"out": (BUF, 3)},
        return_sw=HOOK,
    )
    python = pk_sharc.run_call(linked, "_bump", **args)
    assert python["out"] == [42, 7, None]
    assert pk_sharc.run_call(linked, "_bump", backend="native", **args) == python
    assert pk_sharc.run_call(linked, "_bump", backend="check", **args) == python


@needs_native
def test_native_session_matches_python_and_keeps_its_memory():
    _, linked = pk_sharc.build(BUMP, BASE, {})
    image = pk_sharc.CallSession([linked], HOOK, fresh_registers=True)
    image.poke(BUF + 1, 123)
    python, native = image.fork("python"), image.fork("native")
    for regs in ({"R4": BUF, "R1": 5}, {"R4": BUF}, {"R4": BUF + 2, "R1": 40}):
        assert native.call("_bump", regs) == python.call("_bump", regs)
    assert [native.peek(BUF + i) for i in range(4)] == [
        python.peek(BUF + i) for i in range(4)
    ]
    assert native.fallbacks == {}
    checked = image.fork("check")
    assert checked.call("_bump", {"R4": BUF, "R1": 1})["regs"]["R0"] == 2


def test_check_reports_which_results_differ():
    with pytest.raises(pk_sharc.PkSharcError, match=r"differ in \['instructions'\]"):
        pk_sharc._check_same(
            "call", {"instructions": 5, "regs": {}}, {"instructions": 6, "regs": {}}
        )
