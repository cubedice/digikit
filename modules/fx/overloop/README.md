# Overloop

`digikit/overloop` captures stereo master audio into the external buffer reserved by `core/loop-storage`. It requires the version 2 `core/pattern-position` firmware bridge on DT2 OS 1.17. Overloop remains experimental and has not been validated on hardware.

Keep the framework MIX at 127 and use one instance on the master route.

The device page is **OVL**. This module replaces the earlier loop prototypes; build and install it under the new ID, then select Overloop in the FX slot. Firmware patch selections should use the two `core/` provider IDs above.

| Knob | Behaviour |
| --- | --- |
| REC (A) | Turn above halfway to arm. Recording starts on the next selected START boundary, ends automatically, and holds the captured loop. Turn below halfway before arming again. |
| START (B) | Zero-based master step: 0 means step 1, 4 means step 5, 127 means step 128. A step outside the current pattern waits until it exists. |
| FADE (C) | Crossfade between live master audio and the captured loop. |
| LENGTH (D) | 0 captures one whole master-pattern cycle. 1–127 captures that many nominal master steps, continuing across pattern wraps when necessary. |

START and LENGTH latch at the REC edge. Changes while armed or recording apply to the next recording. REC on a START boundary waits for its next occurrence. Holding REC does not retrigger; turning it down does not cancel an armed or active recording. A previously captured loop remains playable while waiting for the new capture to start.

A capture includes the audio block at its opening boundary and excludes the block at its closing boundary. Tempo changes follow actual step events. A transport stop, pattern/generation change, changed master length, or broken timing stream during capture invalidates the incomplete take. Frozen playback does not depend on continued sequencer timing.

The reservation remains 8 MiB; usable audio is 1,048,544 stereo float32 frames (about 21.84 seconds at 48 kHz), followed by a guard. At 120 BPM this holds 128 normal-speed steps. If a requested recording runs past capacity, it aborts to live audio; it never presents a truncated take as a complete loop. The length knob changes capture duration, not the allocator reservation.

Build and package using `mise run module:build -- modules/fx/overloop` and `mise run module:pack -- digikit/overloop --device dt2`. The capture consumer is exercised by `tests/patchkit/test_overloop.py`; the event bridge has separate ColdFire and DSP tests.

Running-firmware emulator tests verified the UI knobs, a complete 96,000-frame stereo capture of a 16-step pattern at 120 BPM, and a one-step capture beginning at START 4 (step 5). Both complete captures matched the effect input byte for byte. Held REC preserved the buffer after STOP. Boundaries are quantized to 32-sample audio blocks. Non-default pattern lengths/scales, external clock, swing and hardware remain unverified; see `docs/findings/76-pattern-position.md` for evidence and scope.
