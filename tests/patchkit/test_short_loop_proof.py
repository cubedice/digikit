"""The firmware evidence oracle rejects resets and either-channel corruption."""

from __future__ import annotations

import struct
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from typing import Any

from tools.patchkit.pk_short_loop_proof import (
    Proof,
    ProofFailure,
    Snapshot,
    continuity,
    decode_dsp_bytes,
    dsp_watch_evidence,
    pcm_count_ok,
    pcm_payload,
    sample_stats,
)
from tools.patchkit.short_loop_ref import BLOCK, FRAMES, FROZEN


def snapshot(blocks: int, phase: int) -> Snapshot:
    metadata = (FROZEN, FRAMES, phase, 0, 0, 0, 0, *([0] * 9))
    row = (10, 20, 30, 1, *([0] * 20))
    return Snapshot(30, 10, 20, blocks, metadata, row, "left", "right", "metadata")


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

    def test_truncated_wav_or_partial_stereo_frame_is_rejected(self) -> None:
        payload = struct.pack("<4f", 0.25, -0.25, 0.5, -0.5)
        complete = wav(payload)
        self.assertEqual(pcm_payload(complete), payload)
        for damaged in (complete[:-1], complete[:-8], wav(payload[:-4])):
            with self.subTest(length=len(damaged)), self.assertRaises(ProofFailure):
                pcm_payload(damaged)


class LiveMachineTests(unittest.TestCase):
    def make_proof(self) -> tuple[Proof, dict[int, int]]:
        memory = {
            0x80004704: 0x1000,
            0x8000470C: 0x1034,
            0x10D4: 16 << 8,  # byte at old Sound + 0xa2
            0x20D4: 7 << 8,  # byte at destination Sound + 0xa2
        }
        proof = Proof.__new__(Proof)
        proof.cfg = {"kit_ptr": 0x80004704}
        proof.rig = SimpleNamespace(
            w=memory.__getitem__, emu=SimpleNamespace(peek32=memory.get)
        )
        return proof, memory

    def test_machine_type_follows_live_kit_despite_stale_or_null_cache(self) -> None:
        proof, memory = self.make_proof()
        self.assertEqual(proof.track_machine(), 16)
        memory[0x80004704] = 0x2000
        self.assertEqual(proof.sound_address(), 0x2034)
        self.assertEqual(proof.track_machine(), 7)
        memory[0x8000470C] = 0
        self.assertEqual(proof.track_machine(), 7)
        self.assertEqual(memory[0x10D4], 16 << 8)

    def test_null_or_unreadable_kit_and_type_are_rejected(self) -> None:
        proof, memory = self.make_proof()
        memory[0x80004704] = 0
        with self.assertRaises(ProofFailure):
            proof.track_machine()
        del memory[0x80004704]
        with self.assertRaises(ProofFailure):
            proof.track_machine()
        memory[0x80004704] = 0x2000
        del memory[0x20D4]
        with self.assertRaises(ProofFailure):
            proof.track_machine()


class DspWatchTests(unittest.TestCase):
    def evidence(self, rows: str, blocks: tuple[int, ...]) -> dict[str, Any]:
        # Match sharc_peer.rs: byte-address column; hexadecimal word plus its
        # float interpretation, or '?' when the engine cannot read the word.
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "watch.csv"
            path.write_text(
                "frame,pc,busy,pc_at_rx,0x200a0000\n" + rows,
                encoding="utf-8",
            )
            return dsp_watch_evidence(
                path,
                0x200A0000,
                {
                    f"checkpoint-{i}": {"blocks": block}
                    for i, block in enumerate(blocks)
                },
            )

    def test_all_checkpoints_at_idle_match_the_worker_csv_schema(self) -> None:
        evidence = self.evidence(
            "10,0xb8636d,178,0xb863cf,0x00000064|1.4012985e-43\n"
            "11,0xb863df,182,0xb863cf,0x00000065|1.4153114e-43\n",
            (100, 101),
        )
        self.assertTrue(evidence["all_checkpoints_at_idle"])
        self.assertEqual(evidence["rows"], 2)
        self.assertEqual(evidence["unknown_counter_rows"], 0)
        self.assertEqual(evidence["idle_range"], [0xB8636D, 0xB863DF])
        self.assertEqual(
            evidence["checkpoints"]["checkpoint-0"],
            {
                "blocks": 100,
                "observation": {
                    "frame": 10,
                    "pc": 0xB8636D,
                    "busy": 178,
                    "pc_at_rx": 0xB863CF,
                    "at_idle": True,
                },
            },
        )

    def test_missing_checkpoint_cannot_pass_from_other_idle_observations(self) -> None:
        evidence = self.evidence(
            "10,0xb863cf,178,0xb863cf,0x00000064|1.4012985e-43\n",
            (100, 101),
        )
        self.assertFalse(evidence["all_checkpoints_at_idle"])
        self.assertIsNone(evidence["checkpoints"]["checkpoint-1"]["observation"])

    def test_unknown_counter_cannot_be_used_as_a_zero_checkpoint(self) -> None:
        evidence = self.evidence("10,0xb863cf,178,0xb863cf,?\n", (0,))
        self.assertFalse(evidence["all_checkpoints_at_idle"])
        self.assertEqual(evidence["unknown_counter_rows"], 1)
        self.assertIsNone(evidence["checkpoints"]["checkpoint-0"]["observation"])

    def test_non_idle_pc_fails_even_when_receive_pc_is_idle(self) -> None:
        for pc in (0xB8636C, 0xB863E0):
            with self.subTest(pc=hex(pc)):
                evidence = self.evidence(
                    f"10,0x{pc:x},178,0xb863cf,0x00000064|1.4012985e-43\n",
                    (100,),
                )
                self.assertFalse(evidence["all_checkpoints_at_idle"])
                observation = evidence["checkpoints"]["checkpoint-0"]["observation"]
                self.assertEqual(observation["pc"], pc)
                self.assertFalse(observation["at_idle"])


class ContinuityTests(unittest.TestCase):
    def test_phase_and_control_metadata_can_advance_without_losing_capture(
        self,
    ) -> None:
        before = snapshot(100, FRAMES - BLOCK)
        after = snapshot(102, BLOCK)
        after = replace(
            after,
            metadata=(
                *after.metadata[:3],
                123,
                456,
                0x3F800000,
                1,
                *after.metadata[7:],
            ),
            metadata_sha256="changed by advancing playback and kit controls",
        )
        self.assertTrue(all(continuity(before, after).values()))

    def test_either_channel_or_instance_change_fails(self) -> None:
        before, after = snapshot(100, 0), snapshot(101, BLOCK)
        for fields, failed in (
            ({"left_sha256": "corrupt"}, "left_unchanged"),
            ({"right_sha256": "corrupt"}, "right_unchanged"),
            ({"state_address": 40}, "same_instance"),
        ):
            with self.subTest(fields=fields):
                self.assertFalse(continuity(before, replace(after, **fields))[failed])

    def test_phase_reset_is_not_mistaken_for_continuing_playback(self) -> None:
        before, after = snapshot(100, 512), snapshot(102, 0)
        self.assertFalse(continuity(before, after)["phase_continues"])

    def test_counter_wrap_is_supported_but_no_execution_is_not_progress(self) -> None:
        before, after = snapshot(0xFFFFFFFF, 0), snapshot(0, BLOCK)
        self.assertTrue(continuity(before, after)["phase_continues"])
        self.assertFalse(continuity(before, before)["phase_continues"])

    def test_state_reset_and_guard_disable_are_visible(self) -> None:
        before, after = snapshot(100, 0), snapshot(101, BLOCK)
        reset = replace(after, metadata=(0,) * 16)
        self.assertFalse(continuity(before, reset)["still_frozen"])
        self.assertFalse(continuity(before, reset)["capture_length_unchanged"])
        disabled = replace(after, row=(*after.row[:14], 1, *after.row[15:]))
        self.assertFalse(continuity(before, disabled)["enabled_without_fault"])

    def test_nonfinite_audio_produces_json_safe_failed_statistics(self) -> None:
        result = sample_stats(struct.pack("<2f", 0.25, float("nan")))
        self.assertFalse(result["finite"])
        self.assertIsNone(result["peak"])
        self.assertIsNone(result["rms"])
        self.assertEqual(result["samples"], 2)


if __name__ == "__main__":
    unittest.main()
