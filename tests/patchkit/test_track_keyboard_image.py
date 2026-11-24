"""The track keyboard is part of an image on its own (docs/plans/fork-image-diet.md sections 9.1 and 9.2): it links without
core/instrument-play (CANOPY's host), starts through its image tick, the KEYBOARD view handler it plays through is
core/keyboard-panel's, and its player is services/track-keyboard.dks, which the image asks the boot for and which needs no service
of CANOPY's."""

import json
import subprocess
import tomllib

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit

FIRMWARE = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"
TICK_KIND = 3
SEAM = REPO_ROOT / "modules/core/services/track-keyboard/seam.toml"
IMAGE_OBJECT = (
    REPO_ROOT / "modules/core/adapters/track-keyboard/build/track-keyboard.dko.json"
)
SERVICE_OBJECT = (
    REPO_ROOT / "modules/core/services/track-keyboard/dks/build/track-keyboard.dko.json"
)


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


def defined(obj):
    d = json.loads(obj.read_text())
    return {s["name"] for s in d["symbols"] if s.get("section") and s.get("global")}


def test_the_track_keyboard_links_without_canopys_host_and_has_its_tick(tmp_path):
    plan = plan_of(
        tmp_path, ["core/track-keyboard", "core/digikit-menu", "core/health"]
    )
    assert "core/instrument-play" not in ids(plan)
    assert {"core/keyboard-panel", "core/midi-events"} <= ids(plan)
    ticks = [i["name"] for i in plan["info"] if i["kind"] == TICK_KIND]
    assert ticks == ["tick_track_keyboard"]
    # the player is in services/track-keyboard.dks: the image asks the boot for that file and no other, and binds it through tk_image
    infos = [i["name"] for i in plan["info"]]
    assert [n for n in infos if n.startswith("boot_need_")] == [
        "boot_need_track_keyboard"
    ]
    assert "tk_image" in infos


def test_each_forwarder_is_in_the_image_object_and_its_target_in_the_service():
    seam = tomllib.loads(SEAM.read_text())
    image, service = defined(IMAGE_OBJECT), defined(SERVICE_OBJECT)
    for vec in seam["vector"]:
        names = [e[0] for e in vec["entries"]]
        assert len(names) == len(set(names)), vec["slot"]
        assert set(names) <= image, sorted(set(names) - image)
        assert set(names) <= service, sorted(set(names) - service)
    # the track keyboard's own words of the image table are the image's (the others are the runtime's, the UI's, or optional)
    own = {n for n in seam["image"]["functions"] if n.startswith("tk_")}
    assert own <= image, sorted(own - image)


def test_the_service_file_needs_no_service_of_canopys(tmp_path):
    output = tmp_path / "track-keyboard.dks"
    patchkit.run(
        ["pack", "core/services/track-keyboard", "--device", "dt2", "-o", str(output)],
        capture=True,
    )
    result = patchkit.run(["services", "--device", "dt2", str(output)], capture=True)
    services = {
        json.loads(line)["service"]
        for line in result.stdout.splitlines()
        if line.strip()
    }
    assert "instrument" not in services
    assert "machines" not in services


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
    assert size - used > 128 * 1024, (
        "less than 128 KiB of cf.expansion left before the upstream merge"
    )
