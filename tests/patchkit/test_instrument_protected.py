"""Exercise the row-tick context boundary with adversarial permitted clobbers."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc

MRS = ["MR0F", "MR1F", "MR2F", "MR0B", "MR1B", "MR2B"]
MR_WORDS = [0x76543210, 0xFEDCBA98, 0xFFFF8001, 0x12345678, 0x98765432, 0x1234]
PINNED = (
    ["M5", "M6", "M7", "M13", "M14", "M15"] + [f"L{i}" for i in range(6)] + ["MODE1"]
)


@pytest.fixture(scope="module")
def protected_object():
    source = (
        REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp/protected.s"
    ).read_text()
    source += "\n.SECTION/PM seg_pmco;\n.GLOBAL _test_entry;\n_test_entry:\n"
    for name, value in zip(MRS, MR_WORDS, strict=True):
        source += f"R0 = {value:#x};\n{name} = R0;\n"
    source += "CALL _ir_row_tick_protected;\nDM(0x20040000) = R0;\n"
    for i, name in enumerate(MRS):
        source += f"R0 = {name};\nDM({0x20040004 + i * 4:#x}) = R0;\n"
    source += "R0 = DM(0x20040000);\n.GLOBAL _test_done;\n_test_done:\nNOP;\nRTS;\n.GLOBAL _ir_row_tick;\n_ir_row_tick:\n"
    source += "DM(0x20040100) = R8;\n"
    for i, name in enumerate(PINNED):
        source += f"R0 = {name};\nDM({0x20040200 + i * 4:#x}) = R0;\n"
    # Clobber permitted registers and both accumulators. Keep pinned constants,
    # lengths and MODE1 as a conforming V2 callback must.
    for name in (
        [f"R{i}" for i in range(16)]
        + [f"I{i}" for i in range(6)]
        + ["I12", "I13", "B3"]
        + [f"M{i}" for i in range(1, 5)]
    ):
        source += f"{name} = 17;\n"
    for name in MRS:
        source += f"{name} = R0;\n"
    source += "CALL _test_nested;\nR0 = DM(0x20040100);\nRTS;\n_test_nested:\nR1 = R1 + R2;\nRTS;\n"
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
    return pk_sharc.link(obj, 0xB90000, symbols), initial


@pytest.mark.parametrize("mode", [0x1011800, 0x101B800])
@pytest.mark.parametrize("result", [0, 0xFFFFFFF9])
def test_context_and_accumulators_survive_success_and_fault(
    protected_object, mode, result
):
    linked, initial = protected_object
    regs = {f"R{i}": 0x12340000 + i for i in range(1, 16)}
    regs.update({f"S{i}": 0x76540000 + i for i in range(16)})
    regs.update({f"I{i}": 0x20080000 + i * 256 for i in range(16)})
    regs.update({f"M{i}": i + 7 for i in range(16)})
    regs.update({f"L{i}": 64 + i * 4 for i in range(8)})
    regs.update(B3=0x20080300, MODE1=mode, ASTATX=0, ASTATY=0, STKYX=0, STKYY=0)
    regs["R8"] = result
    output = pk_sharc.run_call(
        linked,
        "_test_entry",
        regs=regs,
        dm_words=initial,
        reads={"mrs": (0x20040004 // 4, 6), "pinned": (0x20040200 // 4, len(PINNED))},
        read_regs=tuple(regs),
        max_steps=1200,
        extra_breaks=(linked.symbols["_test_done"],),
    )
    assert output["halt"]["reason"] == "breakpoint", output
    assert output["regs"] == {**regs, "R0": result}
    assert output["mrs"] == MR_WORDS
    assert output["pinned"] == [0, 1, 0xFFFFFFFF, 0, 1, 0xFFFFFFFF] + [0] * 6 + [
        (mode & ~0x20A000) | 0x10000
    ]
