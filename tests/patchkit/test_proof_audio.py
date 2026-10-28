"""Reject incomplete or corrupted audio evidence."""

import struct
import unittest

from tools.patchkit.proof_audio import (
    ProofFailure,
    decode_dsp_bytes,
    pcm_count_ok,
    pcm_payload,
)


def wav(payload: bytes) -> bytes:
    """A correctly sized emulator-format WAV, including empty data."""
    return (
        b"RIFF"
        + struct.pack("<I", 36 + len(payload))
        + b"WAVEfmt "
        + struct.pack("<IHHIIHH", 16, 3, 2, 48000, 384000, 8, 32)
        + b"data"
        + struct.pack("<I", len(payload))
        + payload
    )


class EvidenceTests(unittest.TestCase):
    def test_strict_peek_rejects_unknown_bytes_and_unknown_metadata(self) -> None:
        self.assertEqual(
            decode_dsp_bytes({"hex": "00112233", "unknown": 0}, 4), b"\x00\x11\x22\x33"
        )
        for reply in (
            {"hex": "0011??33", "unknown": 0},
            {"hex": "00112233", "unknown": 1},
            {"hex": "0011", "unknown": 0},
            {"hex": "invalid"},
            {"hex": None},
        ):
            with self.subTest(reply=reply), self.assertRaises(ProofFailure):
                decode_dsp_bytes(reply, 4)

    def test_empty_and_short_capture_cannot_prove_a_silent_destination(self) -> None:
        self.assertEqual(pcm_payload(wav(b"")), b"")
        self.assertFalse(pcm_count_ok(0, 500))
        self.assertFalse(pcm_count_ok(24000, 500))
        self.assertFalse(pcm_count_ok(48001, 500))
        self.assertTrue(pcm_count_ok(48000, 500))
        self.assertTrue(pcm_count_ok(48000 - 64, 500))
        self.assertFalse(pcm_count_ok(48000, 8000))
        self.assertTrue(pcm_count_ok(768000, 8000))

    def test_truncated_wav_or_partial_stereo_frame_is_rejected(self) -> None:
        payload = struct.pack("<4f", 0.25, -0.25, 0.5, -0.5)
        complete = wav(payload)
        self.assertEqual(pcm_payload(complete), payload)
        for damaged in (complete[:-1], complete[:-8], wav(payload[:-4])):
            with self.subTest(length=len(damaged)), self.assertRaises(ProofFailure):
                pcm_payload(damaged)

    def test_long_pcm_requires_a_measured_full_period_despite_timing_tolerance(
        self,
    ) -> None:
        for frames, duration_ms in ((48000, 1000), (384000, 8000)):
            with self.subTest(frames=frames):
                self.assertTrue(pcm_count_ok(frames * 2, duration_ms + 50, frames))
                self.assertFalse(pcm_count_ok(frames * 2 - 2, duration_ms + 50, frames))
                self.assertFalse(
                    pcm_count_ok(frames * 18 // 10, duration_ms + 50, frames)
                )
