"""Running-sequencer evidence must reject silence, missing events and audio drift."""

from __future__ import annotations

import argparse
import csv
import struct
from pathlib import Path

import pytest

from tools.emulator.dspi_log import Frame
from tools.patchkit.pk_pattern_position_proof import (
    NOTE_ON,
    POSITION_MAGIC,
    POSITION_OFFSET,
    POSITION_VERSION,
    POSITION_XOR,
    NoteOn,
    compare_events,
    compare_pcm,
    note_ons,
    parse_trigs,
    pcm_frame_evidence,
    position_evidence,
    position_timing_diagnostics,
    position_words,
    receiver_evidence,
    sequence_evidence,
    window_frames,
)
from tools.patchkit.proof_audio import ProofFailure


def frame(index: int, mask: int = 0, note: int = 0x3C00) -> Frame:
    tx = bytearray(NOTE_ON + 2)
    struct.pack_into(">H", tx, NOTE_ON, mask)
    struct.pack_into(">H", tx, 2, note)
    return Frame(index, True, bytes(tx), b"")


def sequence() -> list[NoteOn]:
    return [NoteOn(100 + 750 * i, 0x3C00) for i in range(9)]


def pcm() -> bytes:
    return b"".join(
        struct.pack("<ff", (i % 997) / 1024, -(i % 991) / 1024) for i in range(48000)
    )


def position_frame(index: int, overrides: dict[int, int] | None = None) -> Frame:
    tx = bytearray(POSITION_OFFSET + 64)
    if index % 500 == 0:
        struct.pack_into(">H", tx, NOTE_ON, 1)
    struct.pack_into(">H", tx, 2, 0x3C00)
    step = (index // 125 + 1) % 16  # deliberate native one-step lookahead
    words = [
        POSITION_MAGIC,
        POSITION_VERSION,
        index + 10,
        index // 5,
        3,
        3,
        0x42600000,
        step,
        16,
        (index // 5) % 25,
        index - index % 5,
        (index - index % 5) * 64,
        index,
        (step + 1) % 16,
        1,
    ]
    for word, value in (overrides or {}).items():
        words[word] = value
    checksum = POSITION_XOR
    for value in words:
        checksum ^= value
    # Independent wire encoding follows CF pp_write_word: low BE16, high BE16.
    for offset, word in enumerate([*words, checksum]):
        tx[POSITION_OFFSET + 4 * offset : POSITION_OFFSET + 4 * offset + 4] = (
            word & 0xFFFF
        ).to_bytes(2, "big") + (word >> 16).to_bytes(2, "big")
    return Frame(index, True, bytes(tx), b"")


def test_position_decoder_rejects_short_or_corrupted_suffix() -> None:
    good = position_frame(0)
    assert position_words(good)[0] == POSITION_MAGIC
    assert good.tx[POSITION_OFFSET : POSITION_OFFSET + 4] == bytes.fromhex("4f535050")
    bad = bytearray(good.tx)
    bad[-1] ^= 1
    for item in (frame(0), Frame(0, True, bytes(bad), b""), position_frame(0, {1: 2})):
        with pytest.raises(ProofFailure):
            position_words(item)


def test_position_decoder_keeps_nonzero_snapshot_and_transmit_block_domains() -> None:
    words = position_words(position_frame(1500, {10: 0x12345678, 12: 0x12345690}))
    assert words[10] == 0x12345678 and words[12] == 0x12345690


def test_position_mapping_measures_consistent_lookahead_across_passes() -> None:
    frames = [position_frame(index) for index in range(4000)]
    evidence = position_evidence(frames, note_ons(frames), (1, 5, 9, 13))
    assert evidence["current_step_offset_from_grid_zero"] == 1
    assert evidence["next_step_offset_from_grid_zero"] == 2
    assert len(evidence["note_positions"]) == 8
    timing = evidence["timing_diagnostics"]
    # Descriptive timing must not turn this valid zero-delay fixture into a failure.
    assert timing["note_timings"][1]["current_step_edge"]["note_delay_blocks"] == 0
    assert not timing["arbitrary_step_boundary_alignment_verified"]
    assert not timing["sample_accurate_capture_verified"]


def test_timing_diagnostics_exclude_initial_values_and_measure_observed_edges() -> None:
    frames = [
        position_frame(100, {3: 10, 7: 2, 9: 1, 10: 100}),
        position_frame(101, {3: 11, 7: 2, 9: 1, 10: 101}),
        position_frame(102, {3: 12, 7: 3, 9: 0, 10: 102}),
        position_frame(103, {3: 12, 7: 3, 9: 0, 10: 102}),
        position_frame(104, {3: 13, 7: 3, 9: 1, 10: 104}),
        position_frame(105, {3: 13, 7: 3, 9: 1, 10: 104}),
    ]
    timing = position_timing_diagnostics(
        frames, [NoteOn(index, 0x3C00) for index in (100, 101, 105)], (1, 5, 9, 13)
    )
    for note in timing["note_timings"][:2]:
        assert note["current_step_edge"] is None
        assert note["fresh_substep1_edge"] is None
    note = timing["note_timings"][2]
    assert note["snapshot_age_blocks"] == 1 and note["substep"] == 1
    assert note["current_step_edge"]["frame"] == 102
    assert note["current_step_edge"]["note_delay_blocks"] == 3
    assert note["current_step_edge"]["note_delay_stereo_frames"] == 96
    assert note["current_step_edge"]["note_delay_ms"] == 2
    assert note["current_step_edge"]["substep"] == 0
    assert note["fresh_substep1_edge"]["frame"] == 104
    assert note["fresh_substep1_edge"]["note_delay_blocks"] == 1
    assert note["fresh_substep1_edge"]["snapshot_age_blocks"] == 0
    assert len(timing["current_step_edges"]) == len(timing["fresh_substep1_edges"]) == 1
    assert timing["uniform_trig_spacing"]
    assert "cyclic phase relabeling" in timing["grid_label_assumption"]


def test_timing_diagnostics_do_not_invent_edges_across_gaps_or_stale_snapshots() -> (
    None
):
    frames = [
        position_frame(100, {3: 10, 7: 2, 9: 0, 10: 100}),
        position_frame(101, {3: 10, 7: 2, 9: 1, 10: 100}),
        position_frame(103, {3: 11, 7: 3, 9: 1, 10: 103}),
        position_frame(104, {3: 12, 7: 3, 9: 1, 10: 104}),
    ]
    timing = position_timing_diagnostics(frames, [NoteOn(104, 0x3C00)], (1, 2, 5, 11))
    assert timing["current_step_edges"] == []
    assert timing["fresh_substep1_edges"] == []
    assert timing["note_timings"][0]["fresh_substep1_edge"] is None
    assert not timing["uniform_trig_spacing"]


def test_timing_diagnostics_choose_the_latest_preceding_matching_edge() -> None:
    frames = [
        position_frame(100, {3: 10, 7: 2, 9: 0, 10: 100}),
        position_frame(101, {3: 11, 7: 3, 9: 1, 10: 101}),
        position_frame(102, {3: 12, 7: 4, 9: 1, 10: 102}),
        position_frame(103, {3: 13, 7: 3, 9: 1, 10: 103}),
        position_frame(104, {3: 13, 7: 3, 9: 1, 10: 103}),
        position_frame(105, {3: 14, 7: 4, 9: 1, 10: 105}),
        position_frame(106, {3: 15, 7: 3, 9: 1, 10: 106}),
    ]
    # The diagnostic also works on an incomplete/unvalidated trace; it does not
    # replace position_evidence's separate rejection of the reordered steps.
    timing = position_timing_diagnostics(frames, [NoteOn(104, 0x3C00)], (1, 5, 9, 13))
    note = timing["note_timings"][0]
    assert note["current_step_edge"]["frame"] == 103
    assert note["fresh_substep1_edge"]["frame"] == 103
    assert note["current_step_edge"]["note_delay_blocks"] == 1


@pytest.mark.parametrize(
    "word,value",
    [
        (2, 99),
        (3, 1),
        (4, 5),
        (6, 0x42600004),
        (7, 9),
        (8, 32),
        (5, 2),
        (10, 1),
        (12, 2005),
        (14, 2),
    ],
)
def test_position_evidence_rejects_stale_mixed_or_inconsistent_records(
    word: int, value: int
) -> None:
    frames = [position_frame(index) for index in range(4000)]
    frames[2000] = position_frame(2000, {word: value})
    with pytest.raises(ProofFailure):
        position_evidence(frames, note_ons(frames), (1, 5, 9, 13))


def test_position_evidence_rejects_out_of_order_steps_between_note_ons() -> None:
    frames = [position_frame(index) for index in range(4000)]
    frames[180] = position_frame(180, {7: 7})
    with pytest.raises(ProofFailure, match="reorders"):
        position_evidence(frames, note_ons(frames), (1, 5, 9, 13))


def write_receiver_watch(path: Path, frames: list[Frame], change: str = "") -> None:
    address, fx_counter = 0x20010000, 0x20020000
    columns = [
        "frame",
        "pc",
        "busy",
        "pc_at_rx",
        *[f"0x{address + i * 4:x}" for i in range(20)],
        f"0x{fx_counter:x}",
    ]
    with path.open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for i, fr in enumerate(frames):
            if change == "missing" and i == 1:
                continue
            values = [*position_words(fr), 100 + i, 3, 97 + i, 1]
            if change == "lag" and i == 1:
                values[2] -= 1
            if change == "counter" and i == 1:
                values[18] += 2
            row = {
                "frame": fr.index,
                "pc": "0x102",
                "busy": 0,
                "pc_at_rx": "0x100",
                f"0x{fx_counter:x}": "0x14",
            }
            row.update(
                {f"0x{address + j * 4:x}": hex(value) for j, value in enumerate(values)}
            )
            if change == "unknown" and i == 1:
                row[f"0x{address:x}"] = "?"
            writer.writerow(row)


def test_receiver_matches_same_wire_frame_at_inclusive_idle_endpoint(
    tmp_path: Path,
) -> None:
    frames = [position_frame(i) for i in range(3)]
    path = tmp_path / "watch.csv"
    write_receiver_watch(path, frames)
    result = receiver_evidence(frames, path, 0x20010000, 0x20020000, (0x100, 0x102))
    assert result["matched_frames"] == 3 and result["exact_same_frame_mirror"]
    assert result["before_fx_consumer_proved"] is False


@pytest.mark.parametrize("change", ["missing", "lag", "counter", "unknown"])
def test_receiver_rejects_missing_stale_or_unknown_observations(
    tmp_path: Path, change: str
) -> None:
    frames = [position_frame(i) for i in range(3)]
    path = tmp_path / "watch.csv"
    write_receiver_watch(path, frames, change)
    with pytest.raises(ProofFailure):
        receiver_evidence(frames, path, 0x20010000, 0x20020000, (0x100, 0x102))


def test_note_oracle_uses_track_edges_and_not_other_tracks_or_held_bits() -> None:
    frames = [frame(10), frame(11, 1), frame(12, 3), frame(13, 2), frame(14, 3, 0x4000)]
    assert note_ons(frames) == [NoteOn(11, 0x3C00), NoteOn(14, 0x4000)]


@pytest.mark.parametrize(
    "bad", [Frame(0, False, bytes(40), b""), Frame(0, True, b"", b"")]
)
def test_missing_wire_data_is_not_silence(bad: Frame) -> None:
    with pytest.raises(ProofFailure):
        note_ons([bad])


@pytest.mark.parametrize("events", [[], [NoteOn(0, 0x3C00)], sequence()[:7]])
def test_no_trig_or_single_pass_cannot_prove_a_running_sequence(
    events: list[NoteOn],
) -> None:
    with pytest.raises(ProofFailure):
        sequence_evidence(events, (1, 5, 9, 13))


def test_two_passes_match_entered_steps_and_reject_wrong_spacing() -> None:
    events = sequence()
    assert sequence_evidence(events, (1, 5, 9, 13))["spacing_max_error_blocks"] == 0
    events[4] = NoteOn(events[4].frame + 10, events[4].note)
    with pytest.raises(ProofFailure, match="spacing"):
        sequence_evidence(events, (1, 5, 9, 13))


def test_event_comparison_allows_only_one_constant_offset() -> None:
    events = sequence()
    shifted = [NoteOn(event.frame + 200, event.note) for event in events]
    assert compare_events(events, shifted)["constant_offset_blocks"] == 200
    shifted[-1] = NoteOn(shifted[-1].frame + 1, shifted[-1].note)
    with pytest.raises(ProofFailure, match="drift"):
        compare_events(events, shifted)


def test_event_comparison_rejects_pitch_and_count_changes() -> None:
    events = sequence()
    with pytest.raises(ProofFailure):
        compare_events(events, events[:-1])
    changed = [*events[:-1], NoteOn(events[-1].frame, 0x4000)]
    with pytest.raises(ProofFailure):
        compare_events(events, changed)


def test_pcm_comparison_accepts_one_global_stereo_offset() -> None:
    payload = pcm()
    assert (
        compare_pcm(payload, b"\0" * 8 + payload, max_shift=2)[
            "constant_offset_stereo_frames"
        ]
        == -1
    )


@pytest.mark.parametrize("offset", [100 * 8, 100 * 8 + 4, 47990 * 8 + 4])
def test_pcm_comparison_detects_either_channel_and_late_tail_corruption(
    offset: int,
) -> None:
    payload = pcm()
    changed = bytearray(payload)
    struct.pack_into("<f", changed, offset, 0.375)
    with pytest.raises(ProofFailure, match="complete stereo PCM"):
        compare_pcm(payload, bytes(changed), max_shift=2)


def test_pcm_comparison_rejects_drift_even_when_each_note_could_be_realigned() -> None:
    payload = pcm()
    changed = payload[: 20000 * 8] + bytes(8) + payload[20000 * 8 : -8]
    with pytest.raises(ProofFailure, match="complete stereo PCM"):
        compare_pcm(payload, changed, max_shift=2)


def test_pcm_comparison_rejects_silence_and_nonfinite_right_channel() -> None:
    payload = pcm()
    for changed in (
        bytes(len(payload)),
        payload[:4] + struct.pack("<f", float("nan")) + payload[8:],
    ):
        with pytest.raises(ProofFailure):
            compare_pcm(payload, changed, max_shift=2)


def test_measured_pcm_interval_rejects_matching_but_truncated_audio_tails() -> None:
    payload = pcm() * 2
    assert pcm_frame_evidence(payload, 20, 3020)["difference_stereo_frames"] == 0
    for size in (33, 128, 4000):
        truncated = payload[: -8 * size]
        assert compare_pcm(truncated, truncated, max_shift=0)["exact"]
        with pytest.raises(ProofFailure, match="measured DSP"):
            pcm_frame_evidence(truncated, 20, 3020)
    with pytest.raises(ProofFailure, match="measured DSP"):
        pcm_frame_evidence(payload + bytes(8 * 33), 20, 3020)


def test_frame_window_requires_independent_zero_origin_and_complete_final_coverage() -> (
    None
):
    frames = [frame(i) for i in range(100)]
    rows, offset = window_frames(frames, 40, 60, 100)
    assert offset == 0 and [row.index for row in rows] == list(range(40, 60))
    with pytest.raises(ProofFailure, match="missing frame"):
        window_frames(frames[:30] + frames[31:], 40, 60, 100)
    for truncated in (frames[25:], frames[:-1]):
        with pytest.raises(ProofFailure, match="truncated"):
            window_frames(truncated, 40, 60, 100)


@pytest.mark.parametrize("value", ["", "0,5", "1,17", "5,1", "1,1", "1,x"])
def test_invalid_grid_steps_are_rejected(value: str) -> None:
    with pytest.raises(argparse.ArgumentTypeError):
        parse_trigs(value)


def test_grid_steps_may_be_nonuniform() -> None:
    assert parse_trigs("1,4,8,13") == (1, 4, 8, 13)
