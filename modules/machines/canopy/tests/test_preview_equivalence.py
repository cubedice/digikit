"""The compact SRC table retains every original value and waveform pixel."""

import hashlib
import subprocess
from pathlib import Path

from modules.machines.canopy.tools.wavetables import preview_values


def test_compiled_preview_preserves_original_table_and_every_position(tmp_path):
    root = Path(__file__).resolve().parents[4]
    ui = root / "modules/core/adapters/instrument-play/src/cf"
    source = Path(__file__).with_name("preview-equivalence.c")
    binary = tmp_path / "preview-equivalence"
    subprocess.run(
        [
            "cc",
            "-std=c11",
            "-Wall",
            "-Wextra",
            "-Werror",
            "-I",
            str(ui),
            str(source),
            "-o",
            str(binary),
        ],
        check=True,
        timeout=20,
    )
    actual = subprocess.run(
        [str(binary)], check=True, capture_output=True, timeout=20
    ).stdout
    waves = preview_values()
    original = bytes(
        value & 255 for table in waves for frame in table for value in frame
    )
    # This digest was taken from the original uncompressed checked-in table.
    assert hashlib.sha256(original).hexdigest() == (
        "d3397a866a484ea0d5e0ee5f143e312de5941561247d06b36c56beb628c76fc9"
    )
    assert actual[: len(original)] == original

    pixels = bytearray()
    for table in waves:
        for position in range(701):
            frame, fraction = divmod(position, 100)
            other = min(frame + 1, 7)
            values = [
                int((a * (100 - fraction) + b * fraction) / 100)
                for a, b in zip(table[frame], table[other], strict=True)
            ]
            peak = max(1, *(abs(value) for value in values))
            for value in values:
                magnitude = (6 * abs(value) + peak // 2) // peak
                pixels.append(20 + (magnitude if value < 0 else -magnitude))
    assert actual[len(original) :] == pixels
