"""Prepare a private OS 1.17 task-boundary observer for the Vitest MIDI proof.

Build separately from device tests. Output contains firmware tail bytes and must
stay under ignored out/. No installable module or readiness provider is created.
"""

import hashlib
import json
from pathlib import Path

from tools.patchkit import patchkit, port_resolve

ROOT = Path(__file__).resolve().parents[2]
SHA = "26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5"


def main():
    firmware = ROOT / "firmware/Digitakt_II_OS1.17.syx"
    if hashlib.sha256(firmware.read_bytes()).hexdigest() != SHA:
        raise ValueError("observer requires the pinned DT2 1.17 image")
    image = port_resolve.sections_of(firmware)[3]
    # Inside the port's reserved cf.expansion region, away from stock heap/state.
    base, usb_tail, din_tail = 0x47E8B000, 0x47E9B000, 0x47E9B020
    linked = patchkit.cf_link(
        [
            ROOT / "tests/support/midi_task_observer.c",
            ROOT / "tests/support/midi_task_observer.s",
        ],
        base,
        {"mi_observe_usb_tail": usb_tail, "mi_observe_din_tail": din_tail},
    )
    if len(linked.image) + linked.bss_size >= usb_tail - base:
        raise ValueError("observer overlaps its continuation storage")

    def jump(address):
        return bytes.fromhex("4ef9") + address.to_bytes(4, "big")

    writes = [{"addr": base, "hex": (linked.image + bytes(linked.bss_size)).hex()}]
    for site, tail, symbol in (
        (0x4011F1E2, usb_tail, "mi_observe_usb"),
        (0x4011F178, din_tail, "mi_observe_din"),
    ):
        original = image[site - 0x40000400 : site - 0x40000400 + 6]
        writes.append({"addr": tail, "hex": (original + jump(site + 6)).hex()})
        writes.append({"addr": site, "hex": jump(linked.symbols[symbol]).hex()})
    output = ROOT / "out/canopy-midi-observer.json"
    output.write_text(
        json.dumps({"syx_sha256": SHA, "symbols": linked.symbols, "writes": writes})
    )
    print(output)


if __name__ == "__main__":
    main()
