"""CANOPY as the DSP consumer of core/midi-events' composed block frame.

The frame runner is shared (tests/support/midi_frame.py); this module supplies its
own DSP sources as the consumer and, for the carrier case, its carrier preparation.
"""

import pytest
from canopy_paths import DSP
from support.midi_frame import MODES, composed_frame

CONSUMER = tuple(
    DSP / f"{name}.s"
    for name in (
        "voice",
        "tuning",
        "events",
        "instrument",
        "wavetable",
        "wavetables",
        "fm",
        "fm-tables",
    )
)


@pytest.mark.parametrize("mode", MODES)
@pytest.mark.parametrize("carrier", [False, True])
@pytest.mark.parametrize("valid_position", [False, True])
def test_canopy_composed_frame_preserves_context(mode, carrier, valid_position):
    composed_frame(
        mode,
        valid_position,
        consumer=CONSUMER,
        carrier=(DSP / "carrier-prepare.s",) if carrier else (),
    )
