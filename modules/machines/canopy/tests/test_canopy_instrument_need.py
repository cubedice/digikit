"""CANOPY needs services/instrument.dks: core/instrument-play, which it requires, runs its host from that file
(`served_by = "instrument"`, docs/plans/fork-image-diet.md section 9), so the packer gives the package a hard need on it."""

import json

from canopy_paths import MODULE

from tools.patchkit import patchkit


def test_the_canopy_package_needs_the_instrument_service(tmp_path):
    patchkit.build_module(MODULE)
    output = tmp_path / "canopy.dkm"
    patchkit.run(
        ["pack", "digikit/canopy", "--device", "dt2", "-o", str(output)], capture=True
    )
    result = patchkit.run(["services", "--device", "dt2", str(output)], capture=True)
    services = {
        json.loads(line)["service"]
        for line in result.stdout.splitlines()
        if line.strip()
    }
    assert {"instrument", "machines"} <= services
