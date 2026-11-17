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

## Several instances at once (2026-11-12)

Status: **[D]** unit and emulator coverage below; one reader; not yet on hardware.

**Engine isolation [D].** Two differently configured CANOPY instances
(wavetable PM with arp, FM with arp), interleaved block by block on the SHARC
simulator, produce bit-identical output, V7 gains and state to each instance
run alone, in direct and protected dispatch
(`test_two_instances_interleaved_match_each_instance_alone`). The module's
global data is per-call scratch; the engine needed no change.

**Admission.** The runtime policy word `tracks` is the admitted instance count
(1..16); its process and state words are per-instance ceilings, so the total
process budget and the descriptor's allocation limit scale with it
(`pending.rs`), and the load registry's track limit is that count
(`instrument.rs`). The adapter policy admits four; CANOPY's descriptor
declares four instances, so admission allocates four 2976-byte states at the
8-byte stride and reserves 4 x 100,000 process instructions.

**Which tracks.** `sound.c` scans the kit once per UI tick: the four lowest
tracks whose Sound carries the instrument type are wanted; an instance keeps
its track while that track stays wanted, freed instances take newly wanted
tracks (lowest first) and keep their sound settings. A fifth CANOPY above the
four stays stock and silent; one added below them takes an instance from the
highest. Scans pause during in-place Sound loads and scheduled pattern
hand-overs, as admission already did.

**DSP slots.** The play adapter keeps four slots (154 words each: lifecycle
scalars, process input, output, events, controls) plus per-slot V7 gains. Every
entry point loads its slot into the unchanged single-instance working set, runs,
and stores it; slot k calls the registry with instance ordinal k (state base +
k x stride). The CF binds slot k to a track with a reset whose word 10 carries
the hint k+1 (`mi_reset_bind`, validator: word 10 < 16, padding from word 11);
binding unbinds any other slot naming that track. Without a hint a reset
targets the slot already bound to its track; a new epoch resets every bound
slot; other tracks are acknowledged as inactive. Unbound slots hold track
0xffffffff, which every bound check tests unsigned (an earlier signed `COMP`
let such a slot be processed and quarantine the row; fixed and covered).

**Per-track carrier state.** `_mi_native_owner[16]` (the stamp of the frame in
which a track's native block completed) and `_mi_post_ptr[16]` (that owner's
V7 gains) replace the single owner flags; `_mi_carrier_begin` clears both every
frame and each slot rewrites its own entries every block. The envelope
interposer matches the voice pair (`voice >> 1`) to its own entry. The native
AMP snapshot now reads the record of the slot's own track (record 2t, 88 bytes
per track): the single-track change had read track 1's AMP page for every
track **[C]** (found while generalising; the any-track proof only checked
audibility).

**Status focus.** Only one 12-word status record reaches the link every other
block. The CF flags the instance on its selected track with bit 15 of control
word 0; the adapter clears the bit before validation and the module, records
the slot as focus and copies only that slot's status to `_instrument_ui_status`
(the others write a scratch sink). A focus change bumps that instance's
revision so its bank is resent with the flag.

**ColdFire.** `control.c` holds per-instance sound settings, keyboard mapper,
SRC state (`src_state[i]`), status (`src_status[i]`), lifecycle and MIDI
channel; the UI acts on the selected track's instance (set by the SRC view and
the keyboard view), the tick services each instance, and the MIDI mapper finds
the instance for its batch track. PANIC releases every instance. Learned shapes
are one shared library. Panel keys name their track (`mi_panel_down(...,
track)`, panel enabled per track by `panel_mask`). External MIDI: EXTERNAL
MIDI, USB, DIN and the selector are shared; CHANNEL is per track (saved under
`canopy/channels/v1`, default the version 1 CHANNEL); a channel already played
by a lower instance is skipped and INPUT shows CHANNEL IN USE.

### Results

| Check | Result |
| --- | --- |
| DSP play (slots, binding, focus, unbound slots), modulation, native AMP firmware, carrier | 316 passed, 1 skipped |
| CANOPY V2 incl. two-instance isolation (subset run) | 8 passed |
| Voice wrapper, guard, sine | 175 passed |
| Host C drivers (native view, panel, keyboard, settings/presets) | 167 passed |
| midi-events ledger crate (incl. per-track panel) | 26 passed |
| Runtime Rust host tests | 54 passed |
| Emulator proof `canopy-track.test.ts` on `canopy-multi-release` | passed: track 11, then tracks 1, 5 and 7 admitted (`ip_tracks` [10, 0, 4, 6]); a fifth on track 13 stays stock; tracks 11 and 1 sound at once (USB on 11, panel on 1; both carrier pairs published in one frame; output RMS 0.0517); DSP focus follows the selected track |
| Compiled Stage 5 and Stage 6 on `canopy-multi-release` | passed (494 s, 489 s) |

The 25 `test_midi_events.py` failures in this run (registration window, loader
tail boundaries, composed frame) fail identically on the previous commit: their
extern tables lack `_fx_diag` and `_fm_enabled`, added to the linked sources
earlier. They are not caused by this change.

### DSP load [D]

With two instances sounding (simple notes), the emulator's meter reported
236,524 busy DSP instructions per block: 35.5 % model, 40.2 % real nominal,
49.9 % real high, 41.1 % peak (`out/canopy-any-track/multi-load.json`). The
bounded worst case of one instance is about 98,000 instructions per block
(finding 341). Four instances all at that worst case would add about 390,000
to the stock engine's ~211,000, exceeding the 666,667-instruction block at
the high real estimate. Typical sounds are far below the worst case, but four
simultaneous worst-case wavetable-PM patches with arps can overload the DSP.
Qualifying four busy instances on hardware remains open **[O]**.
