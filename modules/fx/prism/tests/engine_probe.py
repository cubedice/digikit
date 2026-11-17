"""JSON probe for the actual PRISM SHARC entry through the production FX guard.

The colocated Vitest suite owns scenarios and assertions. This driver only
translates bounded input actions into concrete-core calls and observations.
"""

from __future__ import annotations

import json
import math
import sys
from pathlib import Path

from support.fx_rig import NFAULT, FxRig, Row, f2w, nw, w2f

from tools.patchkit.pk_sharc import SW_ALIAS_BASE
from tools.sharc_dev.sharc_core.addressing import byte_to_normal_word

HERE = Path(__file__).parent
SOURCES = tuple(
    HERE.parent / "src/dsp" / name
    for name in ("prism.s", "capture.s", "tape.s", "grain.s", "space.s")
)
BANKS = (0x9AA2B9C8, 0x9B22B9C8)
FRAMES = 512
LEASE = 8192
LIVE = (0.625,) * 32 + (-0.375,) * 32


def external_word(address: int) -> int:
    value = byte_to_normal_word(address)
    if value is None:
        raise ValueError("address outside the synthetic external lease")
    return value


class Probe(FxRig):
    def __init__(self):
        super().__init__(
            [Row("_prism_process", "", desc="_prism_desc")],
            sources=(*SOURCES, HERE / "provider.s"),
            record_stores=True,
        )
        self.phase = self.beat = self.sequence = self.command = 0
        self.control = [0] * 64
        self.control[0:3] = [0x50484331, 2, 256]
        self.control[4] = self.control[8] = 1
        self.control[10:14] = [1, 1, 1, 7]
        self.control[17] = 16
        self.control[22] = f2w(1)
        self.control[23] = 240
        self.control[24] = 16384
        self.control[25] = f2w(1)
        self.control[34] = f2w(0.7)
        self.control[36] = f2w(1)
        self.control[56] = 7
        self.control[57] = 14400
        # The tape head is off unless a case turns the balance (word 60, bits 22..31) away from "grains only".
        self.control[60] = 511 << 22
        for bank in BANKS:
            word = external_word(bank)
            for index in range(FRAMES * 2):
                self.sess.poke(word + index, 0x7FC00001)
            self.sess.poke(word + LEASE // 4 - 1, 0x5A17B00F)

    def allowed(self, stack_bytes):
        allowed = super().allowed(stack_bytes)
        for symbol, size in (
            ("_phrase_storage_info", 24),
            ("_phrase_control_info", 256),
        ):
            first = self.built.data[symbol]
            split = []
            for at, length, name in allowed:
                if at <= first and first + size <= at + length:
                    if at < first:
                        split.append((at, first - at, name))
                    if first + size < at + length:
                        split.append((first + size, at + length - first - size, name))
                else:
                    split.append((at, length, name))
            allowed = split
        return allowed + [(bank, LEASE, "leased phrase bank") for bank in BANKS]

    def foreign_stores(self, stack_bytes):
        allowed = self.allowed(stack_bytes)
        errors = []
        for event in self.sess.stores:
            if event.access != "write" or not any(
                lo <= event.pc_sw < hi for lo, hi in self.built.module_code
            ):
                continue
            at = event.address
            if SW_ALIAS_BASE <= at < 0x80000000:
                at -= SW_ALIAS_BASE
            if not any(
                lo <= at and at + event.width <= lo + size for lo, size, _ in allowed
            ):
                errors.append(f"{at:#x} from {event.pc_sw:#x}")
        return errors[:4]

    def observe(self, command):
        for field in ("phase", "beat"):
            if field in command:
                value = int(command[field])
                if not 0 <= value <= 0xFFFFFFFF:
                    raise ValueError("synthetic clock must fit an unsigned32-bit phase")
                setattr(self, field, value)
        for index, value in command.get("set", {}).items():
            self.control[int(index)] = int(value) & 0xFFFFFFFF
        for index, value in command.get("storage", {}).items():
            self.sess.poke(
                nw(self.built.data["_phrase_storage_info"]) + int(index), int(value)
            )
        if "action" in command:
            self.command += 1
            self.control[5], self.control[6] = self.command, command["action"]
        diagnostics = []
        peak = 0.0
        finite = True
        maximum = 0
        count = command.get("repeat", 1)
        if not 1 <= count <= 2048:
            raise ValueError("probe commands are bounded to1..2048blocks")
        for _ in range(count):
            self.sequence += 1
            self.control[3] = self.control[62] = self.sequence
            self.control[14] = self.phase & 0xFFFFFFFF
            self.control[15] = self.beat & 0xFFFFFFFF
            self.control[16] = (self.phase >> 16) % self.control[17]
            self.control[18] = (self.phase >> 16) + 1
            base = nw(self.built.data["_phrase_control_info"])
            for index, value in enumerate(self.control):
                self.sess.poke(base + index, value & 0xFFFFFFFF)
            samples = command.get("samples", LIVE)
            if isinstance(samples, (int, float)):
                samples = [samples] * 64
            protected = [self.state_word(0, 11)]
            if self.state_word(0, 19) > 1:
                protected.append(self.state_word(0, 15))
            if command.get("action") == 3:
                protected = []  # CLEAR explicitly releases every reader.
            protected = [bank for bank in protected if bank in BANKS]
            result = self.block(
                samples, ctx=None if command.get("hostile") else "friendly"
            )
            maximum = max(maximum, result.instructions)
            output = self.scratch()
            finite = finite and all(math.isfinite(value) for value in output)
            peak = max(
                peak,
                max(
                    (abs(value) for value in output if math.isfinite(value)), default=0
                ),
            )
            errors = self.foreign_stores(0)
            for event in self.sess.stores:
                if event.access == "write" and any(
                    bank <= event.address < bank + LEASE for bank in protected
                ):
                    errors.append(f"write to a reader-owned bank at {event.address:#x}")
                    break
            if not result.returned or result.ctx_bad or result.regs_bad or errors:
                diagnostics.append(
                    {
                        "returned": result.returned,
                        "halt": result.halt,
                        "context": result.ctx_bad,
                        "registers": result.regs_bad,
                        "stores": errors,
                    }
                )
            self.phase += command.get("advance", 16384)
            self.beat += command.get("beatAdvance", 4096)
        state = [self.state_word(0, index) for index in range(64)]
        status = [self.symbol_word("_phrase_status", index) for index in range(32)]
        observations = {
            "state": state,
            "status": status,
            "peak": peak,
            "finite": finite,
            "instructions": maximum,
            "faults": self.row_word(0, NFAULT),
            "diagnostics": diagnostics,
            "outputBits": [f2w(value) for value in output],
            "tape": [self.state_word(0, 1296 + index) for index in range(23)],
            "head": [w2f(self.state_word(0, 1328 + index)) for index in range(64)],
            "grains": [
                [self.state_word(0, 128 + 16 * slot + index) for index in range(16)]
                for slot in range(8)
            ],
        }
        if command.get("audio") and state[11] and state[13]:
            bank = external_word(state[11])
            start = state[12]
            observations["audio"] = [
                [
                    w2f(
                        self.sess.peek(
                            bank + ((start + frame) % FRAMES) + channel * FRAMES
                        )
                        or 0
                    )
                    for frame in range(state[13])
                ]
                for channel in (0, 1)
            ]
        return observations


def main():
    request = json.load(sys.stdin)
    result = {}
    for case in request["cases"]:
        probe = Probe()
        result[case["name"]] = {
            "budget": probe.desc_words("_prism_desc")[4],
            "steps": [probe.observe(command) for command in case["commands"]],
        }
    json.dump(result, sys.stdout, allow_nan=False)


if __name__ == "__main__":
    main()
