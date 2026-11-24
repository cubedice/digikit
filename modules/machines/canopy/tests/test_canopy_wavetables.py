"""Factory waveform quality and reproducibility, independent of DSP code."""

import hashlib
import struct

import numpy as np
import pytest
from canopy_paths import DSP

from modules.machines.canopy.tools.wavetables import (
    HALF_TABLE_WORDS,
    MIPS,
    SCALE,
    TABLE_NAMES,
    TABLE_WORDS,
    assembly,
    dequantized,
    expand,
    frames,
    full_words,
    half_samples,
    half_words,
    levels,
    quantized,
)


@pytest.fixture(scope="module")
def library():
    return [frames(table) for table in range(len(TABLE_NAMES))]


def test_generated_factory_asset_matches_generator():
    source = DSP / "wavetables.s"
    assert source.read_text(encoding="utf-8") == assembly()


def test_full_table_is_the_factory_data_v1():
    # sha256 of the 23,180 little-endian words that src/dsp/wavetables.s held as .VAR data
    # before it stored each level by half (commit 5936100a1): the table the DSP reads is unchanged.
    words = full_words()
    assert len(words) == len(TABLE_NAMES) * TABLE_WORDS // 2
    digest = hashlib.sha256(struct.pack(f"<{len(words)}I", *words)).hexdigest()
    assert digest == "6b26d22e072b4fbbc5baa9a7412c9b1cbb7df8cd7ff12de2dbe8c65268c9f0b9"


def test_every_quantized_mip_level_is_exactly_antisymmetric():
    every = levels()
    assert len(every) == len(TABLE_NAMES) * 8 * len(MIPS)
    for index, level in enumerate(every):
        size = MIPS[index % len(MIPS)][0]
        assert len(level) == size + 1
        assert level[0] == level[size // 2] == level[size] == 0
        assert all(level[size - n] == -level[n] for n in range(size + 1))


def test_the_module_stores_each_level_by_half():
    samples = half_samples()
    assert len(samples) == sum(
        MIPS[i % len(MIPS)][0] // 2 for i in range(len(levels()))
    )
    words = half_words()
    assert len(words) == len(TABLE_NAMES) * HALF_TABLE_WORDS + 1 and words[-1] == 0
    assert len(words) * 4 == 46084  # of 92,720 table bytes
    # an even-starting level stores [0, w1 ...], the next one [... w(N/2-1), 0]
    first, second = levels()[:2]
    assert samples[:256] == [0, *first[1:256]]
    assert samples[256:384] == [*second[1:128], 0]


def test_expansion_model_rebuilds_the_full_table_word_for_word():
    assert expand(half_words()) == full_words()


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
        for frame, q_frame, d_frame in zip(
            library[table], quantized(table), dequantized(table), strict=True
        ):
            for wave, q, d in zip(frame, q_frame, d_frame, strict=True):
                assert max(abs(v) for v in q) < 0x8000
                assert (
                    max(abs(a - b) for a, b in zip(wave, d, strict=True)) <= 0.5 / SCALE
                )
    # the first word holds sample 0 in its low half and sample 1 in its high half
    first = quantized(0)[0][0]
    word = full_words()[0]
    assert (word & 0xFFFF, word >> 16) == (first[0] & 0xFFFF, first[1] & 0xFFFF)
