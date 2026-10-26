"""Eight-second capture model and explicit external-buffer provider contract.

The reference shares the float32 loop algorithm with the shorter proofs. It
does not establish ownership of RAM: the separately patched provider does.
"""

from dataclasses import dataclass

from tools.patchkit import short_loop_ref as short

FS = short.FS
BLOCK = short.BLOCK
FRAMES = 384_000
META_WORDS = short.META_WORDS
STATE_BYTES = META_WORDS * 4
AUDIO_BYTES = FRAMES * 2 * 4
CANARY_BYTES = 32
CANARY_WORD = 0x5A17B00F
STORAGE_BYTES = AUDIO_BYTES + CANARY_BYTES
AUDIO_ADDRESS_WORD = 7
FRAME_COUNT_WORD = 8
CANARY_INITIALIZED_WORD = 9
PROVIDER_MAGIC = 0x4C425546
PROVIDER_VERSION = 1
EXTERNAL_START = 0x80000000
EXTERNAL_END = 0xA0000000
IDLE, CAPTURING, FROZEN = short.IDLE, short.CAPTURING, short.FROZEN
f32 = short.f32
float_word = short.float_word


def provider_valid(magic: int, version: int, base: int, size: int) -> bool:
    """Validate all words without relying on a wrapping 32-bit base+size sum."""
    return (
        magic == PROVIDER_MAGIC
        and version == PROVIDER_VERSION
        and EXTERNAL_START <= base < EXTERNAL_END
        and base % 4 == 0
        and size % 4 == 0
        and STORAGE_BYTES <= size <= EXTERNAL_END - base
    )


def start_frame(value: float) -> int:
    return short.start_frame(value, FRAMES)


def seam_gain(phase: int, at: int) -> float:
    return short.seam_gain(phase, at, FRAMES)


@dataclass
class BarLoop(short.ShortLoop):
    frames: int = FRAMES
    canary_initialized: bool = False

    def block(self, samples, rec=0.0, start=0.0, fade=0.0):
        # The DSP initializes eight guard words before processing its first
        # valid call. The model specifies metadata, not external memory access.
        self.canary_initialized = True
        return super().block(samples, rec=rec, start=start, fade=fade)

    def state_words(self, audio_address: int = 0) -> list[int]:
        words = self.metadata_words() + [
            audio_address,
            self.frames,
            int(self.canary_initialized),
        ]
        return words + [0] * (META_WORDS - len(words))

    def audio_words(self) -> list[int]:
        return [float_word(x) for x in self.left + self.right]
