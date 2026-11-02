# CANOPY Stage 6 qualification

Stage 6 remains open. The user requested completion, and confirmed DTII USB
audio with no DIN source. Windows enumerates a stereo Digitakt II WASAPI input.
Permission for a two-second audio capture was declined; no audio was captured
and no firmware or module was installed on the physical device in this work.

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

## Physical gates still required

- Preserve the existing Overloop module, selection and settings before deploying
  the preflighted combined image. Current physical image is `7eed7aa5661f7981`,
  runtime 0.5.2, with only `digikit/overloop` v0.4.0 loaded.
- Measure completed MIDI ingress to final audio onset under the coexistence
  workload. The design target is below 10 ms median and 15 ms p99. Host MIDI send
  times plus USB audio arrival include transport and host buffering; they cannot
  be relabeled as this internal interval. Existing transport word 6 records
  first-send block, not ingress time, so timestamp instrumentation is still needed.
- Establish a hardware DSP deadline budget and worst-case block duration with
  maximum event/replacement load, arp, Overloop recording/playback and uploads.
  Instruction counts and host interpreter CPU are not silicon cycle measurements.
  Performance comparisons must use the required paired A/B procedure.
- Repeat physical ownership, pedal, stale-release, route/pattern changes and
  upload recovery checks, on USB and DIN. A DIN source is not connected.

Do not mark Stage 6 complete or remove experimental status from emulator-only
evidence.
