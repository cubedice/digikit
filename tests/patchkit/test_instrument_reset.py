"""Execute the internal V2 reset worker; not live mailbox/device acceptance."""

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import pk_sharc


@pytest.fixture(scope="module")
def reset_worker():
    source = (
        REPO_ROOT / "modules/core/adapters/instrument-registry/src/dsp/lifecycle.s"
    ).read_text()
    source += """
.SECTION/PM seg_pmco;
.GLOBAL _test_reset;
_test_reset:
    I1 = 0x20040000;
    R0 = DM(0, I1);
    R0 = R0 + 1;
    DM(0, I1) = R0;
    DM(1, I1) = R4;
    DM(2, I1) = R8;
    I0 = R4;
    R0 = DM(0, I0);
    DM(3, I1) = R0;
    R0 = 0x12345678;
    DM(0, I0) = R0;
    R0 = DM(4, I1);
    JUMP (M13, I13);
"""
    obj = pk_sharc.assemble(source)
    linked = pk_sharc.link(obj, 0xB90000, {})
    return linked


def run(reset_worker, reason=1, words=8, result=0, overrides=None):
    callback = reset_worker.symbols["_test_reset"]
    regs = {
        "R4": 0x20050000,
        "R5": words,
        "R6": 0x20050000,
        "R7": 0x20051000,
        "R8": reason,
        "R9": callback,
        "R10": callback + 64,
        "R12": callback,
        "M5": 0,
        "M6": 1,
        "M13": 0,
        "L0": 0,
        "L1": 0,
    }
    regs.update(overrides or {})
    dm = {0x20050000 // 4 - 1 + i: 0xDEADBEEF for i in range(1026)}
    dm.update({0x20040000 // 4 + i: 0 for i in range(5)})
    dm[0x20040000 // 4 + 4] = result
    output = pk_sharc.run_call(
        reset_worker,
        "_ir_reset_instance",
        regs=regs,
        dm_words=dm,
        reads={"state": (0x20050000 // 4 - 1, 1026), "calls": (0x20040000 // 4, 5)},
        max_steps=6000,
    )
    assert output["halt"]["reason"] == "breakpoint", output
    return output


@pytest.mark.parametrize("words", [1, 8, 1024])
def test_create_clears_exact_extent_before_callback(reset_worker, words):
    output = run(reset_worker, words=words)
    assert output["regs"]["R0"] == 0
    assert output["calls"][:4] == [1, 0x20050000, 1, 0]
    assert output["state"] == [0xDEADBEEF, 0x12345678] + [0] * (words - 1) + [
        0xDEADBEEF
    ] * (1024 - words + 1)


@pytest.mark.parametrize("reason", [2, 3, 4])
def test_other_resets_preserve_state_until_callback(reset_worker, reason):
    output = run(reset_worker, reason=reason)
    assert output["calls"][:4] == [1, 0x20050000, reason, 0xDEADBEEF]
    assert output["state"] == [0xDEADBEEF, 0x12345678] + [0xDEADBEEF] * 1024


@pytest.mark.parametrize(
    "overrides",
    [
        {"R4": 0},
        {"R4": 0x20050001},
        {"R4": 0x2004FFFC},
        {"R4": 0x20050FF0},
        {"R5": 0},
        {"R5": 1025},
        {"R5": 0xFFFFFFFF},
        {"R6": 0},
        {"R7": 0x20050000},
        {"R7": 0xFFFFFFFF},
        {"R4": 0x7FFFFFFC, "R6": 0x7FFFFFFC, "R7": 0x7FFFFFFF},
        {"R8": 0},
        {"R8": 5},
        {"R8": 0xFFFFFFFF},
        {"R9": 0},
        {"R10": 0},
        {"R12": 0},
        {"R12": 0xFFFFFFFF},
        {"R12": 0xB8FFFF},
        {"R12": 0xBFFFFF},
    ],
)
def test_invalid_extent_reason_or_callback_changes_nothing(reset_worker, overrides):
    output = run(reset_worker, overrides=overrides)
    assert output["regs"]["R0"] == 0xFFFFFFFF
    assert output["calls"][0] == 0
    assert output["state"] == [0xDEADBEEF] * 1026


@pytest.mark.parametrize(
    "result,expected", [(0, 0), (1, 0xFFFFFFFF), (0xFFFFFFF9, 0xFFFFFFF9)]
)
def test_only_zero_callback_result_is_success(reset_worker, result, expected):
    output = run(reset_worker, result=result)
    assert output["calls"][0] == 1
    assert output["regs"]["R0"] == expected
