# 125: CANOPY recording: held-trig page, live chords and step recording

DT2 OS 1.17, 2026-11-13. Three user reports on the installed four-page build
(image `1d4953c4df3f3193`): holding a step to set a p-lock blanks CANOPY's
SRC page; live recording plays no sound or only one note, not the chord;
step recording enters single notes, not chords. Static reading of the stock
MAIN image (one reader, **[D]**) and emulator measurements with
`tests/regression/runtime/canopy-record.test.ts` and uncommitted write-guard
investigation runs (`guard.set`/`guard.report` of the DT2 daemon; logs in
`out/canopy-chord-probe*`) (**[D]**).

## Sequencer UI byte +0x268 is the held-trig edit mark [D]

`0x4005ab0e` returns byte `+0x268` of the sequencer UI object (SRC view
`+0x6c`, KeyboardView `+0x98`). Its only writers are `0x4005ab18` (the
routine finding 123 calls before a held-step edit) and the object's
initialiser `0x4005bb66`. `0x4005ab76` clears the held mask at `+0x258` and
the mark together. Every stock caller of `0x4005ab0e` in the SRC page
(`0x40017c44`, `0x40017f3e`, `0x40018648`, `0x400187dc`) tests it together
with the held-trig test `0x4005b336`: it means "a held trig is being
edited" and is cleared when the trig is released. Measured: holding step 5
in grid recording set it within 300 ms of emulated time, before any knob was
turned.

The stage 5 adapter named it `instrument_src_alternate` and treated a set
byte as a foreign context, dropping CANOPY's ownership of the SRC page. So a
held step alone, and any lock edit on it (CANOPY's own held-step edits call
`0x4005ab18`, finding 123), made the page fall back to the stock painter,
which has nothing to draw for CANOPY's machine type: the reported blank page.
Further turns then went to the stock handler. The adapter now ignores the
byte for ownership. Measured on the fixed image: with step 5 held and COLOR
turned, the mark was 1, CANOPY kept its published SRC token, redrew the page
ten times in 300 ms and showed the locked value (COLOR 84).

## Recording modes [D]

| Mode | Test | Entered by |
| --- | --- | --- |
| Grid | `+0x269` (`0x4005ad36`; written at `0x4005ad1e` from a counter at `+0x26c`, so not a plain toggle: it stayed 1 after RECORD was pressed again to leave grid recording **[O]**) | RECORD |
| Live | transport playing (`0x400d688a` = 1) and `+0x271` (`0x4005ad40`) | RECORD + PLAY |
| Step | `+0x269` and `+0x272` (`0x4005ad8c`) | RECORD + STOP |

The page key handler `0x400786ae` reads RECORD (key `0x13`) and STOP (`0x15`)
held and sets step recording through `0x4005ad9c` at `0x40078ac4`; the
display shows "Step Rec: Standard". KEYBOARD mode is off after RECORD (trig
keys toggle steps).

### Step recording takes notes with KEYBOARD held [D]

Notes are entered by holding KEYBOARD and pressing trig keys (the user's
workflow; the KeyboardView key handler `0x4005dc58` accepts trig keys in step
recording only with KEYBOARD, key `0x12`, held). Measured on a CANOPY track:
each press writes a trig with that key's note on the cursor step inside the
key handler (C, D, E as single presses went to steps 0, 1, 2); while keys
stay held, each further key rewrites the same step's note (a held C, E, G left
step 3 with G only), and the cursor advances after the release. The
KeyboardView note action `0x4005cee2` is not called (CANOPY's note hook saw no
press) but its note-off is, once per released key. The stock audition sounded
one note at a time.

## Live recording [D]

From KEYBOARD mode, RECORD + PLAY keeps KEYBOARD on and each press reaches
the note action, which CANOPY hands to the stock recorder (finding 123). The
stock audition of a press reaches CANOPY as a sequencer trig, and a track
plays one sequencer note at a time: holding C, E and G sounded each note in
turn, never the chord, and each recorded step kept one note.

The recording is committed after the key event: a before/after comparison of
the track's steps inside both the note-on and the note-off call found no
change, while a write guard over the pattern saw the commit within the next
second of emulated time. The stock live recorder `0x40042c20` (live-record
test at `0x40042c40`) writes the trig through the editor routines
`0x400597c8` / `0x40059610` and the note through `0x400392be`.

### Pattern object layout [D]

The active pattern object (`*0x4316501c`):

| Offset | Content |
| --- | --- |
| `+0x6a5 * track` | track block: big-endian trig word at `+2 * step` (bit 0 = trig), per-step byte arrays at `+0x100`, `+0x180`, `+0x200`, `+0x280`, `+0x300`, `+0x380`, `+0x400` (0xff = default); byte `+0x485` is the track's inherited NOTE |
| `+0x6a50` | note list, 0x11400 bytes (cleared as one block by `0x400459e0`): 8192 entries of 6 bytes `{track, step, note, velocity, ?, ?}` |
| `+0x12a50` | head entry index of `(track, step)`, s16 at `2 * (128 * track + step)`, negative = none |
| `+0x13a50` | next entry index, s16 per entry, `0xffff` ends a list |
| `+0x17a50` | note-list allocation word |
| `+0x17e50` | lock lanes (finding 123) |

`0x400392be(editor, track, step, records)` replaces a step's list with a
vector of 4-byte records `{note, velocity, ?, ?}`. A live-recorded press of A
wrote four entries: note `0x45` (69), velocity 100, then three records with
note `0xff`, so a trig has four note slots (the shape of the MIDI machine's
chord notes); the recorded audio-track trigs used the first only. All seven
per-step byte arrays of those trigs stayed `0xff`, and the steps had no lock
lane: the note list is where a recorded trig's note lives.

A manually placed trig instead had `0xff` in its note entry. On track 1,
turning the stock TRIG page NOTE from 60 to 57 changed pattern track byte
`+0x485` from 60 to 57. The held-step chord preview uses that inherited byte
when the step has no explicit note; the other track offsets follow the same
track-block stride. [D]

## Results on the fixed image [D]

`canopy-record.test.ts` on `out/patch/canopy-chord-rec.syx` with the unchanged
`canopy-four-page.dkm`: holding C, E and G during live recording sounded all
three notes together on CANOPY; the watch stored the chord as each note
committed to step 28 (C alone, then C with E, then all three, as the note
list grew), leaving G with chord-note locks -3 and -7; playback sounded 60,
64 and 67 together. `canopy-lock.test.ts` (held-step locks, clear, save and
reload, pattern copy, running transition, live sweep, two tracks) passes on
the same image after moving its knob to SOUND B, COLOR in the four-page
layout.

On the rapid-entry follow-up image `out/patch/canopy-step-record-final.syx`,
the same test presses D, F and A back to back without inter-key delays. All
three sound and play back together; the hook reports three started entries,
three commits, zero drops and zero expired waits. This is an emulator result;
the follow-up has not been installed or measured on the physical DTII. [D]

## Changes

- **Page ownership.** `+0x268` no longer revokes it.
- **Sound while recording.** Live, step and held-trig presses on a CANOPY
  track still go to the stock note action, and now also to CANOPY's panel
  path, so every held key sounds as outside recording. While a recorded key
  is held, the frame path skips that track's sequencer trigs
  (`mi_seq_mute`), so the stock audition does not sound the note twice.
- **Chord-note locks.** Packed lock IDs 53..55 (ordinals 6321..6611, finding
  123's format) hold the second to fourth notes of a recorded chord as an
  interval of -48..+48 from the trig's note. The CF mapper plays them with
  the trig's note, in PLAY semantics, even when the Sound uses a ROOT chord;
  the transport keeps IDs from 53 off the DSP sideband, whose validator
  accepts IDs up to 52 only.
- **Chord groups.** Every key pressed while another recorded key on the
  track is still held joins the group; the group closes when all are up.
- **Held trig.** A press with held trigs stores the group's other notes on
  the held steps at once, relative to the pressed note.
- **Step recording.** The note-list writer at `0x400392be` reports completed
  note commits to a bounded queue while a CANOPY step entry is pending. The
  UI consumes commits in key-press order, sounds held keys on the panel path
  and stores the other notes as chord locks. Released keys retain their place
  in the queue without sounding again, including when a key is pressed again
  before its earlier commit. A pending entry expires after 500 ms; switching
  tracks, machines or recording modes discards it instead of writing locks
  on the wrong track. The stock audition is muted while keys are held.
- **Live recording.** Because the commit follows the key event,
  the adapter's UI tick (`ip_panel_tick`) watches the track's note lists
  (head index and up to four chained notes) and trig words while a group is recorded and for 1.5 s after its last
  release. A step whose list or trig changes, whose trig is set and whose
  first note belongs to the group receives the group's other notes relative
  to that note. In a ROOT chord Sound only the root is kept (chord-note locks
  cleared). Lock writes address the UI's current track, so the watch stops
  when another track is selected or KEYBOARD is left.
