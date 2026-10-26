"""Measure the present FX activation-clear limit with real SHARC execution.

The temporary effect only copies audio. Its descriptor reserves the storage of
250 ms or 500 ms of float32 stereo plus 64 metadata bytes. This proves the cost
of initialization, not a functioning longer loop or a hardware timing limit.
"""

from support.fx_rig import DATA_END, NFAULT, POISON, FxRig, Row, nw

from tools.patchkit import pk_conform

PASSTHROUGH = """
#include "dk_fx.inc"
.SECTION/PM seg_pmco;
.GLOBAL _storage_process;
_storage_process:
    I2 = R8;
    I0 = DM(DK_FX_IN_W_IN, I2);
    I1 = DM(DK_FX_IN_W_OUT, I2);
    LCNTR = DK_FX_BLOCK, DO _storage_copy UNTIL LCE;
        R0 = DM(I0, M6);
_storage_copy: DM(I1, M6) = R0;
    R0 = DK_FX_OK;
    JUMP (M13, I13);

.SECTION/DM seg_dmda;
.GLOBAL _storage_desc;
.VAR _storage_desc = DK_FX_MAGIC;
.VAR _storage_desc1 = DK_FX_V1;
.VAR _storage_desc2 = 0;
.VAR _storage_desc3 = STATE_BYTES;
.VAR _storage_desc4 = 256;
.VAR _storage_desc5 = 0;
.VAR _storage_desc6 = 0;
"""
LIVE = (0.625,) * 32 + (-0.375,) * 32


def test_doubling_loop_storage_exceeds_the_current_activation_budget(tmp_path):
    """Both sizes fit and return; only the larger initial clear exceeds budget."""
    budget = pk_conform.fx_budget() + pk_conform.FX_ADAPTER_MAX
    activation_costs = []
    steady_costs = []
    for milliseconds, state_bytes in ((250, 96_064), (500, 192_064)):
        source = tmp_path / f"storage_{milliseconds}.s"
        source.write_text(
            PASSTHROUGH.replace("STATE_BYTES", str(state_bytes)), encoding="utf-8"
        )
        rig = FxRig(
            [Row("_storage_process", "", desc="_storage_desc")],
            sources=(source,),
            step_limit=100_000,
        )
        state = rig.built.data[rig.state_symbol(0)]
        assert rig.desc_words("_storage_desc")[3] == state_bytes
        assert (state, state_bytes) in rig.built.module_data
        assert state + state_bytes < DATA_END
        base, words = nw(state), state_bytes // 4
        assert all(rig.sess.peek(base + k) == POISON for k in range(words))
        guards = (rig.sess.peek(base - 1), rig.sess.peek(base + words))

        first = rig.block(LIVE)
        assert first.returned, first.halt
        assert not first.ctx_bad and not first.regs_bad
        assert first.master == LIVE
        assert rig.row_word(0, NFAULT) == 0
        assert all(rig.sess.peek(base + k) == 0 for k in range(words))
        assert (rig.sess.peek(base - 1), rig.sess.peek(base + words)) == guards

        steady = rig.block(LIVE)
        assert steady.returned, steady.halt
        assert not steady.ctx_bad and not steady.regs_bad
        assert steady.master == LIVE
        assert rig.row_word(0, NFAULT) == 0
        assert (rig.sess.peek(base - 1), rig.sess.peek(base + words)) == guards
        activation_costs.append(first.instructions)
        steady_costs.append(steady.instructions)
        print(
            f"{milliseconds} ms equivalent storage ({state_bytes} bytes): "
            f"activation {first.instructions}, steady {steady.instructions}, "
            f"generic adapter + effect budget {budget} instructions"
        )

    assert activation_costs[0] <= budget < activation_costs[1]
    assert steady_costs[0] == steady_costs[1] < budget
    # Identical passthrough code: the extra 24,000 words explain the extra cost.
    assert activation_costs[1] - activation_costs[0] == 24_000
