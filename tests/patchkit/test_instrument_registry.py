"""Execute the query-only V2 mailbox and its shared reply on the SHARC core."""

from functools import reduce
from operator import xor

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module")
def registry_object():
    adapters = REPO_ROOT / "modules/core/adapters"
    source = (adapters / "instrument-registry/src/dsp/registry.s").read_text()
    source += "\n" + (adapters / "midi-events/src/dsp/reply.s").read_text()
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20050000
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
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_epoch": 0x20040000,
            "_mi_message": 0x20040004,
        },
    )
    return linked, symbols, initial


@pytest.fixture
def mailbox(registry_object):
    linked, symbols, initial = registry_object
    dm = {**initial, 0x20040000 // 4: 0, 0x20040004 // 4: 0}
    reads = {name: (address // 4, 1) for name, address in symbols.items()}
    reads["ack"] = (symbols["_instrument_registry_reply"] // 4, 8)
    reads["wire"] = (symbols["_instrument_reply"] // 4, 16)

    def execute(words):
        dm.update({symbols["_dl_irows"] // 4 + i: v for i, v in enumerate(words)})
        for entry in ("_instrument_registry_block", "_mi_reply_build"):
            result = pk_sharc.run_call(
                linked,
                entry,
                regs={"M5": 0, "M6": 1, "L0": 0, "L1": 0},
                dm_words=dm,
                reads=reads,
                max_steps=1500,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name, (address, _) in reads.items():
                dm.update({address + i: v for i, v in enumerate(result[name])})
        wire = result["wire"]
        assert wire[4:12] == result["ack"]
        assert wire[12:15] == [0] * 3
        a = b = 0
        for value in wire[:15]:
            a = (a + value) & 0xFFFFFFFF
            b = (b + a) & 0xFFFFFFFF
        assert wire[15] == a ^ b
        return result["ack"]

    return execute


def command(sequence=1, operation=1, **fields):
    words = [0x49524731, 1, sequence, 3, 9, operation, 7] + [0] * 9
    for index, value in fields.items():
        words[int(index)] = value
    words[15] = reduce(xor, words[:15])
    return words


@pytest.mark.parametrize("operation,status", [(1, 0), (2, 1), (3, 1), (99, 2)])
def test_query_and_unsupported_lifecycle(mailbox, operation, status):
    assert mailbox(command(operation=operation)) == [
        0x49525231,
        1,
        3,
        9,
        operation,
        7,
        status,
        0,
    ]


@pytest.mark.parametrize(
    "index,value",
    [
        (0, 0),
        (1, 2),
        (2, 0),
        (3, 32),
        (3, 0xFFFFFFFF),
        (4, 0),
        (6, 8),
        (7, 1),
        (14, 1),
    ],
)
def test_invalid_command_retains_ack(mailbox, index, value):
    ack = mailbox(command())
    assert mailbox(command(sequence=2, **{str(index): value})) == ack
    assert mailbox(command(sequence=2))[1] == 2


def test_checksum_failure_does_not_consume_sequence(mailbox):
    words = command()
    words[15] ^= 1
    assert mailbox(words) == [0] * 8
    assert mailbox(command())[1] == 1


def test_replay_old_sequence_and_wrap_are_rejected(mailbox):
    ack = mailbox(command(sequence=2))
    assert mailbox(command(sequence=2, operation=2)) == ack
    assert mailbox(command(sequence=1)) == ack
    ack = mailbox(command(sequence=0xFFFFFFFF))
    assert ack[1] == 0xFFFFFFFF
    assert mailbox(command(sequence=1)) == ack
