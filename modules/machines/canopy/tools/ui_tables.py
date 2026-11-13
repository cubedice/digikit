"""Generate integer envelope timing weights for CANOPY's SRC diagrams."""

from __future__ import annotations

import math
import re
import struct
from pathlib import Path


def header(voice: str) -> str:
    table = voice.split(".GLOBAL _cp_envelopes;", 1)[1]
    words = re.findall(r"\.VAR _cp_(?:envelopes|env_\d+) = 0x([0-9a-fA-F]{8});", table)[
        :256
    ]
    if len(words) != 256:
        raise ValueError("expected 128 attack/release pairs")
    weights = [[], []]
    for index, word in enumerate(words):
        step = abs(struct.unpack(">f", bytes.fromhex(word))[0])
        weights[index % 2].append(round(256 * math.sqrt(1 / (48000 * step))))
    lines = [
        "/* Generated from CANOPY's envelope increments by ui_tables. */",
        "static const uint16_t canopy_env_weight[2][128]={",
    ]
    for row in weights:
        lines.append("    {")
        for start in range(0, 128, 16):
            lines.append("        " + ",".join(map(str, row[start : start + 16])) + ",")
        lines.append("    },")
    return "\n".join(lines + ["};", ""])


def main() -> None:
    root = Path(__file__).resolve().parents[4]
    voice = Path(__file__).resolve().parents[1] / "src/dsp/voice.s"
    target = root / "modules/core/adapters/instrument-play/src/cf/src-envelope.h"
    target.write_text(
        header(voice.read_text(encoding="utf-8")), encoding="utf-8", newline="\n"
    )


if __name__ == "__main__":
    main()
