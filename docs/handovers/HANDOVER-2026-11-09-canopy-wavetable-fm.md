# CANOPY wavetable FM — 2026-11-09

The user requested FM modulation of the selected wavetable while retaining
position and scanning. This change adds a sine phase modulator to each existing
wavetable voice. The four-operator FM source remains separate. The exact tested
firmware/module pair was subsequently installed at the user's request; see the
physical installation record below. No remote push was requested.

## Player controls

Choose a factory wavetable on TONE, then advance to the fourth SRC page. A/H
remain stock TUNE/LEV. B/C are FM DEP/RAT; D/G are signed ENV/WHEL amounts; E/F
are the existing wavetable timbre-envelope ATK/REL. The stock AMP envelope still
controls loudness. RAT uses four raw ticks per choice. DEP 0 with neutral
ENV/WHEL preserves the prior waveform. A useful starting point is DEP 30 and
RAT 1.00 or 2.00.

See [the design](../design/canopy.md#wavetable-fm-integration-cpsb) for the
control layout, shared timbre-envelope behavior, protocol and preset format.

## Implementation

- CPSB retains the 32-word transport; upper halves 28–31 carry the remembered
  WT-FM bank for every source. Earlier CPS9/CPSA packets still require these
  reserved halves to be zero. Validation precedes publication.
- Detail schema 5 echoes all 32 custom controls; native tune moves to word 42.
  Native tuning uses a separate pending flag rather than a shift beyond bit 31.
- CPST v5 is 144 bytes. Versions 1–4 decode with neutral WT-FM defaults and
  are not rewritten on load. Native tuning, levels and routing remain outside
  this preset record.
- The wavetable carrier retains its own phase. A sine modulator offsets only
  the table read phase, before frame/mip interpolation. Depth and ratio are
  smoothed; depth is quadratic and tapered at higher frequencies. Mip choice
  bounds modulation reachable during the current block, avoiding an abrupt
  high-frequency loss before modulation has ramped in.
- Six reserved words in each of eight standalone-FM histories hold WT-FM
  ownership and modulation state. The persistent instance stays 2,976 bytes;
  process/reset ceilings stay 100,000/1,600 instructions. Arp, tail, steal and
  source-transition histories follow existing lane/token ownership.
- Table address/interpolation work and register preservation were reduced to
  keep the added modulation within those ceilings. Neutral arithmetic order
  is preserved. No emulator-core or toolchain changes were needed.

## Verification

[D] The final raw-DSP runs pass 102 WT-FM behavior/budget cases (513.45 seconds),
114 existing wavetable cases (300.56 seconds), and 30 spectral cases (980.80
seconds). Logs: `out/canopy-wtfm-final-behavior.log`,
`out/canopy-wtfm-final-legacy-wt.log`, and `out/canopy-wtfm-final-quality.log`.
The spectral matrix covers all five tables at MIDI 60/84/96 and ratios 1x/8x.
Nonharmonic residual is -47.35 to -44.68 dB against a -40 dB threshold. It does
not distinguish aliases on the harmonic lattice or establish arbitrary-ratio
alias freedom.

[C] The original budget fixture clamped position to frame 7 and omitted the
second-frame interpolation path. The corrected interior-frame case exceeded
the ceiling until the interpolation/address/register work was optimized.
The expanded 36-case budget matrix now passes. The strict protected maximum
is 97,888 / 100,000 instructions; an additional AHD witness takes 97,806 and
reaches decay in all seven envelope sidecars. Both reset in 1,131 / 1,600
instructions. These are bounded emulator counts, not hardware CPU percentages.
See [finding 117](../findings/117-canopy-wavetable-fm.md) for the corrected
fixture, exact arithmetic ordering and measured limits.

[D] The final historical neutral-PCM comparison against `b816958f` passes all
30 scenarios and 27,840 bit-identical samples. Relevant ownership, oscillator,
wavetable and native AMP history also matches. It includes five tables, three
native AMP modes and both end/interior frames, with scan, table changes,
arp/tails and steals. The old baseline is allowed
250,000 instructions per block solely to complete the comparison; the new
renderer retains its strict 100,000 limit. Final receipt:
`out/canopy-wtfm-zero-ab.json`.

[D] Control transport: 138 passed, one initially skipped ColdFire instruction
probe. That probe passed separately using the existing local ColdFire runner
(`out/canopy-wtfm-cf-ceiling.log`), resolving the skip. Host preset/native
UI/control/SRC checks pass 154 cases. TypeScript and scoped formatting checks
pass. The first compiled UI/preset run reached its 60-minute overall timeout
after passing the WT-FM controls, audio, v5 recall and most standalone-FM
checks. It reported no functional assertion failure, but the overall run is
failed, not accepted (`out/canopy-wtfm-final-ui.log`). The overall bound is now
75 minutes; individual operation guards are unchanged. A complete rerun uses
a fresh card seeded with only the original sample fixtures and the exact
module, verified through the live protocol before selection/restart. No saved
presets, selections or project state from the timed-out run are reused.

The first seeded attempt exceeded the old 150-second cold-start hook bound;
this suite now allows 600 seconds for startup. The next reference-backend run
was deliberately stopped after the user requested test optimization; it is not
a pass or a functional failure. Its log is
`out/canopy-wtfm-final-ui-seeded.log`. The matching compiled proof now passes
with the audited DT2-specific AOT daemon in **403.12 seconds total** (398.354
seconds in the test body). Log: `out/canopy-wtfm-optimized-ui.log`. The fresh
`out/canopy-fm-native-evidence.json` includes WT-FM controls, full/silent/scan
audio, all four standalone-FM algorithms, source-bank memory and v5 recall.
Native LEVEL reaches exact silence; source LEV reduces WT-FM RMS from 0.01887
to 7.90e-8 and standalone FM from 0.01837 to 1.39e-7. Native controls, panel
ownership, held values, tuning, list sensitivity and runtime guards also pass.
The firmware/module hashes below were logged by the passing run. This is a
complete feature proof, not a clean paired whole-suite timing comparison:
card seeding, input batching, backend and cache warmth differ from the earlier
60-minute failed attempt.

Test execution improvements are separate from the frozen synth artifacts:
`a46c8ddc` adds opt-in acknowledged encoder batches (19 host cases pass), and
`637a6f18` streams large card hashes without changing their cache key (62 named
ready/launcher/fingerprint cases pass). Stage 5 opts continuous controls into
16-unit batches, reducing the expected packet count from 524 to 46 while
retaining single-step list/mode and sensitivity checks. Every batch still
requires native input acknowledgement, settled DSP state and an exact result.
This packet count is static, not a wall-time benchmark. Existing Vitest file
workers already provide concurrency; a single shared device cannot safely run
these stateful steps concurrently.

[D] The generated DT2 DSP backend passes three alternating native A/B pairs:
all six stock-sample runs have identical PCM, canonical DSP state and guest
instruction counts. Median paired DSP CPU cost is 0.07936948 of AOT-off cost
(about 12.60 times faster for that DSP workload). ColdFire remains reference
execution; other fast-tier settings are held fixed. This is backend evidence,
not the CANOPY full-suite duration. See
[finding 118](../findings/118-dt2-emulator-test-execution.md) and
`out/canopy-dt2-aot-paired/receipt.json`. The pinned daemon is identified in
`out/canopy-dt2-aot-daemons.json`; its audited key is
`e9f803af54e6bf533c229af822fa2d1cf46d9d98425352b82686fd62e3ca0f9a`.
After the compiled proof passed, its DT2 entry was promoted to the local ignored
`dist/daemons.json`, so subsequent DT2 tests use it by default.

[D] Final-build captures show zero differing pixels in native TUNE and source
LEV between TONE and WT FM, both normally and with SRC held. Logical exclusive
bounds are TUNE `(23,11)-(48,37)` and LEV `(101,38)-(124,64)` on the 128x64 OLED;
the saved PNGs use 4x scaling. The adjacent page indicators differ as expected.
Native values match at TUNE 0.02 and source LEV 127. This establishes page-to-page
consistency, not a new comparison against stock-default values. Captures and
receipt: `out/canopy-wtfm-native-ui/` and `pixel-comparison.json`.
These preserved images came from the first run of the same frozen firmware;
the successful test's temporary captures are removed by the normal artifact
cleanup, leaving its final diagnostic image.

The affected `mise run test` gate was run once against `b816958f`. It passed 24
tasks, including regression host tests/types, lint, module packing and shared
generation checks, but is not green. It reports formatting failures in 16
unchanged TypeScript files and two unchanged Python files, 42 existing layer
violations, and a missing `out/native/dn2-audio/gen` prerequisite. The missing
core also blocks the DT2 coupled-builder target. Conformance, stock, fast-roots
and the selected Python-suite task consequently did not run. The explicit
DTII proof uses the audited DT2-only daemon build and is green. Log for the
wider gate:
`out/canopy-wtfm-gate.log`.

## Artifacts

The matching firmware is `out/patch/canopy-wavetable-fm.syx`, 2,074,784 bytes,
SHA256 `1e0ec14c05ccbdffa79cd498f9ff0edfd00596b95de7237e5e9dc7544026e592`.
The module is `out/patch/canopy-wavetable-fm.dkm`, 235,924 bytes, whole-file
SHA256 `d6ba479cc701212ad8e49df3754d5b2617fc75d4a7fcd205136ad82c078448de`.
The packer's internal content hash is a different digest and must not be used
as the whole-file installation receipt.

Firmware preflight passes: container 1,637,008 / 3,145,728 bytes; stored DSP
199,431 / 1,048,576, decoded DSP 355,140 / 1,048,575; staged MAIN
3,664,968 / 4,193,280. Core `play-cf.dko.json` and `play-dsp.dko.json` were
explicitly rebuilt before patching. Shared generated-file checks pass.

## Physical installation, 2026-11-09

Installed the exact artifacts above. The DTII rebooted from image
`8b8889b9338102fd` to **`826a473886d34148`**, runtime 0.5.4. The new CANOPY module
was uploaded and freshly read back before flashing, without an intermediate
restart. This ordering matters: the new core sends CPSB/version 11, which the
old module does not accept; the new module accepts both old CPSA and CPSB.
The DTII validated the complete firmware image and accepted the flash commit.

Post-reboot INFO has no safe mode, crash, open upload or read-only card. CANOPY,
Overloop and remote retain the same selection, with zero module faults and
guards enabled. CANOPY retains type 16. All three module files were read back;
CANOPY matches the final package hash above, and Overloop/remote match their
pre-install backups. Arena use is 459,296 / 524,288 bytes; DSP-area use is
230,528 / 524,288 bytes. No additional restart was needed.

All four saved presets and MIDI routing entries are byte-identical to the
backup. External MIDI, USB and DIN remain enabled on channel 1. The current
standalone-FM sound differed from every occupied preset slot. The user first
authorized installation without retaining those unsaved edits; a revised choice
to save over slot 4 arrived after the flash had been committed. The reboot reset
the unsaved controls, so slot 4 was left untouched rather than overwritten with
the reset sound. The exact unsaved sound was not preserved or reconstructed.

An initial post-boot capture showed a stale LINK LOST heading. Refreshing SRC
restored the normal CANOPY ARP screen; the held-values capture shows track
LEVEL 67. Saved settings were checked again after this navigation. No physical
audio was recorded, and no new sound/timing qualification is claimed.

Backups and screenshots: `out/canopy-wtfm-install-backup/`. Installation receipt:
`out/canopy-wtfm-hardware-installed.json`. Flash log:
`out/canopy-wtfm-hardware-flash.log`. The final page is ARP.
