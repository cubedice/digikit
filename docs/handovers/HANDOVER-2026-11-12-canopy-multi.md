# CANOPY: up to four instances at once

Branch: `codex/short-loop-proof`, on top of `680c2134` (CANOPY on any track,
[previous handover](HANDOVER-2026-11-11-canopy-any-track.md)). Neither commit
is pushed or installed on the DTII.

Why: after the any-track change the user asked for several CANOPY tracks to
play at once. Design, stock facts and results are in
[finding 121](../findings/121-canopy-any-track.md) "Several instances at once".

## Change

- Runtime: the policy's `tracks` word is the instance count (1..16); process and
  state words are per instance, so the reserved total and the descriptor's
  allocation limit scale with it. CANOPY's descriptor declares four instances.
- instrument-play DSP (`play.s`): four slots, each loaded into the old single
  working set around every entry point; slot k uses registry instance k. Resets
  bind a slot to a track (word 10 = k+1); unbound slots hold track 0xffffffff
  and every bound check is unsigned. Owner tables are rewritten every block.
  Bit 15 of control word 0 marks the focus slot whose status reaches the UI.
- midi-events DSP: per-track native owner stamp and V7 gain pointer
  (`_mi_native_owner[16]`, `_mi_post_ptr[16]`), cleared every frame. Envelope
  interposer and native AMP snapshot read the slot's own track (this fixed a
  latent any-track bug: the snapshot read track 1's AMP page for every track).
- midi-events CF: reset slot hints (`mi_reset_bind`), panel route per track
  (`panel_mask`), CC120 per panel track on cancel, mapped-transform
  publication over every configured track.
- instrument-play CF: `sound.c` admits the four lowest CANOPY tracks with
  continuity; `control.c` keeps per-instance settings, SRC state, status,
  lifecycle and MIDI channel (`canopy/channels/v1`); the UI acts on the selected
  track's instance; PANIC releases all; learned shapes stay shared.

## Verification (2026-11-12)

- Unit and host suites as in finding 121's table; `test_midi_events.py` has 25
  pre-existing failures that fail identically on `680c2134` (stale extern
  table lacking `_fx_diag` and `_fm_enabled`).
- Emulator proof `tests/regression/runtime/canopy-track.test.ts` passed on
  `out/patch/canopy-multi-release.{syx,dkm}` (254 s, `out/canopy-multi-proof.log`).
- Compiled Stage 5 (494 s) and Stage 6 (489 s) pass on the same pair: native
  pages, Overloop coexistence and uploads are unchanged with one instance
  (`out/canopy-multi-stage5.log`, `out/canopy-multi-stage6.log`).
- Two-instance DSP load in the emulator: 41.1 % peak of the block
  (`out/canopy-any-track/multi-load.json`).
- Release pair: image `cb16bc2a…4b2b68` (preflight OK), CANOPY package
  `cbec91c4…96d5e4` (differs from the DTII's `19fafa65…`), Overloop
  `748e3044…` unchanged. Build: `out/build-multi-release.sh` (WSL).

## DSP block rendering (later the same day)

The user then asked for the bit-identical DSP optimizations: wavetable voices
render a block at a time, the FM halfband history uses a block buffer, and the
arp sorts by stable insertion ([finding 122](../findings/122-canopy-dsp-block-rendering.md)).
The worst callback fell from 99,849 to 89,255 instructions (wavetable 99,386 to
69,909), bit-identical in all 1,164 CANOPY DSP tests. Only the CANOPY package
changes: `out/patch/canopy-opt-release.dkm` (file sha256 `d33f58dc…84df`) with the same
image. The four-instance proof and compiled Stages 5 and 6 pass with it
(`out/canopy-opt-proof.log`, `out/canopy-opt-stage5.log`,
`out/canopy-opt-stage6.log`).

FM block rendering followed (same finding, section "FM block rendering"): FM
voices render in five passes with the envelope-stage branching hoisted, the
worst callback falls to 77,017 (FM handoff), bit-identical again in all 1,164
tests. Package `out/patch/canopy-fm-release.dkm` (file sha256
`15acf3ec…eb7d9`), same image; the four-instance proof and compiled Stages 5
and 6 pass with it (`out/canopy-fm-*.log`). The installer below now installs
this package.

## Open

- Four simultaneous worst-case instances (wavetable PM with arps) can exceed
  the DSP block; measure four busy instances on hardware **[O]**.
- With two instances on one MIDI channel, the lower track plays it; there is no
  layering of one channel across tracks.
- Panel keys and the SRC pages act on the selected track only.

## Installation

Not installed; needs the user's approval. `out/canopy-multi-install.py` and
`out/canopy-multi-install-all.py` are the reviewed any-track installer with the
new image, the optimized CANOPY package, their proof logs and a new backup
folder (old image `481b2b9a4fcddf5e`, preset 4 rule unchanged; the CANOPY
package is uploaded because it differs).
They replace the never-run any-track installer. Run from the repository root
on Windows: `py -3.12 -m out.canopy-multi-install backup`, then
`py -3.12 -m out.canopy-multi-install-all`.
