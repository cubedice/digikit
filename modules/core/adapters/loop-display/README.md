# Overloop performance display

Experimental DTII OS 1.17 adapter for one `digikit/overloop` instance. Include `core/loop-display` in the firmware, or use the `overloop` patch set. Overloop 0.4 requires its DSP import, so updating only the DKM on older firmware fails to load rather than silently losing the display.

The adapter owns a 64-byte DSP mailbox, `_loop_ui_status`. Overloop publishes it at the end of each audio block; the DTII DSP-link sender copies it into unused reply entries 11/12 of rows 13–20. The existing ACK and diagnostic rows, header, trailer, and all other bytes are preserved. DNII has no such return extension.

| Word | Meaning |
| --- | --- |
| 0 | ABI magic `0x4f564c31` (`OVL1`) |
| 1 | Process-block heartbeat, wrapping u32 |
| 2 | State: 0 idle, 1 recording, 2 frozen, 3 armed |
| 3–4 | Valid recorded frames; frames written during capture |
| 5–6 | Playback phase within window; applied float32 loop gain |
| 7–9 | Latched arm start, capture goal steps, completed steps |
| 10 | Capture error: 0 none, 1 timing lost, 3 capacity exceeded |
| 11 | Recorded step count |
| 12–13 | Playback window offset and length in frames |
| 14–15 | Current START and LENGTH parameters |

The UI task copies sixteen low-half-first words from the driver reply while interrupts are briefly masked. It validates bounds and rejects a missing or stalled heartbeat after 60 UI passes. No audio buffer is read by the UI. Firmware drawing occurs only in the FX view paint callback. The native view update runs first; our wrapper invalidates only a currently drawable Overloop view. It retains no view pointers. Animation requests require 60 new audio blocks, a 25 Hz ceiling; native input can redraw immediately.

The native title remains above the performance page. Layout uses top-left coordinates, converted to the firmware rectangle routine's bottom-left convention. The display shows recorder state, captured steps/time, wrap-aware window bounds, playback cursor, selected start/length/time, and actual audible LIVE–LOOP gain. The small upper-right cue indicates the next pattern-switch side. No CAP label is drawn.

Parameter IDs remain REC=0, START=1, FADE=2, LENGTH=3. Physical encoder positions are A REC, B START, C LENGTH, D FADE, so saved kits retain their meaning.

Component checks: `tests/patchkit/test_loop_display.py`, `tests/patchkit/test_overloop.py`. Running-firmware scenario: `tests/regression/runtime/overloop-display.test.ts`, using the `overloop` patch set and a separately prepared `out/regression/modules/dt2/overloop.dkm`. Hardware behavior is unverified.

Run the experimental device scenario with `pnpm --filter @digi/regression test:overloop` after preparing the daemon, ready inputs, synthetic sample card and DKM as described in `docs/TESTING.md`. It is a separate Vitest project so normal runtime acceptance does not require experimental firmware or a DTII-only module.
