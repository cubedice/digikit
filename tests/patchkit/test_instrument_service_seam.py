"""The seam between the image adapters core/instrument-play, core/midi-events and services/instrument.dks
(docs/plans/fork-image-diet.md): the generated files are current, every forwarder is a symbol of the image object, and a
module that requires a served adapter needs the service file while one that does not stays without it."""

import json
import sys
import subprocess
import tomllib

import pytest
from support.paths import REPO_ROOT

from tools.patchkit import patchkit

SEAM = REPO_ROOT / "modules/core/services/instrument/seam.toml"
FIRMWARE = REPO_ROOT / "firmware/Digitakt_II_OS1.17.syx"


def spec():
    return tomllib.loads(SEAM.read_text())


def test_the_generated_seam_files_are_current():
    r = subprocess.run(
        [sys.executable, "-m", "modules.core.services.instrument.tools.seam", "--check"],
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
        "core/instrument-play": "modules/core/adapters/instrument-play/build/play-cf.dko.json",
        "core/midi-events": "modules/core/adapters/midi-events/build/midi-events-cf.dko.json",
    }
    for vec in spec()["vector"]:
        names = [n for n, _ in vec["entries"]]
        assert len(names) == len(set(names)), vec["slot"]
        have = defined(objs[vec["adapter"]])
        assert set(names) <= have, sorted(set(names) - have)
    # the image table's own words are defined by the image (the others are optional, or another adapter's)
    img = spec()["image"]
    have = defined(objs["core/instrument-play"])
    own = [n for n in img["functions"] if n.startswith(("cp_", "sr_", "sa_", "ip_"))]
    assert set(own) <= have, sorted(set(own) - have)


def test_a_module_that_requires_a_served_adapter_needs_the_service(tmp_path):
    if not FIRMWARE.exists():
        pytest.skip("DTII 1.17 firmware required")
    pk = str(patchkit.binary())

    def services(module):
        out = tmp_path / module.split("/")[-1]
        out.mkdir()
        subprocess.run([pk, "--root", str(REPO_ROOT), "pack-all", "--device", "dt2", "--out", str(out), module], check=True, capture_output=True)
        dkm = next(out.glob("*.dkm"))
        r = subprocess.run([pk, "--root", str(REPO_ROOT), "services", "--device", "dt2", str(dkm)], check=True, capture_output=True, text=True)
        return {json.loads(line)["service"] for line in r.stdout.splitlines() if line.strip()}

    # CANOPY requires core/instrument-play; RELAY reaches it through core/relay-track
    assert "instrument" in services("digikit/canopy")
    assert "instrument" in services("digikit/relay-machine")
    assert "instrument" not in services("digikit/overloop")
