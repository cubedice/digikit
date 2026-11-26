"""Reproducible original CANOPY factory waves and their band-limited mipmaps.

Run ``uv run python -m modules.machine.digikit.canopy.tools.wavetables`` to regenerate the
module-owned assembly data. No vendor waves or user audio are used.
"""

from __future__ import annotations

import math
import struct
from pathlib import Path

TABLE_NAMES = ("ANALOG", "GLASS", "VOCAL", "ORGAN", "METAL")
MIPS = ((512, 64), (256, 32), (128, 16), (64, 8), (64, 4), (64, 2), (64, 1))
FRAME_WORDS = sum(size + 1 for size, _ in MIPS)  # samples of a frame (the name predates 16-bit samples)
TABLE_WORDS = 8 * FRAME_WORDS
# What the module stores of a frame and a table, in 32-bit words: N/2 samples of each level of N (half_samples).
HALF_FRAME_WORDS = sum(size // 4 for size, _ in MIPS)
HALF_TABLE_WORDS = 8 * HALF_FRAME_WORDS
# The DSP reads 16-bit samples (two to a 32-bit word, the even sample in the low half) with sign-extending short-word
# loads and scales the table output by 2^-14 once (src/dsp/wavetable.s). The peak, about 1.10, uses 18060 of 32767.
SCALE = 1 << 14


def coefficients(table: int, frame: int) -> list[float]:
    """Phase-aligned real sine amplitudes; RMS matched before mip filtering."""
    t = frame / 7
    result = [0.0] * 65
    result[1] = 1.0
    for harmonic in range(2, 65):
        h = float(harmonic)
        if table == 0:
            # A smooth brightness evolution, ending with odd-dominant spectra.
            amp = t**0.8 / h ** (2.6 - 1.5 * t)
            if harmonic % 2 == 0:
                amp *= 1 - 0.75 * t * t
        elif table == 1:
            amp = (0.09 + 0.55 * t) * math.exp(-h / (4 + 20 * t))
            amp *= 1.0 if harmonic in (3, 5, 8, 13, 21, 34, 55) else 0.05
        elif table == 2:
            formant_a, formant_b = 3 + 8 * t, 12 - 6 * t
            amp = 0.33 * math.exp(-(((h - formant_a) / 1.9) ** 2))
            amp += 0.22 * math.exp(-(((h - formant_b) / 2.7) ** 2))
            amp /= math.sqrt(h)
        elif table == 3:
            amp = 0.15 + 0.35 * math.sin((frame + harmonic) * 0.7) ** 2
            amp *= math.exp(-h / (3 + 13 * t))
            amp *= 1.0 if harmonic in (2, 3, 4, 6, 8, 12, 16) else 0.0
        elif table == 4:
            amp = t * (0.2 + 0.8 * math.sin(h * 1.618) ** 2) / h**0.75
            amp *= math.exp(-h / (5 + 35 * t))
        else:
            raise ValueError("unknown factory table")
        result[harmonic] = amp
    # All partials have the same polarity: interpolation cannot phase-cancel.
    scale = 0.8 / math.sqrt(sum(value * value for value in result))
    return [value * scale for value in result]


def frames(table: int) -> list[list[list[float]]]:
    result = []
    for frame in range(8):
        amplitudes = coefficients(table, frame)
        levels = []
        for size, harmonics in MIPS:
            wave = [
                sum(
                    amplitudes[h] * math.sin(2 * math.pi * h * n / size)
                    for h in range(1, harmonics + 1)
                )
                for n in range(size)
            ]
            levels.append(wave + wave[:1])
        result.append(levels)
    return result


def quantized(table: int) -> list[list[list[int]]]:
    """The 16-bit samples the DSP stores: `frames` at the table scale, rounded."""
    out = []
    for frame in frames(table):
        levels = []
        for level in frame:
            q = [round(value * SCALE) for value in level]
            assert all(-0x8000 <= v <= 0x7FFF for v in q)
            levels.append(q)
        out.append(levels)
    return out


def dequantized(table: int) -> list[list[list[float]]]:
    """The waves as the DSP plays them: the 16-bit samples back at unit scale (exact in float)."""
    return [[[v / SCALE for v in level] for level in frame] for frame in quantized(table)]


def _pack(samples: list[int]) -> list[int]:
    """16-bit samples two to a 32-bit word, the even sample in the low half."""
    assert len(samples) % 2 == 0
    return [(samples[i] & 0xFFFF) | (samples[i + 1] & 0xFFFF) << 16 for i in range(0, len(samples), 2)]


def levels() -> list[list[int]]:
    """Every mip level of every table and frame in table order (`quantized`), each with its guard sample."""
    return [level for table in range(len(TABLE_NAMES)) for frame in quantized(table) for level in frame]


def full_words() -> list[int]:
    """The whole factory table the DSP reads (`_wt_factory`, TABLE_WORDS samples a table): every level of `levels`,
    packed. The DSP rebuilds it from `half_words` (`expand`); the tests compare both with this."""
    return _pack([sample for level in levels() for sample in level])


def half_samples() -> list[int]:
    """The samples the module stores: every level holds N + 1 samples w[0..N] with w[N - n] = -w[n] and w[0], w[N/2],
    w[N] zero (each quantized level is exactly antisymmetric: its partials are sines in phase), so w[1..N/2 - 1]
    determine it. A level whose first sample falls on an even sample of the table (the levels alternate, since each
    has an odd length) stores [0, w[1..N/2 - 1]], the other [w[1..N/2 - 1], 0]: N/2 samples, so the forward half of
    every level is a word copy of the stored words (`expand`)."""
    out: list[int] = []
    for index, level in enumerate(levels()):
        size = len(level) - 1
        if any(level[size - n] != -level[n] for n in range(size + 1)) or level[size // 2]:
            raise ValueError("a CANOPY factory mip level is not exactly antisymmetric")
        half = level[1 : size // 2]
        out.extend([0, *half] if index % 2 == 0 else [*half, 0])
    return out


def half_words() -> list[int]:
    """`half_samples` packed, HALF_TABLE_WORDS a table, and one zero word: the expansion of the last level loads one
    word ahead."""
    return [*_pack(half_samples()), 0]


def expand(words: list[int]) -> list[int]:
    """The DSP's expansion (`_wt_expand_table` in src/dsp/wavetable.s), word for word, of `half_words` to
    `full_words`. Per level of N: N/4 stored words c; each is the forward word, and the backward words run down from
    the level's end, (-lo(c), -hi(previous c)) in 16-bit fields; a level that starts on an even sample also writes
    (0, -hi(last c)) at its middle."""
    mask = 0xFFFFFFFF
    out = [0] * (len(TABLE_NAMES) * TABLE_WORDS // 2)
    src = dst = 0
    for _table in range(len(TABLE_NAMES)):
        for index in range(8 * len(MIPS)):
            size, even = MIPS[index % len(MIPS)][0], index % 2 == 0
            back = start = dst + size // 2 - (not even)
            negated_high, cur = 0, words[src]
            for _ in range(size // 4):
                out[dst] = cur
                dst += 1
                out[back] = negated_high | (-cur & 0xFFFF)
                back -= 1
                negated_high = -(cur & 0xFFFF0000) & mask
                src += 1
                cur = words[src]
            if even:
                out[back] = negated_high
            dst = start + 1
    return out


def assembly() -> str:
    lines = [
        "// Generated by modules.machine.digikit.canopy.tools.wavetables; original factory data v1, 16-bit samples at 2^14,",
        "// two to a word (the even sample in the low half). Each mip level by half (tools/wavetables.py half_samples);",
        "// _wt_expand_table (src/dsp/wavetable.s) rebuilds the whole table in _wt_factory before it is read.",
        ".SECTION/DM seg_dmda;",
        ".GLOBAL _wt_factory_half;",
    ]
    words = half_words()
    assert len(words) == len(TABLE_NAMES) * HALF_TABLE_WORDS + 1
    for index, word in enumerate(words):
        if index % HALF_TABLE_WORDS == 0 and index < len(words) - 1:
            lines.append(f"// {TABLE_NAMES[index // HALF_TABLE_WORDS]}: eight frames, seven mip levels, N/4 words a level.")
        elif index == len(words) - 1:
            lines.append("// The word the last level's expansion loads ahead.")
        label = "_wt_factory_half" if index == 0 else f"_wt_h_{index}"
        lines.append(f".VAR {label} = 0x{word:08x};")
    return "\n".join(lines) + "\n"


def preview_values() -> list[list[list[int]]]:
    """Full, phase-aligned factory cycles for the 74-pixel SRC preview."""
    waves = []
    for table in range(len(TABLE_NAMES)):
        rows = []
        for frame in range(8):
            amplitude = coefficients(table, frame)
            rows.append(
                [
                    sum(
                        amplitude[h] * math.sin(2 * math.pi * h * n / 73)
                        for h in range(1, 65)
                    )
                    for n in range(74)
                ]
            )
        waves.append(rows)
    scale = 127 / max(abs(sample) for table in waves for row in table for sample in row)
    return [
        [[round(sample * scale) for sample in row] for row in table] for table in waves
    ]


def preview_header() -> str:
    """Store half of each cycle; the other half is its exact negative mirror."""
    waves = preview_values()
    lines = [
        "/* Generated by modules.machine.digikit.canopy.tools.wavetables; original CANOPY factory waves. */",
        "static const int8_t canopy_preview_waves[5][8][37]={",
    ]
    for table in waves:
        lines.append("    {")
        for row in table:
            if any(row[i] != -row[73 - i] for i in range(74)):
                raise ValueError("CANOPY preview cycle is not exactly antisymmetric")
            lines.append(
                "        {" + ",".join(str(sample) for sample in row[:37]) + "},"
            )
        lines.append("    },")
    return "\n".join(
        lines
        + [
            "};",
            "static inline int canopy_preview_value(unsigned table,unsigned frame,unsigned index)",
            "{",
            "    return index<37 ? canopy_preview_waves[table][frame][index] :",
            "        -(int)canopy_preview_waves[table][frame][73-index];",
            "}",
            "",
        ]
    )


def main() -> None:
    module = Path(__file__).resolve().parents[1]
    root = module.parents[2]
    target = module / "src/dsp/wavetables.s"
    target.write_text(assembly(), encoding="utf-8", newline="\n")
    preview = root / "runtime/platform/instrument-play/src/cf/src-waves.h"
    preview.write_text(preview_header(), encoding="utf-8", newline="\n")
    print(
        f"{target}: {len(half_words()) * 4} stored bytes for {len(TABLE_NAMES) * TABLE_WORDS * 2} table bytes"
    )


if __name__ == "__main__":
    main()
