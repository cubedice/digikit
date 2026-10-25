"""Short-loop contract and the assembled SHARC+ routine behind the real FX adapter.

No missing-assembler skip: concrete tests must assemble and execute the module.
The changed-live-source test is an audio simulation, not a kit/pattern-switch
integration proof. No firmware image or device is involved in these tests.
"""

import random

import pytest
from support.fx_rig import NFAULT, P0, FxRig, Row, f2w, nw
from support.paths import REPO_ROOT

from tools.patchkit import short_loop_ref as R

SRC = REPO_ROOT / "modules/fx/short-loop/src/dsp/short_loop.s"
SILENCE = [0.0] * 64
LIVE = [0.625] * 32 + [-0.375] * 32


def rig_for(rec=0.0, start=0.0, fade=0.0, record_stores=False):
    return FxRig(
        [
            Row(
                "_sl_process",
                "",
                desc="_sl_desc",
                params=tuple(f2w(x) for x in (rec, start, fade)),
            )
        ],
        sources=(SRC,),
        record_stores=record_stores,
    )


def checked_block(rig, samples=LIVE, *, hostile=False):
    b = rig.block(samples, ctx=None if hostile else "friendly")
    assert b.returned, b.halt
    assert not b.ctx_bad and not b.regs_bad, (b.ctx_bad, b.regs_bad)
    assert rig.row_word(0, NFAULT) == 0
    return b


def set_params(rig, rec=0.0, start=0.0, fade=0.0):
    for i, value in enumerate((rec, start, fade)):
        rig.set_row_word(0, P0 + i, f2w(value))


def seeded(*, start=0, phase=64, gain=1.0, varied=True, record_stores=False):
    """Initialize via the adapter, then seed a valid capture to target playback."""
    rig = rig_for(record_stores=record_stores)
    checked_block(rig, SILENCE)
    model = R.ShortLoop(
        mode=R.FROZEN,
        written=R.FRAMES,
        phase=phase,
        active_start=start,
        pending_start=start,
        gain=gain,
    )
    if varied:
        model.left = [R.f32((i % 127 - 63) / 128) for i in range(R.FRAMES)]
        model.right = [R.f32((i % 97 - 48) / 64) for i in range(R.FRAMES)]
    else:
        model.left = [0.25] * R.FRAMES
        model.right = [-0.5] * R.FRAMES
    base = nw(rig.built.data[rig.state_symbol(0)])
    for k, word in enumerate(model.state_words()):
        rig.sess.poke(base + k, word)
    return rig, model


def assert_model_block(rig, model, samples=LIVE, *, hostile=False, **params):
    set_params(rig, **params)
    b = checked_block(rig, samples, hostile=hostile)
    expected = model.block(samples, **params)
    assert tuple(map(f2w, rig.scratch())) == tuple(map(f2w, expected))
    assert [rig.state_word(0, i) for i in range(7)] == model.metadata_words()
    return b


def test_reference_capture_is_exactly_250_ms_and_keeps_stereo_channels_separate():
    model = R.ShortLoop()
    assert model.block(LIVE, fade=1.0) == tuple(LIVE)
    for k in range(R.FRAMES // R.BLOCK):
        samples = capture_block(k)
        assert model.block(samples, rec=1.0, fade=1.0) == tuple(samples)
        assert model.mode == (R.FROZEN if k == 374 else R.CAPTURING)
    assert model.written == R.FRAMES and model.phase == 0
    assert model.left == [R.f32(i / 16384) for i in range(R.FRAMES)]
    assert model.right == [R.f32(-(i + 1) / 16384) for i in range(R.FRAMES)]
    assert R.FRAMES / R.FS == 0.25 and R.STATE_BYTES == 96064


@pytest.mark.parametrize(
    ("value", "expected"), [(-1, 0), (0, 0), (0.5, 5999), (1, 11999), (2, 11999)]
)
def test_reference_start_covers_the_buffer_and_clamps(value, expected):
    assert R.start_frame(value) == expected


def capture_block(k):
    left = [R.f32(i / 16384) for i in range(k * 32, (k + 1) * 32)]
    right = [R.f32(-(i + 1) / 16384) for i in range(k * 32, (k + 1) * 32)]
    return left + right


def test_dsp_full_capture_passthrough_stereo_bounds_and_first_activation_cost():
    rig, model = rig_for(), R.ShortLoop()
    desc = rig.desc_words("_sl_desc")
    assert desc[:4] == (0x444B4658, 1, 3, R.STATE_BYTES)
    base = nw(rig.built.data[rig.state_symbol(0)])
    before = rig.sess.peek(base - 1)
    after = rig.sess.peek(base + R.STATE_BYTES // 4)
    first = assert_model_block(rig, model, LIVE, rec=0.49, fade=1.0, hostile=True)
    # Includes the adapter zeroing all 24,016 state words; excludes no startup work.
    assert R.STATE_BYTES // 4 < first.instructions < 32_000, first.instructions
    costs = []
    for k in range(R.FRAMES // R.BLOCK):
        b = assert_model_block(
            rig,
            model,
            capture_block(k),
            rec=0.5 if k == 0 else 0.0,
            start=1.0 if k == 374 else 0.0,
            fade=1.0,
        )
        assert b.master == tuple(capture_block(k))
        costs.append(b.instructions)
    assert max(costs) < 4_000, max(costs)
    assert rig.state_word(0, 0) == R.FROZEN
    assert rig.state_word(0, 1) == R.FRAMES
    assert rig.state_word(0, 3) == R.FRAMES - 1
    assert [
        rig.state_word(0, i) for i in range(R.STATE_BYTES // 4)
    ] == model.state_words()
    assert rig.sess.peek(base - 1) == before
    assert rig.sess.peek(base + R.STATE_BYTES // 4) == after


def test_dsp_simulated_live_source_change_does_not_replace_frozen_audio():
    """New live samples stand in for a new pattern; no real kit switch is tested."""
    rig, model = seeded()
    captured = model.state_words()[R.META_WORDS :]
    for rec, samples in ((0, LIVE), (1, [-0.75] * 64), (0, [0.875] * 64), (1, SILENCE)):
        b = assert_model_block(rig, model, samples, rec=rec, fade=1.0, hostile=True)
        assert b.master == rig.scratch()
    assert [
        rig.state_word(0, R.META_WORDS + k) for k in range(2 * R.FRAMES)
    ] == captured


@pytest.mark.parametrize("start", [0.0, 0.5, 1.0, -0.5, 1.5])
def test_dsp_start_latches_only_at_wrap_and_includes_the_last_frame(start):
    rig, model = seeded(start=37, phase=R.FRAMES - 64)
    assert_model_block(rig, model, start=start, fade=1.0)
    assert model.active_start == 37
    assert_model_block(rig, model, start=start, fade=1.0)
    assert model.active_start == R.start_frame(start) and model.phase == 0
    assert_model_block(rig, model, start=start, fade=1.0)
    # After the edge fade, the selected start controls both channels.
    at = (R.start_frame(start) + 16) % R.FRAMES
    edge = R.seam_gain(16, at)
    assert rig.scratch()[16] == R.f32(model.left[at] * edge)
    assert rig.scratch()[48] == R.f32(model.right[at] * edge)


def test_dsp_nonzero_start_smooths_the_physical_capture_join_inside_the_loop():
    """START rotates the recorded end/start join away from playback phase zero."""
    rig, model = seeded(start=5999, phase=5990, varied=False)
    assert_model_block(rig, model, start=0.5, fade=1.0)
    # Block index 10 reads capture frame 11999; index 11 reads capture frame 0.
    assert rig.scratch()[9] == 0.25 / 16
    assert rig.scratch()[10:12] == (0.0, 0.0)
    assert rig.scratch()[12] == 0.25 / 16
    assert rig.scratch()[42:44] == (-0.0, -0.0)
    assert rig.scratch()[44] == -0.5 / 16
    assert model.phase == 6022 and model.active_start == 5999


def test_dsp_linear_crossfade_ramps_in_256_frames_and_reaches_exact_endpoints():
    rig, model = seeded(gain=0.0, varied=False)
    b = assert_model_block(rig, model, fade=0.0)
    assert b.master == tuple(LIVE)
    for k in range(8):
        assert_model_block(rig, model, fade=1.0)
        assert model.gain == (k + 1) / 8
    assert rig.scratch()[31] == 0.25 and rig.scratch()[63] == -0.5
    assert_model_block(rig, model, fade=1.0)
    assert rig.scratch() == (0.25,) * 32 + (-0.5,) * 32
    for _ in range(8):
        assert_model_block(rig, model, fade=0.0)
    assert rig.scratch()[31] == LIVE[31] and rig.scratch()[63] == LIVE[63]
    assert_model_block(rig, model, fade=0.0)
    assert rig.scratch() == tuple(LIVE)


@pytest.mark.parametrize("target", [-1.0, 0.31, 2.0])
def test_dsp_gain_stops_at_target_and_clamps_knob_range(target):
    rig, model = seeded(gain=0.5, varied=False)
    for _ in range(5):
        assert_model_block(rig, model, fade=target)
    assert model.gain == R.clamp(target)


def test_dsp_seam_preserves_exact_period_and_cursor_runs_while_live_is_selected():
    rig, model = seeded(phase=R.FRAMES - 32, varied=False)
    assert_model_block(rig, model, fade=1.0)
    assert model.phase == 0
    assert rig.scratch()[15] == 0.25
    assert rig.scratch()[16] == 0.25 * 15 / 16
    assert rig.scratch()[31] == 0.0
    assert_model_block(rig, model, fade=1.0)
    assert rig.scratch()[0] == 0.0
    assert rig.scratch()[1] == 0.25 / 16
    assert rig.scratch()[16] == 0.25
    # Model period over a full loop; concrete boundary above proves wrap placement.
    start_phase = model.phase
    for _ in range(R.FRAMES // R.BLOCK):
        model.block(LIVE, fade=1.0)
    assert model.phase == start_phase
    rig, model = seeded(phase=R.FRAMES - 32, gain=0.0)
    assert_model_block(rig, model, fade=0.0)
    assert model.phase == 0 and rig.scratch() == tuple(LIVE)
    assert_model_block(rig, model, fade=0.0)
    assert model.phase == 32


def test_dsp_hostile_context_guards_and_foreign_store_isolation():
    rig, model = seeded(phase=R.FRAMES - 16, gain=0.5, record_stores=True)
    desc = rig.desc_words("_sl_desc")
    base = nw(rig.built.data[rig.state_symbol(0)])
    before, after = rig.sess.peek(base - 1), rig.sess.peek(base + R.STATE_BYTES // 4)
    rng = random.Random(0x5100)
    for start, fade in ((1.0, 1.0), (0.5, 0.0), (0.0, 0.31), (0.0, 1.0)):
        samples = [R.f32(rng.uniform(-0.9, 0.9)) for _ in range(64)]
        b = assert_model_block(
            rig, model, samples, start=start, fade=fade, hostile=True
        )
        assert rig.foreign_stores(desc[6]) == []
        # Conservative whole-adapter ceiling is stricter than module-only counting.
        assert b.instructions <= desc[4], (b.instructions, desc[4])
        assert rig.sess.peek(base - 1) == before
        assert rig.sess.peek(base + R.STATE_BYTES // 4) == after
