# PRISM

PRISM records a stereo phrase from selected sources and plays clocked,
windowed grains from it. Its normal FX entry contains LOOP, GRAIN, MOVE and
SPACE pages; UP/DOWN moves between them, and H opens the input selection
overlay. LEVEL/DATA controls the single LIVE–PRISM FADE. The routing adapter
replaces only the sources committed with the held phrase.

The first target is Digitakt II OS 1.17. The package requires the version 1
phrase-control, phrase-storage and phrase-routing capabilities. It must not
be loaded as an ordinary master effect on firmware without those providers.
The imports are mandatory; the engine contains no firmware addresses.

## Controls

FUNC + FX advances through the normal effect list; PRISM occupies one entry.
Within PRISM, UP/DOWN selects LOOP, GRAIN, MOVE or SPACE. The last visited
page is remembered for the loaded instance. Hold FUNC + FX for the native
global-FX show-values mode; release the chord to return without advancing.
FUNC + NO clears the held phrase and pending take, returns FADE to LIVE, and
keeps the current PRISM page or IN overlay open. Ordinary NO still closes IN
or exits the FX view normally.

| Encoder | LOOP | GRAIN | MOVE | SPACE |
| --- | --- | --- | --- | --- |
| A | REC | SIZE | POS | DIFF |
| B | FROM | RATE | SCAN | DEC |
| C | TAKE | TUNE | REVR | HPF |
| D | CUE | SHAP | WDTH | LPF |
| E | STRT | SPRY | Diagram | Diagram |
| F | LEN | JITR | Diagram | Diagram |
| G | Crop and duration | Grain activity | Diagram | Diagram |
| H press | IN | IN | IN | IN |

LEVEL/DATA controls FADE on every page. There is no second level control.
Discrete lists require four raw encoder ticks per choice. Diagram cells and
turning H have no hidden parameter.

In IN, tap a TRIG to select it for the next capture, or FUNC + TRIG to select
only that internal track. Hold a TRIG and turn LEVEL/DATA to set its capture
gain without toggling its selection on release. A selects external OFF/L/R/ST;
B sets linked external gain. YES arms or cancels one take. H/NO returns to
the same control page; FUNC + FX closes IN and advances to the next effect.
Input editing locks while armed or recording; FADE and cancellation remain
available. The brief phrase handoff also locks editing and shows BUSY; FADE
remains available. Solid tiles show held membership and outlines show next selection.
The header reports LIVE ERR when routing has restored the live sources after
a fault, and LINK LOST when the DSP status is stale.

The [layout board](../../../docs/design/assets/prism-controls.svg) explains the
four pages and input gestures using authored vector shapes and system text.
The [design proposal](../../../docs/design/prism-granular-fx.md) records the
routing behavior and remaining release qualification.

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

## Grain and space engine

The initial engine has eight total stereo grains, including old readers in
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
the wet return. The recording banks are never feedback destinations.
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

The descriptor reserves 48,000 instructions per block. The 35 concrete-core
assertions measured a worst block of 39,849 instructions, including the
production FX guard, during a saturated phrase handoff; combined control
extremes measured 36,325. This is a DTII engine measurement, not a claim to
meet the tighter generic cross-device FX limit or a hardware cycle count.
The native UI, capture and scoped CANOPY/Overloop coexistence cases pass, including
their safety checks. Full-system load stress and physical audio qualification
remain separate requirements.

The complete affected gate was run; it is not green. Its broader failures
include missing DNII inputs, unrelated CANOPY fixtures and existing style/
layer checks. Scoped PRISM results and the corrected integration failures
are recorded in [finding 352](../../../docs/findings/352-prism-phrase-fx.md).
