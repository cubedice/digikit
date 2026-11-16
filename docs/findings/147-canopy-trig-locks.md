# 123: CANOPY trig locks: DT2 1.17 lock storage and the packed design

DT2 OS 1.17, 2026-11-12. Investigation for parameter locks (p-locks) on CANOPY
tracks, "option A, packed form": CANOPY stores its own (knob, value) pairs in
stock lock lanes of SRC slots its machine does not use. Static reading of the
stock MAIN image (one reader, **[D]**) and emulator measurements
(`tests/regression/runtime/canopy-plock.test.ts` on `canopy-multi-release` +
`canopy-fm-release`, **[D]**). Sequencer trigs (stage 1) and keyboard
recording (stage 2) are implemented; the packed knob-lock implementation and
its remaining qualification are described below.

## Baseline behaviour before stage 3 [D]

| Gesture on a CANOPY track | Result |
| --- | --- |
| Trigs on steps 1 and 5, PLAY | No CANOPY voice starts; the track plays its stock sample (a sustained tone, 0.079 RMS) |
| Grid recording, hold step 5, turn LEV (stock, SOUND H) | Lock recorded: the Sound keeps 100 (raw `25600`); playback applies 96 (`24576`) on step 5 only |
| Grid recording, hold step 5, turn SOUND C (CANOPY's own) | Nothing: no lock, no sound edit, CANOPY's values and DSP bank unchanged |

So stock locks already work for CANOPY's stock knobs (TUNE, LEV); CANOPY's own
knobs and sequencer trigs do not reach CANOPY. The emulator's grid-record
hold-and-turn does record locks (earlier harness attempts in findings 02 and 64
did not use grid recording).

## Lock storage [D]

The active pattern object (pointer at `0x4316501c`) holds its locks at
`+0x17e50`:

| Offset | Content |
| --- | --- |
| `+0x0000` | 80 lanes of `0x102` bytes: runtime parameter index (byte), track (byte), 128 x s16 step values; `< 0` (`0xffff`) = no lock |
| `+0x50a0` | lane map, one byte per `track * 71 + index`; `0xff` = no lane |
| `+0x5510` | lock count per `track * 128 + step` (byte) |

Confirmed in RAM: the recorded LEV lock is lane 1, header `[34, 0]`, step 5
`24576`, neighbours `0xffff`.

| Routine | Address | Behaviour |
| --- | --- | --- |
| set lock (`this`, track, step, index, value) | `0x40040702` | Checks track <= 15, step <= 127, index <= 70, value >= 0 only; **no range clamp**. Reuses the track's lane for the index or claims a free one; **returns 0 and stores nothing when all 80 lanes are used**. Notifies (`vfunc +0x10`) and recounts the step |
| clear lock | `0x40040822` | Writes -1; frees the lane when all its steps are empty |
| get lock | `0x400405ce` | Value or -1 |
| recount step | `0x40040650` | Rebuilds `+0x5510` for (track, step) |
| per-track wrappers (editor object: lock owner `+0x2c`, track `+0x3c`; step < pattern length) | `0x40054ad4` set, `0x40054b1a` clear | |
| `SoundParameterSet` vfunc 17 / 18 (vtable `0x401f2e68`) | `0x4003c040` / `0x4003c0ac` | The UI's generic lock on the **current** track: vfunc 17 is (`this` unused, code, value, step), vfunc 18 (`this`, code, step); code -> index (`0x400da03c`: descriptor table `0x4020f59c`, 60-byte rows, index at `+4`), then the wrapper. Stock values are `value << 8` |
| load lanes | `0x400dc60a` | Copies every value >= 0 unchanged; skips id > 76 or track > 15; ids through `0x400dae88` (storage -> runtime, table `0x40213b64`) |
| save lane | `0x400dc94e` | Copies all 128 values unchanged; id through `0x400daec2` (runtime -> storage, table `0x40213a50`) |
| collect a step's locks into the trig event | `0x400d6916`, called by the trig routine `0x400d6982` at `0x400d6d06` when the step's count is non-zero | (index, value) pairs: count at event `+8`, 8-byte entries from `+0x14` |

SRC slots 25..34 map to storage ids 33..42 and round-trip exactly (all ten,
including 29 and 30).

## Packed values survive [D]

Locks planted directly in RAM on step 5 of a CANOPY track, then played: slot
31 = `0x7fff` reads back exactly as `32767` (above the stock `0x7f00` ceiling,
so no clamp at playback), slot 26 = `5` reads `5` (low bits survive). With write,
save and load also value-transparent, a lock value carries a full 15 bits.
A slot-28 lock planted on step 1 never applied; the likeliest cause is that the
scripted step-1 trig was not recorded, **[O]**.

## Usable slots

CANOPY's machine type uses only TUNE (25) and LEV (34) of the SRC slots. Of
the rest, **28 is the sample slot** (finding 04, `0xe0 + 0x60t`, [V]) and 29/30
are MANUAL SLICE's sample bank/slot rows (finding 02); the sample loader also
gathers p-locked sample slots (`FUN_4004e598` on 1.16). Packed values there may
make the loader preload samples, so the safe set is **26, 27, 31, 32, 33: five
slots**. 28 is usable only if the loader ignores locks on non-sample machines
**[O]**.

Their parameter codes for vfunc 17/18 are `0xcb` (26), `0xcc` (27), `0xce`
(31), `0xcf` (32) and `0xd0` (33) (rows with `+0 = 0`; rows from `0xd2` repeat
the indices for another machine family) **[D]**.

## Where a trig's step comes from [D]

The sequencer's main trig path (`0x400d72ec`..`0x400d73b4`) computes each
track's step, stores it as a byte at `0x43164f94 + 0x15b + track` (remainder
at `+0x16b + track`) and passes it with the pattern `*0x4316501c` to the trig
routine `0x400d6982` (track, pattern, ?, step, ...). So at a trig the frame
path can read the step byte and then the step's locks from the lanes above,
exactly, instead of the raw parameter row (which carries the Sound's base
value when a step has no lock). A second caller (`0x400d8200`) takes its
pattern from `*0x43165024` or `*0x4316501c` and its step from elsewhere; what
it plays (likely a pattern change or temporary pattern) is open **[O]**.

## Design consequences

- **Encoding.** 15 bits per lock: e.g. knob number in 6 bits (all CANOPY
  controls and FM settings) and value in 9 bits, with an escape for the few
  controls wider than 0..511 (largest range 0..1000). A chord of several keys
  packs three intervals of 5 bits into one slot.
- **Reading at playback.** The DSP frame carries the *smoothed* SRC slots
  (finding 04), which would blend packed values; read them on the ColdFire
  from the trig event's lock list (exact, delivered with the trig) or the raw
  row (`0x80003362 + 0x8e * track + 2 * index`, set at the trig).
- **Writing.** `0x40040702` (or the per-track wrapper) accepts any value from
  module code; a full pattern (80 lanes) makes it fail, which CANOPY must
  report. Each slot in use on a track takes one lane for the whole pattern.
- **Stock operations.** Copy, paste, clear, save, load and the trig LED/lock
  counts treat these lanes like any lock, because they are stock lanes.

## Keyboard recording (stage 2) [D]

While the sequencer live records (`0x4005ad40`: transport running and byte
`+0x271` of the sequencer UI object at KeyboardView `+0x98`) or a trig is held
(`0x4005b336`: a bit of the 128-bit mask at `+0x258` below the pattern length),
CANOPY hands KEYBOARD presses on its track to the stock note action instead of
its panel path. Stock records the press as a trig (live) or note lock (held
trig) and auditions it as a sequencer trig, which the frame path (stage 1)
plays on CANOPY. Measured (`canopy-record.test.ts`, live recording
unquantized): two presses (notes 60 and 65) sounded on CANOPY while recording
and played back on CANOPY from the pattern; a press outside recording stays on
the panel path. During recording a track plays one note at a time, as stock
tracks do; several held keys record the last one only. The held-trig case
(note lock from the keyboard) is not measured.

## Open [O]

### Stage 3: packed absolute locks [D]

Five stock lock indices (26, 27, 31, 32 and 33) carry CANOPY values; index 28
is not used because its sample-loader behavior has not been qualified. Each
15-bit value has a `01` low-bit tag and a 13-bit dense index selecting one of
6,321 control/value combinations. FM and WT controls have separate IDs. Stock
UI values use `value << 8` and cannot collide, but arbitrary values written by
other software can match the tag; this is not a cryptographic ownership marker.
There are at most five CANOPY values on one step and the pattern's existing
80-lane limit still applies. Multi-step editing validates all steps first and
rolls back completed writes on a storage failure.

The stock held-step edit path calls `0x4005ab18(sequencer_view, 1)` before
editing a parameter lock. This marks the held gesture as an edit at view byte
`+0x268`. Omitting that call caused some short holds to toggle the trig off on
release, erasing the newly recorded lock. CANOPY calls the same routine before
turning or clearing a held-step control. **[D]**

The stock sequencer prepares events ahead of time. Capturing locks at its
collector (`0x400d6d14`) lost association with the note: a later event could
overwrite the snapshot. DT2 1.17's audio ISR applies the *due* event through
`0x400d9368`, called at `0x4002e358` and `0x4002e5d2`. The preceding pointer
checks (`0x4002e348` and `0x4002e5c2`) also see events without lock lists, so
a later unlocked event supersedes an earlier lock in the same block. Tagged
entries are removed before the stock carrier sees them and paired with the
corresponding frame trig. Capture is gated on an admitted,
ready instrument. A checksummed sideband binds the snapshot to the MIDI packet
identity, track and generation; the DSP overlays it on a private copy of the
sound and then applies native modulation. A WAVE lock carries the target
family's remembered settings in the same snapshot.

In the emulator, a real held-step panel turn recorded COLOR 84 on step 5 while
leaving the Sound's COLOR at 64. A playback trace observed the tagged value
arrive at the DSP as 84 and the following unlocked trig return to 64
(`out/canopy-packed-multitrack-proof.log`); the same proof cleared the lock by
pressing its encoder while holding the step and restored the exact packed value
by reloading the saved project. Copying the pattern to a second slot and
switching back preserved the same value in both patterns. A running transition
also delivered the destination pattern's lock after the boundary committed.
During live recording, a knob turn wrote another tagged value while the Sound's
base value stayed unchanged. A second CANOPY track recorded and played its own
lock without changing the first track's value.
Host checks exercise all encoded ranges,
storage rejection and rollback, queued note retries, chord mapping, damaged
sidebands, source-bank overlays, and native MOD on top of a locked value.
This image has not been installed or tested on physical hardware.

Still to qualify: real-device behavior and the unqualified slot-28 loader path.

## SRC lock feedback and preset-menu status

On DT2 1.17 the custom SRC controls display values from the held step's
packed snapshot. The control highlight uses the mapped parameter ID of the
effective source family; native TUNE and LEV remain with the stock renderer.
Controls covered by a shared diagram highlight their label instead of filling
the diagram. Host tests exercise the mapping and the native inversion draw.
In a DT2 1.17 emulator comparison, a held step with a native SRC LEV lock
alternated the SRC LED between white and off; the released step remained
white. A held CANOPY step with a packed SRC lock remained white. The stock
LEV lock was planted in the disposable emulator pattern, while the CANOPY
lock was recorded with the panel. [D] Making the LED indicate packed locks
remains a separate integration task, and the comparison is not a physical
device measurement. [O]

The DSP status producer continues while the preset menu covers SRC. The UI
tick now drains the bounded status queue for its selected track, retaining the
track and generation checks. A separate interrupted configuration could leave
an instance waiting indefinitely: a reset on another track cancels an active
configuration transaction. A phase-2 instance now re-enters the reset and
configuration handshake if its transaction disappears, with the observation
and transition protected from the audio interrupt. Host tests cover an active
transaction, cancellation, and a reset on another track. A four-instance
emulator run saved and loaded a preset and played a panel note afterward. [D]
These results do not establish the cause of a particular hardware LINK LOST or
prove its recovery on a physical device. [O]
