# CANOPY Stage 6 qualification

Stage 6 remains open for calibrated internal latency and worst-case physical
DSP timing. On November 3 the user authorized USB audio capture, confirmed the
project disposable, and connected a DIN keyboard. The combined firmware and
CANOPY, Overloop and remote-control modules are now installed. Hardware
coexistence, live loop edits, USB ownership and DIN keyboard checks passed;
see [physical evidence](../findings/81-canopy-stage6-hardware.md).

## Combined image

The `canopy-overloop` patch set in `devices/digitakt-ii.toml` combines the
keyboard adapter with Overloop storage, controls, display and runtime FX support.
CANOPY and Overloop remain separately installed dynamic modules.

Build using the repository Python wrapper, under the normal build lock:

```sh
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --set canopy-overloop -o out/patch/canopy-stage6.syx
uv run python -m tools.patchkit.patchkit pack digikit/canopy --device dt2 -o out/patch/canopy-stage6.dkm
uv run python -m tools.patchkit.patchkit pack digikit/overloop --device dt2 -o out/patch/overloop-stage6.dkm
uv run python -m tools.patchkit.patchkit preflight out/patch/canopy-stage6.syx
```

The first ad hoc combined image omitted `core/loop-storage`. Its boot report
rejected Overloop with `E_DSP_IMPORT` (-32), retaining CANOPY. The named set
includes the missing storage provider. Do not use the earlier image.

The corrected image passes preflight: 2,020,128 bytes, SHA256
`fee506c35d5248ad44b2affe87eaa30dce59c33ee7ac7e23889b283e75b134ae`.
CANOPY uses the unchanged Stage 5 package, SHA256
`984f08a57d08280180dcee5b9c51363bdce812eacc0605735eb479e60a40df08`.

## Acceptance scope

`tests/regression/runtime/canopy-stage6.test.ts` loads the combined image and
both packages, admits the track-1 keyboard route, holds four notes, captures
Overloop during a 32 KiB runtime upload, verifies the file, edits the live loop
window, and crossfades to the recording. It checks note ownership survives,
releases the keys, checks recorded audio remains audible, and checks runtime
faults. Captures inspect final stereo PCM for finite, non-clipping output.

[D] The corrected combined run passes (`out/canopy-stage6-runtime.log`,
404.87 seconds in the test). Overloop captured 204,704 stereo frames. The four
held CANOPY tokens and gates survived recording and the upload unchanged.
Live, loop-side and post-release output RMS were 0.0430, 0.0531 and 0.0961;
peaks were 0.1139, 0.1646 and 0.3039. These are finite normalized emulator PCM
measurements, not hardware audio measurements. Raw results are in
`out/canopy-stage6-coexistence.json`.

Changed-file checks pass TypeScript types, lint, formatting, layering and
related host-test routing. The required global pathcheck still reports the
existing 256 stale references in 78 files. Focused checks of the changed docs
report zero stale references. No remote push was performed.

This is an emulator coexistence scenario. It does not establish hardware CPU
headroom, keyboard latency, DIN timing, all pattern transitions, or long-duration
hardware stability. Current production admission is one keyboard instrument on
track 1; the four-note foreground pool has two short release-tail slots.

## Remaining release gates

- Measure completed MIDI ingress to final audio onset under the coexistence
  workload. Target: below 10 ms median and 15 ms p99. Host MIDI-send and USB
  audio-arrival times include transport/buffering and are not this interval.
  Transport word 6 is first-send block, not ingress time. Add bounded ingress
  timestamps and correlate them with final-output sample position before
  collecting a latency distribution; validate the measurement path itself.
- Establish the physical DSP deadline budget and worst-case block duration
  with maximum event/replacement load, arp, Overloop and uploads. Current health
  counters are raw observations, not a qualified full-workload CPU percentage.
  Finding 06's programmed 1 GHz analysis targets older firmware; verify current
  OS1.17 and measurement coverage before using it. Follow the required paired
  A/B procedure for performance comparisons.
- Extend hardware qualification to route/pattern changes and upload recovery.
  USB replacement and sustain-message tests passed; a physical DIN sustain
  pedal is unavailable. Do not represent the short stress run as a soak test.

## Physical work, 2026-11-03

Authorization was granted after the earlier declined capture. The user confirmed
that the current project is disposable. Preflight and image identity checks
passed before flashing image `f064c83d13ad47d4`, runtime 0.5.2. CANOPY and the
remote service were installed alongside the existing Overloop v0.4.0. All three
loaded, and the keyboard route admits CANOPY on track 1, channel 1, USB and DIN.
CANOPY load completion is asynchronous: wait for file phase 3 / session ready,
not the first post-restart INFO response.

The original Overloop package is backed up at
`out/stage6-hardware-backup/overloop.dkm`, SHA256
`e388ebf2a4f0efb5298ec1a6bd40b3336357c8a1a3545dc184c2dd36442e5081`.
SETTINGS originally returned `E_SET_NOKEY`; no record existed. Keyboard settings
are now enabled/persisted. Track 1 is CANOPY with the native AMP set to ADSR and
full sustain, verified on the physical display. These are intentional changes
to the disposable project, not a restoration of the original project state.

The first two coexistence attempts stopped before recording: bounded +8 knob
movements did not reach ARMED. Repeating the Record movement with +30 increments
and checking the display reached ARMED. The successful test captured 60,000
stereo frames / eight steps during a verified 32 KiB upload. Live edits reduced
the window to 7,488 frames at offset 7,488 without replacing the recording.
USB ownership/sustain/replacement, the user's DIN chord sequence, and 1,440
USB note events with four verified uploads passed. Exact measurements and
limitations are recorded in the linked finding.

At completion the sequencer is stopped, Overloop FADE is LIVE, REC was turned
down, and all automated notes/sustain messages were released. The recorded loop
remains available; START and LENGTH are both 1. CANOPY, Overloop and remote remain
selected. No further flash was needed for these tests.

The physical capture scripts and raw artifacts are under ignored `out/`;
production device regression scenarios remain TypeScript. This work changes
qualification documentation only. Do not mark Stage 6 complete or remove the
experimental designation while the remaining measurement gates are open.
