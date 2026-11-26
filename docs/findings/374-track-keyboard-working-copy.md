# 374. Track keyboard settings across a power cycle: the firmware's working copy

The owner's report of 2026-11-26: POLY and the chord of track 10 were gone after the Digitakt II restarted, with or without a project
save. DT2 OS 1.17. Marks as in `docs/FINDINGS.md`.

## Why they were gone [D]

- The track keyboard keeps a pattern's settings (track POLY, ARP, chord and the rest, step locks, recorded chords) in its own record,
  which travels in the tail of the stock pattern record (finding 371). It is written only by the hook on the stock pattern
  serializer, `tk_pattern_fw_save`, so only when the firmware serializes that pattern.
- What a boot brings back is the firmware's working copy of the project: the arena that its boot record holds, that its power-off
  writer stores and that the boot restores (finding 40). The firmware updates it when it sees a change of its own data. A track
  keyboard edit changes no stock byte, so the pattern's record there keeps the settings of the last time the pattern was serialized.
- DigiKit's own restart (`dkrt_restart`, also after a module selection) writes neither the project nor the boot record
  (`DKRT_SAVE_MODE` 0, finding 40): it brings back the working copy of the last power-off.

On the device (image `3aa9bc7b978b0127`, read over `digikit/debug`'s PEEK): track 10 with POLY and MAJ6 (override 1), a quick save
reported by the owner, VOIC changed after it, a power cycle: track 10 came back with the defaults (override 0). Track 11's settings,
saved long before, came back. A later quick save made over `digikit/remote` (FUNC + SETTINGS, YES) ran the save hook twice
(`tk_storage_info` saves 1 to 3); no power cycle followed it.

## Where the working copy keeps a pattern [D]

Emulator, `core/track-keyboard` and `core/health`: KEYS settings on track 1 of A01 and A02, then DigiKit's restart with
`DKRT_SAVE_MODE` 3 (the firmware's project serializer, then its boot record writer). The card overlay of the restart holds the whole
arena in the second record slot (sector `0x48000`, 28,364 sectors written); the track keyboard's `TKR1` tail is at arena offset
`0x15ddc` (A01) and `0x2b9dc` (A02). With the tail at `0x15acc` of a record, project pattern i's record is at arena offset
`0x310 + i * 0x15c00`; its first word is the record version, 4. After the boot the arena in RAM, `0x4099d588`, holds the same
bytes at the same offsets.

| Restart (emulator) | Track 1 after the boot | `tk_storage_info` after the boot |
| --- | --- | --- |
| `DKRT_SAVE_MODE` 3: serialize, then the boot record | POLY, MAJ, override as edited | 1 load, 1 save |
| `DKRT_SAVE_MODE` 0: the shipped restart | defaults | nothing |

## The fix

- `tk_pattern_fw_work_write(address)` (image, `track-pattern-fw.c`) writes a project pattern's live settings into its record in the
  working copy with the save hook's encoder and record writer. It refuses a pattern whose record there does not start with version
  4 (`tk_work_info[1]`). It encodes with interrupts enabled, into a buffer of its own (the save hook's belongs to the firmware's
  save task), and masks interrupts only for the write into the record, so the firmware's save task cannot interleave with it; a
  later serialization of the pattern runs the save hook, which writes the same settings. `tk_work_info`: writes, refusals, records
  that did not fit.
- The service's poll (`keep_working_copy`, `track-runtime.c`) fingerprints the settings of the pattern being edited four times a
  second and calls it two seconds after they last changed. A pattern that is left is written at once if its settings changed since
  the last write. The seam to the image is version 4 (one image function more).
- What is still lost: an edit younger than two seconds at a power cut, and everything since the last power-off at DigiKit's own
  restart (its save mode is upstream's decision; the module menu asks for a project save first).

## Evidence

- **[D]** `runtime/platform/track-keyboard/tests/working-copy.device.test.ts` (emulator): POLY and MAJ on A01's track 1 without a
  save; two seconds later `tk_work_info` counts a write; DigiKit's restart with the boot record writer alone (`DKRT_SAVE_MODE` 2,
  no serialization) brings them back. Then CHRD changed and A02 chosen at once: after the same restart A01 has the new chord.
- **[O]** A power cycle of a device. The emulator has no model of the power-off (finding 356); the boot record writer stands in for
  the firmware's power-off path, which finding 40 read as writing the arena as it is.
- **[O]** The arena address and layout are DT2 1.17's, read once here; the version word check keeps a mismatch from writing.
