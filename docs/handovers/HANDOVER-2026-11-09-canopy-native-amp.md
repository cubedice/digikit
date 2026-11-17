# CANOPY native AMP correction

Date: 2026-11-09. Branch: `codex/short-loop-proof`. Base: `bb9f9520`.

The user reported that the track AMP release did not control CANOPY's tail and
that AHD sometimes became silent. The earlier local attack/release envelopes
ended CANOPY audio before the shared stock envelope could release. A finite
shared AHD envelope could also finish while a key remained held, leaving later
notes without a new shared-envelope trigger.

## Change

Each CANOPY note now follows the track AMP configuration independently. The
opt-in V6 input carries the actual DTII OS 1.17 AMP configuration as scalar
values: attack/decay/release coefficients, hold sample count, sustain level and
restart option. It carries no firmware
pointers. The existing instance size remains 2,976 bytes; seven envelope
sidecars use its reserved words. Polyphony and the process/reset ceilings stay
unchanged.

ADSR note-off begins that note's native release. Fixed AHD hold completes its
own shape even after note-off; NOTE hold follows the gate. A finished AHD key
retains its identity until release, while subsequent keys start independent
envelopes. Infinite release is retained; panic, replacement and unload still
use the bounded de-click/reset path. Finite exponential tails retire below
amplitude 1e-7. Voice stealing copies the envelope together with its voice,
including atomic event rollback.

The adapter bypasses the shared AMP multiplication only for the current
successfully processed native-envelope block and the owned stock stereo call.
Old descriptors, invalid snapshots and stale ownership retain the legacy path.
The stock record and subsequent filter, levels, pan and FX are preserved.
The snapshot is the preceding completed DSP configuration, at most one
32-sample block old.

SOUND SETUP now says `ENVELOPE: AMP PAGE` instead of offering duplicate attack
and release controls. Older preset fields remain readable for compatibility;
the native AMP page is authoritative when V6 is available. This release needs
both the rebuilt core firmware and the new CANOPY package.

## Evidence and remaining checks

The implementation, local packages and focused emulator acceptance are complete.
The final compiled artifacts pass the native-knob/audio proof for finite ADSR
release and repeated AHD notes. Physical qualification and the wider merge gate
remain open.

- Registry/version/bounds checks: 120 passed (`out/canopy-amp-registry.log`).
- Runtime descriptor/admission checks: 138 passed (`out/canopy-amp-rust.log`).
- Preset/menu host checks: 78 passed (`out/canopy-amp-preset-host.log`).
- Adapter/carrier checks: 199 passed, one skipped
  (`out/canopy-amp-adapter-final.log`). The skipped ColdFire source probe needs
  its specific runner/compiler fixture; the compiled firmware proof covers the
  actual AMP-page path separately.
- Actual stock-firmware instruction comparisons: 16 passed
  (`out/canopy-amp-stock-final.log`), covering native curves and native/legacy
  hook isolation. See [finding 135](../findings/135-canopy-carrier.md).
- Native-envelope regressions: 90 passed (`out/canopy-amp-native-final.log`),
  including fallback, native tuning, three sound families, repeated AHD,
  independent release, reset, stealing, rejected-event rollback and hostile
  circular-address context.
- Final FM budget matrix: 48 passed (`out/canopy-amp-budget-fused.log`). The
  unchanged ceilings are 100,000 process and 1,600 reset instructions. The
  observed maxima are 99,385 and 1,131 respectively. These are instruction
  contract checks, not physical CPU measurements.
- Three FM compute/load fusions remove 1,344 instructions from a seven-group
  callback. A paired before/after comparison covers six native/legacy scenarios
  and 24 callback records; every PCM, voice-state and status word is identical.
  Protected ADSR-boundary handoff changes from 100,729 to 99,385 instructions.
  Evidence: `out/canopy-amp-fusion-ab.log` and
  `out/canopy-native-fm-fusion-ab.json`. The normal tests retain their original
  ceiling; only the comparison of the saved over-budget implementation permits
  extra diagnostic execution.
- Relocated raw carrier fixture: one named proof passed, all 17 includes retain
  the same resolved files and hashes (`out/canopy-amp-relocated-fixture.log`).
- Compiled stock AMP-page/audio proof: one test passed in 1,531.50 seconds
  (`out/canopy-amp-compiled-final.log`, `out/canopy-native-amp.json`). It uses
  `CANOPY_NATIVE_AMP_ONLY=1` with the existing Stage 6 entry, and stops after AMP
  acceptance; this is not a repeat of the full FM/Overloop/upload scenario.
  REL 0 has held/tail RMS 0.01067338 / 0.000000866 and retires its voice. REL 80
  has held/tail RMS 0.01068884 / 0.00918942, with released-note amplitude falling
  from 0.903626 to 0.811827 across the tail capture. ColdFire target 0x5000,
  DSP index 80 and finite q bits 0x3f7fff03 agree before and during the note/tail.
  AHD's first held note reaches zero; a second note then produces peak 0.01587683
  while both held tokens remain present. Cleanup and fault checks pass.
- The acceptance helper drives the native knobs and reads their requested Sound
  value as well as the DSP snapshot. MODE uses 30-detent turns toward its clamped
  endpoints; RELEASE uses at least three normal detents per event to match its
  integer-step accumulator. Formatting and TypeScript checks pass.
  The earlier endpoint proof (`out/canopy-amp-compiled-endpoint.log`) also passed
  AHD but did not establish a finite long release: it sampled DSP index 126
  before the note and later observed amplitude one. Use the final REL 80
  evidence above for the finite-release claim.
- The affected gate ran once with `DIGI_GATE_BASE=bb9f9520 mise run test`
  (`out/canopy-amp-gate.log`): 59 successful tasks and 14 failed tasks in 9m30s;
  dependent tasks did not run. This is not a passing merge gate.
- The cached-toolchain retry passes all 339 patchkit Rust tests
  (`out/canopy-amp-patchkit-recheck.log`); the 59 original failures all occurred
  before assertions while looking for the ColdFire toolchain. The cached
  toolchain also supplies the missing Wasm target, and its owner build passes
  (`out/canopy-amp-wasm-recheck.log`).
- The final core rebuild made `modules/core/fw/reference.json` stale; it was
  regenerated and `gen --check --scope shared` passes. Five newly introduced
  layout warnings were fixed by moving the raw fixture into CANOPY's tests.
  Remaining layout warnings (41), 16 TypeScript formatting failures, two Python
  formatting failures and the patchkit Rust formatting failures are unchanged
  from the base. Nx host checks timed out in graph subprocesses, without a
  baseline rerun establishing reproducibility. Missing DNII generated-core
  prerequisites still block broader native/device checks. None of the skipped
  checks is claimed as passing.

Native temporary storage is `/var/tmp/digikit-canopy-fm` to support Unix sockets.

Artifacts are `out/patch/canopy-native-amp.syx` and
`out/patch/canopy-native-amp.dkm`; do not commit either. The firmware hash is
`c79992858ae3ea81aef58b04c70b17956e9458418ded803f5a2b14221a9d9f99`.
The final 233,508-byte module has whole-file SHA256
`1fb39ab17c6a98c0b88f51cffbeef4d24a5aabe2380e4ead588203eab2cc93e4`.
The initial, time-limited compiled run loaded the pre-fusion package, preserved as
`out/patch/canopy-native-amp-prefusion.dkm`, SHA256
`398c5432ec300dad94d18ce67cbf98164a60b49f10909e13709cae0afda60db5`.
The final package differs only in the independently reviewed, bit-exact FM
compute/load fusions described above. The core firmware is identical. The final
acceptance log uses the exact final hashes listed here and passes both the finite
release and repeated-AHD checks.

The implementation phase used emulator checks. The subsequent user-requested
installation is recorded below; physical sound and timing qualification remain
open.

## Physical installation, 2026-11-09

At the user's explicit request, installed the exact firmware and module above.
The DTII rebooted from image ID `56edcafce3ba99d0` to `8b8889b9338102fd`.
Firmware was installed first, followed by the new CANOPY package and a second
restart of the existing selection. The normal boot counter was allowed to clear
between restarts. No selection writes were needed.

Final INFO reports no safe mode, crash, open upload or read-only card. CANOPY,
Overloop and remote are loaded with zero faults and their guards enabled.
CANOPY retains machine type 16. Its final DSP allocation is 225,664 bytes; total
arena use is 455,168 / 524,288 bytes and DSP-area use is 228,672 / 524,288 bytes.
The CANOPY file was read back and matches the final module SHA256. Overloop and
remote readback hashes match their pre-installation backups.

Backups are in `out/canopy-native-amp-install-backup/`. The current Vocal PLAY
sound was preserved in previously empty preset slot 1 as a 96-byte v3 record,
then loaded after the final restart. Existing presets and routing entries remain
byte-identical; USB/DIN and external MIDI remain enabled on channel 1. The
restored ARP values screen shows track LEVEL 67 and source LEV 93, matching the
pre-installation capture. Native TUNE was captured beforehand but was not
separately rechecked afterward. The optional final navigation through TONE to
SOUND was declined, so no further device action was taken; the final page is ARP.

Receipt: `out/canopy-native-amp-hardware-installed.json`. Flash log:
`out/canopy-native-amp-hardware-flash.log`. The receipt and captures establish
installation, settings preservation and module health. No physical audio was
recorded and no new sound or timing qualification is claimed.
