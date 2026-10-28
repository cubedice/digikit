"""Explicit conformance fixture for michael/bar-loop's required storage import.

This supplies test memory, not proof of firmware ownership. Unknown imports
still fail resolution. The provider record is read-only; only the audio and its
32-byte canary are writable, despite the descriptor advertising an 8 MiB lease.
"""

from pathlib import Path

from support.fx_rig import FxRig, Row
from support.paths import REPO_ROOT
from tools.patchkit import bar_loop_ref as R
from tools.patchkit.pk_sharc import SW_ALIAS_BASE
from tools.sharc_dev.sharc_core.addressing import byte_to_normal_word
from tools.sharc_dev.sharc_run import Watchpoint

MODULE_ID = "michael/bar-loop"
SOURCE = REPO_ROOT / "modules/fx/bar-loop/src/dsp/bar_loop.s"
PROVIDER_SOURCE = Path(__file__).with_suffix(".s")
BASE = 0x9CA2B9C8
CAPACITY = 0x00800000


def external_word(byte_address: int) -> int:
    """External normal-word addresses are not simply byte_address // 4."""
    address = byte_to_normal_word(byte_address)
    if address is None:
        raise ValueError(f"no normal-word alias for {byte_address:#x}")
    return address


class BarLoopRig(FxRig):
    """Allow exactly the test lease, and keep the provider record read-only."""

    def __init__(
        self,
        rows=None,
        *,
        sources=(SOURCE,),
        seed=0xD1617,
        record_stores=False,
        watch_external_reads=False,
        lease_base=BASE,
    ):
        self.lease_base = lease_base
        super().__init__(
            [Row("_bl_process", "", desc="_bl_desc")] if rows is None else rows,
            sources=(*sources, PROVIDER_SOURCE),
            seed=seed,
            record_stores=record_stores or watch_external_reads,
        )
        if watch_external_reads:
            self.sess.runner.attach_watchpoints(
                [
                    Watchpoint(0, 1 << 40, on_read=False, stop=False, label="stores"),
                    Watchpoint(
                        R.EXTERNAL_START,
                        R.EXTERNAL_END,
                        on_write=False,
                        stop=False,
                        label="external reads",
                    ),
                ]
            )

    def allowed(self, stack_bytes):
        allowed = super().allowed(stack_bytes)
        for symbol, size in (("_loop_buffer_info", 16), ("_pattern_position_info", 80)):
            info = self.built.data[symbol]
            split = []
            for a, n, name in allowed:
                if a <= info and info + size <= a + n:
                    if a < info:
                        split.append((a, info - a, name))
                    if info + size < a + n:
                        split.append((info + size, a + n - info - size, name))
                else:
                    split.append((a, n, name))
            allowed = split
        return allowed + [
            (self.lease_base, R.STORAGE_BYTES, "explicit test audio and canary lease")
        ]

    def foreign_stores(self, stack_bytes):
        # Preserve actual external byte addresses. The historical base rig's
        # unconditional loader-alias subtraction applies only to L1/L2 here.
        allowed = self.allowed(stack_bytes)
        bad = []
        for event in self.sess.stores:
            if event.access != "write" or not any(
                lo <= event.pc_sw < hi for lo, hi in self.built.module_code
            ):
                continue
            address = event.address
            if SW_ALIAS_BASE <= address < R.EXTERNAL_START:
                address -= SW_ALIAS_BASE
            if not any(
                lo <= address and address + event.width <= lo + n
                for lo, n, _name in allowed
            ):
                bad.append(
                    f"store of {event.width} bytes to {address:#x} at pc {event.pc_sw:#x}"
                )
            if len(bad) == 4:
                break
        return bad

    def external_events(self):
        return [
            event
            for event in self.sess.stores
            if R.EXTERNAL_START <= event.address < R.EXTERNAL_END
        ]

    def audio_events(self):
        return [
            event
            for event in self.external_events()
            if self.lease_base <= event.address < self.lease_base + R.AUDIO_BYTES
        ]

    def canary_events(self):
        return [
            event
            for event in self.external_events()
            if self.lease_base + R.AUDIO_BYTES
            <= event.address
            < self.lease_base + R.STORAGE_BYTES
        ]
