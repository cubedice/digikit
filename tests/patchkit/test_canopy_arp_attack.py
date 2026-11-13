"""Native AMP note and arp onsets remain continuous at minimum ATT."""

import math

import pytest
from test_canopy_v2 import canopy as canopy
from test_canopy_v2 import canopy_object as canopy_object
from test_canopy_v2 import (
    fm_controls,
    make_canopy,
    native_amp,
    note,
    spread_controls,
    voices,
    wavetable_controls,
    wavetable_fm_controls,
    wt_bits,
    wt_float,
)


def source_controls(family, mix):
    controls = {
        "additive": spread_controls,
        "wavetable": wavetable_controls,
        "fm": fm_controls,
        "wavetable-fm": lambda: wavetable_fm_controls(fm_depth=90),
    }[family]()
    controls[6] = controls[21] = mix
    return controls


@pytest.mark.parametrize("canopy_object", [False], indirect=True, ids=["direct"])
@pytest.mark.parametrize("family", ["additive", "wavetable", "fm", "wavetable-fm"])
@pytest.mark.parametrize("mode", [1, 2], ids=["ahd", "adsr"])
@pytest.mark.parametrize("options", [0, 1], ids=["normal", "restart"])
def test_zero_attack_pad_matches_short_declick_pcm(
    canopy_object, family, mode, options
):
    def render(attack):
        synth = make_canopy(canopy_object)
        amp = native_amp(mode=mode, attack=attack, sustain=1, options=options)
        return [
            synth(
                [note(1, 60, velocity=127)],
                controls=source_controls(family, 0),
                native=amp,
                post_amp=True,
            ),
            synth(controls=source_controls(family, 0), native=amp, post_amp=True),
        ]

    instant, reference = render(1), render(1 / 64)
    assert [block["out"] for block in instant] == [block["out"] for block in reference]
    gains = [wt_float(word) for block in instant for word in block["common_amp"]]
    assert gains == pytest.approx(
        [min(1, (i + options * 0.5) / 64) for i in range(64)], abs=1e-7
    )


@pytest.mark.parametrize("canopy_object", [False], indirect=True, ids=["direct"])
@pytest.mark.parametrize("family", ["additive", "wavetable", "fm", "wavetable-fm"])
@pytest.mark.parametrize("mode", [1, 2], ids=["ahd", "adsr"])
@pytest.mark.parametrize("options", [0, 1], ids=["normal", "restart"])
def test_zero_attack_arp_matches_short_declick_pcm(
    canopy_object, family, mode, options, record_property
):
    def render(attack):
        synth = make_canopy(canopy_object)
        # Enable ARP on an already-running note, so its inherited oscillator
        # and FM history need not be at a zero crossing. Keep the pad muted in
        # the measured output; it still renders and remains a chord owner.
        warm = synth(
            [note(1, 60, velocity=127)],
            controls=source_controls(family, 0),
            native=native_amp(mode=mode, attack=1, sustain=1, options=options),
            blocks=11,
        )
        assert abs(wt_float(voices(warm)[1][2])) > 0.3
        blocks = [
            synth(
                controls=source_controls(family, 127),
                native=native_amp(mode=mode, attack=attack, sustain=1, options=options),
                post_amp=True,
                state_edits={144: wt_bits(1)},
            )
        ]
        blocks.extend(
            synth(
                controls=source_controls(family, 127),
                native=native_amp(mode=mode, attack=attack, sustain=1, options=options),
                post_amp=True,
            )
            for _ in range(2)
        )
        assert blocks[0]["state"][136] != 0
        assert set(voices(blocks[-1])) == {1}
        return blocks

    instant = render(1)
    reference = render(1 / 128)
    assert [block["out"] for block in instant] == [block["out"] for block in reference]
    samples = [wt_float(word) for block in instant for word in block["out"]]
    assert all(math.isfinite(sample) and abs(sample) < 1 for sample in samples)
    peak = max(abs(sample) for sample in samples)
    assert peak > 0.005
    # The first sample is silent, or <=1/128 of the instantaneous waveform
    # under stock restart initialization; both eliminate a full-level edge.
    assert abs(samples[0]) < peak / 32
    gains = [wt_float(word) for block in instant for word in block["common_amp"]]
    expected = [min(1, (i + options * 0.5) / 128) for i in range(96)]
    assert gains == pytest.approx(expected, abs=1e-7)
    record_property("peak", peak)
    record_property("initial_jump", abs(samples[0]))
    record_property(
        "maximum_sample_difference",
        max(abs(b - a) for a, b in zip(samples, samples[1:], strict=False)),
    )


@pytest.mark.parametrize("attack", [1 / 256, 1 / 1024])
@pytest.mark.parametrize("options", [0, 1])
def test_arp_retains_slower_selected_attack_and_pad_minimum(canopy, attack, options):
    controls = source_controls("additive", 127)
    amp = native_amp(attack=attack, sustain=1, options=options)
    r = canopy(
        [note(1, 60)],
        controls=controls,
        native=amp,
        post_amp=True,
        state_edits={144: wt_bits(1)},
    )
    assert [wt_float(word) for word in r["amp_curve"]] == pytest.approx(
        [(i + options * 0.5) * attack for i in range(32)], abs=1e-7
    )
    assert wt_float(r["state"][143]) == pytest.approx((32 + options * 0.5) * attack)
    # Pad-only ATT=0 also starts smoothly, independently of the arp.
    r = canopy(
        [note(2, 67)],
        controls=source_controls("additive", 0),
        native=native_amp(attack=1, sustain=1, options=options),
        state=0x20054000,
    )
    assert wt_float(voices(r)[2][7]) == pytest.approx(0.5 + options / 128)


def test_arp_onset_guard_does_not_change_adsr_release(canopy):
    controls = source_controls("additive", 127)
    amp = native_amp(attack=1, sustain=1, release=0.99)
    canopy([note(1, 60)], controls=controls, native=amp, blocks=4)
    r = canopy(controls=controls, native=amp, state_edits={146: 45000})
    assert r["state"][137] == 0
    assert [wt_float(word) for word in r["amp_curve"]] == pytest.approx(
        [0.99**i for i in range(32)], rel=1e-6
    )
    assert wt_float(r["state"][143]) == pytest.approx(0.99**32, rel=1e-6)


def test_arp_ahd_tail_keeps_partial_onset_when_replaced(canopy):
    controls = source_controls("additive", 127)
    amp = native_amp(mode=1, attack=1, hold=1000)
    first = canopy([note(1, 60)], controls=controls, native=amp)
    assert wt_float(first["state"][143]) == 0.25
    second = canopy(controls=controls, native=amp, state_edits={146: 89999})
    tail = next(slot for slot in (4, 5) if second["state"][slot * 8 + 1] & 4)
    assert wt_float(second["state"][tail * 8 + 7]) == 0.5
    # The saved tail continues its ramp without jumping or restarting.
    assert [wt_float(word) for word in second["amp_curve"]] == pytest.approx(
        [(32 + i) / 128 for i in range(32)], abs=1e-7
    )
    assert wt_float(second["state"][143]) == 0.25
