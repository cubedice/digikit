"""Execute internal row lifecycle scheduling with a synthetic V2 callback."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module")
def row_object():
    root = REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp"
    source = "\n".join(
        (root / name).read_text() for name in ("lifecycle.s", "rows.s", "protected.s")
    )
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_protected;
_test_protected:
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    CALL _ir_row_tick_protected;
    RTS;
.GLOBAL _test_reset;
_test_reset:
    I1 = 0x20040000;
    R0 = DM(0, I1);
    R0 = R0 + 1;
    DM(0, I1) = R0;
    DM(1, I1) = R4;
    DM(2, I1) = R8;
    I0 = R4;
    R1 = DM(0, I0);
    DM(3, I1) = R1;
    DM(0, I0) = R8;
    R1 = DM(4, I1);
    COMP(R0, R1);
    IF EQ JUMP _test_fail;
    R0 = 0;
    JUMP (M13, I13);
_test_fail:
    R0 = -7;
    JUMP (M13, I13);
"""
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20060000
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob) + 64
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(obj, 0xB90000, symbols)
    return linked, symbols, initial


@pytest.fixture
def rows(row_object):
    linked, symbols, initial = row_object
    dm = dict(initial)
    callback = linked.symbols["_test_reset"]
    row = [
        1,
        3,
        9,
        3,
        0x20050000,
        4,
        0x20050000,
        0x20050100,
        callback,
        callback,
        callback + 64,
        0,
        0,
        0,
        0,
        0,
    ]
    dm.update({symbols["_ir_rows"] // 4 + i: value for i, value in enumerate(row)})
    dm.update({0x20050000 // 4 + i: 0xDEADBEEF for i in range(64)})
    dm.update({0x20040000 // 4 + i: 0 for i in range(5)})
    reads = {
        "rows": (symbols["_ir_rows"] // 4, 128),
        "state": (0x20050000 // 4, 64),
        "calls": (0x20040000 // 4, 5),
    }

    def call(
        entry="_test_protected",
        index=0,
        owner=3,
        generation=9,
        operation=2,
        fail_at=None,
        fields=None,
    ):
        if fail_at is not None:
            dm[0x20040000 // 4 + 4] = fail_at
        for field, value in (fields or {}).items():
            dm[symbols["_ir_rows"] // 4 + field] = value
        result = pk_sharc.run_call(
            linked,
            entry,
            regs={
                "R4": index,
                "R5": owner,
                "R6": generation,
                "R8": operation,
                "M5": 0,
                "M6": 1,
                "M13": 0,
                "L0": 0,
                "L1": 0,
            },
            dm_words=dm,
            reads=reads,
            max_steps=2000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        for name, (address, _) in reads.items():
            dm.update({address + i: value for i, value in enumerate(result[name])})
        return result

    return call


def test_initialize_and_detach_one_instance_per_tick(rows):
    result = rows("_ir_row_begin")
    assert result["rows"][0] == 2 and result["calls"][0] == 0
    for index in range(3):
        result = rows()
        assert result["calls"][:4] == [index + 1, 0x20050000 + index * 16, 1, 0]
        assert result["rows"][0] == 2
        assert result["rows"][11:14] == [index + 1, 0, index + 1]
    result = rows()
    assert result["rows"][0] == 3 and result["calls"][0] == 3
    result = rows("_ir_row_begin", operation=3)
    assert result["rows"][0] == 4
    for index in range(3):
        result = rows()
        assert result["calls"][:4] == [index + 4, 0x20050000 + index * 16, 4, 1]
        assert result["rows"][0] == 4
    result = rows()
    assert result["rows"][0:4] == [6, 3, 9, 3]
    assert result["calls"][0] == 6
    assert rows("_ir_row_begin", operation=3)["calls"][0] == 6
    assert result["state"][12:] == [0xDEADBEEF] * 52


def test_partial_initialization_fault_retains_and_detaches_touched_instances(rows):
    rows("_ir_row_begin")
    rows()
    result = rows(fail_at=2)
    assert result["regs"]["R0"] == 0xFFFFFFF9
    assert result["rows"][0] == 5
    assert result["rows"][11:14] == [1, 0xFFFFFFF9, 2]
    assert rows()["calls"][0] == 2  # fault cannot silently continue
    rows("_ir_row_begin", operation=3)
    rows()
    rows()
    result = rows()
    assert result["rows"][0] == 6 and result["calls"][0] == 4
    assert result["state"][8:] == [0xDEADBEEF] * 56


def test_detach_fault_remains_owned_and_can_be_retried(rows):
    rows("_ir_row_begin")
    rows()
    rows("_ir_row_begin", operation=3)
    result = rows(fail_at=2)
    assert result["rows"][:3] == [5, 3, 9]
    rows("_ir_row_begin", operation=3)
    rows()
    result = rows()
    assert result["rows"][:3] == [6, 3, 9]
    assert result["calls"][0] == 3


@pytest.mark.parametrize(
    "arguments",
    [
        {"owner": 4},
        {"generation": 10},
        {"generation": 0},
        {"index": 8},
        {"index": 0xFFFFFFFF},
        {"operation": 4},
    ],
)
def test_wrong_owner_generation_row_or_operation_changes_nothing(rows, arguments):
    result = rows("_ir_row_begin", **arguments)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["rows"][:3] == [1, 3, 9]
    assert result["calls"][0] == 0


@pytest.mark.parametrize(
    "fields",
    [
        {3: 0},
        {3: 17},
        {5: 0},
        {5: 1025},
        {11: 4},
        {11: 3},
        {11: 1},
        {13: 4},
        {4: 0},
        {7: 0x20050004},
    ],
)
def test_bad_metadata_faults_without_callback(rows, fields):
    rows("_ir_row_begin")
    result = rows(fields=fields)
    assert result["rows"][0] == 5
    assert result["calls"][0] == 0
    assert result["state"] == [0xDEADBEEF] * 64


def test_cancel_before_first_tick_never_calls_reset(rows):
    rows("_ir_row_begin")
    rows("_ir_row_begin", operation=3)
    result = rows()
    assert result["rows"][0] == 6 and result["calls"][0] == 0
