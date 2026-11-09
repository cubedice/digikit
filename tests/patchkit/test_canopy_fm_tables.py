"""Reproducibility and numerical properties of CANOPY's original FM assets."""

import math

import numpy as np
import pytest
from support.paths import REPO_ROOT

from tools.patchkit.canopy_fm_tables import assembly, halfband


def test_fm_generated_assets_are_reproducible():
    actual = REPO_ROOT / "modules/machines/canopy/src/dsp/fm-tables.s"
    assert actual.read_text() == assembly()


def test_fm_halfband_has_unity_dc_symmetric_phase_and_stopband_rejection():
    taps = np.array(halfband())
    assert sum(taps) == pytest.approx(1, abs=1e-14)
    assert taps == pytest.approx(taps[::-1], abs=1e-14)
    assert taps[15] == 0.5 and np.count_nonzero(taps[1::2]) == 1
    frequencies = np.linspace(0.35, 0.5, 1000)
    response = np.exp(-2j * np.pi * frequencies[:, None] * np.arange(31)) @ taps
    assert max(abs(response)) < 0.0003


def test_fm_sine_interpolation_error_is_below_float_audio_budget():
    phases = np.arange(1024)
    a = np.sin(2 * math.pi * phases / 1024)
    b = np.sin(2 * math.pi * (phases + 1) / 1024)
    expected = np.sin(2 * math.pi * (phases + 0.5) / 1024)
    assert max(abs((a + b) / 2 - expected)) < 0.000005
