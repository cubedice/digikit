"""Mailbox -> protected DSP row work -> checksummed completion reply."""

from functools import reduce
from operator import xor

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


def command(sequence, operation, owner=3, generation=9):
    words = [0x49524731, 1, sequence, owner, generation, operation, 7] + [0] * 9
    words[15] = reduce(xor, words[:15])
    return words


@pytest.fixture(scope="module")
def scheduler_object():
    root = REPO_ROOT / "modules/core/adapters"
    source = "\n".join(
        (root / f"instrument-registry/src/dsp/{name}.s").read_text()
        for name in ("registry", "rows", "lifecycle", "protected")
    )
    source += "\n" + (root / "midi-events/src/dsp/reply.s").read_text()
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_block;
_test_block:
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    CALL _instrument_registry_block;
    CALL _mi_reply_build;
    RTS;
.GLOBAL _test_reset;
_test_reset:
    I1 = 0x20040010;
    R0 = DM(0, I1);
    R0 = R0 + 1;
    DM(0, I1) = R0;
    DM(1, I1) = R8;
    I0 = R4;
    DM(0, I0) = R8;
    R1 = DM(2, I1);
    COMP(R0, R1);
    IF EQ JUMP _test_fail;
    R0 = 0;
    JUMP (M13, I13);
_test_fail:
    R0 = -7;
    JUMP (M13, I13);
"""
    obj = pk_sharc.assemble(source)
    sections, initial, reads, address = {}, {}, {}, 0x20050000
    for index, (name, (blob, align, _)) in enumerate(obj.data.items()):
        address = (address + align - 1) // align * align
        sections[name] = address
        reads[f"section{index}"] = (address // 4, len(blob) // 4)
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
    linked = pk_sharc.link(
        obj, 0xB90000, {**symbols, "_mi_epoch": 0x20040000, "_mi_message": 0x20040004}
    )
    return linked, symbols, initial, reads


@pytest.fixture
def scheduler(scheduler_object):
    linked, symbols, initial, sections = scheduler_object
    dm = dict(initial)
    dm.update({0x20040000 // 4 + i: 0 for i in range(8)})
    dm.update({0x20070000 // 4 + i: 0xDEADBEEF for i in range(16)})
    callback = linked.symbols["_test_reset"]
    row = [
        1,
        3,
        9,
        2,
        0x20070000,
        4,
        0x20070000,
        0x20070040,
        callback,
        callback,
        callback + 64,
        0,
        0,
        0,
        0,
        0,
    ]
    base = symbols["_ir_rows"] // 4 + 7 * 16
    dm.update({base + i: value for i, value in enumerate(row)})
    dm[symbols["_ir_lifecycle_ready"] // 4] = 1  # test-only trusted admission
    reads = {
        **sections,
        "wire": (symbols["_instrument_reply"] // 4, 16),
        "row": (base, 16),
        "calls": (0x20040010 // 4, 3),
        "state": (0x20070000 // 4, 16),
    }

    def block(words=None, fail_at=None, ready=None, empty=False):
        if empty:
            dm.update({base + i: 0 for i in range(16)})
        if words is not None:
            dm.update(
                {symbols["_dl_irows"] // 4 + i: value for i, value in enumerate(words)}
            )
        if fail_at is not None:
            dm[0x20040018 // 4] = fail_at
        if ready is not None:
            dm[symbols["_ir_lifecycle_ready"] // 4] = ready
        result = pk_sharc.run_call(
            linked,
            "_test_block",
            regs={"M5": 0, "M6": 1, "L0": 0, "L1": 0, "MODE1": 0x1011800},
            dm_words=dm,
            reads=reads,
            max_steps=2500,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        for name, (address, _) in reads.items():
            dm.update({address + i: value for i, value in enumerate(result[name])})
        a = b = 0
        for value in result["wire"][:15]:
            a = (a + value) & 0xFFFFFFFF
            b = (b + a) & 0xFFFFFFFF
        assert result["wire"][15] == a ^ b
        result["ack"] = result["wire"][4:12]
        return result

    return block


def test_success_waits_for_all_callbacks_and_replay_does_not_repeat_them(scheduler):
    previous = scheduler(command(1, 1))["ack"]
    for count, words in [(1, command(2, 2)), (2, None)]:
        result = scheduler(words)
        assert result["ack"] == previous and result["calls"][0] == count
    result = scheduler()
    assert result["row"][0] == 3
    assert result["ack"] == [0x49525231, 2, 3, 9, 2, 7, 0, 7]
    assert scheduler()["calls"][0] == 2
    for count, words in [(3, command(3, 3)), (4, None)]:
        result = scheduler(words)
        assert result["ack"][1] == 2 and result["calls"][0] == count
    result = scheduler()
    assert result["row"][:3] == [6, 3, 9]
    assert result["ack"] == [0x49525231, 3, 3, 9, 3, 7, 0, 7]


def test_pending_ticket_survives_mailbox_clear_and_new_command(scheduler):
    scheduler(command(1, 2))
    result = scheduler([0] * 16)
    assert result["calls"][0] == 2 and result["ack"][0] == 0
    result = scheduler(command(2, 3))
    assert result["ack"][1:6] == [1, 3, 9, 2, 7]
    assert result["row"][0] == 3
    result = scheduler()  # new command is accepted only after prior completion
    assert result["row"][0] == 4 and result["calls"][0] == 3


def test_partial_callback_fault_retains_row_until_completed_detach(scheduler):
    scheduler(command(1, 2))
    result = scheduler(fail_at=2)
    assert result["ack"][1:] == [1, 3, 9, 2, 7, 4, 7]
    assert result["row"][:3] == [5, 3, 9]
    assert result["row"][12] == 0xFFFFFFF9
    assert scheduler()["calls"][0] == 2
    scheduler(command(2, 3))
    scheduler()
    result = scheduler()
    assert result["row"][:3] == [6, 3, 9]
    assert result["ack"][6] == 0 and result["calls"][0] == 4


def test_detach_fault_requires_new_sequence_and_successful_cleanup(scheduler):
    scheduler(command(1, 2))
    scheduler()
    scheduler()
    result = scheduler(command(2, 3), fail_at=3)
    assert result["ack"][1:] == [2, 3, 9, 3, 7, 4, 7]
    assert result["row"][:3] == [5, 3, 9]
    assert scheduler()["calls"][0] == 3
    scheduler(command(3, 3))
    scheduler()
    result = scheduler()
    assert result["ack"][1:] == [3, 3, 9, 3, 7, 0, 7]
    assert result["row"][:3] == [6, 3, 9]
    assert result["calls"][0] == 5


@pytest.mark.parametrize("owner,generation", [(4, 9), (3, 10)])
def test_stale_identity_is_rejected_without_work(scheduler, owner, generation):
    result = scheduler(command(1, 2, owner, generation))
    assert result["ack"][6] == 3
    assert result["row"][0] == 1 and result["calls"][0] == 0


@pytest.mark.parametrize("ready", [0, 2])
def test_unadmitted_provider_remains_unsupported(scheduler, ready):
    result = scheduler(command(1, 2), ready=ready)
    assert result["ack"][6:] == [1, 0]
    assert result["row"][0] == 1 and result["calls"][0] == 0


def install_command(scheduler, sequence=2, generation=9):
    original = scheduler(command(1, 1))["row"]
    words = command(sequence, 4, generation=generation)
    words[7:15] = original[3:11]
    words[15] = reduce(xor, words[:15])
    return words


def test_install_reply_precedes_initialization_and_replay_preserves_row(scheduler):
    words = install_command(scheduler)
    result = scheduler(words, empty=True)
    assert result["ack"] == [0x49525231, 2, 3, 9, 4, 7, 0, 7]
    assert result["row"][:3] == [1, 3, 9]
    assert result["row"][3:11] == words[7:15]
    assert result["state"] == [0xDEADBEEF] * 16
    assert result["calls"][0] == 0
    assert scheduler()["row"] == result["row"]
    scheduler(command(3, 2))
    scheduler()
    result = scheduler()
    assert result["row"][0] == 3 and result["calls"][0] == 2
    assert result["ack"][1:] == [3, 3, 9, 2, 7, 0, 7]


@pytest.mark.parametrize("ready", [0, 2])
def test_install_is_disabled_without_trusted_admission(scheduler, ready):
    words = install_command(scheduler)
    result = scheduler(words, ready=ready, empty=True)
    assert result["ack"][6:] == [1, 0]
    assert result["row"] == [0] * 16


def test_install_bad_metadata_and_owned_row_are_rejected(scheduler):
    words = install_command(scheduler)
    result = scheduler(words)
    assert result["ack"][6] == 3 and result["calls"][0] == 0
    words[2] = 3
    words[7] = 17
    words[15] = reduce(xor, words[:15])
    result = scheduler(words, empty=True)
    assert result["ack"][6] == 3
    assert result["row"] == [0] * 16
    assert result["state"] == [0xDEADBEEF] * 16
