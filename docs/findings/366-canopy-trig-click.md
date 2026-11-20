# 366. CANOPY's click at a trig: the linear steal ramp

What the owner reported on the Digitakt II with image `8278188469b2dc9f` (2026-11-20): CANOPY's arp clicks now and then
while the transport plays, with RST at TRIG. Digitakt II OS 1.17; measured on the SHARC core with CANOPY's DSP harness
(`modules/machines/canopy/tests/test_canopy_arp_click.py`, the native core of `pk_sharc`). Nothing here ran on hardware.
Marks as in `docs/FINDINGS.md`; every result here is **[D]** unless it is marked **[O]**.

## The measure

A chord held from the keys (C, E, G), the arp of `FINE` at RATE 8 with RST TRIG, the position input of a running
transport at 120 BPM (V5, with tempo), and a trig of the track every 131 blocks from block 97 (off the arp's grid). The
click measure of finding 354: the largest second difference from 2 blocks before a trig to 16 after it, against the
largest one of the same take away from the trigs (the arp's own onsets included), at the arp's level 127 (arp alone),
64 (arp and pad) and 0 (pad alone).

| Trig | Before (main) | After |
| --- | --- | --- |
| a new phrase only (the arp restarts) | 0.57 to 1.11 | the same |
| the chord again from the trig: the notes before it off, new ones on, in one block | arp 5.86 then 6.47 at every trig; arp and pad 2.67 then 4.90; pad 1.60 then 4.68 | arp 1.94 then 1.35; arp and pad 0.93 to 1.26; pad 0.96 to 1.17 |
| the same, the old notes off a block later | 5.85 / 6.47, 2.63 / 4.90, 1.55 / 4.69 | 1.94 / 1.35, 0.91 to 1.26, 1.00 to 1.18 |
| a note of the trig over the chord, released 40 blocks later | 0.46 to 1.11 | the same |

The arp's onsets keep their grid through every case (one every 94 blocks at RATE 8, restarting at the trig).

## The cause

At a chord trig the new notes take voices from the notes before them. Read from the voice records of the harness at the
first trig: of the three released notes, two move into the two shared tails, and every one of them, and the arp's
current note at its restart (no free tail left), is faded with the reset/steal ramp, gate 2 of `voice.s`. That ramp was
linear: the level fell by 1/64 a sample from where it stood. Its corners are the click: the largest second difference
sits at the first sample of the trig's block (the ramp begins) and at its 64th (the ramp ends). Only while the transport
plays, because only then do trigs come; now and then, because it needs a trig that plays notes while the voices are
full.

## The fix

The reset and steal ramp is eased: from the level e0 where it begins to 0 over D = ceil(64 e0) samples (1 to 64, the
time the linear ramp took), with the weight 1 - s(k / D), s(u) = u u (3 - 2u): no corner at either end. A queued
replacement still begins where the ramp ends, at the same sample as before, so the arp's cadence and finding 348's
two-block bound are unchanged. The ramp's levels go to `_cp_amp_samples` as a native envelope's do, so every renderer
(additive, wavetable, FM, the post-AMP common envelope of V7) takes them. While the gate is 2 the voice's AMP sidecar
(instance words 716 to 743) holds the ramp: its token, the samples done, e0 and `CP_RESET_MARK`; the voice retires at
the ramp's end and its next note starts a new sidecar. Only voices of a V2 instance (a pad or tail slot, or the arp
group) have a sidecar; a bank without one (the CPS1 sine carrier) keeps the linear ramp.

Tests changed with it: `test_post_amp_reset_ramp_remains_smooth_and_then_exactly_quiet` (the common envelope is now the
eased ramp, 64 samples from 1.0, then exactly quiet), and the two steal tests that read the tail's sidecar after the
steal's first block (it holds the ramp). The rest of CANOPY's DSP suites pass unchanged (below).

## What else was looked at

- **Repeated, late or skipped position records** (finding 363). The arp's scheduler (finding 348) takes only the tempo
  from the records; a record again is the same tempo, a gap keeps the last one: no burst and no gap of notes. A TRIG
  restart comes with the phrase word of the instrument input, which the ColdFire's mapper sets with the trig's events,
  not from the position record. A PATTERN restart comes at a change of the record's pattern identity: a record again
  carries the same identity (no second restart), and after a gap the change is seen with the next record (as late as
  the gap). At a change at the end the scheduled pointer moved the identity a step early (finding 365), so a PATTERN
  restart, and the wavetable scan's pattern reset, came a step early; with the playing pattern they come where the new
  pattern is heard. CONT keeps its phase. `test_pattern_arp_transport_and_phase_churn_do_not_reset_cadence` already holds
  the cadence through repeated, jumping and wrapping stamps and transport churn; no timing path to the click was found,
  so no new jitter cadence test was added.
- **The effect path.** `LW_SEQ` and `LW_TAKEN` of the wet mailbox are written and read on the DSP only (PRISM's block
  writes `LW_SEQ`, Overloop's `LW_TAKEN`), so the link's jitter cannot reach them (read in `prism.s` and `overloop.s`).
- **[O]** Per-trig locks (finding 347), control bank commits and MOD values at a trig: not measured; the harness case has
  no locks.
- **[O]** DSP load while playing: not measured.
- **[O]** Whether the owner's trigs play notes (a chord or the trig's own note) was not asked; the case that clicks is a
  trig that plays the chord again while the voices are full.

## For listening

`CANOPY_CLICK_WAV=DIR uv run pytest -q modules/machines/canopy/tests/test_canopy_arp_click.py -k write` writes
`chord-trigs-arp.wav`, `chord-trigs-arp-and-pad.wav` and `chord-trigs-pad.wav` (3 s, a chord trig every 131 blocks, the
same +14 dB for every take). Before and after are in `firmware/hardware/canopy-arp-click-audio/` of the WSL worktree
(`before/`, `after/`, `README.txt`); renders of the harness, not recordings of a device, never committed.

## Proofs

| Layer | Check | Result |
| --- | --- | --- |
| SHARC core | `test_canopy_arp_click.py`: the twelve cases of the table (four trigs at three levels), click below 2.0 at every trig and the arp's onsets on their grid; the same file at main's `voice.s`: the six chord cases fail with the numbers of the table | 12 passed (6 failed at main) |
| SHARC core | `test_canopy_v2.py` with the three tests changed as above | 911 passed |
| SHARC core | `test_canopy_arp_attack.py`, `test_canopy_voice.py`, `test_canopy_fm_quality.py`, `test_canopy_fm_tables.py`, `test_canopy_wavetables.py`, `test_canopy_instrument.py`, `test_canopy_carrier.py`, `test_canopy_midi_frame.py`, `test_instrument_play.py`, `test_canopy_native_amp_firmware.py` | 434 passed |
| Device | CANOPY's seven device tests on DT2 in the gate of finding 365 (`A:core/instrument-play`), with PRISM's `coexistence` | pass |
