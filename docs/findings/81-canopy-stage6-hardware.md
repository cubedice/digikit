# CANOPY Stage 6 physical qualification

## Scope, 2026-11-03 [D]

Digitakt II OS1.17, combined image `f064c83d13ad47d4`, runtime 0.5.2.
The user authorized USB audio capture and changes to a disposable project.
CANOPY, Overloop v0.4.0 and the remote-control service were loaded. One keyboard
instrument was admitted on track 1, channel 1, with USB and DIN enabled.
The user connected a DIN keyboard; no physical sustain pedal was available.
All observations below are single-session hardware evidence, not release approval.

Firmware SHA256:
`fee506c35d5248ad44b2affe87eaa30dce59c33ee7ac7e23889b283e75b134ae`.
CANOPY package SHA256:
`984f08a57d08280180dcee5b9c51363bdce812eacc0605735eb479e60a40df08`.
Overloop package SHA256:
`e388ebf2a4f0efb5298ec1a6bd40b3336357c8a1a3545dc184c2dd36442e5081`.

## Recording, uploads and live playback [D]

Four CANOPY notes were sent over USB. Overloop was armed through its physical
control path, then the sequencer started. While recording, a 32 KiB file was
uploaded and its device-computed SHA256 matched. The recorder completed eight
steps / 60,000 stereo frames with capture error zero. The sequencer was stopped,
the fader moved fully to LOOP, and the keys released. A two-second 48 kHz stereo
WASAPI capture remained audible: RMS 0.216813, peak 0.776127, all samples finite.
The five-word MIDI guard fault record was zero.

The master capture can contain the project's other tracks. Its audio level is
evidence for master-loop playback, not an isolated CANOPY gain measurement.
The physical upload began during recording; its complete duration was not
timestamped against the end of the capture.

Without recording again, START and LENGTH were changed from 0/8 to 1/1.
The playback offset and length became 7,488 / 7,488 frames while the valid
recorded length stayed 60,000. Actual applied loop gain reached float32 1.0.
The edited window's two-second capture had RMS 0.232496 and peak 0.595237;
all samples were finite and the guard remained clear.

The first two manual test attempts exhausted their +8-detent polling bound
before reaching ARMED. A subsequent +30 sequence reached ARMED, confirmed on
the OLED and in the DSP-returned state. This is a test-control observation,
not evidence of a fixed production recorder defect.

Artifacts: `out/canopy-stage6-hardware-coexistence.json`,
`out/canopy-stage6-hardware-loop.f32le`, `out/canopy-stage6-live-window.json`,
and `out/canopy-stage6-live-window.f32le`.

## USB ownership and sustain messages [D]

The CANOPY SRC status was read through the authenticated DSP status return.
Each expected state required a new DSP block number, avoiding acceptance of
a frozen display snapshot. Observed sequences:

- C-E, then C-E-G, then C-G after releasing E, then empty.
- Four notes held through note-offs while CC64 was high; empty after CC64 low.
- Five note-ons admitted the newest four pitches. Releasing the stolen oldest
  note preserved those four, including their returned ownership tokens.
- All remaining note-offs returned to an empty foreground set; guard zero.

The result is in `out/canopy-stage6-usb-lifetime.json`. These are ownership
observations; this run did not measure waveform continuity or internal latency.

A separate bounded run sent 1,440 note-on/off events in 120 six-note replacement
cycles. Four 32 KiB uploads all verified. It finished in 18.954 seconds with an
empty foreground set, unchanged enabled route and zero guard faults. Raw health
windows were saved in `out/canopy-stage6-usb-stress.json`; this is neither a
long-duration soak nor the worst-case DSP budget qualification.

## DIN keyboard and audio [D]

During a 90-second capture, the user played the connected DIN keyboard on
channel 1. No test notes were sent over USB during this capture. Fresh CANOPY
status showed the following times relative to host capture start:

| Seconds | Foreground pitches |
| --- | --- |
| 62.36 | C4 |
| 62.97 | C4, E4 |
| 63.43 | C4, E4, G4 |
| 64.65 | C4, G4 |
| 66.32 | Empty |

Four-note chords followed and all notes were released by 75.01 seconds. The
final sample at 89.91 seconds was still empty. There were 592 distinct DSP block
snapshots; the route configuration stayed unchanged and the guard remained zero.
The stereo USB audio was finite, RMS 0.027197 and peak 0.321398. The user reported
having no sustain pedal, so physical DIN pedal behavior was not exercised.
These coarse host times describe the performed sequence, not keyboard latency.

Artifacts: `out/canopy-stage6-din-manual.json` and
`out/canopy-stage6-din-manual.f32le`.

## Limits and remaining release gates [O]

- Internal latency still needs completed-message ingress timestamps correlated
  to final-output sample position, with the measurement path itself validated.
  Neither host send/capture times nor UI-status polling prove the median/p99 target.
- Physical DSP clock, counter coverage and worst-case workload remain to be
  qualified. Health counters are raw observations here. The older firmware
  clock analysis in finding 06 is not a current OS1.17 board measurement.
- Hardware route/pattern transitions, upload failure/recovery, longer-duration
  stress and DIN pedal behavior are not covered by these runs.
- The earlier isolated USB chord capture was very quiet. The native AMP was
  changed from AHD to ADSR/full sustain and held audio persisted, but gain was
  not qualified across stock filter, mixer and envelope settings.

The device was left stopped, fader LIVE, REC turned down, notes/pedal released,
with the one-step playback window and recorded loop retained. CANOPY, Overloop
and remote remain selected. Stage 6 remains open.
