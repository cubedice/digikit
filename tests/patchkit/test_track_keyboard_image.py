"""The track keyboard is part of an image on its own (docs/plans/fork-image-diet.md section 9): it links without core/instrument-play
(CANOPY's host), starts through its image tick, and the KEYBOARD view handler it plays through is core/keyboard-panel's."""

import json
import subprocess

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit

FIRMWARE = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
TICK_KIND = 3


def plan_of(tmp_path, modules):
    if not FIRMWARE.exists():
        pytest.skip("DTII 1.17 firmware required")
    out = tmp_path / "image.syx"
    subprocess.run(
        [
            str(patchkit.binary()),
            "--root",
            str(REPO_ROOT),
            "apply",
            "--firmware",
            str(FIRMWARE),
            "--modules",
            ",".join(modules),
            "-o",
            str(out),
            "--plan",
            str(out.with_suffix(".plan.json")),
        ],
        check=True,
        capture_output=True,
    )
    return json.loads(out.with_suffix(".plan.json").read_text())


def ids(plan):
    return {m.split("@", 1)[0] for m in plan["modules"]}


def test_the_track_keyboard_links_without_canopys_host_and_has_its_tick(tmp_path):
    plan = plan_of(
        tmp_path, ["core/track-keyboard", "core/digikit-menu", "core/health"]
    )
    assert "core/instrument-play" not in ids(plan)
    assert {"core/keyboard-panel", "core/midi-events"} <= ids(plan)
    ticks = [i["name"] for i in plan["info"] if i["kind"] == TICK_KIND]
    assert ticks == ["tick_track_keyboard"]
    # no service is asked for: the image runs the track keyboard by itself
    assert not [i["name"] for i in plan["info"] if i["name"].startswith("boot_need_")]


def test_the_hardware_set_has_the_track_keyboard_and_canopys_host(tmp_path):
    sets = (REPO_ROOT / "devices/digitakt-ii.toml").read_text()
    line = next(
        row for row in sets.splitlines() if row.startswith("canopy-overloop-prism = ")
    )
    plan = plan_of(tmp_path, json.loads(line.split("=", 1)[1]))
    assert {
        "core/track-keyboard",
        "core/keyboard-panel",
        "core/instrument-play",
    } <= ids(plan)
    used, size = plan["regions"]["cf.expansion"]
    assert size - used > 64 * 1024, (
        "less than 64 KiB of cf.expansion left before the upstream merge"
    )
