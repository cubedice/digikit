"""Strict audio evidence and timeout helpers for firmware proofs."""

import contextlib
import hashlib
import math
import signal
import struct
from collections.abc import Iterator
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[2]
SINE = "test/sine"


class ProofFailure(RuntimeError):
    """A failed prerequisite or measured invariant, retained in the report."""


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode_dsp_bytes(reply: dict[str, Any], length: int) -> bytes:
    """Reject unknown or incomplete memory; never turn missing evidence into zero."""
    encoded = reply.get("hex")
    if not isinstance(encoded, str):
        raise ProofFailure("DSP peek did not return hex bytes")
    if reply.get("unknown", 0) or "?" in encoded:
        raise ProofFailure("DSP peek contains unknown bytes")
    try:
        data = bytes.fromhex(encoded)
    except ValueError as exc:
        raise ProofFailure("DSP peek did not return valid hex bytes") from exc
    if len(data) != length:
        raise ProofFailure(f"DSP peek returned {len(data)} bytes, expected {length}")
    return data


def pcm_payload(wav: bytes) -> bytes:
    """Read a complete 48 kHz stereo float32 WAV without trimming damaged data."""
    if len(wav) < 12 or wav[:4] != b"RIFF" or wav[8:12] != b"WAVE":
        raise ProofFailure("PCM capture is not a RIFF/WAVE file")
    if int.from_bytes(wav[4:8], "little") + 8 != len(wav):
        raise ProofFailure("PCM capture has a truncated or inconsistent RIFF length")
    position, format_ok, payload = 12, False, None
    while position < len(wav):
        if position + 8 > len(wav):
            raise ProofFailure("PCM capture has a truncated chunk header")
        tag = wav[position : position + 4]
        size = int.from_bytes(wav[position + 4 : position + 8], "little")
        start, end = position + 8, position + 8 + size
        if end + (size & 1) > len(wav):
            raise ProofFailure("PCM capture has a truncated chunk body")
        if tag == b"fmt ":
            if size < 16:
                raise ProofFailure("PCM capture has a truncated format chunk")
            fmt, channels, rate, byte_rate, align, bits = struct.unpack_from(
                "<HHIIHH", wav, start
            )
            format_ok = (fmt, channels, rate, byte_rate, align, bits) == (
                3,
                2,
                48000,
                384000,
                8,
                32,
            )
        elif tag == b"data":
            if payload is not None:
                raise ProofFailure("PCM capture has more than one data chunk")
            payload = wav[start:end]
        position = end + (size & 1)
    if not format_ok or payload is None:
        raise ProofFailure("PCM capture must contain 48 kHz stereo float32 data")
    if len(payload) % 8:
        raise ProofFailure("PCM capture ends inside a stereo frame")
    return payload


def pcm_count_ok(samples: int, milliseconds: int, minimum_frames: int = 0) -> bool:
    """Allow frame-boundary/timing slack, but reject empty or shortened captures."""
    expected = milliseconds * 96  # 48 stereo frames per millisecond
    return (
        milliseconds > 0
        and samples > 0
        and samples % 2 == 0
        and minimum_frames >= 0
        and samples >= minimum_frames * 2
        and expected * 9 // 10 <= samples <= expected * 11 // 10
    )


def sample_stats(data: bytes) -> dict[str, Any]:
    """Finite float32 signal statistics without changing the captured bytes."""
    values = struct.unpack(f"<{len(data) // 4}f", data)
    finite = all(math.isfinite(x) for x in values)
    return {
        "samples": len(values),
        "sha256": sha256(data),
        "finite": finite,
        "peak": max(map(abs, values), default=0.0) if finite else None,
        "rms": math.sqrt(sum(x * x for x in values) / len(values))
        if values and finite
        else None,
    }


@contextlib.contextmanager
def deadline(seconds: int) -> Iterator[None]:
    """Bound even a stuck subprocess/socket call; the daemon context cleans up."""
    if not hasattr(signal, "SIGALRM"):
        raise ProofFailure("Run this firmware proof in Linux/WSL (SIGALRM required)")

    def expired(_signum: int, _frame: Any) -> None:
        raise TimeoutError(f"firmware proof exceeded its {seconds}s wall limit")

    previous = signal.signal(signal.SIGALRM, expired)
    signal.alarm(seconds)
    try:
        yield
    finally:
        signal.alarm(0)
        signal.signal(signal.SIGALRM, previous)
