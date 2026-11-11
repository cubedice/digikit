# CANOPY compact controls, latch removal and native MOD

Branch: `codex/short-loop-proof`, based on `4c73e440`.

This revision removes keyboard/arp latch, consolidates the SRC pages (TUNE/LEV
only on SOUND A/H, WAVE on SOUND D, clock/BPM on ARP G/H, the complete
position envelope on wavetable TONE, a MODULATION settings group) and exposes
six fixed stock MOD destinations per sound family through an optional
parameter-code callback in the machines adapter, with a per-block adapter that
applies the completed stock LFO frame to a scratch copy of the CPSB bank.
Design, mapping, narrow firmware checks and results are in
[finding 120](../findings/120-canopy-controls-modulation.md).

Verification on 2026-11-10:

- Focused Python suite: 283 passed, 1 skipped (`out/canopy-controls-mod-pytests.log`).
- Compiled Stage 5 proof passed in 475.16 s and Stage 6 in 466.09 s on the
  release pair `out/patch/canopy-controls-mod-release.{syx,dkm}` (image
  `b6ac5d85…381f`, package `19fafa65…c67f`, preflight OK;
  `out/canopy-controls-mod-release-compiled.log`,
  `out/canopy-controls-mod-release-stage6.log`).
- The first compiled Stage 6 run failed only in the proof's picker walk: the
  stock DEST popup lists Tune and Sample Level before the six custom slots,
  moves one item per UP/DOWN press (encoder ticks are accelerated), its
  confirmation toast swallows panel input, and every [MOD] press advances the
  LFO page. The walk now follows the list order and counts [MOD] presses.
- Patchkit pin `declared_machines_patch_to_the_pinned_images` moved to the new
  DT2 digest (section 3 only); `modules/core/fw/reference.json` regenerated.
- The affected gate (`out/canopy-controls-mod-gate.log`) otherwise fails only
  on this host's environment limits or pre-existing format/layer findings in
  untouched files; see the finding.

Working-copy hygiene: the previous session's edits had CRLF line endings; the
changed files were normalized to LF before the objects were rebuilt.

Installation on the physical DTII follows this handover (see the installation
section below once recorded). Physical audio qualification of the MOD routes
remains open.
