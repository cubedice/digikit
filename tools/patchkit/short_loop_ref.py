"""Float32 specification for the 250 ms stereo short-loop proof.

Capture is pre-crossfade, starts on a REC rising edge, and freezes after exactly
12,000 frames. This model deliberately has no sequencer or kit integration: a
different input block represents a changed live source, not a firmware pattern
change. Restarting the effect is the only way to replace a frozen capture.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field

FS = 48_000
BLOCK = 32
FRAMES = 12_000
META_WORDS = 16
STATE_BYTES = (META_WORDS + 2 * FRAMES) * 4
SEAM_FRAMES = 16
GAIN_STEP = 1.0 / 256.0
IDLE, CAPTURING, FROZEN = 0, 1, 2


def f32(value: float) -> float:
    """Round one arithmetic result to the DSP's single precision."""
    return struct.unpack("<f", struct.pack("<f", value))[0]


def float_word(value: float) -> int:
    return struct.unpack("<I", struct.pack("<f", value))[0]


def clamp(value: float) -> float:
    return min(1.0, max(0.0, f32(value)))


def start_frame(value: float, frames: int = FRAMES) -> int:
    return int(f32(clamp(value) * f32(frames - 1)))


def seam_gain(phase: int, at: int, frames: int = FRAMES) -> float:
    """Window both the phase wrap and the physical capture join, without retiming."""
    distance = min(phase, frames - 1 - phase, at, frames - 1 - at)
    return f32(distance / SEAM_FRAMES) if distance < SEAM_FRAMES else 1.0


@dataclass
class ShortLoop:
    mode: int = IDLE
    written: int = 0
    phase: int = 0
    active_start: int = 0
    pending_start: int = 0
    gain: float = 0.0
    prev_rec: bool = False
    left: list[float] = field(default_factory=list)
    right: list[float] = field(default_factory=list)
    frames: int = FRAMES

    def __post_init__(self):
        if self.frames <= 0 or self.frames % BLOCK:
            raise ValueError(
                "loop frames must be a positive multiple of the block size"
            )
        if not self.left:
            self.left = [0.0] * self.frames
        if not self.right:
            self.right = [0.0] * self.frames
        if len(self.left) != self.frames or len(self.right) != self.frames:
            raise ValueError("both captured channels must match the loop length")

    def block(
        self, samples, rec: float = 0.0, start: float = 0.0, fade: float = 0.0
    ) -> tuple[float, ...]:
        if len(samples) != 2 * BLOCK:
            raise ValueError("a block is 32 left samples followed by 32 right samples")
        dry = tuple(f32(x) for x in samples)
        self.pending_start = start_frame(start, self.frames)
        high = f32(rec) >= 0.5
        if self.mode == IDLE and high and not self.prev_rec:
            self.mode = CAPTURING
        self.prev_rec = high

        if self.mode != FROZEN:
            self.gain = 0.0
            if self.mode == CAPTURING:
                end = self.written + BLOCK
                self.left[self.written : end] = dry[:BLOCK]
                self.right[self.written : end] = dry[BLOCK:]
                self.written = end
                if end == self.frames:
                    self.mode = FROZEN
                    self.phase = 0
                    self.active_start = self.pending_start
            return dry

        target = clamp(fade)
        out = [0.0] * (2 * BLOCK)
        for i in range(BLOCK):
            if self.gain < target:
                self.gain = min(target, f32(self.gain + GAIN_STEP))
            elif self.gain > target:
                self.gain = max(target, f32(self.gain - GAIN_STEP))
            at = (self.active_start + self.phase) % self.frames
            edge = seam_gain(self.phase, at, self.frames)
            for channel, buffer in enumerate((self.left, self.right)):
                j = i + channel * BLOCK
                wet = f32(buffer[at] * edge)
                if self.gain == 0.0:
                    out[j] = dry[j]
                elif self.gain == 1.0:
                    out[j] = wet
                else:
                    delta = f32(wet - dry[j])
                    out[j] = f32(dry[j] + f32(delta * self.gain))
            self.phase += 1
            if self.phase == self.frames:
                self.phase = 0
                self.active_start = self.pending_start
        return tuple(out)

    def metadata_words(self) -> list[int]:
        return [
            self.mode,
            self.written,
            self.phase,
            self.active_start,
            self.pending_start,
            float_word(self.gain),
            int(self.prev_rec),
        ]

    def state_words(self) -> list[int]:
        """Descriptor-owned state layout, useful for focused DSP playback tests."""
        metadata = self.metadata_words()
        metadata += [0] * (META_WORDS - len(metadata))
        return metadata + [float_word(x) for x in self.left + self.right]
