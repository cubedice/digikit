"""Execute production MIDI ownership and dispatch code on the ColdFire core.

This is a component check, not a USB/DIN hardware or final-audio proof.
"""

import hashlib
import json
import os
import subprocess
import tomllib
from pathlib import Path

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit, pk_sharc, pk_toolchains, port_resolve

ROOT = REPO_ROOT
MOD = ROOT / "modules/core/adapters/midi-events"
FIRMWARE = ROOT / "firmware/Digitakt_II_OS1.17.syx"


@pytest.fixture(scope="module")
def reply_builder():
    obj = pk_sharc.assemble((MOD / "src/dsp/reply.s").read_text())
    sections, address, initial = {}, 0x20020000, {}
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
    epoch_at, message_at = 0x20040000, 0x20040004
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_epoch": epoch_at,
            "_mi_message": message_at,
            "_instrument_registry_reply": 0,
            "_instrument_session": 0,
        },
    )

    registry_at = 0x20041000
    registry_linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_epoch": epoch_at,
            "_mi_message": message_at,
            "_instrument_registry_reply": registry_at,
            "_instrument_session": registry_at + 32,
        },
    )

    def build(epoch, message, heartbeat, registry=None, session=0):
        dm = dict(initial)
        if registry is not None:
            dm.update({registry_at // 4 + i: w for i, w in enumerate(registry)})
        dm[(registry_at + 32) // 4] = session
        dm[epoch_at // 4] = epoch
        dm[message_at // 4] = message
        dm[symbols["_mi_heartbeat"] // 4] = heartbeat
        base = symbols["_instrument_reply"] // 4
        dm[base - 1] = dm[base + 16] = 0xDEADBEEF
        result = pk_sharc.run_call(
            registry_linked if registry is not None else linked,
            "_mi_reply_build",
            regs={"M5": 0, "M6": 1, "L0": 0},
            dm_words=dm,
            reads={"reply": (base, 16), "guard": (base - 1, 18)},
            max_steps=1000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["guard"][0] == result["guard"][-1] == 0xDEADBEEF
        return result["reply"]

    return build


@pytest.mark.parametrize(
    "epoch,message,heartbeat",
    [(0, 0, 0), (1, 1, 41), (0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF)],
)
def test_dsp_reply_contains_only_ack_and_fresh_heartbeat(
    reply_builder, epoch, message, heartbeat
):
    words = reply_builder(epoch, message, heartbeat)
    assert words[:4] == [0x494EA101, epoch, message, (heartbeat + 1) & 0xFFFFFFFF]
    assert words[4:15] == [0] * 11
    a = b = 0
    for word in words[:15]:
        a = (a + word) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    assert words[15] == a ^ b


@pytest.fixture(scope="module")
def packet_validator():
    obj = pk_sharc.assemble((MOD / "src/dsp/packet.s").read_text())
    return pk_sharc.link(obj, 0xB90000, {})


def packet_check(words):
    a = b = 0
    for i, word in enumerate(words):
        if i == 7:
            continue
        a = (a + word) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    return a ^ b


@pytest.mark.parametrize("mode", [0x1011800, 0x1013800, 0x1213800])
@pytest.mark.parametrize(
    "consumer,carrier", [(False, False), (True, False), (True, True)]
)
@pytest.mark.parametrize("valid_position", [False, True])
def test_composed_frame_preserves_context(mode, consumer, carrier, valid_position):
    position = (
        ROOT / "modules/core/adapters/pattern-position/src/dsp/pattern_position.s"
    )
    source = position.read_text()
    for name in ("packet", "receiver", "config", "consumer", "reply", "block"):
        source += "\n" + (MOD / f"src/dsp/{name}.s").read_text()
    if consumer:
        canopy = ROOT / "modules/machines/canopy/src/dsp"
        for name in ("voice", "tuning", "instrument"):
            source += "\n" + (canopy / f"{name}.s").read_text()
    if carrier:
        for name in ("carrier", "carrier-frame", "velocity", "envelope", "workspace"):
            source += "\n" + (MOD / f"src/dsp/{name}.s").read_text()
        source += "\n" + (canopy / "carrier-prepare.s").read_text()
        for name in ("registry", "lifecycle", "rows", "protected"):
            source += (
                "\n"
                + (
                    ROOT / f"modules/core/adapters/instrument-registry/src/dsp/{name}.s"
                ).read_text()
            )
    obj = pk_sharc.assemble(source)
    sections, address, dm = {}, 0x20020000, {}
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        dm.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob) + 64
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    externs = {
        **symbols,
        "_pp_block_back": 0xB9F000,
    }
    if carrier:
        externs["_mi_envelope_next"] = 0
        externs["_mi_workspace_next"] = 0
    if not carrier:
        externs["_instrument_registry_block"] = 0
        externs["_instrument_registry_reply"] = 0
        externs["_instrument_session"] = 0
        externs["_mi_carrier_begin"] = 0
        externs["_instrument_carrier_prepare"] = 0
    if not consumer:
        externs["_instrument_reset"] = 0
        externs["_instrument_events"] = 0
        externs["_instrument_configure"] = 0
    linked = pk_sharc.link(obj, 0xB90000, externs)
    if carrier:
        command = [0x49524731, 1, 1, 3, 9, 1, 7] + [0] * 9
        for value in command[:15]:
            command[15] ^= value
        dm.update(
            {symbols["_dl_irows"] // 4 + i: value for i, value in enumerate(command)}
        )
    rx = 0x200A0000
    words = [0x494E0101, 2, 1, 1, 0, 0, 42, 0, 0, 2] + [0] * 22
    words[7] = packet_check(words)
    position_words = [0x50504F53, 2, 7, 4, 2, 3, 0x47123456, 5, 0, 11, 0, 37, 50, 6, 1]
    checksum = 0x7B91D6E3
    for word in position_words:
        checksum ^= word
    position_words.append(checksum)
    if not valid_position:
        position_words[-1] ^= 1
    frame = [0] * (0xABC // 4)
    frame[0x9FC // 4 : 0xA7C // 4] = words
    frame[0xA7C // 4 :] = position_words
    dm.update({rx // 4 + i: word for i, word in enumerate(frame)})
    regs = {f"R{i}": 0x12340000 + i for i in range(16)}
    regs.update({f"S{i}": 0x76540000 + i for i in range(16)})
    regs.update(
        I0=0x20080000,
        I1=0x20081000,
        I2=0x20082000,
        I3=rx,
        M0=7,
        M1=0xFFFFFFFD,
        M5=9,
        M6=13,
        L0=64,
        L1=128,
        L2=256,
        MODE1=mode,
        ASTATX=0,
        ASTATY=0,
        STKYX=0,
        STKYY=0,
    )
    result = pk_sharc.run_call(
        linked,
        "_pp_block_entry",
        regs=regs,
        dm_words=dm,
        reads={
            "reply": (symbols["_instrument_reply"] // 4, 16),
            "position": (symbols["_pattern_position_info"] // 4, 20),
            "frame": (rx // 4, len(frame)),
            **(
                {
                    "stamp": (symbols["_mi_carrier_stamp"] // 4, 1),
                    "owners": (symbols["_mi_velocity_owners"] // 4, 32),
                }
                if carrier
                else {}
            ),
        },
        read_regs=tuple(regs),
        max_steps=8000,
        return_sw=0xB9F000,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert result["regs"] == regs
    assert result["frame"] == frame
    assert result["reply"][:4] == [0x494EA101, int(consumer), int(consumer), 1]
    assert result["position"][0] == (position_words[0] if valid_position else 0)
    if carrier:
        assert result["reply"][4:12] == [0x49525231, 1, 3, 9, 1, 7, 0, 0]
        assert result["stamp"] == [1]
        assert result["owners"] == [1, 0] * 16


def configuration_packets(values, revision=1):
    a = b = 0
    for value in values:
        a = (a + value) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    packets = []
    chunks = (len(values) + 15) // 16
    for index in range(chunks + 1):
        data = values[index * 16 : (index + 1) * 16]
        words = [0x494E0101, 3 if data else 4, 1, index + 1, 0, revision, 42, 0]
        words += [0, 1, revision, index, len(values) * 4, 1, a ^ b, len(data)]
        words += data + [0] * (16 - len(data))
        words[7] = packet_check(words)
        packets.append(words)
    return packets


@pytest.fixture(scope="module")
def lifecycle_consumer():
    source = "\n".join(
        (MOD / f"src/dsp/{name}.s").read_text()
        for name in ("packet", "receiver", "config", "consumer")
    )
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _lifecycle_callback;
_lifecycle_callback:
    R0 = DM(_lifecycle_ready);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _lifecycle_return;
    R1 = DM(_lifecycle_calls);
    R1 = R1 + 1;
    DM(_lifecycle_calls) = R1;
_lifecycle_return:
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _lifecycle_ready;
.GLOBAL _lifecycle_calls;
.VAR _lifecycle_ready = 1;
.VAR _lifecycle_calls = 0;
"""
    obj = pk_sharc.assemble(source)
    sections, address, initial = {}, 0x20020000, {}
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
    callbacks = ("_instrument_reset", "_instrument_events", "_instrument_configure")
    provisional = pk_sharc.link(
        obj, 0xB90000, {**symbols, **dict.fromkeys(callbacks, 0)}
    )

    def session(present=True):
        callback = provisional.symbols["_lifecycle_callback"] if present else 0
        linked = pk_sharc.link(
            obj, 0xB90000, {**symbols, **dict.fromkeys(callbacks, callback)}
        )
        dm = dict(initial)

        def call(words, ready=1):
            words = words.copy()
            words[7] = packet_check(words)
            dm.update({0x20010000 // 4 + i: word for i, word in enumerate(words)})
            dm[symbols["_lifecycle_ready"] // 4] = ready
            reads = {
                name: (sections[name] // 4, len(blob) // 4)
                for name, (blob, _, _) in obj.data.items()
            }
            result = pk_sharc.run_call(
                linked,
                "_mi_receive",
                regs={"R4": 0x20010000, "M5": 0, "M6": 1, "L0": 0, "L1": 0, "L2": 0},
                dm_words=dm,
                reads=reads,
                max_steps=4000,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name in reads:
                dm.update(
                    {
                        sections[name] // 4 + i: word
                        for i, word in enumerate(result[name])
                    }
                )
            bank = symbols["_mi_instances"] // 4
            return (
                result["regs"]["R0"],
                [dm[bank + i] for i in range(32)],
                dm[symbols["_lifecycle_calls"] // 4],
            )

        return call

    return session


def test_lifecycle_ownership_and_atomic_callback_failure(lifecycle_consumer):
    call = lifecycle_consumer()
    reset = [0x494E0101, 2, 1, 1, 0, 0, 42, 0, 0, 1] + [0] * 22
    assert lifecycle_consumer(False)(reset)[0:3:2] == (0, 0)
    assert call(reset, ready=0)[0:3:2] == (0, 0)
    result, bank, calls = call(reset)
    assert (result, bank[:2], calls) == (1, [1, 0], 1)
    assert call(reset)[0:3:2] == (2, 1)  # duplicate never re-applies
    reset[3] = 2
    assert call(reset)[0:3:2] == (0, 1)  # fresh message, same generation
    packets = configuration_packets([42])
    for i, packet in enumerate(packets):
        packet[3] = i + 2
    assert call(packets[0])[1][:2] == [1, 0]
    assert call(packets[1], ready=0)[1][:2] == [1, 0]
    assert call(packets[1])[1][:2] == [1, 1]
    event = [
        0x494E0101,
        257,
        1,
        4,
        1,
        1,
        42,
        0,
        10,
        1,
        1 | (60 << 14) | (100 << 21),
    ] + [0] * 21
    bad = event.copy()
    bad[9] = 2
    assert call(bad)[0:3:2] == (0, 2)
    bad = event.copy()
    bad[5] = 2
    assert call(bad)[0:3:2] == (0, 2)
    assert call(event, ready=0)[0:3:2] == (0, 2)
    assert call(event)[0:3:2] == (1, 3)
    # New epoch clears every prior track's ownership only after reset succeeds.
    reset[2:4] = [2, 1]
    reset[8:10] = [15, 1]
    assert call(reset, ready=0)[1][:2] == [1, 1]
    result, bank, calls = call(reset)
    assert result == 1 and bank == [0] * 30 + [1, 0] and calls == 4
    assert call(event)[0] == 4  # old-epoch events cannot reach the consumer


def test_lifecycle_reset_aborts_staging_and_isolates_tracks(lifecycle_consumer):
    call = lifecycle_consumer()
    reset = [0x494E0101, 2, 1, 1, 0, 0, 42, 0, 0, 1] + [0] * 22
    assert call(reset)[0] == 1
    packets = configuration_packets([42])
    packets[0][3] = 2
    assert call(packets[0])[0] == 1
    # Another track's reset preempts staged configuration, but preserves track 0.
    reset[3], reset[8] = 4, 15
    result, bank, _ = call(reset)
    assert result == 1 and bank[:2] == bank[30:] == [1, 0]
    packets[1][3] = 5
    assert call(packets[1])[0] == 0
    packets[0][3], packets[1][3] = 5, 6
    assert call(packets[0])[0] == call(packets[1])[0] == 1
    # Wrong-generation config cannot disturb the active bank or reach publisher.
    newer = configuration_packets([99], revision=2)
    newer[0][3], newer[0][9] = 7, 2
    result, bank, count = call(newer[0])
    assert result == 0 and bank[:2] == [1, 1] and count == 3
    # Invalid mixed-generation batches are rejected before any note application.
    event = [
        0x494E0101,
        513,
        1,
        7,
        1,
        1,
        42,
        0,
        10,
        1,
        1 | (60 << 14) | (100 << 21),
        11,
        2,
        1 | (64 << 14) | (80 << 21),
    ] + [0] * 18
    assert call(event)[0:3:2] == (0, 3)
    event[12] = 1
    assert call(event)[0:3:2] == (1, 4)
    reset[3], reset[8], reset[9] = 8, 0, 2
    assert call(reset)[1][:2] == [2, 0]
    event[3] = 9
    assert call(event)[0:3:2] == (0, 5)


@pytest.fixture(scope="module")
def config_receiver():
    source = (MOD / "src/dsp/packet.s").read_text()
    source += "\n" + (MOD / "src/dsp/config.s").read_text()
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_config;
.GLOBAL _test_publish;
_test_config:
    CALL _mi_packet_valid;
    R0 = PASS R0;
    IF EQ JUMP _test_done;
    CALL _mi_config_apply;
_test_done:
    RTS;
_test_publish:
    R0 = DM(_test_allow);
    R0 = PASS R0;
    IF EQ JUMP _test_done;
    I0 = R4;
    I1 = R8;
    R0 = DM(5, I1);
    R1 = 1;
    COMP(R0, R1);
    IF NE JUMP _test_no_schema;
    R0 = DM(2, I1);
    DM(_test_revision) = R0;
    R5 = DM(4, I1);
    R5 = LSHIFT R5 BY -2;
    I1 = _test_bank;
_test_copy:
    R0 = DM(I0, M6);
    DM(I1, M6) = R0;
    R5 = R5 - 1;
    IF NE JUMP _test_copy;
    R0 = 1;
    RTS;
_test_no_schema:
    R0 = 0;
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _test_allow;
.GLOBAL _test_revision;
.EXTERN _test_bank;
.VAR _test_allow = 1;
.VAR _test_revision = 0;
.PKBSS _test_bank, 256;
"""
    obj = pk_sharc.assemble(source)
    sections, address, initial = {}, 0x20020000, {}
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
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
    provisional = pk_sharc.link(obj, 0xB90000, {**symbols, "_mi_config_publish": 0})
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {**symbols, "_mi_config_publish": provisional.symbols["_test_publish"]},
    )

    def factory():
        dm = dict(initial)

        def call(words, publish=1, abort=False):
            packet = 0x20010000
            dm.update({packet // 4 + i: value for i, value in enumerate(words)})
            dm[symbols["_test_allow"] // 4] = publish
            result = pk_sharc.run_call(
                linked,
                "_mi_config_abort" if abort else "_test_config",
                regs={"R4": packet, "M5": 0, "M6": 1, "L0": 0, "L1": 0, "L2": 0},
                dm_words=dm,
                reads={
                    **{
                        name: (base // 4, len(obj.data[name][0]) // 4)
                        for name, base in sections.items()
                    },
                    "bank": (symbols["_test_bank"] // 4, 64),
                    "revision": (symbols["_test_revision"] // 4, 1),
                },
                max_steps=5000,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name, base in sections.items():
                dm.update(
                    {base // 4 + i: value for i, value in enumerate(result[name])}
                )
            return result

        return call

    return factory


@pytest.mark.parametrize("length", [1, 16, 17, 64])
def test_configuration_stages_then_publishes_atomically(config_receiver, length):
    call = config_receiver()
    values = [0xFFFFFFFF - i for i in range(length)]
    packets = configuration_packets(values)
    for packet in packets[:-1]:
        result = call(packet)
        assert result["regs"]["R0"] == 1
        assert result["bank"] == [0] * 64 and result["revision"] == [0]
    result = call(packets[-1], publish=0)
    assert result["regs"]["R0"] == 0 and result["revision"] == [0]
    assert result["bank"] == [0] * 64
    result = call(packets[-1])
    assert result["regs"]["R0"] == 1 and result["revision"] == [1]
    assert result["bank"][:length] == values


def test_configuration_rejects_gaps_mismatches_corruption_and_aborted_commit(
    config_receiver,
):
    call = config_receiver()
    packets = configuration_packets(list(range(17)))
    assert call(packets[1])["regs"]["R0"] == 0
    assert call(packets[0])["regs"]["R0"] == 1
    changed = packets[1].copy()
    changed[9] += 1
    changed[7] = packet_check(changed)
    assert call(changed)["regs"]["R0"] == 0
    call([], abort=True)
    assert call(packets[1])["regs"]["R0"] == 0
    assert call(packets[-1])["regs"]["R0"] == 0
    for packet in packets:
        changed = packet.copy()
        changed[14] ^= 1  # consistent metadata, incorrect whole-image checksum
        changed[7] = packet_check(changed)
        result = call(changed)
    assert result["regs"]["R0"] == 0 and result["bank"] == [0] * 64
    for packet in packets:
        result = call(packet)
    assert result["regs"]["R0"] == 1 and result["bank"][:17] == list(range(17))


@pytest.mark.parametrize(
    "index,value",
    [
        (1, 3 | 256),
        (4, 1),
        (5, 2),
        (8, 16),
        (9, 0),
        (10, 0),
        (11, 4),
        (12, 0),
        (12, 3),
        (12, 260),
        (12, 0xFFFFFFFC),
        (13, 0),
        (15, 0),
        (15, 17),
        (31, 1),
    ],
)
def test_configuration_packet_bounds(config_receiver, index, value):
    words = configuration_packets([42])[0]
    words[index] = value
    words[7] = packet_check(words)
    result = config_receiver()(words)
    assert result["regs"]["R0"] == 0
    assert result["bank"] == [0] * 64 and result["revision"] == [0]


@pytest.mark.parametrize("consumer", [False, True])
def test_ordered_receiver_only_acknowledges_applied_packets(consumer):
    source = (MOD / "src/dsp/packet.s").read_text()
    source += "\n" + (MOD / "src/dsp/receiver.s").read_text()
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_apply;
_test_apply:
    R0 = DM(_test_ready);
    R0 = PASS R0;
    IF EQ JUMP _test_return;
    R0 = DM(_test_calls);
    R0 = R0 + 1;
    DM(_test_calls) = R0;
    R0 = 1;
_test_return:
    RTS;
.SECTION/DM seg_dmda;
.GLOBAL _test_calls;
.GLOBAL _test_ready;
.VAR _test_calls = 0;
.VAR _test_ready = 1;
"""
    obj = pk_sharc.assemble(source)
    sections, address, initial = {}, 0x20020000, {}
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        for i in range(len(blob) // 4):
            initial[address // 4 + i] = int.from_bytes(
                blob[i * 4 : i * 4 + 4], "little"
            )
        address += len(blob)
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    provisional = pk_sharc.link(obj, 0xB90000, {**symbols, "_mi_apply_packet": 0})
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            **symbols,
            "_mi_apply_packet": provisional.symbols["_test_apply"] if consumer else 0,
        },
    )
    packet = 0x20010000
    dm = dict(initial)
    accepted = epoch = sequence = 0

    def receive(kind, ep, seq, expected, ready=1, corrupt=False):
        nonlocal accepted, epoch, sequence
        words = [0x494E0101, kind, ep, seq, 0, 0, 42, 0] + [0] * 24
        if kind == 2:
            words[8:10] = [0, 2]
        else:
            words[1] |= 256
            words[4:6] = [1, 1]
            words[8:11] = [1, 2, 1 | (60 << 14) | (100 << 21)]
        words[7] = packet_check(words)
        if corrupt:
            words[7] ^= 1
        dm.update({packet // 4 + i: value for i, value in enumerate(words)})
        dm[symbols["_test_ready"] // 4] = ready
        result = pk_sharc.run_call(
            linked,
            "_mi_receive",
            regs={"R4": packet, "M5": 0, "M6": 1, "L0": 0},
            dm_words=dm,
            reads={
                "state": (sections["seg_dmda"] // 4, len(obj.data["seg_dmda"][0]) // 4)
            },
            max_steps=2500,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        assert result["regs"]["R0"] == expected, result
        dm.update(
            {
                sections["seg_dmda"] // 4 + i: value
                for i, value in enumerate(result["state"])
            }
        )
        if expected == 1:
            accepted += 1
            epoch, sequence = ep, seq
        assert dm[symbols["_test_calls"] // 4] == accepted
        assert dm[symbols["_mi_epoch"] // 4] == epoch
        assert dm[symbols["_mi_message"] // 4] == sequence

    if not consumer:
        receive(2, 1, 1, 0)
        receive(1, 1, 2, 3)
        return
    receive(1, 1, 1, 3)  # no notes before reset
    receive(2, 1, 1, 1)
    receive(2, 1, 1, 2)  # retry does not invoke consumer
    receive(1, 1, 2, 0, ready=0)  # unapplied configuration: no ACK
    receive(1, 1, 2, 1)
    receive(1, 1, 2, 2)
    receive(1, 1, 4, 3)
    receive(2, 1, 5, 1)  # reset supersedes missing ordinary messages
    receive(1, 1, 3, 4)
    receive(1, 1, 6, 0, corrupt=True)
    receive(1, 1, 6, 1)
    receive(1, 2, 1, 3)
    receive(2, 2, 1, 1)
    receive(2, 1, 9, 4)


@pytest.mark.parametrize(
    "case",
    [
        "note",
        "reset",
        "eight",
        "bend",
        "signed_boundary",
        "last_sequence",
        "bad_check",
        "magic",
        "epoch",
        "message",
        "empty",
        "count",
        "revision",
        "sequence_wrap",
        "token",
        "generation",
        "mixed_generation",
        "port",
        "reserved",
        "mixed_track",
        "padding",
        "reset_track",
        "reset_padding",
    ],
)
def test_packet_validation_on_sharc(packet_validator, case):
    words = [0x494E0101, 1 | (1 << 8), 1, 1, 1, 3, 42, 0] + [0] * 24
    words[8:11] = [1, 1, 1 | (60 << 14) | (100 << 21)]
    valid = case in {
        "note",
        "reset",
        "eight",
        "bend",
        "signed_boundary",
        "last_sequence",
    }
    if case in {"reset", "reset_track", "reset_padding"}:
        words[1] = 2
        words[4] = words[5] = 0
        words[8:] = [15, 2] + [0] * 22
        if case == "reset_track":
            words[8] = 16
        if case == "reset_padding":
            words[31] = 1
    if case in {
        "eight",
        "signed_boundary",
        "sequence_wrap",
        "mixed_generation",
        "mixed_track",
    }:
        words[1] = 1 | (8 << 8)
        for i in range(8):
            words[8 + i * 3 : 11 + i * 3] = [
                i + 1,
                1,
                1 | ((60 + i) << 14) | (100 << 21),
            ]
    changes = {
        "magic": (0, 0),
        "epoch": (2, 0),
        "message": (3, 0),
        "empty": (1, 1),
        "count": (1, 1 | (9 << 8)),
        "revision": (5, 0),
        "sequence_wrap": (4, 0xFFFFFFFE),
        "signed_boundary": (4, 0x7FFFFFFE),
        "last_sequence": (4, 0xFFFFFFFF),
        "token": (8, 0),
        "generation": (9, 0),
        "mixed_generation": (12, 2),
        "port": (10, words[10] | (2 << 4)),
        "reserved": (10, words[10] | (1 << 28)),
        "mixed_track": (13, words[13] | (1 << 10)),
        "padding": (31, 1),
        "reset_padding": (31, 1),
    }
    if case in changes:
        at, value = changes[case]
        words[at] = value
    if case == "bend":
        words[8] = 0
        words[10] = 4 | (127 << 14) | (127 << 21)
    words[7] = packet_check(words)
    if case == "bad_check":
        words[20] ^= 1
    address = 0x20010000
    result = pk_sharc.run_call(
        packet_validator,
        "_mi_packet_valid",
        regs={"R4": address, "M5": 0, "M6": 1, "L0": 0},
        dm_words={address // 4 + i: value for i, value in enumerate(words)},
        reads={"packet": (address // 4, 32)},
        max_steps=2500,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    assert result["regs"]["R0"] == int(valid), (case, result)
    assert result["packet"] == words


@pytest.fixture(scope="module")
def link_receiver_object():
    link = ROOT / "modules/core/adapters/dsp-link"
    source = pk_sharc.expand_includes(
        (link / "src/dsp/dt2.s").read_text(),
        [link / "src/dsp", *pk_sharc.sharc_include_dirs(link)],
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
    return obj, syms, initial


@pytest.mark.parametrize(
    "position,instrument,capacity",
    [(False, False, 171), (True, False, 155), (True, True, 123)],
)
@pytest.mark.parametrize("oversize", [False, True])
def test_dsp_loader_enforces_each_optional_tail_boundary(
    link_receiver_object, position, instrument, capacity, oversize
):
    obj, syms, initial = link_receiver_object
    rx, reply, frame, mailbox, back = (
        0x20010000,
        0x20020000,
        0x20030000,
        0x20040000,
        0xB9F000,
    )
    externs = {
        "_dl_back": back,
        "_dl_vrows": 0,
        "_dl_vcount": 0,
        "_dl_frows": 0,
        "_dl_fcount": 0,
        "_dl_irows": 0,
        "_dl_iwords": 0,
        "_dv_diag": 0,
        "_dv_rec": 0,
        "_loop_ui_status": 0,
        "_pattern_position_info": mailbox if position else 0,
        "_instrument_reply": mailbox if instrument else 0,
        **syms,
    }
    linked = pk_sharc.link(obj, 0xB90000, externs)
    n = capacity + int(oversize)
    words = [0xD15C, (1 << 27) | (n << 17), *range(1, n + 1)]
    s1 = s2 = 0
    for value in words:
        s1 = (s1 + value) & 0xFFFFFFFF
        s2 = (s2 + s1) & 0xFFFFFFFF
    words.append(s1 ^ s2)
    dm = dict(initial)
    dm.update({mailbox // 4 + i: 0 for i in range(16)})
    dm.update({(rx + 0x804) // 4 + i: value for i, value in enumerate(words)})
    dm[frame // 4 - 3] = rx
    dm[frame // 4 - 2] = reply
    area = syms["_dl_area"] // 4
    dm.update({area + i: 0xDEADBEEF for i in range(n + 1)})
    result = pk_sharc.run_call(
        linked,
        "_dl_hook",
        regs={"I4": reply, "I6": frame},
        dm_words=dm,
        reads={"area": (area, n + 1), "status": (syms["_dl_status"] // 4, 1)},
        return_sw=back,
        max_steps=6000,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    if oversize:
        assert result["status"] == [3]
        assert result["area"] == [0xDEADBEEF] * (n + 1)
    else:
        assert result["status"] == [0]
        assert result["area"] == [*range(1, n + 1), 0xDEADBEEF]


@pytest.mark.parametrize("present", [False, True])
def test_instrument_reply_preserves_overloop_and_every_other_record_word(present):
    source = (
        ROOT / "modules/core/adapters/dsp-link/src/dsp/instrument-status.inc"
    ).read_text()
    obj = pk_sharc.assemble(
        ".EXTERN _instrument_reply;\n.EXTERN _dl_instrument_status_return;\n"
        ".GLOBAL _dl_instrument_status;\n" + source
    )
    mailbox, reply, back = 0x20090000, 0x200A0000, 0xB9F000
    linked = pk_sharc.link(
        obj,
        0xB90000,
        {
            "_instrument_reply": mailbox if present else 0,
            "_dl_instrument_status_return": back,
        },
    )
    words = [0x494E5331, *range(1, 16)]
    original = [0xA5000000 + i for i in range(0xABC // 4)]
    dm = {reply // 4 + i: value for i, value in enumerate(original)}
    dm.update({mailbox // 4 + i: value for i, value in enumerate(words)})
    result = pk_sharc.run_call(
        linked,
        "_dl_instrument_status",
        regs={"I4": reply},
        dm_words=dm,
        reads={"reply": (reply // 4, len(original))},
        return_sw=back,
        max_steps=100,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    expected = original.copy()
    if present:
        for j, value in enumerate(words):
            offset = 0x1C + 0x54 * (21 + j // 2) + 0x44 + (j % 2) * 4
            expected[offset // 4] = value
    assert result["reply"] == expected


def test_din_hook_resolves_uniquely_on_pinned_image():
    if not FIRMWARE.exists():
        pytest.skip("DTII 1.17 firmware required")
    declarations = tomllib.loads((ROOT / "devices/fw/midi.toml").read_text())
    site = next(s for s in declarations["site"] if s["name"].endswith(".din_rx"))
    port = tomllib.loads((ROOT / "devices/ports/dt2-1.17.toml").read_text())
    anchor = next(a for a in port["anchor"] if a["name"] == site["name"])
    main = port_resolve.sections_of(FIRMWARE)[3]
    rule = site["rule"]
    size = rule["search_len"]
    matches = [
        at
        for at in range(0, len(main) - size + 1, 2)
        if hashlib.sha256(main[at : at + size]).hexdigest() == rule["sha256"]
    ]
    assert len(matches) == 1
    assert matches[0] + 0x40000400 + rule["offset"] == anchor["addr"]
    start = anchor["addr"] - 0x40000400
    assert (
        hashlib.sha256(main[start : start + anchor["len"]]).hexdigest()
        == anchor["sha256"]
    )


def test_stock_usb_parser_frames_channel_messages(tmp_path):
    runner = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not FIRMWARE.exists() or not runner.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("DTII firmware, m68k compiler and cf_script required")
    # Addresses below are measured on this exact image, not a cross-version API.
    assert hashlib.sha256(FIRMWARE.read_bytes()).hexdigest() == (
        "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    )
    linked = patchkit.cf_link(
        [ROOT / "tests/support/midi_parser_probe.c"],
        0x47E00000,
        {"mi_stock_usb_receive": 0x4011DA4C},
    )
    operations = [
        {
            "op": "poke",
            "addr": linked.base,
            "hex": (linked.image + bytes(linked.bss_size)).hex(),
        }
    ]
    for address, symbol in (
        (0x4011D956, "mi_parser_alloc"),
        (0x40001896, "mi_parser_push"),
        (0x4000155C, "mi_parser_wake"),
    ):
        operations.append(
            {
                "op": "poke",
                "addr": address,
                "hex": "4ef9" + linked.symbols[symbol].to_bytes(4, "big").hex(),
            }
        )
    operations.extend(
        [
            {"op": "poke32", "addr": 0x44D4E930, "value": 0x12345678},
            {"op": "poke32", "addr": 0x44D4E934, "value": 0x23456789},
        ]
    )
    for case in range(5):
        operations.extend(
            [
                {"op": "poke", "addr": 0x44D2E8C0, "hex": bytes(20).hex()},
                {
                    "op": "call",
                    "tag": str(case),
                    "pc": linked.symbols["mi_parser_case"],
                    "args": [case],
                    "max": 100_000,
                },
            ]
        )
    script = tmp_path / "parser.json"
    script.write_text(json.dumps(operations))
    result = subprocess.run(
        [str(runner), str(FIRMWARE), str(script)],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [row for row in rows if row.get("op") == "call"]
    assert [row["tag"] for row in calls] == [str(case) for case in range(5)]
    for row in calls:
        assert row["ok"] and not row["error"] and row["d0"] == 0, row


def test_usb_din_trampolines_consume_owned_notes(tmp_path):
    runner = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not FIRMWARE.exists() or not runner.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("DTII firmware, m68k compiler and cf_script required")
    runtime = ROOT / "modules/core/runtime"
    linked = patchkit.cf_link(
        [
            *(
                MOD / f"src/cf/{name}"
                for name in ("ownership.c", "transport.c", "adapter.c", "din.s")
            ),
            runtime / "src/cf/tramp.s",
            runtime / "src/cf/midi_dispatch.c",
            ROOT / "tests/support/midi_ingress_probe.c",
            ROOT / "tests/support/midi_ingress_probe.s",
        ],
        0x47E00000,
        dict.fromkeys(
            (
                "fx_controls_tick",
                "loop_ui_tick",
                "dkrt_tick",
                "fw_queue_pop",
                "dkrt_boot",
                "dkrt_boot_next",
                "dkrt_exc",
            ),
            0,
        ),
        [MOD / "src/cf", runtime / "build/gen", ROOT / "modules/core/fw/inc"],
    )
    script = tmp_path / "ingress.json"
    script.write_text(
        json.dumps(
            [
                {
                    "op": "poke",
                    "addr": linked.base,
                    "hex": (linked.image + bytes(linked.bss_size)).hex(),
                },
                *(
                    {
                        "op": "call",
                        "tag": str(source),
                        "pc": linked.symbols["mi_probe_ingress"],
                        "args": [source],
                        "max": 2_000_000,
                    }
                    for source in (2, 4, 8, 16)
                ),
            ]
        )
    )
    result = subprocess.run(
        [str(runner), str(FIRMWARE), str(script)],
        capture_output=True,
        text=True,
        timeout=60,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    calls = [
        json.loads(line)
        for line in result.stdout.splitlines()
        if line.startswith("{") and json.loads(line).get("op") == "call"
    ]
    assert [row["tag"] for row in calls] == ["2", "4", "8", "16"]
    for row in calls:
        assert row["ok"] and not row["error"] and row["d0"] == 0, row


def test_ownership_and_dispatch_on_coldfire(
    tmp_path, packet_validator, config_receiver, reply_builder
):
    runner = Path(
        os.environ.get("DIGIKIT_CF_SCRIPT", ROOT / "target/release/examples/cf_script")
    )
    if not FIRMWARE.exists() or not runner.exists() or not pk_toolchains.m68k_bin():
        pytest.skip("DTII firmware, m68k compiler and cf_script required")
    memory = tmp_path / "memory.c"
    memory.write_text("""
void *memcpy(void *d, const void *s, unsigned n) {
    volatile unsigned char *p=d; const volatile unsigned char *q=s;
    while(n--) { *p++=*q++; }
    return d;
}
void *memset(void *d, int c, unsigned n) {
    volatile unsigned char *p=d;
    while(n--) { *p++=(unsigned char)c; }
    return d;
}
""")
    linked = patchkit.cf_link(
        [
            MOD / "src/cf/ownership.c",
            MOD / "src/cf/transport.c",
            MOD / "src/cf/adapter.c",
            MOD / "host/tests/ownership.c",
            MOD / "host/tests/transport.c",
            MOD / "host/tests/dispatch.c",
            ROOT / "modules/core/runtime/src/cf/midi_dispatch.c",
            memory,
        ],
        0x47E00000,
        {"dl_reset_begin": 0, "dl_reset_quiesce": 0},
        [MOD / "src/cf"],
    )
    names = [
        "fifo",
        "reroute",
        "overflow",
        "messages",
        "panic",
        "reset_presses",
        "token_wrap",
        "ring_wrap",
        "dispatch",
        "transport_batch",
        "transport_reset",
        "transport_partition",
        "transport_timeout",
        "transport_wrap",
        "transport_wire",
        "config_transaction",
        "config_maximum",
        "reply_ack",
        "reply_watchdog",
        "session_guard",
        "session_midi",
        "reply_record",
    ]
    script = tmp_path / "calls.json"
    script.write_text(
        json.dumps(
            [
                {
                    "op": "poke",
                    "addr": linked.base,
                    "hex": (linked.image + bytes(linked.bss_size)).hex(),
                },
                *[
                    {
                        "op": "call",
                        "tag": name,
                        "pc": linked.symbols["mi_test_" + name],
                        "args": [],
                        "max": 5_000_000,
                    }
                    for name in names
                ],
                {
                    "op": "peek",
                    "addr": linked.symbols["mi_test_wire_words"],
                    "len": 256,
                    "tag": "wire",
                },
                {
                    "op": "peek",
                    "addr": linked.symbols["mi_test_config_wire"],
                    "len": 384,
                    "tag": "config_wire",
                },
                {
                    "op": "poke",
                    "addr": 0x47D00000,
                    "hex": b"".join(
                        w.to_bytes(4, "big") for w in reply_builder(1, 1, 41)
                    ).hex(),
                },
                {
                    "op": "call",
                    "tag": "reply_cross",
                    "pc": linked.symbols["mi_test_reply_from_dsp"],
                    "args": [0x47D00000],
                    "max": 5_000_000,
                },
                {
                    "op": "poke",
                    "addr": 0x47D00000,
                    "hex": b"".join(
                        w.to_bytes(4, "big")
                        for w in reply_builder(
                            1, 1, 41, [0x49525231, 17, 2, 4, 4, 0, 0, 7]
                        )
                    ).hex(),
                },
                {
                    "op": "call",
                    "tag": "registry_cross",
                    "pc": linked.symbols["mi_test_reply_from_dsp"],
                    "args": [0x47D00000],
                    "max": 5_000_000,
                },
                {
                    "op": "poke",
                    "addr": 0x47D00000,
                    "hex": b"".join(
                        w.to_bytes(4, "big")
                        for w in reply_builder(
                            1,
                            0,
                            41,
                            [0x49525231, 17, 0, 0xDEADBEEF, 5, 0, 0, 0],
                            session=0xDEADBEEF,
                        )
                    ).hex(),
                },
                {
                    "op": "call",
                    "tag": "session_cross",
                    "pc": linked.symbols["mi_test_session_from_dsp"],
                    "args": [0x47D00000],
                    "max": 5_000_000,
                },
            ]
        )
    )
    result = subprocess.run(
        [str(runner), str(FIRMWARE), str(script)],
        capture_output=True,
        text=True,
        timeout=90,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    rows = [
        json.loads(line) for line in result.stdout.splitlines() if line.startswith("{")
    ]
    calls = [row for row in rows if row.get("op") == "call"]
    assert [row["tag"] for row in calls] == names + [
        "reply_cross",
        "registry_cross",
        "session_cross",
    ]
    for row in calls:
        assert row["ok"] and not row["error"] and row["d0"] == 0, row
    wire = bytes.fromhex(next(row["hex"] for row in rows if row.get("tag") == "wire"))
    for offset in (0, 128):
        # Actual ColdFire-produced words, converted from its big-endian RAM to
        # the SHARC ring's word values. Physical SPI halfword ordering is tested
        # by DSP-link; this checks the protocol across both instruction sets.
        words = [
            int.from_bytes(wire[i : i + 4], "big")
            for i in range(offset, offset + 128, 4)
        ]
        assert words[7] == packet_check(words)
        address = 0x20010000
        result = pk_sharc.run_call(
            packet_validator,
            "_mi_packet_valid",
            regs={"R4": address, "M5": 0, "M6": 1, "L0": 0},
            dm_words={address // 4 + i: value for i, value in enumerate(words)},
            max_steps=2500,
        )
        assert result["halt"]["reason"] == "breakpoint" and result["regs"]["R0"] == 1, (
            result
        )
    wire = bytes.fromhex(
        next(row["hex"] for row in rows if row.get("tag") == "config_wire")
    )
    receive = config_receiver()
    for offset in (0, 128, 256):
        words = [
            int.from_bytes(wire[i : i + 4], "big")
            for i in range(offset, offset + 128, 4)
        ]
        result = receive(words)
        assert result["regs"]["R0"] == 1
        assert result["revision"] == [1 if offset == 256 else 0]
    assert result["bank"][:17] == [i * 19 for i in range(17)]


@pytest.mark.parametrize(
    "base,capacity,offset,accepted",
    [
        (0x20040000, 8, 6, True),  # last two words fit exactly
        (0x20040000, 8, 7, False),
        (0, 8, 0, False),
        (0x20040000, 0, 0, False),
        (0x20040001, 8, 0, False),
        (0x20040000, 0xFFFFFFFF, 0, False),
        (0x20040000, 0x20001, 0, False),
    ],
)
def test_v2_registration_window_is_bounded(
    link_receiver_object, base, capacity, offset, accepted
):
    obj, syms, initial = link_receiver_object
    rx, reply, frame, target, back = (
        0x20010000,
        0x20020000,
        0x20030000,
        0x20040000,
        0xB9F000,
    )
    externs = {
        "_dl_back": back,
        "_dl_vrows": 0,
        "_dl_vcount": 0,
        "_dl_frows": 0,
        "_dl_fcount": 0,
        "_dl_irows": base,
        "_dl_iwords": target + 64,
        "_dv_diag": 0,
        "_dv_rec": 0,
        "_loop_ui_status": 0,
        "_pattern_position_info": 0,
        "_instrument_reply": 0,
        **syms,
    }
    linked = pk_sharc.link(obj, 0xB90000, externs)
    words = [0xD15C | (3 << 30), (1 << 27) | (2 << 17) | offset, 0x11223344, 0x55667788]
    s1 = s2 = 0
    for value in words:
        s1 = (s1 + value) & 0xFFFFFFFF
        s2 = (s2 + s1) & 0xFFFFFFFF
    words.append(s1 ^ s2)
    dm = dict(initial)
    dm[(target + 64) // 4] = capacity
    dm.update({(rx + 0x804) // 4 + i: v for i, v in enumerate(words)})
    dm[frame // 4 - 3], dm[frame // 4 - 2] = rx, reply
    dm.update({target // 4 - 1 + i: 0xDEADBEEF for i in range(10)})
    result = pk_sharc.run_call(
        linked,
        "_dl_hook",
        regs={"I4": reply, "I6": frame},
        dm_words=dm,
        reads={"target": (target // 4 - 1, 10), "status": (syms["_dl_status"] // 4, 1)},
        return_sw=back,
        max_steps=1000,
    )
    assert result["halt"]["reason"] == "breakpoint", result
    expected = [0xDEADBEEF] * 10
    if accepted:
        expected[offset + 1 : offset + 3] = words[2:4]
    assert result["target"] == expected
    assert result["status"] == [0 if accepted else 3]
