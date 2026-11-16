# CANOPY AMP release after track effects

Branch: `codex/short-loop-proof`, based on `67dbd000`.

The user reported that AMP-page REL changed the time until silence, while the
note stayed loud and then ended abruptly. USB recordings reproduce this on
their FM TWIN sound with bit reduction enabled. CANOPY V6 put each note's AMP
before the stock track bitcrusher and bypassed the later AMP stage. The
bitcrusher produced a fixed nonzero residue as the input faded.

V7 factors a common envelope out of CANOPY's per-note mix before the effects
and reapplies it at the stock AMP position. Independent relative envelopes,
pad/arp weighting, shared tails and bounded reset/steal fades are retained.
Both core firmware and CANOPY must be updated together. Older V6 modules retain
their existing path on the new firmware.

Implementation, measurements, output hashes and limits are in
[finding 143](../findings/143-canopy-post-fx-amp.md). Focused DSP, SDK, registry
and adapter tests pass. The complete compiled native-AMP/WT/FM/Overloop proof
passes in 402.77 seconds. In the new bit-reduced audio regression, successive
release windows fall 13.78399 and 13.77511 dB against a 13.77770 dB prediction.
The original 100,000/1,600 instruction limits are unchanged.

Matching local files:

- `out/patch/canopy-post-fx-amp.syx` and its `.plan.json`.
- `out/patch/canopy-post-fx-amp.dkm`.
- `out/canopy-post-amp-compiled-final.log`, `out/canopy-native-amp.json` and
  `out/canopy-stage6-coexistence.json` contain compiled acceptance evidence.
- `out/canopy-post-amp-gate.log` records the required affected gate, including
  pre-existing format/layer failures and one unchanged ABOUT timeout test that
  passed its single targeted rerun. The DN2 generated core is missing, blocking
  two additional targets. After 22 tasks passed and these failures were
  collected, the remaining sweep was stopped at 20m 29s; DT2 coupled rebuild
  was interrupted and queued checks did not complete. The full DTII CANOPY
  playback proof above used the separately verified existing emulator.

The user subsequently requested installation. On 2026-11-10 the physical DTII
accepted the matching pair and rebooted into image ID `2c601dc3a9bf3d38`.
CANOPY readback matches the hash in finding 143; Overloop and Remote remain
unchanged. All three modules report zero faults with guards enabled, with no
crash, safe mode or open upload.

Before installation, all presets, settings and installed modules were backed
up in `out/canopy-postamp-install-backup/`. The edited sound was saved over
preset 4 as previously authorized, validated as CPST v5 and restored after
reboot. The previous preset 4 remains in the backup. Saved settings and USB/DIN
routing compare unchanged against the preservation baseline. Native AMP still
shows A 0, D 9, S 39, REL 15, ADSR, volume 64 and track level 67; AMP is selected.
The installation receipt is `out/canopy-postamp-hardware-installed.json`.

The reviewed local installer is `out/canopy-postamp-install.py`, orchestrated
by `out/canopy-postamp-install-all.py`. Do not rerun it: its one-time save guard
and old-image checks intentionally reject an already completed installation.
The module was staged before the stock firmware-upgrade RPC, without an
intermediate restart into an incompatible pair.

Physical audio qualification of V7 remains open; this installation did not
record a new release curve. The native screenshot comparison only covers the
remembered pages, not every secondary filter/FX/LFO page. No push was requested.
