"""Compile a real ColdFire V2 declaration and inspect it through patchkit."""

import json
from pathlib import Path

from tools.patchkit import patchkit

ROOT = Path(__file__).resolve().parents[2]


def test_v2_c_declaration_is_inspectable_but_not_packable(tmp_path):
    patchkit.build_module(ROOT / "tests/fixtures/modules/instrument-declaration")
    result = patchkit.run(["desc", "test/instrument-declaration"], capture=True)
    assert result.returncode == 0, result.stderr
    declaration = json.loads(result.stdout)
    assert declaration["instruments"] == [
        {
            "id": "canopy",
            "name": "CANOPY",
            "short": "CANOPY",
            "reset": "_canopy_reset",
            "process": "_canopy_process",
            "desc": "_canopy_desc",
        }
    ]
    assert declaration["machines"] == declaration["voices"] == []
    output = tmp_path / "must-not-exist.dkm"
    result = patchkit.run(
        ["pack", "test/instrument-declaration", "--device", "dt2", "-o", str(output)],
        capture=True,
    )
    assert result.returncode != 0
    assert "requires V2 registration and runtime capability" in result.stderr
    assert not output.exists()


def test_canopy_package_keeps_v2_callbacks_separate_from_v1(tmp_path):
    patchkit.build_module(ROOT / "modules/machines/canopy")
    output = tmp_path / "canopy.dkm"
    result = patchkit.run(
        ["pack", "digikit/canopy", "--device", "dt2", "-o", str(output)], capture=True
    )
    assert result.returncode == 0, result.stderr
    file = output.read_bytes()

    def word(at):
        return int.from_bytes(file[at : at + 4], "big")

    assert file[0x3A:0x3C] == bytes([2, 0])
    at = word(0x98)
    assert word(at + 4) == 6
    assert word(at + 0x30) == 0  # no V1 voices
    assert word(at + 0x44) == 1  # one V2 instrument
    assert word(word(0x90) + 36) == 0  # DT2 has no DN2 own-page flag
    assert word(0x94) == 124  # one CF machine registration
    assert word(0x7C) == 0  # no CF imports
