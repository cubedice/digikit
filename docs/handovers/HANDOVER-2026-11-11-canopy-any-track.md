# CANOPY on any track

Branch: `codex/short-loop-proof`, commit `680c2134` on top of `b48df9c8`; superseded for installation by the [four-instance handover](HANDOVER-2026-11-12-canopy-multi.md).

Why: on the physical DTII, pattern A02 held CANOPY on track 11 and showed the
stock TUNE/LEV page in silence, because the adapter only admitted track 1
(`out/a02-diag/`, INPUT page "SELECT CANOPY ON T1", STATUS "LINK LOST").

## Change

The instrument-play adapter admits the lowest kit track whose Sound byte
`+0xa2` carries the instrument type, publishes it as `ip_track` (0xffffffff when
none), and binds the transport to it (`bound_track` in control.c) through an
explicit reset before the first configuration there; a change of track suspends,
resets the old track and rebinds. UI ownership (native SRC page, KeyboardView
panel bridge), MIDI readiness, routes, quarantine and status all key on that
track. The DSP keeps `_iv_track`: adopted by a reset while unconfigured and
silent, required by configuration metadata word 0, used to render only that
track's even voice, to publish velocity/carrier ownership, and to read the
stock frame row (type halfword `74 + track`, parameters `48` halfwords per
track). Status word 1 bits 0..3 carry it (input word 2). The MOD split/recombine
shims pass their loop track (d7/d6) to `instrument_mod_carrier(track)`, which
reads the SRAM type byte at `row + 0x9a * track`.

Stock OS 1.17 facts read from the MAIN image for this (mark **[D]**, one
reader): `refresh_track` 0x4002d438 caches the Sound at
`0x80003340 + 4*(0x4f3+track)`, copies 0x8e bytes to `0x80003362 + 0x8e*track`
and 0x9a bytes to `0x80003cd0 + 0x9a*track`; the kit track object is
`kit + 0x6c + 0x3c0*track` (0x40036296: `(t<<10)-(t<<6)`); the split loop
iterates d7 over 16 rows of 0x8e at 0x4002e8d8, the recombine loop d6 at
0x4002e96e. Disassembly helper: `out/disasm_windows.py` (uses
`tools.coldfire.disasm`; run in WSL as `uv run python -m out.disasm_windows
START-END ...`).

MIDI bridge: the panel route (route 32) may target any track; panel keys and
cancel use that route's track/generation; `mt_prepare_mapped` asks every
configured track for a pending UI publication instead of track 0 only.
`kb_process` no longer rejects events by track; the adapter routes one track to
it and stamps that track on the mapped output.

Voice wrapper (`core/dsp-voice`): voice 0's per-block `_dv_mask` now also
includes the published types of the trusted V2 table. Previously only voice 0
reached a V2 row, so on track 11 the instance rendered (`_iv_output` non-zero)
but voices 20/21 took the stock renderer and stayed silent. The envelope
interposer's native-AMP skip now matches the owner's voice pair
(`_mi_native_owner_track`) instead of voices 0/1 only. Details and results:
[finding 145](../findings/145-canopy-any-track.md).

## Verification (2026-11-11)

- Unit suites: DSP play/native AMP/carrier 263 passed (1 skipped); MOD DSP and
  ColdFire hook 79; voice wrapper/guard/sine 175; host C drivers 167; ledger
  crate 26; patchkit image pin passed; ruff clean.
- Emulator proof `tests/regression/runtime/canopy-track.test.ts` passed on
  `out/patch/canopy-any-track-release.{syx,dkm}`: track 11 owns the native SRC
  page while track 1 stays stock, panel and USB notes sound on track 11, and the
  instance follows the instrument to track 1.
- Compiled Stage 5 (416 s) and Stage 6 (465 s) pass on the same pair, so the
  track-1 behaviour, Overloop coexistence and uploads are unchanged
  (`out/canopy-any-track-stage5.log`, `out/canopy-any-track-stage6.log`).
- Release pair: image `cdebb602…7d6` (preflight OK; rebuilt from LF objects,
  byte-identical to the image all three proofs ran), CANOPY package
  `19fafa65…c67f` and Overloop `748e3044…` unchanged from the DTII's current
  ones.

Line endings: this checkout has `core.autocrlf=true`, and several working files
were CRLF. The ColdFire object digest hashes raw bytes, so the 44 CRLF working
files under the adapters, runtime, SDK and CANOPY were normalized to LF before
the final object build. `machines-dt2` and `dyndsp-dt2` objects differ from HEAD
only in `source_sha256` (their committed digests had been taken from CRLF bytes).

## Installation

Not yet installed. `out/canopy-any-track-install.py` (one-time, modelled on the
reviewed controls/MOD installer: old image `481b2b9a4fcddf5e`, preset 4 reused
only while it still equals the previous preservation copy, module upload skipped
when the device already holds the identical CANOPY package) and its runner
`out/canopy-any-track-install-all.py` are prepared. Run from the repository root
on Windows: `py -3.12 -m out.canopy-any-track-install backup`, then
`py -3.12 -m out.canopy-any-track-install-all`.
