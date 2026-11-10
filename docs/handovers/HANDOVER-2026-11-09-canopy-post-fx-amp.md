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
[finding 119](../findings/119-canopy-post-fx-amp.md). Focused DSP, SDK, registry
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

The physical DTII still runs the preceding wavetable-FM firmware/module pair.
Only temporary release measurement was performed: REL was restored to 15,
confirmed on the native values screen. No presets, modules or firmware were
changed. Physical qualification of V7 remains open.

Before a later installation, preserve the current sound and settings. All four
saved preset slots were occupied during the previous installation. The user
subsequently requested saving the current sound over preset 4; do not assume
the presently edited sound is already saved or discard it silently. The older
installer in `out/canopy-wtfm-install.py` pins the preceding artifacts and must
not be reused unchanged. Stage the matching new module before flashing the
new firmware, with no intermediate restart into an incompatible pair, then
verify identity, module readback, presets/routing and module health.

No push or new installation was requested for this bug-fix turn.
