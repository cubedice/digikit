# CANOPY / Overloop upstream rebase — 2026-11-05

Rebased `codex/short-loop-proof` onto upstream/main `9f071b4e7003f62f767b0f3a26bd639642f50a22`. The original tested branch is preserved locally as `backup/short-loop-pre-rebase-20261105` at `0ca0945a07f27ffaf18ab65045d8c4d868cd0eac`. Fork main is synchronized with upstream.

## Compatibility decisions

- Upstream DKDS 5 remains the sample-bank / per-track sample-table format. Persistent instrument sections now use DKDS 7 (host metadata) and DKDS 8 (executable ranges); the live instrument loader accepts only 8. Rebuild old CANOPY packages using this checkout: pre-rebase instrument formats 5/6 are rejected. Sample and instrument declarations cannot share a section.
- Instrument declaration kind is 13; upstream sample declarations retain 6/7. Regenerated SDK headers and rebuilt the instrument declaration fixture and CANOPY cache.
- Restored upstream sample packing, stereo declarations, parameter latching, native pages, resource ceilings, file retirement and registry image bounds while preserving the instrument lifetime / transfer-lease protections.
- Deferred instrument publication retains the original CF image size for registry revalidation. SRC-page selection falls back to upstream native-page counts when no generation-scoped CANOPY registration exists.
- Corrected the DT2 DSP import table count to include all eight entries. Firmware interface generation accepts bounded SHARC secondary registers (`s0`–`s15`).
- Migrated the older CANOPY/Overloop regression imports to upstream's split helpers and emulated-time waits.

## Validation

Passed in WSL:

- dkneeds: 16 unit tests; runtime SDK: 329 unit tests.
- patchkit: 172 unit tests and 19 firmware-interface tests.
- standalone runtime: 53 unit tests; timer integration: 15 tests.
- machine page / DSP-link adapter tests: 74 passed, 2 skipped.
- CANOPY native SRC UI: 8 tests; regression TypeScript type-check.
- Rebuilt runtime, machine, DSP-link, DSP-voice, dyndsp, MIDI, instrument-play and menu objects. ColdFire/SHARC instruction validation passed.
- CANOPY package generated at `out/patch/canopy-rebased-dt2.dkm` using DKDS 8.
- Combined CANOPY/Overloop firmware linked with the previous Stage 6 module selection at `out/patch/canopy-overloop-rebased.syx` (272 writes). No firmware was installed on hardware.
- `git diff --check` passes.

The required affected gate was attempted (`DIGI_GATE_BASE=upstream/main node tools/nx/gate.mjs --tier merge`) but did not pass. This environment lacks cargo-nextest, the wasm target and the default generated DN2 DSP core; its /tmp free-space check reported approximately 7 GB versus the required 50 GB. Host lifecycle tests also failed at that free-space guard. The failed broad run was stopped rather than continuing expensive prerequisites. Full emulator/physical acceptance remains unverified for the rebased image; the prior Stage 6 measurements apply to the backup tip, not this image.

Logs are ignored local artifacts under `out/rebase-*.log`. The stage 6 release measurement gaps remain open; this rebase does not close them.
