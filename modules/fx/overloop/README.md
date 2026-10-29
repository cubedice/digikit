# Overloop

`digikit/overloop` captures stereo master audio into the external buffer reserved by `core/loop-storage`. Version 0.4 requires the version 2 `core/pattern-position` bridge, `core/loop-controls`, and `core/loop-display` on DT2 OS 1.17. Overloop remains experimental and has not been validated on hardware.

Keep the framework MIX at 127 and use one instance on the master route.

The device page is **OVL**. This module replaces the earlier loop prototypes; build and install it under the new ID, then select Overloop in the FX slot. Firmware patch selections must include all four `core/` provider IDs above. The module refuses to load if a required provider is missing.

| Knob | Behaviour |
| --- | --- |
| REC (A) | Turn above halfway to arm. Recording starts on the next selected START boundary, ends automatically, and holds the captured loop. Turn below halfway before arming again. |
| START (B) | Zero-based master step: 0 means step 1, 4 means step 5, 127 means step 128. Selects the next capture start and adjusts the frozen playback start live. |
| LENGTH (C) | 0 captures one whole master-pattern cycle; 1–127 captures that many steps. During playback, 0 uses the whole recording, and 1–127 selects a live window in recorded steps. |
| FADE (D) | Crossfade between live master audio and the captured loop. |

The performance display shows EMPTY, ARMED, RECORDING, or RECORDED. ARMED+LOOP means the previous recording is still available while waiting for the selected start step. The upper-right cue shows which side the next pattern switch will select. The second line shows capture progress or the recorded step count and duration. No CAP label is displayed.

The timeline highlights the playback window within the complete recording, with S/E boundary markers and a moving playhead. Wrapped windows occupy both ends of the timeline. The row below shows the selected start (one-based on screen), length, and effective playback duration. The bottom LIVE–LOOP fader follows the applied audio gain; without a valid recording it stays at LIVE. NO STATUS indicates a missing or stalled DSP status stream. Changing LENGTH or START updates the window live.

Knobs C and D exchanged physical positions in 0.4. Parameter IDs and stored kit values retain their earlier meanings, so existing fade and length settings are not exchanged when loading a saved kit.

START and LENGTH latch at the REC edge for recording. Playback reads these knobs every audio block. The playback start wraps on the recorded source pattern grid and then within the captured phrase. LENGTH cannot expose unrecorded audio: values longer than the capture use the entire capture. The window can wrap across the physical buffer seam, using exact recorded step boundaries even if the tempo changed during capture. A shorter window resets playback phase when the old phase is outside it.

REC on a START boundary waits for its next occurrence. Holding REC does not retrigger; turning it down does not cancel an armed or active recording. A previously captured loop remains playable while waiting for the new capture to start. An unfulfilled arm is consumed on a pattern switch.

On each pattern switch, the destination receives the source START and LENGTH. If the source REC was enabled (at least halfway), the destination FADE becomes 127 (recorded loop); otherwise FADE becomes 0 (live master). Destination REC becomes 0 in both cases. The normal short gain ramp suppresses clicks. If there is no complete recording, playback stays live even with FADE at 127. These changes write the destination kit controls, so subsequent knob movements and kit saves use the new values.

A capture includes the audio block at its opening boundary and excludes the block at its closing boundary. Tempo changes follow actual step events. A transport stop, premature pattern/generation change, changed master length, or broken timing stream during capture invalidates the incomplete take. A complete take can close at the destination's first step when it immediately follows the last source step. Frozen playback does not depend on continued sequencer timing.

The reservation remains 8 MiB; usable audio is 1,048,544 stereo float32 frames (about 21.84 seconds at 48 kHz), followed by a guard. At 120 BPM this holds 128 normal-speed steps. If a requested recording runs past capacity, it aborts to live audio; it never presents a truncated take as a complete loop. The length knob changes capture duration, not the allocator reservation.

Build and package using `mise run module:build -- modules/fx/overloop` and `mise run module:pack -- digikit/overloop --device dt2`. The capture consumer is exercised by `tests/patchkit/test_overloop.py`; the event bridge has separate ColdFire and DSP tests.

The named `overloop` firmware patch set includes the required adapters: `mise run patch -- --firmware firmware/Digitakt_II_OS1.17.syx --set overloop -o out/patch/overloop-dt2.syx`.

Upgrading to 0.4 requires rebuilding and flashing the firmware as well as uploading the module:

```sh
mise run patch -- --firmware firmware/Digitakt_II_OS1.17.syx --modules core/runtime,core/digikit-menu,core/health,core/dynreg,core/dyndsp,core/dynfx,core/fxpages,core/loop-storage,core/pattern-position,core/loop-controls,core/loop-display -o out/patch/overloop-dt2.syx
mise run patch:preflight -- out/patch/overloop-dt2.syx
mise run module:pack -- digikit/overloop --device dt2
```

Running-firmware emulator tests verified the UI knobs, a complete 96,000-frame stereo capture of a 16-step pattern at 120 BPM, and a one-step capture beginning at START 4 (step 5). Both complete captures matched the effect input byte for byte. Held REC preserved the buffer after STOP. Boundaries are quantized to 32-sample audio blocks. Non-default pattern lengths/scales, external clock, swing and hardware remain unverified; see `docs/findings/76-pattern-position.md` for evidence and scope.
