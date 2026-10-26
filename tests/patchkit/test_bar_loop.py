"""Concrete eight-second DSP proof with an explicit test storage provider.

This fixture supplies memory; it does not prove that stock firmware relinquishes
it. The separate allocator/provider proof owns that claim. The same provider
fixture is explicitly registered with the ordinary conformance worker.
"""

from dataclasses import replace

import pytest
from support.bar_loop_provider import BASE, CAPACITY, BarLoopRig, external_word
from support.fx_conform_worker import make_rig
from support.fx_rig import NFAULT, P0, POISON, FxRig, Row, f2w, nw
from support.paths import REPO_ROOT

from tools.patchkit import bar_loop_ref as R
from tools.patchkit import pk_conform
from tools.patchkit.pk_sharc import PkSharcError

SRC = REPO_ROOT / "modules/fx/bar-loop/src/dsp/bar_loop.s"
LIVE = (0.625,) * 32 + (-0.375,) * 32
SILENCE = (0.0,) * 64


def set_params(rig, rec=0.0, start=0.0, fade=0.0):
    for k, value in enumerate((rec, start, fade)):
        rig.set_row_word(0, P0 + k, f2w(value))


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
        BASE
    )
    return block


def provider_words(rig):
    at = nw(rig.built.data["_loop_buffer_info"])
    return tuple(rig.sess.peek(at + k) for k in range(4))


def set_provider(rig, words):
    at = nw(rig.built.data["_loop_buffer_info"])
    for k, value in enumerate(words):
        rig.sess.poke(at + k, value)


def external_guards(rig):
    guards = {BASE - 4: 0x13579BDF, BASE + R.STORAGE_BYTES: 0x2468ACE0}
    for address, value in guards.items():
        rig.sess.poke(external_word(address), value)
    return guards


def assert_guards(rig, guards):
    assert {
        address: rig.sess.peek(external_word(address)) for address in guards
    } == guards


def assert_canary(rig, base=BASE):
    at = external_word(base + R.AUDIO_BYTES)
    assert [rig.sess.peek(at + k) for k in range(8)] == [R.CANARY_WORD] * 8


def seed_frozen(rig, *, phase=64, start=0, extra_frames=()):
    """Seed only frames the targeted calls may read; full capture is tested below."""
    checked(rig, SILENCE)
    model = R.BarLoop(
        mode=R.FROZEN,
        written=R.FRAMES,
        phase=phase,
        active_start=start,
        pending_start=start,
        gain=1.0,
        canary_initialized=True,
        left=[0.25] * R.FRAMES,
        right=[-0.5] * R.FRAMES,
    )
    at = nw(rig.built.data[rig.state_symbol(0)])
    for k, value in enumerate(model.state_words(BASE)):
        rig.sess.poke(at + k, value)
    frames = {(phase + start + k) % R.FRAMES for k in range(96)} | set(extra_frames)
    base = external_word(BASE)
    for frame in frames:
        rig.sess.poke(base + frame, f2w(0.25))
        rig.sess.poke(base + R.FRAMES + frame, f2w(-0.5))
    return model


INVALID_PROVIDERS = [
    (0, 1, BASE, CAPACITY),
    (R.PROVIDER_MAGIC, 0, BASE, CAPACITY),
    (R.PROVIDER_MAGIC, 2, BASE, CAPACITY),
    (R.PROVIDER_MAGIC, 1, 0, CAPACITY),
    (R.PROVIDER_MAGIC, 1, 0x7FFFFFFC, CAPACITY),
    (R.PROVIDER_MAGIC, 1, R.EXTERNAL_END, CAPACITY),
    (R.PROVIDER_MAGIC, 1, 0xFFFFFFFC, CAPACITY),
    (R.PROVIDER_MAGIC, 1, BASE + 1, CAPACITY),
    (R.PROVIDER_MAGIC, 1, BASE + 2, CAPACITY),
    (R.PROVIDER_MAGIC, 1, BASE, 0),
    (R.PROVIDER_MAGIC, 1, BASE, R.AUDIO_BYTES - 4),
    (R.PROVIDER_MAGIC, 1, BASE, R.AUDIO_BYTES),
    (R.PROVIDER_MAGIC, 1, BASE, R.STORAGE_BYTES - 4),
    (R.PROVIDER_MAGIC, 1, BASE, CAPACITY + 1),
    (R.PROVIDER_MAGIC, 1, BASE, 0xFFFFFFFC),
    (R.PROVIDER_MAGIC, 1, BASE, 0x80000000),
    (R.PROVIDER_MAGIC, 1, BASE, 0x70000000),
    (R.PROVIDER_MAGIC, 1, R.EXTERNAL_END - R.STORAGE_BYTES + 4, R.STORAGE_BYTES),
]


@pytest.mark.parametrize("words", INVALID_PROVIDERS)
def test_invalid_provider_faults_before_any_external_read_or_write(words):
    assert not R.provider_valid(*words)
    rig = BarLoopRig(watch_external_reads=True)
    set_provider(rig, words)
    set_params(rig, rec=1.0, fade=1.0)
    block = checked(rig, faults=1, hostile=True)
    assert block.master == LIVE and block.instructions < 2000
    assert [rig.state_word(0, k) for k in range(R.META_WORDS)] == [0] * R.META_WORDS
    assert provider_words(rig) == words
    assert rig.external_events() == []
    assert rig.foreign_stores(0) == []


@pytest.mark.parametrize(
    ("base", "size"),
    [
        (BASE, CAPACITY),
        (R.EXTERNAL_START, R.STORAGE_BYTES),
        (R.EXTERNAL_END - R.STORAGE_BYTES, R.STORAGE_BYTES),
        (R.EXTERNAL_START, R.EXTERNAL_END - R.EXTERNAL_START),
    ],
)
def test_provider_address_and_capacity_boundaries_are_valid(base, size):
    words = (R.PROVIDER_MAGIC, 1, base, size)
    assert R.provider_valid(*words)
    rig = BarLoopRig(watch_external_reads=True, lease_base=base)
    set_provider(rig, words)
    set_params(rig, fade=1.0)
    block = checked(rig, hostile=True)
    assert block.master == LIVE and block.instructions < 2000
    assert rig.state_word(0, R.AUDIO_ADDRESS_WORD) == base
    assert rig.state_word(0, R.FRAME_COUNT_WORD) == R.FRAMES
    assert rig.audio_events() == []
    assert len(rig.canary_events()) == 8
    assert all(event.access == "write" for event in rig.canary_events())
    assert rig.state_word(0, R.CANARY_INITIALIZED_WORD) == 1
    assert_canary(rig, base)
    assert rig.foreign_stores(0) == []
    assert provider_words(rig) == words


def test_required_provider_import_has_no_fallback():
    with pytest.raises(PkSharcError, match="unresolved symbol _loop_buffer_info"):
        FxRig([Row("_bl_process", "", desc="_bl_desc")], sources=(SRC,))


def test_conformance_factory_registers_only_the_explicit_provider():
    spec = pk_conform.fx_spec("michael/bar-loop")
    assert spec is not None
    rig = make_rig(spec, record_stores=True)
    assert isinstance(rig, BarLoopRig)
    set_params(rig, rec=1.0, fade=1.0)
    assert checked(rig, hostile=True).master == LIVE
    assert rig.foreign_stores(0) == []
    assert_canary(rig)
    # The same DSP source under an unregistered module gets no injected import.
    with pytest.raises(PkSharcError, match="unresolved symbol _loop_buffer_info"):
        make_rig(replace(spec, module="test/unregistered-provider"))


def test_registered_factory_does_not_resolve_additional_unknown_imports(tmp_path):
    spec = pk_conform.fx_spec("michael/bar-loop")
    assert spec is not None
    source = tmp_path / "unknown_import.s"
    text = SRC.read_text().replace(
        ".EXTERN _loop_buffer_info;",
        ".EXTERN _loop_buffer_info;\n.EXTERN _unknown_provider;",
    )
    source.write_text(
        text.replace("    I5 = R4;", "    R0 = _unknown_provider;\n    I5 = R4;"),
        encoding="utf-8",
    )
    with pytest.raises(PkSharcError, match="unresolved symbol _unknown_provider"):
        make_rig(replace(spec, sources=(source,)))


def test_poisoned_external_storage_is_unreadable_before_complete_capture():
    rig, model = BarLoopRig(watch_external_reads=True), R.BarLoop()
    guards = external_guards(rig)
    base = external_word(BASE)
    for offset in (0, 32, R.FRAMES - 1, R.FRAMES, R.FRAMES + 32, 2 * R.FRAMES - 1):
        rig.sess.poke(base + offset, POISON)
    first = matches(rig, model, fade=1.0, hostile=True)
    assert first.master == LIVE and first.instructions < 2000
    assert rig.audio_events() == []
    assert len(rig.canary_events()) == 8
    assert all(event.access == "write" for event in rig.canary_events())
    assert_canary(rig)
    partial = matches(rig, model, rec=1.0, fade=1.0, hostile=True)
    assert partial.master == LIVE and model.mode == R.CAPTURING
    assert all(event.access == "write" for event in rig.audio_events())
    assert all(event.access == "read" for event in rig.canary_events())
    assert rig.sess.peek(base + 32) == POISON
    assert rig.sess.peek(base + R.FRAMES + 32) == POISON
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)
    assert_canary(rig)
    print(f"eight-second storage: first activation {first.instructions} instructions")


def test_full_eight_second_capture_is_exact_stereo_and_stays_inside_lease():
    rig, model = BarLoopRig(), R.BarLoop()
    guards = external_guards(rig)
    descriptor = rig.desc_words("_bl_desc")
    assert descriptor[:4] == (0x444B4658, 1, 3, 64)
    assert R.FRAMES / R.FS == 8 and R.AUDIO_BYTES == 3_072_000
    matches(rig, model)
    worst = 0
    for k in range(R.FRAMES // R.BLOCK):
        indices = range(k * R.BLOCK, (k + 1) * R.BLOCK)
        samples = [i / 1048576 for i in indices] + [-(i + 1) / 1048576 for i in indices]
        block = matches(
            rig,
            model,
            samples,
            rec=1.0 if k == 0 else 0.0,
            start=1.0,
            fade=1.0,
        )
        assert block.master == tuple(samples)
        assert model.mode == (R.FROZEN if k == 11_999 else R.CAPTURING)
        worst = max(worst, block.instructions)
    assert worst < 2000
    assert (model.written, model.phase, model.active_start) == (384_000, 0, 383_999)
    base = external_word(BASE)
    assert [rig.sess.peek(base + k) for k in range(2 * R.FRAMES)] == model.audio_words()
    for _ in range(9):
        matches(rig, model, SILENCE, start=1.0, fade=1.0)
    assert rig.scratch() != SILENCE
    assert_guards(rig, guards)
    assert_canary(rig)
    assert provider_words(rig) == (R.PROVIDER_MAGIC, 1, BASE, CAPACITY)


@pytest.mark.parametrize("start", [0.0, 0.5, 1.0])
def test_start_wrap_and_playback_use_valid_external_frames(start):
    rig = BarLoopRig(watch_external_reads=True)
    target = R.start_frame(start)
    model = seed_frozen(
        rig,
        phase=R.FRAMES - R.BLOCK,
        start=123,
        extra_frames=((target + k) % R.FRAMES for k in range(R.BLOCK)),
    )
    guards = external_guards(rig)
    matches(rig, model, start=start, fade=1.0, hostile=True)
    assert model.phase == 0 and model.active_start == target
    matches(rig, model, start=start, fade=1.0, hostile=True)
    assert any(event.access == "read" for event in rig.external_events())
    assert all(event.access != "write" for event in rig.external_events())
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)


@pytest.mark.parametrize("reset", ["disable", "output_fault", "provider_fault"])
def test_resets_invalidate_stale_audio_without_clearing_external_storage(reset):
    rig = BarLoopRig(watch_external_reads=True)
    seed_frozen(rig)
    guards = external_guards(rig)
    set_params(rig, fade=1.0)
    base = external_word(BASE)
    faults = 0
    if reset == "disable":
        rig.set_enabled(0, False)
        checked(rig)
        rig.set_enabled(0, True)
    else:
        if reset == "output_fault":
            rig.sess.poke(base + 64, POISON)
        else:
            set_provider(rig, (R.PROVIDER_MAGIC, 1, BASE, 0))
        failed = checked(rig, faults=1, hostile=True)
        assert failed.master == LIVE
        assert [rig.state_word(0, k) for k in range(R.META_WORDS)] == [0] * R.META_WORDS
        if reset == "provider_fault":
            assert rig.external_events() == []
            set_provider(rig, (R.PROVIDER_MAGIC, 1, BASE, CAPACITY))
        faults = 1
    stale = rig.sess.peek(base + 64)
    first = checked(rig, faults=faults, hostile=True)
    assert first.master == LIVE and first.instructions < 2000
    assert rig.state_word(0, 0) == R.IDLE
    assert rig.audio_events() == []
    assert len(rig.canary_events()) == 8
    assert all(event.access == "write" for event in rig.canary_events())
    assert_canary(rig)
    assert rig.sess.peek(base + 64) == stale
    set_params(rig, rec=1.0, fade=1.0)
    partial = checked(rig, SILENCE, faults=faults)
    assert partial.master == SILENCE
    assert rig.state_word(0, 0) == R.CAPTURING and rig.state_word(0, 1) == R.BLOCK
    assert all(event.access == "write" for event in rig.audio_events())
    assert rig.sess.peek(base + 64) == stale
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)


@pytest.mark.parametrize("word", [0, 7])
def test_tail_canary_corruption_faults_before_audio_access_and_resets(word):
    rig = BarLoopRig(watch_external_reads=True)
    seed_frozen(rig)
    guards = external_guards(rig)
    canary = external_word(BASE + R.AUDIO_BYTES)
    rig.sess.poke(canary + word, 0)
    set_params(rig, fade=1.0)
    fault = checked(rig, faults=1, hostile=True)
    assert fault.master == LIVE
    assert rig.audio_events() == []
    assert all(event.access == "read" for event in rig.canary_events())
    assert [rig.state_word(0, k) for k in range(R.META_WORDS)] == [0] * R.META_WORDS
    assert rig.sess.peek(canary + word) == 0
    reset = checked(rig, faults=1, hostile=True)
    assert reset.master == LIVE and reset.instructions < 2000
    assert rig.audio_events() == []
    assert len(rig.canary_events()) == 8
    assert all(event.access == "write" for event in rig.canary_events())
    assert_canary(rig)
    assert rig.foreign_stores(0) == []
    assert_guards(rig, guards)
