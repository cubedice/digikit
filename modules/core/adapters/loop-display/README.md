# Overloop performance display

Experimental DTII OS 1.17 adapter for one `digikit/overloop` instance. Include `core/loop-display` in the firmware, or use the `overloop` patch set. Overloop 0.4 requires its DSP import, so updating only the DKM on older firmware fails to load rather than silently losing the display.

The adapter owns a 64-byte DSP mailbox, `_loop_ui_status`. Overloop publishes it at the end of each audio block; the DTII DSP-link sender encodes it into fourteen spare words at row offsets `+0x44/+0x48` in rows 17–20 and 29–31. The `OVL2` carrier packs the three 0–127 START/LENGTH values into one word; the mailbox and public UI snapshot remain sixteen-word `OVL1` records. The existing ACK, diagnostic and instrument reply rows, header, trailer, and all other bytes are preserved. DNII has no such return extension. Both firmware sender and reader must be updated together; the Overloop DKM ABI is unchanged. See [reply ownership](../../../../docs/findings/340-overloop-reply-ownership.md).

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

The UI task copies fourteen low-half-first carrier words from the driver reply while interrupts are briefly masked, checks the carrier version and reserved bits, then reconstructs the sixteen logical words. It validates bounds and rejects a missing or stalled heartbeat after 60 UI passes. No audio buffer is read by the UI. Firmware drawing occurs only in the FX view paint callback. The native view update runs first; our wrapper invalidates only a currently drawable Overloop view. It retains no view pointers. Animation requests require 60 new audio blocks, a 25 Hz ceiling; native input can redraw immediately.

The native title remains above the performance page. Layout uses top-left coordinates, converted to the firmware rectangle routine's bottom-left convention. The display shows recorder state, captured steps/time, wrap-aware window bounds, playback cursor, selected start/length/time, and actual audible LIVE–LOOP gain. The small upper-right cue indicates the next pattern-switch side. No CAP label is drawn.

Parameter IDs remain REC=0, START=1, FADE=2, LENGTH=3. Physical encoder positions are A REC, B START, C LENGTH, D FADE, so saved kits retain their meaning.

Component checks: `tests/patchkit/test_loop_display.py`, `modules/fx/overloop/tests/test_overloop.py`. Running-firmware scenario: `modules/fx/overloop/tests/display.manual.device.test.ts`, using the `overloop` patch set and a separately prepared `out/regression/modules/dt2/overloop.dkm`. Hardware behavior is unverified.

Run the experimental device scenario with `pnpm --filter @digi/regression test:overloop` after preparing the daemon, ready inputs, synthetic sample card and DKM as described in `docs/TESTING.md`. It is a separate Vitest project so normal runtime acceptance does not require experimental firmware or a DTII-only module.
