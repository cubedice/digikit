"""CANOPY's half-table expansion (src/dsp/wavetable.s _wt_expand_table) on the SHARC instruction core: the table the
DSP rebuilds from the stored halves (src/dsp/wavetables.s) equals the generator's full table word for word."""

import pytest
from canopy_paths import DSP

from modules.machine.digikit.canopy.tools.wavetables import (
    TABLE_NAMES,
    TABLE_WORDS,
    full_words,
)
from tools.patchkit import pk_sharc

PER_TABLE = TABLE_WORDS // 2  # 32-bit words of one table in _wt_factory
WORDS = len(TABLE_NAMES) * PER_TABLE
POISON, GUARD = 0xA5A5A5A5, 0xDEADBEEF
DRIVER = """
.SECTION/PM seg_pmco;
.GLOBAL _test_expand_table;
_test_expand_table:
    CALL _wt_expand_table;
    RTS;
.GLOBAL _test_expand_step;
_test_expand_step:
    CALL _wt_expand_step;
    RTS;
"""
# The caller's registers: the routines clobber R0..R3 only (and ASTAT).
KEPT = {f"R{n}": 0x51000000 + n for n in range(4, 16)} | {
    "I0": 0x20001000,
    "I1": 0x20001100,
    "I2": 0x20001200,
    "M7": 7,
}


@pytest.fixture(scope="module")
def rig():
    source = "\n".join(
        (DSP / name).read_text() for name in ("wavetable.s", "wavetables.s")
    )
    obj = pk_sharc.assemble(source + DRIVER)
    sections, initial, address = {}, {}, 0x20060000
    for name, (blob, align, _) in obj.data.items():
        address = (address + 64 + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob)
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    # wavetable.s also names the FM tables and the envelopes; the expansion reads neither.
    externs = {
        name: 0x20010000
        for name in ("_cp_envelopes", "_fm_bank", "_fm_ratios", "_fm_sines")
    }
    linked = pk_sharc.link(obj, 0xB90000, {**externs, **symbols})
    table = symbols["_wt_factory"] // 4
    template = pk_sharc.CallSession([linked], 0xB9F000, fresh_registers=True)
    for word, value in initial.items():
        template.poke(word, value)
    # The loader neither sends nor zeroes bss: start the table from poison, between two guard words.
    for i in range(WORDS):
        template.poke(table + i, POISON)
    template.poke(table - 1, GUARD)
    template.poke(table + WORDS, GUARD)
    return template, symbols, table


def call(session, entry, table=None):
    regs = {"M5": 0, "M6": 1, "L0": 0, "L1": 0, "L2": 0, "MODE1": 0x1011800, **KEPT}
    if table is not None:
        regs["R0"] = table
    result = session.call(
        entry, regs=regs, max_steps=20000, read_regs=("R14", "R15", "I1", "I2", "M7")
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert {name: result["regs"].get(name) for name in KEPT} == KEPT
    return result["instructions"]


@pytest.mark.parametrize("order", ["background", "demand"])
def test_dsp_expansion_equals_the_generators_full_table(rig, order):
    template, symbols, table = rig
    session = template.fork(pk_sharc.session_backend())
    step = ("_test_expand_step", None)
    if order == "background":
        # _wt_prepare's _wt_expand_step: one table a call, then nothing
        calls = [step] * 7
    else:
        # the tables a block is about to read, in any order, one of them again; then
        # every step finds all five whole
        calls = [("_test_expand_table", t) for t in (4, 2, 0, 3, 1, 2)] + [step] * 5
    costs = [call(session, entry, t) for entry, t in calls]
    words = [session.peek(table + i) for i in range(-1, WORDS + 1)]
    assert words[0] == words[-1] == GUARD
    assert words[1:-1] == full_words()
    assert session.peek(symbols["_wt_expanded"] // 4) == 31
    assert session.peek(symbols["_wt_expand_next"] // 4) == 5
    expanding, idle = costs[:5], costs[5:]
    assert max(expanding) < 11000 and max(idle) < 20
    print("CANOPY expansion instructions per call", order, costs)


def test_dsp_expansion_of_one_table_writes_only_that_table(rig):
    template, _, table = rig
    session = template.fork(pk_sharc.session_backend())
    call(session, "_test_expand_table", 3)
    words = [session.peek(table + i) for i in range(WORDS)]
    full = full_words()
    for t in range(len(TABLE_NAMES)):
        part = slice(t * PER_TABLE, (t + 1) * PER_TABLE)
        assert words[part] == (full[part] if t == 3 else [POISON] * PER_TABLE)
