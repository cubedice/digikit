"""The composed DSP block frame of core/midi-events, run on the SHARC core.

`composed_frame` links the pattern-position entry, the midi-events receiver and,
optionally, an instrument's DSP consumer and carrier sources, runs one block and
checks that the frame, registers and replies are preserved. The instrument
sources come from the caller: adapter tests run it without an instrument, and an
instrument's own tests run it with its sources.
"""

from pathlib import Path

from support.paths import REPO_ROOT
from tools.patchkit import pk_sharc

ROOT = REPO_ROOT
MOD = ROOT / "modules/core/adapters/midi-events"
MODES = (0x1011800, 0x1013800, 0x1213800)


def packet_check(words):
    a = b = 0
    for i, word in enumerate(words):
        if i == 7:
            continue
        a = (a + word) & 0xFFFFFFFF
        b = (b + a) & 0xFFFFFFFF
    return a ^ b


def composed_frame(
    mode: int,
    valid_position: bool,
    consumer: tuple[Path, ...] = (),
    carrier: tuple[Path, ...] = (),
) -> None:
    """CONSUMER: an instrument's DSP sources (none: no consumer). CARRIER: its carrier
    sources; when given, the adapter's carrier and the instrument registry are linked too."""
    position = (
        ROOT / "modules/core/adapters/pattern-position/src/dsp/pattern_position.s"
    )
    source = position.read_text()
    for name in ("packet", "receiver", "config", "consumer", "reply", "block"):
        source += "\n" + (MOD / f"src/dsp/{name}.s").read_text()
    for path in consumer:
        source += "\n" + path.read_text()
    if carrier:
        for name in ("carrier", "carrier-frame", "velocity", "envelope", "workspace"):
            source += "\n" + (MOD / f"src/dsp/{name}.s").read_text()
        for path in carrier:
            source += "\n" + path.read_text()
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
        "_instrument_play_status": 0,
        "_instrument_ui_status": 0,
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
    assert result["reply"][:4] == [
        0x494EA101,
        int(bool(consumer)),
        int(bool(consumer)),
        1,
    ]
    assert result["position"][0] == (position_words[0] if valid_position else 0)
    if carrier:
        assert result["reply"][4:12] == [0x49525231, 1, 3, 9, 1, 7, 0, 0]
        assert result["stamp"] == [1]
        assert result["owners"] == [1, 0] * 16
