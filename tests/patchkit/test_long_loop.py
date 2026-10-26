"""One-second owned-storage proof behind the real SHARC FX adapter.

The first capture starts over NaN-poisoned BSS. Reset/fault tests deliberately
leave old audio in place and require complete replacement before playback.
"""

import pytest
from support.fx_rig import NFAULT, P0, POISON, FxRig, Row, f2w, nw
from support.paths import REPO_ROOT

from tools.patchkit import long_loop_ref as R

SRC = REPO_ROOT / "modules/fx/long-loop/src/dsp/long_loop.s"
LIVE = (0.625,) * 32 + (-0.375,) * 32
SILENCE = (0.0,) * 64


def rig_for(*, record_stores=False):
    return FxRig(
        [Row("_ll_process", "", desc="_ll_desc")],
        sources=(SRC,),
        record_stores=record_stores,
    )


def audio_words(rig):
    base = nw(rig.built.data["_ll_audio"])
    return [rig.sess.peek(base + k) for k in range(R.AUDIO_BYTES // 4)]


def set_params(rig, rec=0.0, start=0.0, fade=0.0):
    for i, value in enumerate((rec, start, fade)):
        rig.set_row_word(0, P0 + i, f2w(value))


def checked(rig, samples=LIVE, *, faults=0, hostile=False):
    block = rig.block(samples, ctx=None if hostile else "friendly")
    assert block.returned, block.halt
    assert not block.ctx_bad and not block.regs_bad, (block.ctx_bad, block.regs_bad)
    assert rig.row_word(0, NFAULT) == faults
    return block


def matches(rig, model, samples=LIVE, *, hostile=False, **params):
    set_params(rig, **params)
    block = checked(rig, samples, hostile=hostile)
    expected = model.block(samples, **params)
    assert tuple(map(f2w, rig.scratch())) == tuple(map(f2w, expected))
    assert [rig.state_word(0, k) for k in range(R.META_WORDS)] == model.state_words(
        rig.built.data["_ll_audio"]
    )
    return block


def boundary_guards(rig):
    """Neighbors outside both owned ranges; adjacent metadata is checked exactly."""
    regions = (
        (nw(rig.built.data["_ll_audio"]), R.AUDIO_BYTES // 4),
        (nw(rig.built.data[rig.state_symbol(0)]), R.META_WORDS),
    )
    addresses = {a + offset for a, n in regions for offset in (-1, n)}
    guards = {
        a: rig.sess.peek(a)
        for a in addresses
        if not any(lo <= a < lo + n for lo, n in regions)
    }
    assert len(guards) >= 2
    return guards


def assert_guards(rig, guards):
    assert {a: rig.sess.peek(a) for a in guards} == guards


def capture_block(k):
    # Exact binary fractions, distinct channels, unique positions across 48000 frames.
    left = [R.f32(i / 65536) for i in range(k * 32, (k + 1) * 32)]
    right = [R.f32(-(i + 1) / 65536) for i in range(k * 32, (k + 1) * 32)]
    return left + right


def seeded(*, phase=64, start=0, gain=1.0, record_stores=False):
    rig = rig_for(record_stores=record_stores)
    checked(rig, SILENCE)
    model = R.LongLoop(
        mode=R.FROZEN,
        written=R.FRAMES,
        phase=phase,
        active_start=start,
        pending_start=start,
        gain=gain,
        left=[0.25] * R.FRAMES,
        right=[-0.5] * R.FRAMES,
    )
    for symbol, words in (
        (rig.state_symbol(0), model.state_words(rig.built.data["_ll_audio"])),
        ("_ll_audio", model.audio_words()),
    ):
        base = nw(rig.built.data[symbol])
        for k, word in enumerate(words):
            rig.sess.poke(base + k, word)
    return rig, model


def test_poisoned_bss_stays_unreadable_and_activation_is_bounded():
    rig, model = rig_for(record_stores=True), R.LongLoop()
    desc = rig.desc_words("_ll_desc")
    assert desc[:4] == (0x444B4658, 1, 3, 64)
    assert R.AUDIO_BYTES == 384_000 and R.FRAMES / R.FS == 1
    assert audio_words(rig) == [POISON] * (2 * R.FRAMES)
    guards = boundary_guards(rig)
    first = matches(rig, model, fade=1.0, hostile=True)
    assert first.master == LIVE and first.instructions < 2000
    assert audio_words(rig) == [POISON] * (2 * R.FRAMES)
    assert rig.foreign_stores(desc[6]) == []
    for k in range(3):
        block = matches(rig, model, capture_block(k), rec=1.0, fade=1.0)
        assert block.master == tuple(capture_block(k))
        assert model.mode == R.CAPTURING
        assert rig.foreign_stores(desc[6]) == []
    data = audio_words(rig)
    assert data[:96] == model.audio_words()[:96]
    assert (
        data[R.FRAMES : R.FRAMES + 96] == model.audio_words()[R.FRAMES : R.FRAMES + 96]
    )
    assert data[96 : R.FRAMES] == [POISON] * (R.FRAMES - 96)
    assert data[R.FRAMES + 96 :] == [POISON] * (R.FRAMES - 96)
    assert_guards(rig, guards)
    print(f"one-second storage: first activation {first.instructions} instructions")


def test_interrupted_capture_resets_then_overwrites_exactly_one_second():
    rig, model = rig_for(), R.LongLoop()
    guards = boundary_guards(rig)
    matches(rig, model, rec=1.0, fade=1.0)
    rig.set_enabled(0, False)
    checked(rig)
    old_audio = audio_words(rig)
    rig.set_enabled(0, True)
    model = R.LongLoop()
    first = matches(rig, model, fade=1.0, hostile=True)
    assert first.master == LIVE and first.instructions < 2000
    assert audio_words(rig) == old_audio
    costs = []
    for k in range(R.FRAMES // R.BLOCK):
        block = matches(
            rig,
            model,
            capture_block(k),
            rec=1.0 if k == 0 else 0.0,
            start=1.0 if k == R.FRAMES // R.BLOCK - 1 else 0.0,
            fade=1.0,
        )
        assert block.master == tuple(capture_block(k))
        assert model.mode == (R.FROZEN if k == R.FRAMES // R.BLOCK - 1 else R.CAPTURING)
        costs.append(block.instructions)
    assert max(costs) < 2000
    assert (model.written, model.phase, model.active_start) == (48_000, 0, 47_999)
    assert audio_words(rig) == model.audio_words()
    for _ in range(9):
        matches(rig, model, SILENCE, start=1.0, fade=1.0)
    assert rig.scratch() != SILENCE
    assert_guards(rig, guards)


@pytest.mark.parametrize("start", [-1.0, 0.0, 0.5, 1.0, 2.0])
def test_start_latches_at_wrap_and_playback_keeps_both_channels(start):
    rig, model = seeded(phase=R.FRAMES - 32, start=123, record_stores=True)
    guards = boundary_guards(rig)
    matches(rig, model, start=start, fade=1.0, hostile=True)
    assert model.phase == 0 and model.active_start == R.start_frame(start)
    matches(rig, model, start=start, fade=1.0, hostile=True)
    at = (R.start_frame(start) + 16) % R.FRAMES
    gain = R.seam_gain(16, at)
    assert rig.scratch()[16] == 0.25 * gain
    assert rig.scratch()[48] == -0.5 * gain
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)


def test_reactivation_invalidates_stale_audio_without_clearing_it():
    rig, model = seeded()
    matches(rig, model, fade=1.0)
    old_audio = audio_words(rig)
    rig.set_enabled(0, False)
    checked(rig)
    rig.set_enabled(0, True)
    model = R.LongLoop()
    first = matches(rig, model, fade=1.0, hostile=True)
    assert first.master == LIVE and first.instructions < 2000
    assert model.mode == R.IDLE
    assert audio_words(rig) == old_audio
    for _ in range(3):
        assert matches(rig, model, SILENCE, fade=1.0).master == SILENCE
    # A partial replacement still cannot expose the old frozen samples.
    assert matches(rig, model, SILENCE, rec=1.0, fade=1.0).master == SILENCE
    assert model.mode == R.CAPTURING and model.written == R.BLOCK
    assert audio_words(rig)[32] == f2w(0.25)


def test_actual_output_guard_fault_invalidates_audio_and_clears_only_metadata():
    rig, _model = seeded(record_stores=True)
    guards = boundary_guards(rig)
    # The process reads an invalid captured sample and the real output guard faults.
    base = nw(rig.built.data["_ll_audio"])
    rig.sess.poke(base + 64, POISON)
    old_audio = audio_words(rig)
    set_params(rig, fade=1.0)
    bad = checked(rig, faults=1, hostile=True)
    assert bad.master == LIVE
    assert [rig.state_word(0, k) for k in range(R.META_WORDS)] == [0] * R.META_WORDS
    assert audio_words(rig) == old_audio
    assert bad.instructions < 3500
    good = checked(rig, faults=1, hostile=True)
    assert good.master == LIVE and good.instructions < 2000
    assert rig.state_word(0, 0) == R.IDLE
    assert rig.state_word(0, R.AUDIO_ADDRESS_WORD) == rig.built.data["_ll_audio"]
    assert audio_words(rig) == old_audio
    set_params(rig, rec=1.0, fade=1.0)
    partial = checked(rig, SILENCE, faults=1)
    assert partial.master == SILENCE
    assert rig.state_word(0, 0) == R.CAPTURING
    assert rig.state_word(0, 1) == R.BLOCK
    assert rig.sess.peek(base + 64) == POISON
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)
