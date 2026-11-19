# 361. The loop as PRISM's input, and PRISM's return past Overloop's crossfader

Step 4 of the redesign in [the plan](../plans/prism-overloop-redesign.md): the whole transition of the plan's section 4 is
played on PRISM's page. INPT switches PRISM between the tracks and Overloop's loop, XFAD and ARM are Overloop's on PRISM's
page, and PRISM's output is heard at every position of Overloop's crossfader (the owner's option B of 2026-11-18). Digitakt
II OS 1.17, on the SHARC-only rig, on the host and on the emulator with the SHARC core. Nothing ran on hardware and nobody
has listened. Marks as in `docs/FINDINGS.md`; every result here is **[D]** unless it is marked **[O]** or **[C]**.

## Where the FX return meets the crossfader

Read once from the DT2 1.17 image (the SHARC database, function `FUN_1c207b`, the master function the send hook of
`core/fx` enters, finding 62): the FX return bus `0x254778` is added to the master bus (loop at `0x1c21b6`); then the
dynamics routine `0xb82d41` (the master compressor) runs; the sum is multiplied by `0x404a6320` (3.1623, +10 dB) and
clipped to +-1.0 (`0x1c2228` to `0x1c22cf`); the master overdrive `0x1cb3d8` runs on both sides (amount `0x2526e8`); then
the volume ramp: the volume word `0x2526ec` squared is the new gain, the last block's end gain is kept as this block's
start in `0x2558c0`, the step (new - start) / 32 in `0x2558c8`, and sample k is multiplied by start + k steps, added up
sample by sample. Overloop runs later in the block (the master chain's hook, finding 55). So between PRISM's return and
Overloop's crossfader sit the compressor, the +10 dB clip and the overdrive: PRISM's return cannot be taken out of the
mix Overloop receives. The only other writer of the two ramp words is an initialisation function (`FUN_1c15e3`, reached
from `0x1c147e`), not part of the block.

## Option B as built

- **The wet mailbox** `_loop_wet` (68 words, `LW_*` in `sdk/dsp/contract/dk_loop.inc`, in `core/loop-storage`): PRISM
  writes its head, its count `LW_SEQ` and 64 samples; Overloop writes `LW_TAKEN`, the count of the blocks it finished. One
  writer a word.
- **The handshake.** PRISM, in its block, counts Overloop as running when `LW_TAKEN` moved since PRISM's last block (not in
  PRISM's first block after a start). Then a share `a` of every output sample goes to the mailbox and `1 - a` to its output
  block (the FX return); `a` moves toward 1 by 1/256 a sample (5.3 ms), so the start is a crossfade of the two ways. At
  `a = 1` the output block is 0 and the mailbox the whole output, both exactly. Overloop adds the mailbox when `LW_SEQ`
  moved since its own last block (not in its first block after a start), every sample is finite and at most 8, and the
  magic is right. When Overloop did not finish a block (a fault, a switch-off, an unload), PRISM sees `LW_TAKEN` stand in
  its next block, sets `a` to 0 at once and fades its output block in over that block from `1 - a`: the block that went
  to the mailbox and was not heard is the one block lost (0.67 ms). Nothing is ever added twice: each `LW_SEQ` is taken
  once, and a restarted Overloop only notes the count in its first block.
- **The level.** Overloop adds `m * 3.1623 * g_k`, `g_k` the stock ramp's gain before sample k (start + k steps from the
  two anchored words `dsp.master.value.volume_ramp.start` and `.step`, new in `devices/fw/dsp/master.toml` and the DT2 1.17
  port file, imported by Overloop through `core/dyndsp` as `_fw_master_gain` and `_fw_master_step`; the +10 dB is
  `OL_MASTER_BOOST` of `modules/fx/overloop/src/overloop.h`). It is added after the crossfader and after the record, and
  the sum is held within +-7.9. No code hook was added.
- **What it costs.** With Overloop loaded, PRISM's echoes skip the master compressor, the clip of the master stage and the
  master overdrive, also without a loop (the owner accepted it); the master volume applies. Overloop's heaviest block went
  from 4384 to 5020 instructions; PRISM's from 17381 to 19,291 (a release of HOLD while INPT switches and the output moves, at the extreme controls).

| | Measured |
| --- | --- |
| Overloop starts (rig, both engines coupled, master linear) | largest difference to PRISM on the return 5.5e-8 of the peak |
| Overloop faults (rig) | one block without PRISM, the next block fades in over 32 samples, then the crossfade back (9.1e-8 of the peak) |
| Overloop gone (rig) | one block lost, the fade in, then the FX return word for word |
| Never twice (rig) | no sample of PRISM louder than on the return alone |
| Every crossfader position (rig) | output = the output without the send effect plus `m * 3.1623 * g`, every sample (LIVE, LOOP, 64, LIVE), ramp at 0.8 and a ramp of -1/4096 a sample |
| The record (rig) | the two buffers are the same words with and without PRISM's output |
| The two ways on the emulator | PRISM holding a loop, transport stopped: through Overloop 0.010885 RMS, through the FX return 0.010885 RMS, ratio 1.000000 (a difference of 1.3e-15) |

## The transition

- **INPT** is `core/phrase-control`'s raw value `PH_P_INPT` (0 or 1, not saved), published as `PH_C_INPUT`. The engine
  moves the loop's share `c` of its input toward it by 1/256 a sample: `input = send * (1 - c) + tap * PRISM_LOOP_TRIM * c`
  (LIVE and LOOP each bit for bit, the switch 0.95 and 1.03 x steady on the rig). The loop side is the tap of the block before
  while Overloop runs and the tap's flag says a loop is mixed; else silence, never older memory (rig: a stopped Overloop,
  a tap flagged empty, PRISM's first block).
- **The trim** `PRISM_LOOP_TRIM` (`modules/fx/prism/src/prism.h`, 0x3f004dce, -6 dB) applies to the tap, which is now the
  loop times the crossfader **divided by the master's gain** at the block's end (at most 1024 times): the loop at the level
  of the mix before the master stage, the whole mix at full send. **[C]** to finding 360: the tap was the loop times the
  crossfader at the output's level.
- **INPT to LOOP by itself.** The tap's flags carry Overloop's loop count (`LT_COUNT_SHIFT`). When PRISM sees the count
  move, with a loop mixed, its input is the loop at once from that block (no crossfade) and stays there for
  `PRISM_AUTO_BLOCKS` (750, 0.5 s) while INPT is not yet LOOP; the page (`phrase_ui_tick`, once a UI pass) sets INPT to
  LOOP when the loop count in Overloop's status moves. Waiting for the page would let the new pattern's first notes into
  PRISM for the page's latency; a crossfade would let in part of them. **[O]** PRISM runs before Overloop in a block, so
  in the block of the change it still hears the tracks: the new pattern's first 32 frames reach PRISM's tape (rig: those
  frames are the send bus exactly, from the next block the tap times the trim exactly).
- **INPT to LIVE** happens when the loop is dropped, or when the status shows no loop, or when Overloop has not reported
  for 60 UI passes after the home's own 60. It does not happen when the crossfader reaches LIVE (decision 2a).
- **XFAD, ARM, FUNC + NO** on PRISM's page go through the one home, `core/loop-control` (`<dk/loop.h>`), as Overloop's page
  does; neither page keeps a copy. The home is optional for `core/phrase-ui`: without it the page has no loop.
- **INPT at LIVE while the crossfader stands at LOOP** (the owner's move): PRISM hears the new pattern by its sends and its
  echoes are heard over the loop (rig: pattern B 0.120 beside the loop's A 0.215). The finding and both READMEs
  say so.
- **The fresh engine** left open in finding 360 is closed: a new Overloop engine (a load, a fault, a lease that moved) goes
  on counting loops from the count in its status mailbox, so its next loop has a count no reader has seen; the home, the
  page and PRISM all key on the count.
- **Feedback:** the loop never holds PRISM's output (its record is taken before the add), and the held loop's buffer is
  never written (rig: the buffer's words are unchanged from the capture to the end of the transition).

## The page

Section 2.1 and section 4 of the plan, built in `core/phrase-ui`. Where it differs from the drawings:

- H's cell shows `ARMD` and the steps the next change keeps (`16 ST`; `CUT` when the buffers hold less, `WAIT` before a whole
  step was heard), not `KEEPS A02`: the engine knows a pattern only by a number.
- With a loop, H shows `LIVE`, a number or `LOOP`, and under it a line with the crossfader's place as it sounds; armed again
  while a loop plays, `ARMD` inverted in place of the line.
- The header's loop line is `LOOP 16 ST 2.00 S` (no pattern name), shown while PRISM hears the loop, in place of the zone and
  the time; the pattern cell at the left of the header is not there.
- G and H turn only while there is a loop; a press of G switches too.

## Proofs

| Layer | What | Result |
| --- | --- | --- |
| Rig, PRISM | INPT, the auto switch, the route, the tap absent, empty or stale | pass. LIVE and LOOP bit for bit, the switch 0.95 and 1.03 x steady; a new loop: the loop at once, bit for bit, LIVE again after 730 blocks without INPT; output block + mailbox = the output (1.7e-8 of the peak), the fade back exact; heaviest block 19,291 |
| Rig, Overloop | the wet mailbox at every crossfader position, the ramp, the bound, stale and bad blocks, its count | pass (26 cases); heaviest block 5,020 |
| Rig, both coupled | the handover, the transition, INPT at LIVE over the loop | pass. The handover numbers above; at the change the output is that of a run without a change word for word up to PRISM's first echo of the loop (PRISM's echo there 0.159 RMS); from the block after it PRISM takes the tap times the trim bit for bit, pattern B in its tape -185 dB below A; at LIVE the feed is 0 and B alone and dry (PRISM's DC blocker tail 3.9e-4 of the peak); INPT LIVE: B in PRISM's tape |
| Host | the page model with a fake home (G and H only with a loop, INPT at a new count and at a drop, the texts), the home with two pages, the provider's INPT | pass |
| Assembly | the DSP imports of PRISM and Overloop against the sets they load in (`phrase`, `overloop`, `canopy-overloop-prism`); the dyndsp table's count word | pass |
| Device | DT2 emulator with the SHARC core, `digikit/prism:test-device--transition`, test instrument on track 1 (pattern 1: four notes, pattern 2: eight), PRS amount full, PRISM SLAP 60 ms, RPTS 64. (1) LIVE: PRISM route 1 (Overloop takes its output), its output peaks at 0.0236 in the mailbox. (2) H: `ARMD 16 ST`; PTN + TRIG 2: loop 1 of 96,000 frames, INPT 1 by itself, XFAD 127, PRISM input 1 and fed; from the choice of pattern 2 to 60 ms after the change every sample of the capture equals the one a cycle earlier (largest difference 0). (3) MIX to 64: the output differs from a cycle earlier by 0.184 of the peak; track 1's PRS amount to 0 and back to full while PRISM hears the loop: the output repeats with the loop's cycle (1.9e-7 of the peak). (4) H to 64 on PRISM's page: `XFAD 64` on Overloop's page too; ARM pressed on Overloop's page shows `ARMD` on PRISM's, a second press takes it back; H to LIVE: INPT stays LOOP, PRISM's output 8.9e-13 after 3 s. (5) G: INPT 0, PRISM echoes pattern 2 (0.0135). An unarmed pattern change: loop, INPT and XFAD unchanged. (6) FUNC + NO: no loop, INPT LIVE, `-- NO LOOP`. Then the level of the two ways: pass. Also green: Overloop's device test, PRISM's ui, send and coexistence tests (stallMs 1500), the conformance of both, the rig conformance | |

**[C]** The rig kit read and wrote external memory at the wrong address (`peek`/`poke` divide every address by 4, which
is right for internal memory only): the tape and buffer reads of the PRISM and Overloop rig tests read a constant, and the
NaN poison of the buffers never reached them. `FxRig.peekExternal` and `pokeExternal` are new; both rigs use them. The
earlier claims that rested on those reads (finding 358's tape comparisons and finding 360's poison) hold again with the
real reads (the suites pass).

**[C]** Step 2 left the count word of the dyndsp import table at 65 with 64 entries (it removed `_phrase_route_info`); the
runtime read one entry past the table. The count is now 67 with 67 entries, and an assembly case holds the count to the
entries.

## Sizes and the image

The fork's hardware set `canopy-overloop-prism`, built from the stock image (apply, preflight, verify; `patchkit flash --dry-run` sends nothing): `cf.expansion` 1,316,464 of 1,396,736 bytes (1,616 more than after step 3), `dsp.l2` 566,332 of 922,756 (272 more: the wet mailbox). **Image id `5da92c3eb7d9dfac`** (the flash checks: verify, round trip and the id against the MAIN OS and DSP sections ok). The set `overloop` alone: 1,178,008 and 532,992. Packages: `overloop.dkm` 5,676 bytes, `prism.dkm` 9,946, `canopy.dkm` 147,368, `remote.dkm` 2,520; the fx and machines services bind.

## Open

- **[O]** Hardware: nothing ran on a device.
- **[O]** The new pattern's first block reaches PRISM at the change (above).
- **[O]** PRISM switched off while Overloop adds its output: that output stops without the framework's fade.
- **[O]** The trim (-6 dB) is a starting value, to be set by ear.
