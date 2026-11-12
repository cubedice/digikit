# 123: CANOPY trig locks: DT2 1.17 lock storage and the packed design

DT2 OS 1.17, 2026-11-12. Investigation for parameter locks (p-locks) on CANOPY
tracks, "option A, packed form": CANOPY stores its own (knob, value) pairs in
stock lock lanes of SRC slots its machine does not use. Static reading of the
stock MAIN image (one reader, **[D]**) and emulator measurements
(`tests/regression/runtime/canopy-plock.test.ts` on `canopy-multi-release` +
`canopy-fm-release`, **[D]**). Nothing here is implemented yet.

## Current behaviour [D]

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
| `SoundParameterSet` vfunc 17 / 18 (vtable `0x401f2e68`) | `0x4003c040` / `0x4003c0ac` | The UI's generic lock by parameter code and step: code -> index (`0x400da03c`), then the wrapper. Stock values are `value << 8` |
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

## Open [O]

- **Which trig is held.** CANOPY's knob handler must know the held step(s)
  (and grid versus live recording). The stock path passes the step into
  `SoundParameterSet` vfunc 17 from a caller reached through the vtable; not
  traced. Alternative: route held turns of CANOPY's knobs through the stock
  SRC handler on a slot's code and rewrite the value in a hook on
  `0x40040702` for CANOPY tracks and slots 26..33.
- **Trig playback (prerequisite).** Sequencer trigs must start CANOPY notes and
  silence the stock sample; the trig routine `0x400d6982` is the candidate hook
  (track, pattern, step, event with locks).
- Clearing a CANOPY lock from the panel, live recording of knob moves, and
  display of locked values on CANOPY's pages.
- Slot 28's loader behaviour; the step-1 planted lock.
