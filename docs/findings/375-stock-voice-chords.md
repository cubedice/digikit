# 375. Chords from one track's stock sample voice

Can a stock sample machine play a chord on its own track, without borrowing other tracks' voices (the voice groups of finding 369)?
DT2 OS 1.17. Marks as in `docs/FINDINGS.md`. Emulator only: the device has not run it.

## The stock voice is fixed at two records per track [D]

From the existing findings (06, 15, 53, 112): track t owns voice records 2t and 2t+1 (`0x1d8` bytes each, 32 at `0x2412cc`; for a stereo
sample L and R, for a mono sample the same sample pointer in both). The counts 16 and 32 are literal loop counts and table sizes all
through the per-block function `FUN_1c642a`, the init `FUN_1c15e3` and the pan and latch loops, and the frame from the ColdFire carries one
pitch, one trig bit and one parameter block per track. Raising the voice count is not practical.

## The sample renderer can be called again on a copy [D]

The renderer `0x1c4ecf` (selectors 0, 1, 3, 4, 5: ONESHOT, WERP, REPITCH, GRID, SLICE; STRETCH is `0x1c5576`) is a compiled function of
the stock calling convention, called from one loop in `FUN_1c642a` (`0x1c6ae8` .. `0x1c6b09`) with R4 = the voice record (stepped by
`0x1d8`), R8 = the voice's output block (a word of a 32-entry table) and R12 = a value loaded once before the loop, the same for every voice.
It saves and restores every register it uses.

The Python SHARC interpreter (`tools/sharc_dev/sharc_harness`, the stock DSP image after its init) shows:

- One call costs 3,610 to 3,614 instructions for a sounding voice at any pitch from -12 to +24 semitones (step 0.25 to 2.0), 3,614 to 3,626
  when looping, 91 with no sample pointer and 95 for an inactive record. Two voices of a pair cost about 7,200 per extra note.
- A copy of a voice record placed elsewhere (L1 `0x318000` or L2 `0x2009c37c`) at another pitch, rendered right after the owner in the same
  block, gives output bit-identical to each voice rendered alone, in either order, for 12 blocks. Apart from the two records, the
  stack and the output block, the call writes nothing.
- Byte `+0x1b9` of the record (the note-on mark of the SDK machines, finding 06) must stay clear for a stock voice: with it set the
  renderer ends the voice after one block (95 instructions, silence).

The stages after the renderer (bit reduction `0x1cdd8b`, the filter stages, the amp envelope `0x1cbb57`) also take state and buffer
pointers, but the code that sets up their calls (filter type, coefficients) is inline in `FUN_1c642a`. The amp envelope takes two more
values on the stack that are not identified.

## Copies summed before the voice chain [D]

`core/dsp-voice` (`src/dsp/dsp-voice.s`, image stub), `_cs_voice`:

- A voice whose bit is set in `_cs_vmask` is rendered by `_cs_voice` in place of the renderer, when its track's machine type is below 7 (a
  stock one; a run-time machine renders with its own routine, and STRETCH never reaches the hook). It calls the hooked render path for the
  owner (so a registered machine or the stock renderer renders it as before), then the stock renderer for up to three copies of the voice
  record, and adds each copy's 32 samples into the owner's output block. The track's bit reduction, filter, amp envelope, pan and sends then
  run on the sum, so the notes share them.
- Per track, `_cs_cfg` holds 6 words: the number of copies (0 to 3), a gain applied to the sum (float; 0 leaves it), the three pitch ratios
  (float) and the shape word they came from. The copies are `_cs_rec`: three per voice, 45,312 bytes for the 32 voices.
- Note-on: the owner is active and its sample pointer or phase is not what its last render left (`_cs_last`), so the firmware's arm seeded
  them; the copies of the last note are then stale. While the owner sounds, every copy the note does not have yet is taken from the owner's
  record: all of them after a note-on, or the ones a shape that arrived after the note-on added. Nothing is written to the owner's record.
- Every block a copy's step (`+0x1a8`, a signed Q31 int64) is set to the owner's step times its ratio, so the owner's pitch changes carry
  over. The copies play to their own sample end; a retrigger of the owner copies again.
- The guard: after its render, a copy's block must hold samples whose bits without the sign are at most those of 8.0 (finite, magnitude at
  most 8.0). Otherwise the copy ends (its ACTIVE byte clears, so the renderer zero-fills it), `_cs_faults` counts it and its block is not
  added, so the track's filter and envelope never see it.
- With `_cs_vmask` 0 every voice pays three instructions more.

## The track keyboard sends the shape [D]

- The engine (`track-keyboard.c`): on a backend with copies (`tk_set_copies`), a chord without strum sounds as its first note, and the
  others go with that note as up to three intervals in semitones (`TkNoteEvent.copies`, `.intervals`). VOIC still limits the notes; a chord
  of more than four notes, a note with a velocity of its own or an interval outside -32 to 31 keeps a voice per note. Another shape on a held
  key starts the voice again with it.
- Copies are offered to TK_NATIVE_AUDIO tracks except STRETCH (`tk_native_copies`, set on every poll with the voice capacity).
- The native layer (`track-native.c`) records the shape of the note's destination track (`tk_pool_link_chord`, an image function: seam
  version 5) before it sends the note; a note without copies records an empty shape.
- The frame: the track keyboard's region of the frame tail grows from 4 words to 5 (`DL_TRACK_POOL_WORDS`; the DSP limit
  `DL_TAIL_WORDS - 158`), so the chunk area of core/dsp-link loses one word per block. The first word, at frame byte `0x850` before the pool map
  at `0x854`, is one track's shape word: `0xc5` in bits 24-31, the track in 20-23, the copies in 18-19 and the intervals (6 bits, signed) in
  12-17, 6-11 and 0-5. The image sends a changed shape at once and twice more, otherwise every track in turn (16 blocks). The pool receiver
  passes the word to `_cs_receive`, which sets the track's row, its ratios (2^(i/12) through `_dv_exp2`) and its two bits of `_cs_vmask`.

`runtime/platform/track-keyboard/tests/stock-chord.device.test.ts`: track 1 of the test card (a one-second 220 Hz sine on ONESHOT), KEYS POLY,
KEYBOARD, TRIG 9. Levels at 220 Hz, the third, the fifth and a second (not in the chord), left channel (the right is the same):

| | root | second | third | fifth |
| --- | --- | --- | --- | --- |
| a single note (CHRD PLAY) | 0.009028 | 0.00024 | 0.00001 | 0.000003 |
| CHRD MAJ | 0.009024 | 0.00040 | 0.008989 | 0.009000 |
| the same with NaN in both third copies' decimator state | 0.009025 | 0.00024 | 0.000025 | 0.009024 |
| the next CHRD MAJ note | 0.009022 | 0.00042 | 0.008985 | 0.008997 |

While the chord sounded only track 1 was active (`tk_native_audio_active` 1: no helper), `_cs_vmask` held voices 0 and 1 and the row two
copies. The NaN made the guard count two faults and end both copies; no sample of the output was non-finite, the owner and the fifth played on,
and the next note took the copies again. An earlier run with the configuration written through `dsp.patch` (no track keyboard) gave the same
triad, the same level with the path on and no copies, and the copies' steps `0x50a28c00` and `0x5fe44380` against the owner's `0x40000000`.

Two assembler facts found on the way: an immediate such as `3 * 0x1d8` assembles as its first term (`3`); use literals (a sum such as
`DL_TAIL_WORDS - 158` assembles as written). Data symbols of an image object are exported without `.GLOBAL` (with it, patchkit refuses
"exported twice").

## Open [O]

- The device has not run it, and the cost is measured in the interpreter only (about 7,200 instructions per extra note of a stereo pair).
- The notes share the track's amp envelope and filter; a release of one note of a held chord is not possible. Per-note envelopes would need
  the inline set-up of `FUN_1c642a` reproduced, or an envelope of DigiKit's own before the sum.
- STRETCH (`0x1c5576`) is not hooked and keeps a voice per note. WERP, REPITCH, GRID and SLICE use the same renderer as ONESHOT and are not
  tested.
- The stock renderer reads 16-bit PCM, so a copy cannot produce a bad sample from the sample alone; the guard covers corrupt state.
