"""One-second loop specification, with metadata separate from owned audio BSS.

The DSP clears STATE_BYTES on activation/fault. Audio is intentionally not
cleared: the reset mode makes it unreadable until a full new capture finishes.
"""

from dataclasses import dataclass

from tools.patchkit import short_loop_ref as short

FS = short.FS
BLOCK = short.BLOCK
FRAMES = 48_000
META_WORDS = short.META_WORDS
STATE_BYTES = META_WORDS * 4
AUDIO_BYTES = FRAMES * 2 * 4
AUDIO_ADDRESS_WORD = 7
FRAME_COUNT_WORD = 8
IDLE, CAPTURING, FROZEN = short.IDLE, short.CAPTURING, short.FROZEN
f32 = short.f32
float_word = short.float_word


def start_frame(value: float) -> int:
    return short.start_frame(value, FRAMES)


def seam_gain(phase: int, at: int) -> float:
    return short.seam_gain(phase, at, FRAMES)


@dataclass
class LongLoop(short.ShortLoop):
    frames: int = FRAMES

    def state_words(self, audio_address: int = 0) -> list[int]:
        """Descriptor state only; words 7 and 8 expose the relocated allocation."""
        words = self.metadata_words() + [audio_address, self.frames]
        return words + [0] * (META_WORDS - len(words))

    def audio_words(self) -> list[int]:
        return [float_word(x) for x in self.left + self.right]
