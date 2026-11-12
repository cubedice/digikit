# CANOPY release and the stock track-effects order

DTII OS 1.17, 2026-11-09. The user reported that AMP REL changed the time until
silence, but the sound stayed loud and then stopped abruptly.

## Reproduction and cause

**[D]** USB audio from the installed wavetable-FM build reproduces this with the
user's current FM TWIN sound. ADSR has attack 0, decay 9, sustain 39 and release
15. The track FX page shows bit reduction 4.85, overdrive 0 and sample-rate
reduction 2. A held MIDI note is released after 1.5 seconds. Its output stays
near -57.35 dBFS RMS for roughly another quarter second before falling rapidly.
A temporary increase to REL 37 extends this plateau to over two seconds; the
final falling edge remains much faster than the selected envelope. Release
was restored to 15, confirmed on the native values screen. No firmware,
presets, selection or other sound parameters were changed for these captures.

The captures have no input overflows and their lengths agree with elapsed time.
Local evidence is `out/canopy-release-current-audio/`,
`out/canopy-release-long-audio/`, `out/canopy-release-inspect/` and
`out/canopy-release-effects/`. They are user audio/screenshots and are not
committed. The installed firmware image ID is `826a473886d34148`; CANOPY,
Overloop and remote report zero faults.

**[D]** The stock voice path calls bit reduction at `0x1c6b21` (entry
`0x1cdd8b`) before AMP at `0x1c6f29` (entry `0x1cbb57`). CANOPY's V6 path instead
multiplies each note by its envelope inside the source renderer and bypasses
the later shared AMP stage. This puts the envelope before the nonlinear track
effects.

The actual stock bitcrusher explains the plateau. At a raw normalized BR
parameter of `32/127`, negative inputs from -0.001 through -0.0000001 all
produce -0.0066081285, while their positive counterparts produce zero. Exact
zero produces zero. The disappearing input is therefore converted to a
fixed-amplitude waveform until rounding or final voice retirement removes it.
BR 0 follows the input instead. This is measured by 45 bounded calls of the
hash-pinned stock DSP instructions, including their SIMD path; it is not a
replacement quantizer model. Receipt: `out/canopy-release-br-probe.json`.

**[D]** Stock coefficient mapping and CANOPY's per-note envelope calculation
are correct in the inspected cases. REL 15 has multiplier
0.9991319179534912, REL 37 has 0.9999001026153564, and REL 80 has
0.9999849200248718. The expected level is multiplied by that coefficient per
sample, not held for a duration and then ramped. The installed package's
32-sample envelope buffer is contiguous and all inspected encoded references
point to the right offsets. Sixteen focused raw-render cases also attenuate
PCM correctly through early, middle and quiet release checkpoints for legacy,
wavetable, standalone FM and wavetable FM sources. These isolated tests did
not include stock track bit reduction and therefore did not expose the fault.

## Correction contract

Keep each note's independent native envelope. Extract a common envelope from
the combined pad/arp output, divide the source PCM by it before the stock
track effects, then multiply by it at the original stock AMP position. For a
single note, the track effects now receive the oscillator before its AMP
envelope, as on the stock machine. The final volume follows AMP even when bit
reduction creates a nonzero quantization floor.

The common envelope must account for audible pad, arp and shared-tail lanes,
the actual PAD/ARP blend, and bounded reset/steal ramps. A muted lane must not
hold it high. Zero common gain means exact silence; division by zero is never
attempted. The existing per-note relative envelopes remain in the normalized
mix. Shared nonlinear effects still couple overlapping voices; separate
effects per voice are outside this design. Stateful filters intentionally see
a different input ordering, so this is not a claim of bit-identical old/new
filtered audio.

The opt-in `POST_AMP` capability uses input V7, extending V6 with a borrowed
adapter-owned 32-float output buffer and its length. The module returns its
existing enveloped PCM and writes the common gain. The adapter validates the
completed output, normalizes it, and publishes the gain only for the current
successfully rendered owner/frame. The existing stock AMP hook reapplies it
to the matching stereo call. Older V6 descriptors keep their existing path;
the reserved V6 word is not reinterpreted. Failed, incomplete, stale and
unowned output must not publish a post-AMP envelope.

## Implementation and focused verification

**[D]** The SDK, runtime admission, DSP registry, instrument adapter and CANOPY
implement V7 together. Valid descriptor flags are 0, 1, 3 and 7. V7 adds two
words to the unchanged V6 prefix: a borrowed, aligned 32-float output and its
length. The adapter pre-fills it with NaNs, checks complete finite gains in
`[0, 1]`, and validates the normalized PCM before publishing either owner.
Subnormal gains and PCM are handled explicitly; zero gain yields exact positive
zero. Failed callbacks, partial writes, stale frames and lifecycle resets cannot
leave a published gain behind. V6 and unavailable-native-snapshot fallbacks keep
their previous behavior.

Focused checks pass:

- 260 adapter/carrier/stock tests, with one existing ColdFire fixture skip;
  28 final V7 normalization cases, including the final subnormal-PCM branch.
- 158 registry tests, 139 runtime SDK tests, three packer descriptor tests and
  three SDK host tests.
- 55 final CANOPY V7 tests: rendered stock-coefficient release across legacy,
  wavetable, FM and wavetable-FM; independent polyphonic envelopes; muted blend
  endpoints and arp tails; reset ramps; source transitions; and malformed input.
- Eight paired V6 comparisons against `67dbd000`: 6,912 samples and complete
  744-word instance states are bit exact across all four FM algorithms, AHD and
  ADSR, steals, releases and wavetable-FM transitions. This covers the FM
  instruction scheduling change, not audio after the newly reordered effects.
- Actual stock bitcrusher plus AMP-hook regressions reproduce the former
  plateau and verify exponential attenuation followed by exact silence.
- 84 strict processing-budget cases keep the existing 100,000-instruction
  process and 1,600-instruction reset limits. Measured maxima are 99,849 for
  FM, 99,386 for wavetable FM and 1,131 for reset. These are bounded emulator
  instruction counts, not a physical hardware CPU-load measurement.

The lane tests also caught redundant global declarations causing the raw test
linker to resolve the pad and arp scratch buffers as code symbols. Removing the
declarations leaves the PKBSS data exports unambiguous. The regression checks
both relocated pointers and independently written lane data. A separate packed
image audit checks all six references: pad `0x20036684`, arp `0x20036704`, two
distinct 128-byte buffers at the package's nominal base.

Local receipts are `out/canopy-post-amp-adapters-final.log`,
`out/canopy-post-amp-play-final.log`, `out/canopy-post-amp-registry.log`,
`out/canopy-post-amp-sdk-rust.log`, `out/canopy-post-amp-pack-rust.log`,
`out/canopy-post-amp-sdk-host.log`, `out/canopy-postamp-final.log` and
`out/canopy-postamp-pcm-ab.json` and `out/canopy-postamp-budget.log`.
Independent source review found no actionable
integration or register-preservation defect.

## Compiled playback measurement

**[D]** The rebuilt pair passes the compiled native-AMP subtest, using the
native controls and final stereo audio. At REL 40 and BR raw 64, the stock
release coefficient is `0.9999173879623413`. Three 100 ms RMS windows starting
0.25, 0.65 and 1.05 seconds into the release measure 0.004384209,
0.000896792 and 0.000183627. The successive drops are 13.78399 and 13.77511 dB;
the coefficient predicts 13.77770 dB per 0.4 seconds. Thus bit reduction no
longer keeps the tail at a fixed volume. The same subtest passes the REL 0/80
comparison and a new audible AHD note after an earlier held note completes.
Receipt: `out/canopy-native-amp.json` and its corresponding captures.

The complete compiled proof also passes wavetable and FM control/audio checks,
four-note arpeggiation and shared tails, presets, Overloop coexistence and
uploads. Test duration is 402.77 seconds, total runner time 412.20 seconds.
Receipt: `out/canopy-post-amp-compiled-final.log` and
`out/canopy-stage6-coexistence.json`.

The matching local outputs are:

| Artifact | Bytes | Whole-file SHA-256 |
| --- | ---: | --- |
| `out/patch/canopy-post-fx-amp.syx` | 2,075,296 | `4c344e745ec288a7409a4df3426accb23972f1ced9d14b989d2a580be4753f72` |
| `out/patch/canopy-post-fx-amp.dkm` | 237,076 | `19fafa6524a53f87ae3a8ead177ed32c17e3a4130324ee26a33dec5e64b6c67f` |

Firmware preflight passes. The package's embedded digest, printed by the packer,
is `677c220c3d6e76ebb41d19f38d2251e88bba67a92d90e40a168edf300b8d5972`;
it is calculated with its digest field zeroed and differs from the whole-file
SHA-256 above.

**[O]** The required affected gate was run with `DIGI_GATE_BASE=67dbd000`.
It is not green: 22 tasks passed, including the runtime/SDK/patchkit Rust suites,
regression typecheck and desktop/web checks. The gate retains existing Python,
TypeScript and Rust formatting failures and 42 architecture-layer violations.
The desktop Rust and DN2 coupled-build targets also fail because
`out/native/dn2-audio/gen` is absent. After these failures and the broad Rust
suite result were collected, the remaining sweep was stopped at 20m 29s;
`digi-coupled:build-dt2` was interrupted and queued device/pack/Python checks did
not complete. This does not replace their coverage with the focused proof.

The host suite also exposed an unchanged ABOUT-page 30 ms timeout assertion
(491 other tests passed). That single test passes on its targeted rerun; the
rejection timer can precede the separate wall-clock expiry predicate. No
unrelated timeout or formatting code was changed. Receipts:
`out/canopy-post-amp-gate.log` and
`out/canopy-post-amp-remote-about-recheck.log`.

**[D]** The matching pair was installed on the physical DTII on 2026-11-10 at
the user's request. The stock upgrade RPC accepted the image and rebooted into
image ID `2c601dc3a9bf3d38`. CANOPY package readback matches the whole-file hash
above; Overloop and Remote are unchanged. All three modules report zero faults
with guards enabled, and the runtime reports no crash, safe mode or open upload.

The current edited CANOPY sound was saved over preset 4 as previously
authorized, validated as a 144-byte CPST v5 record, and loaded after reboot.
The previous preset 4, other presets, settings and installed modules were
backed up first. The saved settings were compared after installation, including
unchanged USB/DIN routing. Native AMP screenshots retain A 0, D 9, S 39, REL 15,
ADSR mode, AMP volume 64 and track level 67. The device was left on AMP.
Local receipts are `out/canopy-postamp-hardware-installed.json` and
`out/canopy-postamp-install-backup/`.

**[O]** Post-fix physical audio qualification remains open. Installation,
readback and displayed native values do not replace a new USB recording of the
release curve. Repeated holds captured the same remembered filter, FX and LFO
pages; those screenshots are not coverage of every secondary page.
