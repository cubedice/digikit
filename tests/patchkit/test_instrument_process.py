"""Instruction-core process dispatch through an admitted V2 row."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module")
def process_object():
    root = REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp"
    source = "\n".join(
        (root / name).read_text() for name in ["process.s", "process-protected.s"]
    )
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_entry;
_test_entry:
    R0 = 0;
    MR0F = R0;
    MR1F = R0;
    MR2F = R0;
    MR0B = R0;
    MR1B = R0;
    MR2B = R0;
    CALL _ir_process_protected;
    RTS;
.GLOBAL _test_process;
_test_process:
    I0 = R4;
    R0 = DM(0, I0);
    R0 = R0 + 1;
    DM(0, I0) = R0;
    I1 = R8;
    R0 = DM(5, I1);
    I1 = R0;
    R0 = 0x3e800000;
    R1 = 32;
_test_output:
    DM(I1, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _test_output;
    I1 = R8;
    R0 = DM(0, I1);
    R1 = 5;
    COMP(R0, R1);
    IF LT JUMP _test_result;
    R0 = DM(22, I1);
    I1 = R0;
    R0 = DM(0, I1);
    DM(1, I1) = R0; // Observe that the dispatcher cleared old readiness.
    R0 = 123;
    DM(12, I1) = R0;
    R0 = 1;
    DM(0, I1) = R0;
    I1 = R8;
    R0 = DM(0, I1);
    R1 = 7;
    COMP(R0, R1);
    IF NE JUMP _test_result;
    R0 = DM(32, I1);
    I1 = R0;
    R0 = 0x3f000000;
    R1 = 32;
_test_post_amp:
    DM(I1, M6) = R0;
    R1 = R1 - 1;
    IF NE JUMP _test_post_amp;
_test_result:
    R0 = DM(0x20053000);
    JUMP (M13, I13);
.PKDATA _ir_rows, 512;
.PKDATA _ir_process_rows, 256;
.PKDATA _ir_process_flags, 32;
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
    return pk_sharc.link(obj, 0xB90000, symbols), symbols, initial


@pytest.fixture
def process(process_object):
    linked, symbols, initial = process_object
    dm = dict(initial)
    dm.update({0x20050000 // 4 + i: 0 for i in range(8)})
    dm.update({0x20051000 // 4 + i: 0 for i in range(32)})
    dm.update({0x20054000 // 4 + i: 99 for i in range(14)})
    dm.update({0x20055000 // 4 + i: 0x7FC00000 for i in range(33)})
    entry = linked.symbols["_test_process"]
    row = [3, 2, 9, 2, 0x20050000, 4, 0x20050000, 0x20050020, 0, 0, 0, 0, 0, 0, 0, 0]
    meta = [2, 9, 20, entry, entry, entry + 64, 8, 1]
    packet = [2, 72, 0, 3, 100, 0x20051000, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0]
    reads = {
        "row": (symbols["_ir_rows"] // 4, 16),
        "state": (0x20050000 // 4, 8),
        "out": (0x20051000 // 4, 32),
        "status": (0x20054000 // 4, 14),
        "post_amp": (0x20055000 // 4, 33),
    }

    def call(
        instance=0,
        phase=3,
        owner=2,
        generation=9,
        result=0,
        fields=None,
        input_base=0x20052000,
        output_base=0x20051000,
        post_amp_base=0x20055000,
        capabilities=0,
    ):
        dm[symbols["_ir_process_flags"] // 4] = capabilities
        packet[5] = output_base
        reads["out"] = (output_base // 4, 32)
        for i in range(32):
            dm.setdefault(output_base // 4 + i, 0)
        reads["post_amp"] = (post_amp_base // 4, 33)
        for i in range(33):
            dm.setdefault(post_amp_base // 4 + i, 0x7FC00000)
        row[0] = phase
        meta[0], meta[1] = owner, generation
        for base, values in [
            (symbols["_ir_rows"], row),
            (symbols["_ir_process_rows"], meta),
            (input_base, packet),
        ]:
            dm.update({base // 4 + i: value for i, value in enumerate(values)})
        fields = {32: post_amp_base, 33: 32, **(fields or {})}
        for offset, value in fields.items():
            dm[input_base // 4 + offset] = value
        dm[0x20053000 // 4] = result
        out = pk_sharc.run_call(
            linked,
            "_test_entry",
            regs={
                "R4": 0,
                "R5": instance,
                "R8": input_base,
                "M5": 0,
                "M6": 1,
                "M13": 0,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
            },
            dm_words=dm,
            reads=reads,
            max_steps=2000,
        )
        assert out["halt"]["reason"] == "breakpoint", out
        for name, (base, _) in reads.items():
            dm.update({base + i: value for i, value in enumerate(out[name])})
        return out

    return call


@pytest.mark.parametrize(
    "version,size,capabilities",
    [(5, 96, 1), (5, 96, 3), (5, 96, 7), (6, 128, 3), (6, 128, 7), (7, 136, 7)],
)
def test_status_output_requires_installed_capability_and_clears_ready(
    process, version, size, capabilities
):
    fields = {0: version, 1: size, 22: 0x20054000, 23: 13}
    rejected = process(fields=fields)
    assert rejected["state"] == [0] * 8
    assert rejected["status"] == [99] * 14
    accepted = process(fields=fields, capabilities=capabilities)
    assert accepted["state"][0] == 1
    assert accepted["status"][0:2] == [1, 0]
    assert accepted["status"][12:] == [123, 99]
    failed = process(fields=fields, capabilities=capabilities, result=0xFFFFFFFF)
    assert failed["row"][0] == 5 and failed["status"][0] == 0


@pytest.mark.parametrize(
    "field,value",
    [(1, 88), (22, 0), (22, 0x20054001), (22, 0x200FDFD0), (23, 0), (23, 12), (23, 14)],
)
@pytest.mark.parametrize(
    "version,size,capabilities", [(5, 96, 1), (6, 128, 3), (7, 136, 7)]
)
def test_status_output_rejects_bad_borrow_without_mutation(
    process, field, value, version, size, capabilities
):
    fields = {0: version, 1: size, 22: 0x20054000, 23: 13, field: value}
    rejected = process(fields=fields, capabilities=capabilities)
    assert rejected["state"] == [0] * 8
    assert rejected["status"] == [99] * 14


@pytest.mark.parametrize("capabilities", [0, 1, 2, 4, 5, 6, 8, 0xFFFFFFFF])
def test_native_amp_input_requires_both_known_capabilities(process, capabilities):
    result = process(
        fields={0: 6, 1: 128, 22: 0x20054000, 23: 13}, capabilities=capabilities
    )
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8
    assert result["status"] == [99] * 14


@pytest.mark.parametrize("base,accepted", [(0x200FDF80, True), (0x200FDF84, False)])
def test_native_amp_input_extent_stays_below_boot_workspace(process, base, accepted):
    result = process(
        input_base=base,
        fields={0: 6, 1: 128, 22: 0x20054000, 23: 13},
        capabilities=3,
    )
    assert result["state"][0] == int(accepted)
    assert result["regs"]["R0"] == (0 if accepted else 0xFFFFFFFF)


@pytest.mark.parametrize("capabilities", [0, 1, 2, 3, 4, 5, 6, 8, 15, 0xFFFFFFFF])
def test_post_amp_input_requires_all_three_known_capabilities(process, capabilities):
    result = process(
        fields={0: 7, 1: 136, 22: 0x20054000, 23: 13}, capabilities=capabilities
    )
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8
    assert result["status"] == [99] * 14
    assert result["post_amp"] == [0x7FC00000] * 33


@pytest.mark.parametrize("result_code", [0, 1])
def test_post_amp_borrow_reaches_callback_for_audio_and_quiet_results(
    process, result_code
):
    result = process(
        fields={0: 7, 1: 136, 22: 0x20054000, 23: 13},
        capabilities=7,
        result=result_code,
    )
    assert result["regs"]["R0"] == result_code
    assert result["state"][0] == 1
    assert result["post_amp"] == [0x3F000000] * 32 + [0x7FC00000]


@pytest.mark.parametrize(
    "field,value",
    [
        (1, 128),
        (1, 140),
        (32, 0),
        (32, 0x1FFFFFFC),
        (32, 0x20055001),
        (32, 0x200FDF84),
        (32, 0xFFFFFFFF),
        (33, 0),
        (33, 31),
        (33, 33),
    ],
)
def test_post_amp_rejects_invalid_borrow_before_any_callback_write(
    process, field, value
):
    result = process(
        fields={0: 7, 1: 136, 22: 0x20054000, 23: 13, field: value}, capabilities=7
    )
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8
    assert result["status"] == [99] * 14
    assert result["post_amp"] == [0x7FC00000] * 33


@pytest.mark.parametrize("base,accepted", [(0x200FDF78, True), (0x200FDF7C, False)])
def test_post_amp_input_extent_stays_below_boot_workspace(process, base, accepted):
    result = process(
        input_base=base,
        fields={0: 7, 1: 136, 22: 0x20054000, 23: 13},
        capabilities=7,
    )
    assert result["state"][0] == int(accepted)
    assert result["regs"]["R0"] == (0 if accepted else 0xFFFFFFFF)


@pytest.mark.parametrize("base", [0x20000000, 0x200FDF80])
def test_post_amp_output_accepts_exact_l2_endpoints(process, base):
    result = process(
        post_amp_base=base,
        fields={0: 7, 1: 136, 22: 0x20054000, 23: 13},
        capabilities=7,
    )
    assert result["regs"]["R0"] == 0
    assert result["post_amp"] == [0x3F000000] * 32 + [0x7FC00000]


def test_process_preserves_instance_state_and_uses_distinct_strides(process):
    assert process()["state"] == [1, 0, 0, 0, 0, 0, 0, 0]
    assert process()["state"] == [2, 0, 0, 0, 0, 0, 0, 0]
    result = process(instance=1)
    assert result["state"] == [2, 0, 0, 0, 1, 0, 0, 0]
    assert result["out"] == [0x3E800000] * 32
    assert result["regs"]["R0"] == 0


@pytest.mark.parametrize("phase", [0, 1, 2, 4, 5, 6])
def test_only_ready_rows_may_render(process, phase):
    result = process(phase=phase)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8


@pytest.mark.parametrize(
    "kwargs",
    [{"owner": 3}, {"generation": 8}, {"instance": 2}, {"instance": 0xFFFFFFFF}],
)
def test_stale_bindings_and_outside_instances_do_not_call(process, kwargs):
    result = process(**kwargs)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8


@pytest.mark.parametrize(
    "fields", [{0: 1}, {1: 68}, {5: 0}, {5: 0x200FDF84}, {8: 9}, {10: 2}]
)
def test_invalid_input_is_rejected_before_callback(process, fields):
    result = process(fields=fields)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8


@pytest.mark.parametrize(
    "value,expected", [(1, 1), (2, 0xFFFFFFFF), (0xFFFFFFF9, 0xFFFFFFF9)]
)
def test_quiet_or_fault_result_and_silence_on_fault(process, value, expected):
    result = process(result=value)
    assert result["regs"]["R0"] == expected
    assert result["row"][0] == (3 if value == 1 else 5)
    assert result["out"] == ([0x3E800000] * 32 if value == 1 else [0] * 32)


def test_static_adapter_buffers_above_dynamic_arena(process):
    result = process(input_base=0x20090000, output_base=0x200FDF80)
    assert result["regs"]["R0"] == 0
    assert result["out"] == [0x3E800000] * 32


@pytest.mark.parametrize("base", [0x1FFFFFFC, 0x200FDFBC, 0x20090001])
def test_input_extent_and_alignment_are_bounded(process, base):
    result = process(input_base=base)
    assert result["regs"]["R0"] == 0xFFFFFFFF
    assert result["state"] == [0] * 8
