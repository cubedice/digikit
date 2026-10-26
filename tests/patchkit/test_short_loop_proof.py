"""The firmware evidence oracle rejects resets and either-channel corruption."""

from __future__ import annotations

import struct
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from types import SimpleNamespace
from typing import Any
from unittest.mock import patch

from tools.patchkit.pk_short_loop_proof import (
    CANARY_WORD,
    PROVIDER_RECORD,
    SPECS,
    Proof,
    ProofFailure,
    Snapshot,
    audio_layout,
    byte_range,
    continuity,
    decode_dsp_bytes,
    dsp_watch_evidence,
    initial_metadata_ok,
    module_bounds,
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

    def test_long_pcm_reports_full_capture_but_compares_exactly_one_period(
        self,
    ) -> None:
        spec = SPECS["michael/long-loop"]
        one_period = struct.pack("<2f", 0.25, -0.5) * spec.frames
        payload = one_period + bytes(2400 * 8)
        elapsed = []
        proof = Proof.__new__(Proof)
        proof.spec = spec
        proof.report = {"checks": [], "pcm": {}}
        proof.rig = SimpleNamespace(
            emu=SimpleNamespace(call=lambda *_args, **_kwargs: None),
            s=SimpleNamespace(run_ms=elapsed.append),
        )
        with tempfile.TemporaryDirectory() as directory:
            proof.work = Path(directory)
            path = proof.work / "test.wav"
            path.write_bytes(wav(payload))
            result = proof.pcm("test", spec.duration_ms)
            self.assertEqual(elapsed, [1050])
            self.assertEqual(result, sample_stats(one_period))
            evidence = proof.report["pcm"]["test"]
            self.assertEqual(evidence["requested_ms"], 1050)
            self.assertEqual(evidence["minimum_frames"], spec.frames)
            self.assertEqual(evidence["recorded_frames"], spec.frames + 2400)
            self.assertEqual(evidence["analysis_frames"], spec.frames)
            self.assertEqual(evidence["analysis_window"], "first complete loop")
            path.write_bytes(wav(one_period[:-8]))
            with self.assertRaises(ProofFailure):
                proof.pcm("test", spec.duration_ms)


class StorageTests(unittest.TestCase):
    def metadata(self, module: str, audio: int) -> tuple[int, ...]:
        return (*([0] * 7), audio, SPECS[module].frames, *([0] * 7))

    def test_bar_startup_requires_initialized_canary_flag_but_long_does_not(
        self,
    ) -> None:
        for module in ("michael/long-loop", "michael/bar-loop"):
            spec = SPECS[module]
            metadata = self.metadata(module, PROVIDER_RECORD[2])
            initialized = (*metadata[:9], 1, *metadata[10:])
            with self.subTest(module=module):
                self.assertEqual(initial_metadata_ok(spec, metadata), not spec.provider)
                self.assertEqual(initial_metadata_ok(spec, initialized), spec.provider)
                valid = initialized if spec.provider else metadata
                self.assertFalse(initial_metadata_ok(spec, (1, *valid[1:])))
                self.assertFalse(
                    initial_metadata_ok(spec, (*valid[:10], 1, *valid[11:]))
                )
                self.assertFalse(initial_metadata_ok(spec, valid[:-1]))

    def test_bar_guard_must_equal_all_eight_known_canary_words_before_hashing(
        self,
    ) -> None:
        proof = Proof.__new__(Proof)
        proof.spec = SPECS["michael/bar-loop"]
        address = PROVIDER_RECORD[2] + proof.spec.frames * 8
        expected = struct.pack("<8I", *([CANARY_WORD] * 8))
        proof.dsp_bytes = lambda _address, _size: expected
        self.assertEqual(len(proof.guard_hashes(((address, 32),))), 1)
        damaged_word = struct.pack("<8I", *([CANARY_WORD] * 7), CANARY_WORD ^ 1)
        for data in (bytes(32), damaged_word, expected[:-4]):
            proof.dsp_bytes = lambda _address, _size, data=data: data
            with self.subTest(data=data.hex()), self.assertRaises(ProofFailure):
                proof.guard_hashes(((address, 32),))

    def test_module_modes_keep_the_original_default_and_provider_is_bar_only(
        self,
    ) -> None:
        short, long, bar = SPECS.values()
        self.assertEqual(
            (short.frames, short.state_bytes, short.duration_ms), (12000, 96064, 250)
        )
        self.assertEqual(
            (long.frames, long.state_bytes, long.duration_ms), (48000, 64, 1000)
        )
        self.assertEqual(
            (bar.frames, bar.state_bytes, bar.duration_ms), (384000, 64, 8000)
        )
        self.assertEqual(short.core, long.core)
        self.assertEqual(bar.core, short.core + ",michael/loop-storage")

    def test_byte_ranges_reject_unaligned_invalid_and_wrapping_values(self) -> None:
        self.assertEqual(byte_range(0xFFFFFFFC, 4), (0xFFFFFFFC, 0x100000000))
        for address, size in (
            (0, 4),
            (-4, 4),
            (0x1001, 4),
            (0x1000, 0),
            (0x1000, -4),
            (0x1000, 5),
            (0xFFFFFFFC, 8),
            (0x100000000, 4),
            (True, 4),
            (0x1000, None),
        ):
            with (
                self.subTest(address=address, size=size),
                self.assertRaises(ProofFailure),
            ):
                byte_range(address, size)

    def test_long_audio_and_metadata_must_fit_runtime_module_allocation(self) -> None:
        spec = SPECS["michael/long-loop"]
        records = [{"name": spec.module, "dsp_off": 0x100, "dsp_len": 0x60000}]
        owned = module_bounds(0x20080000, records, spec.module)
        audio, state = owned[0] + 0x1000, owned[1] - 64
        metadata = self.metadata(spec.module, audio)
        self.assertEqual(audio_layout(spec, state, metadata, owned), (audio, ()))
        for address, modified, allocation in (
            (state, (*metadata[:7], audio + 1, *metadata[8:]), owned),
            (state, (*metadata[:8], 12000, *metadata[9:]), owned),
            (state, metadata, (audio + 4, owned[1])),
            (state, metadata, (owned[0], audio + spec.frames * 8 - 4)),
            (audio, metadata, owned),
            (owned[1], metadata, owned),
            (state, metadata, None),
        ):
            with (
                self.subTest(state=address, metadata=modified, owned=allocation),
                self.assertRaises(ProofFailure),
            ):
                audio_layout(spec, address, modified, allocation)

    def test_runtime_records_cannot_name_unowned_or_out_of_range_l2(self) -> None:
        module = "michael/long-loop"
        valid = {"name": module, "dsp_off": 0, "dsp_len": 0x60000}
        for area, records in (
            (0x80000000, [valid]),
            (0x20090000, [valid]),
            (0x20080000, []),
            (0x20080000, [valid, valid]),
            (0x20080000, [{**valid, "name": "test/sine"}]),
            (0x20080000, [{**valid, "dsp_off": -4}]),
            (0x20080000, [{**valid, "dsp_off": 1}]),
            (0x20080000, [{**valid, "dsp_len": 0x80004}]),
        ):
            with (
                self.subTest(area=area, records=records),
                self.assertRaises(ProofFailure),
            ):
                module_bounds(area, records, module)

    def test_bar_buffer_matches_exact_provider_and_guard_stays_in_reservation(
        self,
    ) -> None:
        spec = SPECS["michael/bar-loop"]
        base, capacity = PROVIDER_RECORD[2:]
        metadata = self.metadata(spec.module, base)
        address, guards = audio_layout(
            spec, 0x200A0000, metadata, provider=PROVIDER_RECORD
        )
        self.assertEqual(address, 0x9CA2B9C8)
        self.assertEqual(guards, ((base + 384000 * 8, 32),))
        self.assertLessEqual(guards[0][0] + guards[0][1], base + capacity)
        # CF sample offsets carry a 0x400 header bias, already subtracted by
        # the sender. Adding it to this DSP pointer is a real mapping error.
        for pointer in (base - 0x400, base + 0x400, base + 4, 0xFFFFFFFC):
            with self.subTest(pointer=hex(pointer)), self.assertRaises(ProofFailure):
                audio_layout(
                    spec,
                    0x200A0000,
                    self.metadata(spec.module, pointer),
                    provider=PROVIDER_RECORD,
                )
        for record in (
            None,
            (0, *PROVIDER_RECORD[1:]),
            (PROVIDER_RECORD[0], 2, base, capacity),
            (*PROVIDER_RECORD[:2], base + 0x400, capacity),
            (*PROVIDER_RECORD[:3], capacity - 4),
        ):
            with self.subTest(record=record), self.assertRaises(ProofFailure):
                audio_layout(spec, 0x200A0000, metadata, provider=record)

    def test_snapshot_reads_separate_audio_pointer_and_both_full_channels(self) -> None:
        spec = SPECS["michael/long-loop"]
        audio, state, counter = 0x20081000, 0x200FF000, 0x200A0000
        metadata = (FROZEN, spec.frames, 0, 0, 0, 0, 0, audio, spec.frames, *([0] * 7))
        left = struct.pack("<f", 0.25) * spec.frames
        right = struct.pack("<f", -0.5) * spec.frames
        reads = []
        memory = {
            (state, 64): struct.pack("<16I", *metadata),
            (audio, spec.frames * 8): left + right,
            (counter, 4): struct.pack("<I", 123),
        }
        proof = Proof.__new__(Proof)
        proof.spec, proof.owned = spec, (0x20080000, 0x20100000)
        proof.provider_address = None
        proof.rig = SimpleNamespace(plan=None)
        proof.row = lambda: (10, 20, state, 1, *([0] * 20))
        proof.controls = lambda: {}
        proof.snapshots, proof.report = {}, {"snapshots": {}}

        def read(address: int, size: int) -> bytes:
            reads.append((address, size))
            return memory[address, size]

        proof.dsp_bytes = read
        with patch("tools.patchkit.pk_dkrt_machine.sym", return_value=counter):
            result = proof.snapshot("captured")
        self.assertEqual(reads, [(state, 64), (audio, spec.frames * 8), (counter, 4)])
        self.assertEqual(result.audio_address, audio)
        self.assertEqual(result.frames, spec.frames)
        self.assertNotEqual(result.left_sha256, result.right_sha256)
        self.assertEqual(
            proof.report["snapshots"]["captured"]["left"]["samples"], spec.frames
        )
        self.assertEqual(proof.report["snapshots"]["captured"]["right"]["rms"], 0.5)


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
    def test_long_modes_use_their_own_period_and_track_storage_guards(self) -> None:
        for module in ("michael/long-loop", "michael/bar-loop"):
            with self.subTest(module=module):
                frames = SPECS[module].frames
                before = replace(
                    snapshot(100, 0),
                    metadata=(
                        FROZEN,
                        frames,
                        frames - BLOCK,
                        0,
                        0,
                        0,
                        0,
                        0x90000000,
                        frames,
                        *([0] * 7),
                    ),
                    frames=frames,
                    audio_address=0x90000000,
                    guards=((0x90300000, "unchanged"),),
                )
                after = replace(
                    before,
                    blocks=102,
                    metadata=(*before.metadata[:2], BLOCK, *before.metadata[3:]),
                )
                self.assertTrue(all(continuity(before, after, frames).values()))
                self.assertFalse(continuity(before, after)["capture_length_unchanged"])
                self.assertFalse(
                    continuity(
                        before, replace(after, audio_address=0x90000400), frames
                    )["audio_address_unchanged"]
                )
                self.assertFalse(
                    continuity(
                        before,
                        replace(after, guards=((0x90300000, "changed"),)),
                        frames,
                    )["guards_unchanged"]
                )

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
