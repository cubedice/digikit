# PRISM

PRISM is a tape delay and a granular effect on the sources you select. It works as soon as it is loaded: the last 21.5
seconds of the selected sources are its tape, a delay head plays the tape back, and eight grains read it. Recording a
phrase is optional: while a phrase is held the grains play the phrase instead of the tape, and the delay goes on with the
live sources. Its FX entry has the pages DELAY, LOOP, GRAIN, MOVE and SPACE; UP/DOWN moves between them, and H opens the
input selection. LEVEL/DATA is MIX.

The first target is Digitakt II OS 1.17. The package requires version 2 of the phrase control and the phrase-storage and
phrase-routing capabilities. It must not be loaded as an ordinary master effect on firmware without those providers. The
imports are mandatory; the engine contains no firmware addresses.

## Controls

FUNC + FX advances through the normal effect list; PRISM occupies one entry. It opens on DELAY; UP/DOWN selects DELAY,
LOOP, GRAIN, MOVE or SPACE, and the last visited page is remembered for the loaded instance. Hold FUNC + FX for the native
global-FX show-values mode; release the chord to return without advancing. FUNC + NO clears the held phrase and a pending
take and keeps the current page or IN open; MIX stays where it is (after a routing fault it goes to zero). Ordinary NO
still closes IN or exits the FX view normally.

| Encoder | DELAY | LOOP | GRAIN | MOVE | SPACE |
| --- | --- | --- | --- | --- | --- |
| A | TIME | REC | SIZE | POS | DIFF |
| B | FDBK | FROM | RATE | SCAN | DEC |
| C | MODE | TAKE | TUNE | REVR | HPF |
| D | SKEW | CUE | SHAP | WDTH | LPF |
| E | WOW | STRT | SPRY | Diagram | Diagram |
| F | DRIV | LEN | JITR | Diagram | Diagram |
| G | BAL | Crop and duration | Grain activity | Diagram | Diagram |
| H press | IN | IN | IN | IN | IN |

LEVEL/DATA controls MIX on every page: over the first half the effect comes up while the selected sources stay whole; over
the second half the sources give way, and at the top only the effect is heard. Discrete lists (TIME, MODE and the lists of
the other pages) take four raw encoder ticks per choice. Diagram cells and turning H have no hidden parameter.

DELAY:

- TIME is a note length at the current tempo, from 1/32 to two bars (sixteen choices, with triplets and dotted values). A
  change glides: the head moves at most half a frame per sample, as a tape would.
- FDBK writes the head's output back onto the tape through a DC blocker, a low pass near 3 kHz and a soft limiter that
  keeps the feedback at or below half of full scale, so repeats settle near the level of the input instead of building
  up; above 100 % the lower notes keep repeating at that level. A phrase recorded from the tape contains the repeats.
- MODE: FWD, REV (each stretch of TIME plays backwards) or HOLD (the tape stops and the head loops its last TIME).
- SKEW makes one side up to half as long again (L or R); WOW is a slow drift of the delay of at most 0.3 %.
- DRIV saturates the effect's output before SPACE. BAL sets head against grains: both full in the middle, one of them off
  at each end.

MOVE on the tape: POS runs from the newest audio (0) to the oldest of the grains' window (the newest two seconds); SCAN has
no effect there. With a held phrase both work on the phrase as before.

## Capture and ownership

- NEXT records once starting at NOW or the next selected master-step cue.
  PAST freezes the corresponding history ending at that cue.
- A numbered cue armed while stopped survives an ordinary transport START.
  Other timeline changes cancel the take while preserving the held phrase.
- TAKE resolves PAT at arm time, or captures 1–128 master-step intervals.
  The phase clock follows actual musical time, including tempo changes.
- Changing the input mix invalidates only rolling history. Cancelling,
  invalid timing, insufficient history or capacity exhaustion preserves the
  previous held recording and its source membership.
- Two exclusively leased banks swap roles without copying a whole phrase.
  The previous bank remains readable during a 960-sample handoff, then all old
  readers retire before that bank is writable again.
- Each production 8 MiB bank holds 1,032,192 stereo float frames (21.504 s at
  48 kHz), plus one musical phase stamp per 32-frame block. The engine also
  supports smaller descriptors for bounded acceptance tests. This storage is
  additional to Overloop's reservation.
- Recordings are volatile. CLEAR, unload and faults restore live routing;
  stored parameters and next-input choices are managed by the provider.
  CLEAR and CANCEL work even without a valid clock; a rejected ARM is
  acknowledged immediately. PAST reports TOO LONG once a full history bank
  cannot reach the requested start, rather than continuing to show FILL.

## Tape, grain and space engine

The head reads the rolling bank behind the block that is being written, so it never reads a frame of the current block.
When the tape cannot be read (during a phrase handoff, after CLEAR, after a change of the input selection or of the
timeline, until TIME of new tape exists) the head's output runs to zero in one block and comes back with a one-block fade.
HOLD waits while a take is armed or recording. A grain on the tape reads a window fixed at its start, entirely inside tape
that was written before it started; a reset of the tape fades such grains out over 32 samples.

The engine has eight total stereo grains, including old readers in
a phrase handoff. Exhaustion skips a launch and reports LIMIT; it does not
steal a sounding grain or rewrite SIZE/RATE. RATE OFF stops launches while
existing grains and diffusion tails finish.
If every slot belongs to the previous phrase at a take change, one old reader
fades out over at most 32 samples. A new reader starts on the following block,
while the other old readers finish the phrase handoff.

SIZE spans 240–24,000 samples (5–500 ms). Pitch ratio spans 0.25–4 (±24 st),
independent of scan speed. SOFT, TRI, RISE and FALL all have a minimum 32-sample
anti-click edge. Fractional reads interpolate adjacent samples and wrap only
inside the active crop. Crop changes use a short output bridge and never
read newly excluded audio. Scan and crop normalization use bounded arithmetic,
including a full-length-to-minimum crop change.

Spray, timing jitter, reverse probability and stereo balance use a private
deterministic random generator. Launch gain compensates expected overlap;
additional headroom leaves the effect guard margin. DIFF is a normalized
four-line feedback network with a 0.15–8 s decay control; HPF/LPF affect only
the wet return. The rolling bank takes the head's feedback; a held phrase is never written.
The MOVE diagram receives actual grain launch positions, and its separate
scan cursor does not rewrite the saved POS setting.

## Verification

Build the dedicated firmware image and module from the repository root:

```bash
mise run patch -- --firmware firmware/Digitakt_II_OS1.17.syx --set phrase -o out/patch/phrase-dt2.syx
mise run module:pack -- digikit/prism --device dt2 -o out/patch/prism-dt2.dkm
mise run patch:preflight -- out/patch/phrase-dt2.syx --plan out/patch/phrase-dt2.plan.json
```

The generated plan binds the executable and data ranges to the exact firmware
image by SHA-256. Pass its matching plan to preflight so embedded data, such as
the phrase-storage descriptor, is not decoded as executable instructions.
Keep the unedited plan emitted by the build. It records compiler/linker ranges;
it is not a signature authenticating a third-party firmware image. The release
preflight and its 14 scanner/preflight regression tests pass.

The `phrase` patch set includes the normal emulator providers plus phrase UI,
control, routing and storage. The default `device` and `emulator` sets are
unchanged. Install `phrase-dt2.syx` with the normal DigiKit firmware update
procedure, then upload `prism-dt2.dkm` to `modules/prism.dkm` and enable PRISM
in DIGIKIT > MODULES. Building these artifacts does not contact the device.
For example, from the repo in a native OS shell with a working MIDI port:

```bash
uv run --no-project --python 3.12 --with python-rtmidi python -m tools.device.dkupload --port Digitakt put out/patch/prism-dt2.dkm modules/prism.dkm
```

`pnpm nx:pilot run digikit/prism:test-conformance` selects the same `phrase`
image, keeps its module inventory scoped to PRISM, and builds/hashes the complete
provider dependency closure. Other modules retain their default conformance
image. `pnpm nx:pilot run digikit/prism:test-device` runs the module's native UI
capture and CANOPY/Overloop coexistence acceptance cases.

The module-owned concrete-core checks execute the actual assembled SHARC
entry through the production FX guard using explicitly leased synthetic
banks. They cover exact NEXT/PAST windows, tempo changes, history invalidation,
capture failure, immutable leases, handoff lifetime, safe interpolation/crop
extremes, all four shapes,
RATE OFF, diffusion tails, CLEAR, grain position telemetry, grain exhaustion
and hostile register contexts. The native UI
acceptance test lives beside them. Firmware timing, allocator safety and
selective routing have their own adapter tests; they are separate from the
isolated engine checks.

The descriptor reserves 48,000 instructions per block. The 47 concrete-core cases, which include the tape head in every
mode, feedback above 100 %, tape grains at their extremes and a saturated phrase handoff, all stay below it (asserted per
block, with the production FX guard). This is a DTII engine measurement, not a claim to meet the tighter generic
cross-device FX limit or a hardware cycle count. The native UI, capture and CANOPY/Overloop coexistence cases pass,
including their safety checks; the capture case also measures the always-on echo (its onset against TIME). Full-system
load stress and physical audio qualification remain separate requirements.

Results, deviations from the proposal and open points: [finding 354](../../../docs/findings/354-prism-tape-delay.md); the
original phrase effect: [finding 352](../../../docs/findings/352-prism-phrase-fx.md).
