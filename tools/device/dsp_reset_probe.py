"""Explicit DTII DSP-only reset proof; requires the prepared image and probe DKM.

Read-only by default. --assert-reset stops audio and leaves DSP reset asserted;
restart the whole device afterwards. Never flashes, installs, or changes selection.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import struct
import time
from pathlib import Path

from tools.device.dkupload import RtMidiTransport, Uploader
from tools.patchkit.port_resolve import sections_of


def symbol(plan: dict, name: str) -> int:
    matches = {v for k, v in plan["symbols"].items() if k.endswith(":" + name)}
    if len(matches) != 1:
        raise ValueError(f"expected one {name} symbol")
    return matches.pop()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--firmware", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--assert-reset", action="store_true")
    args = parser.parse_args()
    plan = json.loads(args.firmware.with_suffix(".plan.json").read_text())
    if plan["base"]["device"] != "dt2" or plan["base"]["version"] != "1.17":
        raise ValueError("probe supports only DTII 1.17")
    if (
        hashlib.sha256(args.firmware.read_bytes()).hexdigest()
        != plan["output"]["syx_sha256"]
    ):
        raise ValueError("firmware hash does not match its plan")
    main_os = sections_of(args.firmware)[3]
    if main_os.count(b"DKIMGID1") != 1:
        raise ValueError("expected one runtime image identity")
    at = main_os.index(b"DKIMGID1") + 8
    image_id = main_os[at : at + 8].hex()
    expected_hook = bytes.fromhex("4ef9") + struct.pack(
        ">I", symbol(plan, "mi_dsp_reset_hook")
    )
    link = symbol(plan, "dl_link")
    hw = symbol(plan, "dl_reset_hw")
    report: dict = {
        "expected_image_id": image_id,
        "assert_requested": args.assert_reset,
    }
    transport = RtMidiTransport(args.port)
    try:
        up = Uploader(transport)
        report["info"] = info = up.info()
        if info["image_id"] != image_id or info["port"] != "dt2":
            raise ValueError("device is not running the prepared reset-proof image")
        if up.peek(0x400CD01C, 6) != expected_hook:
            raise ValueError("installed reset entry differs from the prepared hook")
        report["link_before"] = before = list(struct.unpack(">24I", up.peek(link, 96)))
        if args.assert_reset and (not (before[18] or before[20]) or before[21]):
            raise ValueError("test requires a used, not-yet-revoked DSP link")
        status, data = up.request(0x3F)
        if status or len(data) != 96:
            raise ValueError("DSP reset probe service is not loaded")
        report["before"] = list(struct.unpack(">24I", data))
        if not args.assert_reset:
            return
        # Busy SPI is mandatory; a refusal has made no device mutation. Bound
        # retries, rather than claim an idle reset exercised an in-flight frame.
        for _attempt in range(40):
            status, data = up.request(0x3F, b"RSET")
            if status != -78:
                break
            time.sleep(0.025)
        report["trigger_status"] = status
        if status or len(data) != 96:
            raise RuntimeError(f"reset trigger refused: {status}")
        report["trigger"] = list(struct.unpack(">24I", data))
        snapshots = []
        report["later"] = snapshots
        for _ in range(3):
            time.sleep(0.2)
            status, data = up.request(0x3F)
            if status or len(data) != 96:
                raise RuntimeError("lost diagnostic response after reset")
            snapshots.append(list(struct.unpack(">24I", data)))
        report["hardware"] = hardware = list(struct.unpack(">8I", up.peek(hw, 32)))
        report["link_after"] = after = list(struct.unpack(">24I", up.peek(link, 96)))
        if hardware[0] != 1 or not hardware[3] & 0x40000000:
            raise RuntimeError("quiescence or active-frame entry was not proven")
        if after[21] != 1 or after[2] != before[2] or after[18:21] != before[18:21]:
            raise RuntimeError("reset did not preserve revoked ownership")
        baseline = report["trigger"][12:14]
        for sample in snapshots:
            current = sample[16:24]
            mask = 2 if current[7] else 128
            if (
                current[0] & 1 != 1
                or current[1] & 0x40000000
                or (current[2] | current[3]) & 0x41
            ):
                raise RuntimeError("SPI or DMA restarted after reset")
            if current[4:6] != baseline or current[6] & mask:
                raise RuntimeError("DMA addresses moved or DSP reset pin is released")
        report["passed"] = True
        print(
            "PASS: active SPI stopped; DMA stayed stopped; DSP reset asserted. Whole-device restart required."
        )
    except Exception as exc:
        report["error"] = str(exc)
        raise
    finally:
        transport.close()
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps(report, indent=2) + "\n")


if __name__ == "__main__":
    main()
