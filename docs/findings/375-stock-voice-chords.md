# 375. Chords from one track's stock sample voice

Can a stock sample machine play a chord on its own track, without borrowing other tracks' voices (the voice groups of finding 369)?
DT2 OS 1.17. Marks as in `docs/FINDINGS.md`. Emulator, and one measurement on the owner's device.

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

## Hidden voices: further keys on the same voice [D]

The same three records per voice also serve as hidden voices: further keys of a track that plays no copies, each with its own note-on, pitch and
note-off.

- DSP (`_hv_prepare`, `_hv_audible`, `_hv_owner_env`): a hidden-voice word (`0xc6` in bits 24-31, the track in 20-23, the voice h in 18-19, the
  gate in 17, a serial in 14-16, the pitch in 7-13 as a MIDI note, the velocity in 0-6) arrives through the same frame word as the shapes and
  sets the track's bit of `_cs_hmask`. A new serial while the gate is open starts the voice: the owner's record is copied, its buffers and filter
  state (words 1-96) cleared and its phase seeded as the firmware's arm does (start, or end minus one sample when reversed). Every block its step
  is the owner's times 2^((pitch - the track's note) / 12), the track's note read from the frame copy (halfword 1 + t). Each voice has an
  envelope of its own, moved by the even voice of the pair: up by 1/3 per block (2 ms) while its gate is open, 0.97 per block once it closed,
  ending below 0.0005. Voice 0 is the owner's own note: 1 while its gate is open, the same release when the track keyboard closes it. The
  envelopes ramp over the block; the hidden voices are summed into the owner's block, so the track's filter, amp envelope and sends still run
  on the sum, and the guard covers them as it covers the copies.
- Track keyboard (`track-native.c`): a track with hidden voices (`tk_native_set_hidden`, the same stock machines as the copies) can take up to
  three more notes. A note no track of its group can take (no free helper, or an AUTO helper not admitted yet) while the owner's own note sounds
  without copies plays on a free hidden voice: no stock note, only its word. Its key up closes its gate. The owner's key up while hidden voices
  sound closes the owner's gate (its sample fades) and holds its stock note-off (`NOTE_HELD`) until the last hidden voice is released; the
  owner's next note opens its gate again. A saved group's helpers still come first; AUTO now takes the hidden voices before it borrows a track.
- The frame word carries 80 words in turn (16 shapes, 4 hidden-voice words per track once set), changed ones first.

The behaviour is paraphonic: the track's stock amp envelope opens with the first key and releases after the last, and each note's own
envelope only fades it in and out within that. Each hidden voice sounds at the level its own velocity gives a stock note (below).

`stock-chord.device.test.ts`, CHRD PLAY, TRIG 9 (220 Hz) held, then TRIG 13 (330 Hz): both sounded at 0.00903 and 0.00902 with only track 1
active (no helper) and hidden voice 1's gate open; TRIG 9 up: 220 Hz fell to 0.000003 within 150 ms while 330 Hz stayed at 0.00902; both up:
silence. A run with the words written through `dsp.patch` (no track keyboard) gave the same: a fifth and a third added to a held note at the
note's level, the note's own gate closed to 0.000012 while the two played on, then the fifth's gate closed alone.

## Velocity of hidden voices [D]

The stock voice's level follows its velocity through a factor latched at its note-on (finding 135): the per-track unpack (`0x1c24e9`,
`dsp.voice.fn.track_unpack`) reads the frame's velocity word (halfword 26 + t, velocity << 8) and the track's velocity curve (halfword 42 + t,
clamped to 0 .. 4), looks the velocity up in one of five tables of 128 floats (`0x8045a9b8` + 512 bytes per curve, the literal at `0x1c256b`;
port value `dsp.voice.value.velocity_curves`) and keeps 127/128 of the entry. The voice latches it at its note-on, and the gain builder
multiplies the amp by it. A hidden voice is summed before the amp, so without a factor of its own it sounds at the owner's velocity.

- `_hv_velocity`: the factor of hidden voice h is the curve's entry for its velocity (bits 0-6 of its word) over the curve's entry for the owner's
  velocity (the frame's), at most 8.0, and 1.0 when the owner's entry is not a finite number of at least 2^-20. `_hv_run` takes it again, with
  the pitch ratio, only when the owner's velocity, the curve, the track's note or the note's pitch or velocity changed (state word 4); the
  envelope ramp the hidden voice is added with is multiplied by it (state word 7).
- Curve 0 has the same entry for every velocity (finding 135): factor 1, so hidden voices ignore velocity as the stock voice does.
- The ratio assumes the gain builder's clamp (`2 * clamp(a * a * b, 0, 1) * c`) does not saturate for the owner. A factor above 1 means the
  hidden voice reaches the track's bit reduction, filter and overdrive louder than a stock note could.

`stock-chord.device.test.ts`, MIDI note-ons over USB on track 1 (Auto Channel 1, CHRD PLAY), note 60 and then note 67 (the hidden voice, at
330 Hz); the test card's track 1 has curve 4. Both at velocity 100: factor 1.0000, the second note at 0.00902. The second at
40: factor 0.1116, level 0.00100 (0.111 of the first). The owner at 60 and the second at 100: factor
4.314, level 0.00902 (1.000): the second note keeps its own level whatever the owner's velocity. An earlier run with the owner
at 40 reached the cap: curve 4 needs 8.96 for 100 over 40, so the note sounded at 0.89 of its level.

## On the device [D]

Image `51c24268b85b5eeb` (proto/stock-chord a1e0049a7) on the owner's Digitakt II with CANOPY, Overloop, PRISM, RELAY, the remote and
`digikit/debug`, the owner's project loaded and the transport stopped. The DSP's block time from core/health's one-second windows (`health_hs`,
the DSP's own EMUCLK count of its block handler, read over PEEK); percentages of an assumed 1 GHz block (666,667 cycles). Keys through
`digikit/remote` on track 10 (ONESHOT) of pattern A02, retriggered every 400 ms; averages of the whole windows:

| | cycles per block | of a block |
| --- | --- | --- |
| idle | 437,700 | 65.7 % |
| one stock note (POLY off) | 445,900 | 66.9 % |
| CHRD MAJ6, POLY on: the note and three copies | 477,900 | 71.7 % |
| POLY on, CHRD PLAY: one key | 445,500 | 66.8 % |
| four keys: the note and three hidden voices | 479,100 | 71.9 % |

- A sounding stock note (its two voice records) costs about 8,200 cycles per block, 1.13 times the interpreter's instruction count of the
  renderer. A copy costs about 10,700 (three copies 32,000 over the single note) and a hidden voice about 11,200 (three 33,500), per note of the
  pair: 1.6 to 1.7 % of a block each.
- Leftover cost: a track that keeps a chord shape stays on the chord path after its notes end, and calls the renderer, the guard and the sum
  for its three copies every block: about 6,000 cycles (443,700 idle afterwards). A track that has used hidden voices stays on their path:
  about 4,800 cycles. Both should go when the records are inactive.

## Load limits [D]

From the device figures above, `_cs_voice` now keeps the extra voices within a budget and drops what they no longer need:

- A copy that stopped (ACTIVE clear) gets no step and no render, guard or sum. A track leaves the hidden path (its bit of `_cs_hmask`, and its
  `_cs_vmask` bits when it has no copies) when, at the pair's odd voice, no hidden voice sounded or waited to start and its own note is not
  faded while its sample still plays; a word with an open gate (or the owner's closed one) puts it back.
- The budget: once per block, at the first chord voice, the extra voices may take `_cs_limit` (560,000 cycles, 84 % of an assumed 1 GHz
  block) minus what the last block cost without them: the block handler's own EMUCLK count from word 0 of the reply record the last block built
  (`_dv_rec[0]`, rotated by 16 bits, finding 125), less the extra voices' own cycles of that block. Each extra render is measured with EMUCLK and
  paid from it. One that does not fit (its cycles so far plus 6,000), or past 24 extra records in the block, ends (ACTIVE clear) and `_cs_skips`
  counts it: a copy comes back at the owner's next note-on, a hidden voice at its next note. It does not stutter.

Emulator (where EMUCLK counts instructions): with `_cs_limit` 1 a MAJ chord played only its root (both copies of both voices dropped: 4) and
with 560,000 the next one was whole; after the two keys, once the first key's sample ended, track 1 was off both paths.

Two assembler facts found on the way: an immediate such as `3 * 0x1d8` assembles as its first term (`3`); use literals (a sum such as
`DL_TAIL_WORDS - 158` assembles as written). Data symbols of an image object are exported without `.GLOBAL` (with it, patchkit refuses
"exported twice").

## Open [O]

- The budget assumes a 1 GHz block (666,667 cycles): with a slower clock 560,000 cycles would leave less margin. The new build has not run on
  the device: its idle cost of a track with a chord set, and the budget, are measured in the emulator only.
- The notes of a chord share the track's amp envelope and filter; a release of one note of a held chord is not possible. Hidden voices have
  their own fade in and out but the track's envelope and filter still shape them all (paraphonic). A per-voice stock envelope and filter would
  need the inline set-up of `FUN_1c642a` reproduced on private state.
- A hidden voice's velocity factor is at most 8.0. On a steep curve a soft first key and a hard second key need more (curve 4: 8.96 for 100
  over 40), and the second key then sounds below its level. Raising the owner's latched factor to the loudest note's and scaling the owner's
  own block down instead would keep every factor at most 1, but writes stock state (the latch, finding 135).
- STRETCH (`0x1c5576`) is not hooked and keeps a voice per note. WERP, REPITCH, GRID and SLICE use the same renderer as ONESHOT and are not
  tested.
- The stock renderer reads 16-bit PCM, so a copy cannot produce a bad sample from the sample alone; the guard covers corrupt state.
