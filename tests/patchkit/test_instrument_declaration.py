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
