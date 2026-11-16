"""This module's own directories, for its tests (pytest puts this directory on the path)."""

from pathlib import Path

# modules/<kind>/<name>: the module's manifest, sources and tools.
MODULE = Path(__file__).resolve().parents[1]
# The CANOPY DSP sources.
DSP = MODULE / "src/dsp"
