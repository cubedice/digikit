"""Factory waveform quality and reproducibility, independent of DSP code."""

import numpy as np
import pytest
from canopy_paths import DSP

from modules.machines.canopy.tools.wavetables import (
    MIPS,
    SCALE,
    TABLE_NAMES,
    assembly,
    dequantized,
    frames,
    quantized,
)


@pytest.fixture(scope="module")
def library():
    return [frames(table) for table in range(len(TABLE_NAMES))]


def test_generated_factory_asset_matches_generator():
    source = DSP / "wavetables.s"
    assert source.read_text(encoding="utf-8") == assembly()


def test_factory_waves_have_matching_level_and_no_dc(library):
    rms = []
    for table in library:
        for frame in table:
            for wave in frame:
                assert wave[0] == wave[-1]
                assert abs(np.mean(wave[:-1])) < 1e-12
                assert max(abs(value) for value in wave) < 1.2
            rms.append(np.sqrt(np.mean(np.square(frame[0][:-1]))))
    assert max(rms) / min(rms) < 1.000001


def test_prefiltered_mips_remove_upper_partials_without_boosting_low_ones(library):
    for table in library:
        for frame in table:
            reference = np.abs(np.fft.rfft(frame[0][:-1])) / len(frame[0][:-1])
            for wave, (size, limit) in zip(frame, MIPS, strict=True):
                spectrum = np.abs(np.fft.rfft(wave[:-1])) / size
                assert np.max(spectrum[limit + 1 :]) < 1e-12
                np.testing.assert_allclose(
                    spectrum[: limit + 1], reference[: limit + 1], atol=1e-12
                )


def test_adjacent_frame_interpolation_has_no_phase_cancellation_dips(library):
    for table in library:
        for left, right in zip(table, table[1:], strict=False):
            a, b = np.array(left[0][:-1]), np.array(right[0][:-1])
            endpoint = min(np.sqrt(np.mean(a * a)), np.sqrt(np.mean(b * b)))
            for mix in (0.25, 0.5, 0.75):
                wave = (1 - mix) * a + mix * b
                assert np.sqrt(np.mean(wave * wave)) > endpoint * 0.98


def test_factory_tables_and_frames_are_audibly_distinct(library):
    first_mips = [np.array(frame[0][:-1]) for table in library for frame in table]
    # Sine-like first frames can intentionally overlap. Every whole table must
    # evolve, and each final spectrum must differ from every other final one.
    for table in library:
        assert np.sqrt(np.mean(np.square(np.array(table[-1][0]) - table[0][0]))) > 0.05
    endings = first_mips[7::8]
    for i, wave in enumerate(endings):
        for other in endings[i + 1 :]:
            assert np.sqrt(np.mean((wave - other) ** 2)) > 0.02


def test_16_bit_samples_round_the_waves_and_pack_two_to_a_word(library):
    for table in range(len(TABLE_NAMES)):
        for frame, q_frame, d_frame in zip(library[table], quantized(table), dequantized(table), strict=True):
            for wave, q, d in zip(frame, q_frame, d_frame, strict=True):
                assert max(abs(v) for v in q) < 0x8000
                assert max(abs(a - b) for a, b in zip(wave, d, strict=True)) <= 0.5 / SCALE
    # the first word holds sample 0 in its low half and sample 1 in its high half
    first = quantized(0)[0][0]
    word = int(assembly().split("_wt_factory = 0x")[1][:8], 16)
    assert (word & 0xFFFF, word >> 16) == (first[0] & 0xFFFF, first[1] & 0xFFFF)
