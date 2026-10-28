"""External-buffer contract for the step-counted capture proof.

These bounds do not establish RAM ownership; the patched provider does.
"""

from tools.patchkit import short_loop_ref as short

FS = short.FS
BLOCK = short.BLOCK
FRAMES = 1_048_544
META_WORDS = 32
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
