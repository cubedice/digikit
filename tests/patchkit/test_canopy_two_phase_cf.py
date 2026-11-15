"""ColdFire-only bounded two-phase publication experiment.

The injected 'IRQ' is a synchronous callback in a disposable UI-task run;
instruction intervals are not cycle counts or real hardware latency bounds.
"""
import hashlib
import json
import os
import re
import subprocess
from pathlib import Path

import pytest
from tools.patchkit import patchkit, port_resolve

ROOT = Path(__file__).resolve().parents[2]
FW = ROOT / "firmware/Digitakt_II_OS1.17.syx"
FIXTURE = ROOT / "tests/regression/fixtures/canopy-two-phase"
ADAPTER = ROOT / "modules/core/adapters/instrument-play/src/cf"
BASE = 0x40000400


def mainloop(image):
    # Same masked main-loop signature used by the emulator profile; require a
    # unique match on the pinned image before queuing a disposable guest call.
    sig = bytes.fromhex("48794094ef3c4eb940001928588f722824407192b28065e8")
    mask = [False] * len(sig)
    i = 0
    while i + 4 <= len(sig):
        v = int.from_bytes(sig[i:i + 4], "big")
        if BASE <= v < 0x48000000:
            mask[i:i + 4] = [True] * 4
            i += 4
        else:
            i += 1
    mask[15] = True
    pattern = b"".join(b"." if skip else re.escape(bytes([v])) for v, skip in zip(sig, mask))
    found = list(re.finditer(pattern, image, re.DOTALL))
    assert len(found) == 1
    return BASE + found[0].start()


def test_two_phase_commit_interval_on_coldfire(tmp_path):
    runner = Path(os.environ.get("DIGIKIT_PANEL_DRIVE", ROOT / "out/native/target/nogen/release/examples/panel_drive"))
    if not runner.exists():
        pytest.skip("panel_drive must be built explicitly")
    assert hashlib.sha256(FW.read_bytes()).hexdigest() == "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"
    image = port_resolve.sections_of(FW)[3]
    loop = mainloop(image)
    linked = patchkit.cf_link(
        [FIXTURE / "two-phase.c", FIXTURE / "cf-probe.c", FIXTURE / "cf-probe.s", ADAPTER / "sound-pool.c"],
        0x47E00000, {"memset": 0x401363D0, "memcpy": 0x40136388},
    )
    mask = linked.symbols["tp_probe_mask_applied"]
    restore = linked.symbols["tp_probe_restore_before"]
    result_addr = linked.symbols["tp_probe_result"]
    steps = tmp_path / "steps.txt"
    blob = linked.image + bytes(linked.bss_size)
    steps.write_text(f"poke:{linked.base:#x}:{blob.hex()}\nsend:00\npeek:{result_addr:#x}:32\n")
    proc = subprocess.run([
        str(runner), str(FW), "--after", "0", "--max", "2000000000", "--steps", "@" + str(steps),
        "--regs-at", f"{mask:#x},{restore:#x}", "--call-at", f"{loop:#x}",
        "--call-fn", f"{linked.symbols['tp_probe_run']:#x}",
    ], text=True, capture_output=True, timeout=180)
    assert proc.returncode == 0, proc.stderr + proc.stdout[-4000:]
    result = json.loads(proc.stdout.splitlines()[-1])
    assert result["outcome"] == "done" and not result["regs_dropped"], result
    peek = next(x for x in result["results"] if "peek" in x)
    words = [int(peek["hex"][i:i + 8], 16) for i in range(0, 64, 8)]
    assert words[:5] == [0x600D, 0, 90, 93, 1], words
    events = [(int(x["pc"], 16), x["icount"]) for x in result["regs"]]
    assert [x[0] for x in events] == [mask, restore] * 3, events
    intervals = [events[i + 1][1] - events[i][1] for i in range(0, len(events), 2)]
    # Interval 0: snapshot, 1: simulated IRQ, 2: four-entry commit. The
    # interval runs from immediately after masking to just before restore.
    assert all(0 < n < 2000 for n in intervals), intervals
    print("ColdFire masked instruction intervals (preflight, simulated IRQ, commit):", intervals)
