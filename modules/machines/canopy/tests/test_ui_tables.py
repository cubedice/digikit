"""The SRC graphics must depict this instrument's actual source data."""

from pathlib import Path

from modules.machines.canopy.tools.ui_tables import header as envelope_preview_header
from modules.machines.canopy.tools.wavetables import preview_header


def test_src_preview_tables_match_factory_wave_and_envelope_data():
    module = Path(__file__).resolve().parents[1]
    root = module.parents[2]
    ui = root / "modules/core/adapters/instrument-play/src/cf"
    voice = module / "src/dsp/voice.s"
    assert (ui / "src-waves.h").read_text(encoding="utf-8") == preview_header()
    assert (ui / "src-envelope.h").read_text(
        encoding="utf-8"
    ) == envelope_preview_header(voice.read_text(encoding="utf-8"))
