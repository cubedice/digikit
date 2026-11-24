# 372. The fork's `cf.expansion` budget and what the instrument code is tied to

Static measurement for the plan [fork-image-diet.md](../plans/fork-image-diet.md): DT2 OS 1.17, the committed objects
(`build/*.dko.json`) at `3a3a91270`, the install record `firmware/hardware/2026-11-20-looptrans` (ignored by git) and the
generator of CANOPY's wavetables. No image was built in this work (WSL did not start), nothing ran on a device or the
emulator. Marks as in `docs/FINDINGS.md`; every result here is **[D]**. No result was checked by a second agent.

## The budget

`cf.expansion` is 1,396,736 bytes. The installed image `9ee1ed898e39214e` (set `canopy-overloop-prism`, commit
`7369a18f5`) uses 1,318,652 (78,084 free); `dsp.l2` 566,460 of 922,756. The sum of the object sizes of the set's module
closure at `3a3a91270` is 1,315,003 bytes: the link adds about 3.6 KB of tails, stubs and alignment.

The largest parts are shared with upstream: the runtime object, 924,912 bytes (text 241,634, rodata 12,220, data 26,473,
bss 644,585, of which the arena is 524,288), and the runtime's device object, 151,740 (the 151,552-byte stage). The
fork-only ColdFire objects of the set are 166,305 bytes; `core/instrument-play` (`play-cf`, 102,837: text 66,682, rodata
8,034, data 80, bss 28,041) and `core/midi-events` (`midi-events-cf`, 34,388: text 12,770, bss 21,618) are 137,225 of
them. The set plus `core/relay-track` and `core/track-keyboard` sums to 1,452,531 bytes, about 59 KB over at the 512 KiB
arena; RELAY and the track keyboard are not in the installed image.

## The edges of the instrument code

From the relocations of the objects (a symbol one object leaves undefined and another defines):

- Into the two objects: `core/dsp-link` (`instrument_write_frame`, `instrument_write_lock_frame`, from `dl_tick`, the block
  interrupt), the runtime (`instrument_play_tick`, `instrument_play_faulted`, `instrument_runtime_policy`,
  `mi_registry_reply`, `mi_registry_expect_session`), `core/digikit-menu` (2), `core/platform` (`mi_channel_message`),
  `core/machines` (`instrument_mod_param`), `core/relay-track` (9) and `core/track-keyboard` (9). All of them are
  functions; no object reads another's data.
- Out of them: `core/ui` (14 SDK functions), the runtime (`dkrt_instrument_type`, `dkrt_instrument_generation`,
  `dkrt_settings_get`, `_put`, `_flush`, `dkrt_irq_mask`, `dkrt_irq_restore`), `core/pattern-position` (2),
  `core/dsp-link` (2), `core/machines` (2), and the optional `core/track-keyboard` (14) and `core/relay-track` (4).
- The firmware: 17 hook sites (15 of `core/instrument-play`, 2 of `core/midi-events`), 11 vtable entries and 2 call
  targets that patchkit writes at build, and about 90 anchors and hook returns.

**The block-interrupt closure.** The sections reachable over the relocations from the two frame writers (assembler
sections split at their symbols): 50,872 bytes, 12,862 of code and 37,946 of bss. The bss is the state the UI task shares:
`mi_ledger` 12,408, `inst` 9,624, `status_words` 6,144, `shared_notes` 5,400 and smaller. The frame writer runs the MIDI
transport and the keyboard mapper (`kb_process`, `mt_prepare_mapped`) under the interrupt mask.

## CANOPY's wavetables

`src/dsp/wavetables.s` equals `modules.machine.digikit.canopy.tools.wavetables.assembly()`. Its 46,360 samples (92,720 bytes) are
five tables of eight frames of seven mip levels with a guard sample. Each of the 280 levels, as quantized, is exactly
antisymmetric: `w[N-n] = -w[n]` for every n, and `w[0]`, `w[N/2]` and `w[N]` are 0. So 22,760 samples (45,520 bytes)
determine the tables exactly. The package was 147,768 bytes in the install record; 131,072 needs 16,696 fewer.

## Built, 2026-11-23

`patchkit apply --set canopy-overloop-prism` at `5936100a1` (WSL, DT2 1.17), all **[D]**:

- The head uses 1,340,300 of 1,396,736 bytes (56,436 free), not the 1,318,652 of the installed image: PRs 10 to 13 grew the
  committed objects of the set by 21,648 bytes (`play-cf` +12,694, `loop-control-cf` +3,514, `phrase-ui-cf` +1,780,
  `midi-events-cf` +1,580, `pattern-position-cf` +754, `fxview-cf` +338, `loop-display-cf` +270, `dsp-link-cf` +208,
  `phrase-control` `control-cf` +98). The static figure above (about where the installed image was) was wrong.
- The set with `core/relay-track` and `core/track-keyboard` does not link: `core/relay-track` (51,536 bytes) is refused with
  1,348,656 used, before the track keyboard is placed.
- With the split of plan section 9 (`feat/instrument-service`, `4b0a1e411`), the set uses 1,214,192 (182,544 free): the image
  parts of the two adapters keep about 11 KB of the 137 KB.

The track keyboard's player in its own service file (plan section 9.2, 2026-11-24): the set with `core/track-keyboard` uses
1,216,624 (180,112 free; 117,396 with the player in the image). `services/track-keyboard.dks` is 81,416 bytes packed with an
arena need of 65,360; the files of the hardware set's card need 469,904 of the 524,288-byte arena.

Two facts that decided what stays in the image (read in the runtime's sources at `5936100a1`, not run):

- The runtime calls `crate::instrument::bind_once()` at the top of its first `dkrt_tick`, which reads
  `instrument_runtime_policy()` once and keeps the admitted instance count for the boot; and `instrument::capacity()`, which the
  boot plan's fit reads before any file loads, returns zeros without the policy. A forwarder that answered 0 there would
  admit no CANOPY instance.
- The 25 weak references of the moved C to `core/track-keyboard`, `core/relay-track` and `core/dsp-link` are tests of a
  function's address; in the service build each reads the word of the image table instead (`IP_HAVE`).
