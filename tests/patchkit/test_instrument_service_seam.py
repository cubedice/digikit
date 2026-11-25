"""The seam between the image adapters core/instrument-play, core/midi-events, core/keyboard-panel and services/instrument.dks
(docs/plans/fork-image-diet.md): the generated files are current and every forwarder is a symbol of its image object. That CANOPY
needs the service is a test in the module's own directory."""

import json
import subprocess
import sys
import tomllib

from support.paths import REPO_ROOT

SEAM = REPO_ROOT / "services/instrument/seam.toml"
FIRMWARE = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"


def spec():
    return tomllib.loads(SEAM.read_text())


def test_the_generated_seam_files_are_current():
    r = subprocess.run(
        [
            sys.executable,
            "-m",
            "services.instrument.tools.seam",
            "--check",
        ],
        cwd=REPO_ROOT,
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, r.stdout + r.stderr


def defined(obj):
    d = json.loads((REPO_ROOT / obj).read_text())
    return {s["name"] for s in d["symbols"] if s.get("section") and s.get("global")}


def test_each_forwarder_is_in_its_image_object_and_no_moved_function_stays_there():
    objs = {
        "core/instrument-play": "image-modules/adapters/instrument-play/build/play-cf.dko.json",
        "core/midi-events": "image-modules/adapters/midi-events/build/midi-events-cf.dko.json",
        "core/keyboard-panel": "image-modules/adapters/keyboard-panel/build/keyboard-panel-cf.dko.json",
    }
    for vec in spec()["vector"]:
        names = [e[0] for e in vec["entries"]]
        assert len(names) == len(set(names)), vec["slot"]
        have = defined(objs[vec["adapter"]])
        assert set(names) <= have, sorted(set(names) - have)
    # the image table's own words are defined by the image (the others are optional, or another adapter's)
    img = spec()["image"]
    have = defined(objs["core/instrument-play"]) | defined(objs["core/keyboard-panel"])
    own = [n for n in img["functions"] if n.startswith(("cp_", "sr_", "sa_", "ip_"))]
    assert set(own) <= have, sorted(set(own) - have)
