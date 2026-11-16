# 127. CANOPY Sound persistence outside Sound memory

CANOPY's own SRC settings (32 controls, the remembered FM bank, family,
keyboard mode and the effective chord shape) now travel with the Elektron
Sound on DTII 1.17: in a saved record as a 40-byte extension, and in RAM in
an association store outside Sound memory. Nothing CANOPY-specific is written
into a live Sound. This replaces the rejected in-Sound handles (`Sound +
0x13a` is a stock field; runtime parameter 0 is reachable from the generic
setter, two descriptor rows and the lock loops), see
[the persistence investigation](../design/canopy-native-persistence.md).
Emulator only; not installed on hardware.

## Record extension [D]

The v3 Sound writer `0x400dba94` zero-fills record positions 0..89
(`memset(record + 0x1c, 0, 0xb4)`) and then writes the 69 saved runtime
parameters through the save permutation `0x40213a50`. The loader
`0x400db3b2` reads only positions 0..76, in ascending order, each into Sound
`+0x14 + 2 * load[position]` through the load permutation `0x40213b64`.
Positions 0, 4, 8, ..., 32 load into dummy parameter 0; 32 is the last of
them, and the writer always leaves it zero.

The extension uses positions 4..28 (step 4) and 77..89: twenty halfwords that
stock always writes as zero and never leaves in a live parameter. Stock 1.17
therefore loads a record carrying the extension to exactly the Sound it loads
from the same record without it, and saving on stock firmware drops the
extension. Byte 0 is `0xc0 | format` (format 1), bytes 1..37 pack 293 bits of
preset fields, bytes 38..39 are a CRC-16/CCITT. Any `0xc_` format byte is
carried unchanged; only format 1 decodes. The facts are pinned by
`tests/patchkit/test_canopy_sound_record_sites.py`; the codec is
`modules/core/adapters/instrument-play/src/cf/sound-record.c`.

## Association store [D]

Pattern k's kit is at `0x426532b8 + k * 0x5574` and its track t Sound at
`+0x34 + t * 0x450`; pool Sound i is at `0x4291377a + i * 0x450` (i <= 127).
Both bases are literals of stock code (`cf.project.site.kit_table_lit`,
`cf.project.site.sound_pool_lit`). The store (`sound-assoc.c`) has one fixed
slot per kit-track Sound (129 x 16) and pool Sound (128): 2,192 slots of 48
bytes, 105,216 bytes. The rest of a 128 KiB block from the firmware heap holds
497 temporary entries (52 bytes each) for Sounds copied elsewhere (clipboard objects, undo
buffers, stack temporaries), each sealed with a hash of the copied Sound's
first 0x140 bytes; a temporary is used only while that hash still matches.
`cf.expansion` has no room for the store (340 bytes free in the
`canopy-overloop` set).

The association follows the stock operations that replace whole Sounds:

| Hook | Site | Effect |
| --- | --- | --- |
| `cf.util.hook.memcpy` | `0x40136388` | copies of at least 0x450 bytes carry every whole Sound's extension |
| `cf.util.hook.memset`, `cf.util.hook.bzero` | `0x401363d0`, `0x40135db4` | fills of at least 0x450 bytes clear it |
| `cf.machine.hook.sound_deserialize` (existing wrapper) | `0x400db3b2` | a successful load sets the record's extension, or none |
| `cf.machine.hook.sound_serialize` | `0x400dba94` | a successful save writes the extension into the record |
| `cf.machine.hook.sound_init_slices` | `0x400e65c8` | the Sound initializer clears it |
| `cf.machine.hook.sound_type_write` | `0x4005179e` | a direct machine-type change clears it |

The static census found about 32 memcpy/memset sites with a Sound (0x450) or
kit (0x5574) length, among 767 memcpy, 246 memset and 74 bzero references, so
one routine hook covers the observed copy paths (clipboard `0x401aaccc` and
`0x400521aa`, pattern copy `0x40047a16`, the swap through stack buffers
`0x40045774`/`0x40045794`, undo) without per-site patches. Copies and fills
shorter than one Sound take the stock path after three instructions; longer
ones that touch neither the slots nor a 64 KiB page holding a temporary return
after a few compares. A first build without that filter starved the boot:
stock moves large buffers often enough that scanning all temporaries with
interrupts masked kept the firmware from reaching ready.

## Instance side [D]

Each admitted instance follows the Sound it is admitted on (`control.c`,
`sa_follow`): it adopts that Sound's extension when it binds or when stock
replaces the Sound under it (every slot write advances a sequence number), and
publishes its own state back whenever it changes. The save and copy hooks also
publish a bound instance's current state first, so a save or copy right after
an edit carries the edit. A Sound without an extension keeps the instance's
current state, as every Sound did before, and the instance claims it; an
extension that does not decode is left unchanged until the state changes.
Applying an adopted state goes through the existing preset boundary
(`preset_apply_locked`: suspend and reconfigure).

Consequence: switching patterns now switches each CANOPY track to that
pattern's Sound settings, like stock parameters, instead of keeping the
previous pattern's configuration.

## Emulator results [D]

All runs use the production adapter (`core/instrument-play`, no test
fixtures) on the disposable DTII 1.17 emulator. The three tests are registered
in the `runtimeAcceptance` of `modules/machines/canopy/tests` (they ran first
behind opt-in variables, with the evidence below).

- Project save and power cycle (`persistence.device.test.ts`): two tracks edited independently to
  `(mode, CHRD, VOIC, STRUM)` = `(1, 4, 3, 16)` and `(1, 2, 2, 8)`, saved with
  the stock panel, then a fresh emulator started from the saved card and the
  project loaded with the stock panel: both tracks came back unchanged. By
  ready the store had seen 29,708 long copies and fills, all filtered; the
  load applied the 2 saved extensions. Evidence
  `out/canopy-persistence/digi-session-r2GjEy/probe.json`. The baseline before
  this change returned `(0, 0, 0, 0)` for both tracks.
- Pattern copy, paste and undo (`pattern-copy.device.test.ts`, formerly
  `pattern-copy-proof`, first with one CANOPY track): A01 and A02 edited differently and saved;
  returning to A01 restored A01's settings; A01 copied, then edited again;
  pasted onto A02 the copy-time chord (5, not the later 6), and undo
  restored A02's own (7). Two temporary hits (clipboard and undo buffer).
  Evidence `out/canopy-pattern-copy/digi-session-CLlyap/probe.json`.
- Track Sound copy and paste (`copy-handoff.device.test.ts`): track 1 edited, copied with TRK+TRIG+RECORD,
  edited again, pasted onto track 2 with TRK+TRIG+STOP: track 2 took the
  copy-time settings (chord 4, not the later 5) through one sealed
  temporary. Evidence `out/canopy-copy-handoff/digi-session-nkZyUb/probe.json`.
- The pattern test with two CANOPY tracks (now its only form): each track
  of the pasted pattern took its own copy-time settings (track 1 chord 5,
  track 2 chord 3) and undo restored both (7 and 8). Evidence
  `out/canopy-pattern-copy/digi-session-lIk6vX/probe.json`.

## Preset pool and +Drive library [D]

`sound-transfer.device.test.ts` drives the stock PRESET menu (left column:
LOAD and SAVE for the +Drive library, MANAGE, POOL). A pool save is POOL, the
slot, RIGHT, SAVE TO HERE, then the name and tag pages; a pool load is POOL,
the slot, RIGHT, LOAD TO TRACK and a CONFIRM Y/N. A +Drive save is SAVE, the
`A:` slot, the name and tag pages; a +Drive load is LOAD and the preset.

- Pool: track 1 at `(mode, CHRD, VOIC, STRUM) = (1, 4, 3, 16)` saved into pool
  slot 001, then changed to STRUM 26. Pool slot 001 loaded onto track 2 (which
  had `(1, 2, 2, 8)`) gave `(1, 4, 3, 16)` and left track 1 unchanged; loaded
  back onto track 1 it restored `(1, 4, 3, 16)`. The save is one whole-Sound
  copy into the pool's fixed slot; the loads are whole-Sound copies, two of
  them through sealed temporaries.
- +Drive: track 2 at STRUM 36 saved to `A:001` (the Sound writer added the
  extension to the record), changed to STRUM 41; `A:001` loaded onto track 1
  gave STRUM 36 and left track 2 at 41. The load applied the record's
  extension (`ip_sa_info[4]` = 3).
- After a power cycle (a fresh emulator on the card the guest wrote), a new
  CANOPY track at `(0, 0, 0, 0)` loaded `A:001` and came back at
  `(1, 4, 3, 36)`: only the saved record could supply it.

Evidence `out/canopy-transfer/digi-session-dMy9G3/probe.json`, with an OLED
frame of every menu step in the run's `remote-input` artifacts.

## SysEx: blocked in the emulator [O]

DTII 1.17's SETTINGS > SYSEX DUMP sends a PROJECT or a PATTERN and receives a
PATTERN (into a chosen pattern) or PRESETS (into a +Drive bank); no menu sends
a single preset. A pattern dump is therefore the SysEx path that carries track
Sounds. `sysex-pattern.manual.device.test.ts` sends pattern A01 with two
CANOPY tracks over USB, intending to change the tracks and receive the dump
back. In three runs the firmware handed exactly 4032 bytes (one message,
`F0 00 20 3C 14 00 50 ...`, command 0x50) to its USB transmit function and then
stayed on SENDING for at least 240 s of emulated time: the emulator does not
model the USB peripheral, so the transmit completion the firmware waits for
never comes. With the CANOPY runtime image the firmware sent on USB without
asking SELECT INTERFACE (the stock `emulator` image asks USB or USB+MIDI), so
the modelled DIN output received nothing. Whether a pattern dump carries the
extension (it does if the dump goes through the Sound writer `0x400dba94`) is
open until USB transmit is modelled or the dump is tested on hardware.

## Write-watch inventory [D]

`pattern-copy.device.test.ts` ran with the ColdFire write-watch over the kit
table (`0x426532b8..0x4290422c`) and the Sound pool
(`0x4291377a..0x42935f7a`) from instruction 1 (`out/run-persist-ww.sh`; the
summary is `out/sidetable-ww/ww-pattern-copy.summary.json`). The harness
replaces the emulator during a session and each instance rewrites the log, so
the record covers the last instance only: machine selection on four Sounds,
SRC edits, pattern switches, a project save and the pattern copy, paste and
undo. 11,110 stores came from six PCs:

| PC | Code | Stores | What |
| --- | --- | ---: | --- |
| `0x401363a4`, `0x401363b2` | stock memcpy | 10,986 | whole Sounds and kits: hooked |
| `0x47fc4df2` | `ip_type_site` | 4 | the replayed machine-type write (`+0xa2`): hooked |
| `0x40050c0e` | stock machine change | 32 | SRC parameters 25..34 of each new CANOPY Sound: a partial edit |
| `0x47fcbc86` | `instrument_mod_defaults` | 84 | CANOPY's neutral MOD offsets (`+0x48..+0x56`): a partial edit |
| `0x4003681a` | stock | 4 | kit data after the 16 Sounds, not a Sound |

No whole-Sound write bypassed the hooks in these operations. Operations not
in the test (pool and file Sound transfers, kit reload, SysEx) are not covered
by this record. **[O]**
