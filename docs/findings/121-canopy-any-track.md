# CANOPY on any track

Status: **[D]** per-track OS 1.17 layout read from the MAIN image by one
reader; **[D]** host, DSP and ColdFire unit coverage; emulator proof and
hardware installation recorded below as they complete.

## Problem

The physical DTII held a CANOPY sound on track 11 of pattern A02 and showed the
stock descriptor page (TUNE and LEV only) in silence. The adapter admitted the
instrument on track 1 only: `sound.c` read the kit's track-0 object, the native
SRC view and the KeyboardView bridge owned track 0, the transport, routes and
quarantine used ledger track 0, the DSP rendered only track 0's even voice and
read track 0's row of the completed stock frame, and the MOD conversion shims
bypassed the sampler split/recombine on track 0 alone
(`out/a02-diag/`: INPUT "SELECT CANOPY ON T1", STATUS "LINK LOST").

## Per-track stock layout [D]

Read from DT2 OS 1.17 MAIN (base `0x40000400`) with
`tools.coldfire.disasm` (`out/disasm_windows.py`):

| Site | Fact |
| --- | --- |
| `0x4002d438` refresh_track(source, track) | caches the Sound pointer at `0x80003340 + 4*(0x4f3 + track)`; copies 0x8e bytes from Sound+0x14 to `0x80003362 + 0x8e*track` (parameter row); copies 0x9a bytes from Sound+0xa2 to `0x80003cd0 + 0x9a*track` (machine row, type byte first) |
| `0x40036296` kit track object | `kit + 0x6c + ((t<<10) - (t<<6))`, i.e. stride `0x3c0`, track clamped to 0..15 (track 16 is the special value) |
| `0x4002e8d8..0x4002e908` sampler split loop | d7 = track 0..15, a4 = parameter row advancing by 0x8e; the hook site `0x4002e8e2` reads `0x5a(a4)` |
| `0x4002e96e..0x4002e99e` recombine loop | d6 = track, same row stride; hook site `0x4002e978` |
| `0x4002e926` | `lea 0x80003cd0,a0`: the type-row literal anchored as `cf.machine.site.instrument_mod_type_row_literal` is track 0's machine row |
| DSP completed frame (`dsp.voice.value.frame_base`) | type halfword `0x94/2 + track`; machine parameters 48 halfwords (0x60 bytes) per track (dsp-voice.s), so mirror `m` of track `t` is halfword `84 + 48t + m` |

## Change

ColdFire (`core/instrument-play`): `sound()` scans the sixteen kit track objects
and returns the first Sound whose byte `+0xa2` is the instrument type, publishing
the track as `ip_track` (0xffffffff for none) on every prepare; retention across
a pattern switch also requires the same track. `control.c` binds the transport
to that track (`bound_track`) through an explicit reset before the first
configuration there, so the DSP can adopt it; suspension resets the bound track
and a change of `ip_track` suspends. MIDI readiness, routes (including the local
panel route), quarantine, keyboard event mapping, configuration revisions and
the status/SRC context all use that track. The native SRC view and KeyboardView
bridge own the instrument's track instead of track 0; the STATUS page shows
`TRACK n`. The MOD shims pass their loop track to `instrument_mod_carrier(track)`,
which reads the type byte at `row + 0x9a*track`.

MIDI bridge (`core/midi-events`): the panel route may target any track; panel
keys and cancel use that route's track and generation; `mt_prepare_mapped`
offers the pending-UI publication to every configured track (the mapper owns at
most one). `kb_process` no longer rejects events by track; the adapter routes
one track to it.

DSP (`play.s`, `modulation.s`): `_iv_track` is adopted by a reset while the
instance is unconfigured and silent (the ColdFire resets the old track and
waits for its ACK first), configuration metadata word 0 must name it, the
voice-row entry renders only that track and reports it to the carrier and
velocity ownership, input word 2 carries it (status word 1 bits 0..3, which the
packet normalizer also checks), and the modulation reader indexes that track's
frame row. The mapper output events are stamped with the admitted track before
they enter the wire batch (the normalizer compares event track bits 10..13 with
input word 2). The envelope interposer's native-AMP skip compares the voice
pair (`voice >> 1`) with `_mi_native_owner_track`, published with the opt-in,
instead of accepting only voices 0/1.

Voice wrapper (`core/dsp-voice`) **[D]**: voice 0 builds `_dv_mask` once per
block, and with an empty mask every other voice takes the cheap stock path
before any table scan. The mask was built only from the legacy `_dv_table`,
while CANOPY registers in the separate trusted `_instrument_voice_rows` table.
Only voice 0, which always takes the full path, ever reached the V2 row, so an
instrument on any other track rendered its block (`_iv_output` non-zero, owner
valid) yet stock rendered the voices and the carrier pair was never published.
Voice 0 now adds the published V2 types to the mask. With no V2 table (the
optional extern is 0) the mask is unchanged.

## Coverage

- `tests/patchkit/test_instrument_play.py`: binding to the reset track,
  rejection of other tracks' configuration and packets, silence of the same
  type on another track, velocity ownership on the bound track, rebinding only
  after the owner's reset and silence.
- `tests/patchkit/test_instrument_modulation.py`: the bound track's frame row.
- `tests/patchkit/test_instrument_modulation_cf.py`: bypass on track 0 and 11
  with the 0x9a-stride type byte, never for a foreign or unbound track.
- `tests/regression/host/canopy-native.c`, `canopy-panel.c`: ownership follows
  `ip_track`; midi-events `mi_test_panel_track`: panel route on track 10.
- `tests/patchkit/test_dsp_voice.py`: a V2 row is reached on tracks 0, 10 and
  15 with no legacy rows; the old mask sends track 10 to stock; an unpublished
  V2 row leaves the mask empty.
- `tests/regression/runtime/canopy-track.test.ts`: emulator proof on track 11
  with stock track 1, panel and USB notes, output RMS, and the move to track 1.

## Results

| Check | Result |
| --- | --- |
| DSP play, native AMP firmware, carrier (`test_instrument_play.py`, `test_canopy_native_amp_firmware.py`, `test_canopy_carrier.py`) | 263 passed, 1 skipped |
| MOD DSP and ColdFire hook (`test_instrument_modulation.py`, `test_instrument_modulation_cf.py`) | 79 passed |
| Voice wrapper, guard, sine (`test_dsp_voice.py`, `test_dsp_guard.py`, `test_sine.py`) | 175 passed |
| Host C drivers (`canopy-native`, `canopy-keyboard`, `canopy-preset` incl. panel/settings) | 167 passed |
| midi-events ledger crate | 26 passed |
| Patchkit image pin `declared_machines_patch_to_the_pinned_images` | passed |
| Emulator proof `canopy-track.test.ts` | passed (track 11 SRC page owned and drawn, panel and USB notes audible, move to track 1) |

The proof's track-11 capture shows the CANOPY ARP page (RATE/ORDR/OCTS/GATE,
SWNG/RST/CLK/BPM) beside SMP 11, and track 1 the stock Oneshot page.
Compiled Stage 5 and Stage 6 pass on the same image (`cdebb602…7d6`), so
track-1 behaviour and Overloop coexistence are unchanged.
