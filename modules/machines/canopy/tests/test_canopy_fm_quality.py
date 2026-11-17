"""Rendered FM checks against analytic phase modulation and spectral limits."""

import math

import numpy as np
import pytest
from test_canopy_v2 import canopy as canopy
from test_canopy_v2 import canopy_object as canopy_object
from test_canopy_v2 import (
    fm_controls,
    note,
    wt_float,
)

from modules.machines.canopy.tools.fm_tables import RATIOS, halfband

# Protected-dispatch and seven-group bounds run in test_canopy_v2. These long
# waveform captures exercise the same renderer once, without duplicating PCM.
pytestmark = pytest.mark.parametrize(
    "canopy_object", [False], indirect=True, ids=["direct"]
)


def analytic_fm(count, pitch, algorithm, ratio_a, ratio_b, depth, balance):
    """Independent sine equations; no lookup, DSP state, or assembly parsing."""
    frequency = 440 * 2 ** ((pitch - 69) / 12)
    phase = np.arange(1, count * 2 + 1) * (2 * math.pi * frequency / 96000)
    fastest = max(1, ratio_a, ratio_b) * frequency / 96000
    scale = np.clip((0.18 - fastest) * 8, 0, 1) * min(1, 0.007 / fastest)
    index = (
        np.repeat(4 * depth / 127 * (1 - (1 - 0.004158) ** np.arange(1, count + 1)), 2)
        * scale
    )
    b = np.sin(phase * ratio_b)
    a_phase = phase * ratio_a
    if algorithm == 0:
        x = np.sin(phase + index * np.sin(a_phase))
        y = np.sin(phase + index * b)
    elif algorithm == 1:
        a = np.sin(a_phase + 0.65 * index * b)
        x = np.sin(phase + 0.85 * index * a)
        y = np.sin(phase)
    elif algorithm == 2:
        x = np.sin(phase + 0.5 * index * (np.sin(a_phase) + b))
        y = np.sin(phase)
    else:
        a = np.sin(a_phase + 0.6 * index * b)
        x = np.sin(phase + 0.7 * index * a)
        y = np.sin(phase + 0.7 * index * x)
    return (
        np.convolve(x * (1 - balance) + y * balance, halfband())[: count * 2][1::2]
        * 0.08
    )


@pytest.mark.parametrize(
    "algorithm,pitch,ratio_a,ratio_b",
    [
        (0, 60, 3, 8),
        (0, 84, 5, 7),
        (1, 84, 5, 7),
        (2, 84, 5, 7),
        (3, 84, 5, 7),
        (3, 120, 15, 15),
    ],
)
def test_fm_rendered_graph_pitch_and_gain_match_analytic_reference(
    canopy, algorithm, pitch, ratio_a, ratio_b
):
    controls = fm_controls(
        algorithm=algorithm, ratio_a=ratio_a, ratio_b=ratio_b, depth=85, balance=37
    )
    rendered = canopy(
        [note(1, pitch, 127)], controls=controls, blocks=128, capture=True
    )
    audio = np.array([wt_float(value) for value in rendered["audio"]])
    assert len(audio) == 4096 and np.all(np.isfinite(audio))
    expected = analytic_fm(
        len(audio), pitch, algorithm, RATIOS[ratio_a], RATIOS[ratio_b], 85, 0.37
    )
    # The independent amplitude attack lasts480samples; compare beyond it.
    residual = audio[1024:] - expected[1024:]
    error = float(np.sqrt(np.mean(residual**2)))
    rms = float(np.sqrt(np.mean(expected[1024:] ** 2)))
    assert error < 0.00015
    assert np.sqrt(np.mean(audio[1024:] ** 2)) == pytest.approx(rms, rel=0.005)
    print("FM analytic waveform", algorithm, pitch, "RMS error", error)


@pytest.mark.parametrize(
    "algorithm,pitch,ratio,balance",
    [
        (0, 84, 15, 50),
        (1, 84, 15, 50),
        (2, 84, 15, 50),
        (3, 60, 15, 50),
        (3, 84, 15, 50),
        (3, 96, 15, 50),
        (3, 120, 15, 50),
        (3, 45, 15, 100),
        (3, 49, 15, 100),
        (3, 60, 10, 100),
        (3, 76, 3, 100),
    ],
)
def test_fm_high_index_integer_ratio_alias_energy_is_bounded(
    canopy, algorithm, pitch, ratio, balance
):
    controls = fm_controls(
        algorithm=algorithm, ratio_a=ratio, ratio_b=ratio, depth=127, balance=balance
    )
    rendered = canopy(
        [note(1, pitch, 127)], controls=controls, blocks=384, capture=True
    )
    audio = np.array([wt_float(value) for value in rendered["audio"]])[-8192:]
    assert np.all(np.isfinite(audio))
    spectrum = np.abs(np.fft.rfft(audio * np.hanning(len(audio)))) ** 2
    bins = np.fft.rfftfreq(len(audio), 1 / 48000)
    fundamental = 440 * 2 ** ((pitch - 69) / 12)
    allowed = np.zeros(len(bins), dtype=bool)
    for harmonic in range(int(24000 / fundamental) + 1):
        allowed |= np.abs(bins - harmonic * fundamental) < 6 * 48000 / len(audio)
    # At integer ratios with feedback off, energy away from harmonic bins is
    # alias/noise. Feedback can produce nonperiodic spectra, so this metric does
    # not qualify feedback or establish an alias-free FM claim.
    alias = float(spectrum[~allowed].sum() / spectrum.sum())
    assert alias < 10 ** (-35 / 10)
    print(
        "FM high-index alias dB", algorithm, pitch, ratio, balance, 10 * np.log10(alias)
    )
