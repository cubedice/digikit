# 363. Frames that come twice or late, and the readers that tolerate them

What the owner found on the Digitakt II with image `d0e4825dd04d8646` (branch `feat/prism-track-select`): ARM did not seem
to work from Overloop's page; Overloop's playback display skipped around; the loop did not seem to be kept at a pattern
change; and PRISM's INPT could not be switched to the loop. Digitakt II OS 1.17, on the host, on the SHARC-only rig and on
the emulator with the SHARC core. Nothing here ran on hardware and nobody has listened. Marks as in `docs/FINDINGS.md`;
every result here is **[D]** (measured or read once in this work) unless it is marked **[O]**.

## What the code did with a repeated or late frame

On the device the frame between the ColdFire and the DSP comes twice or late at times: PRISM's engine faulted on each
refused control frame until the guard switched it off, and 65f7aa4a0 made the phrase receiver keep the last good mailbox
(`PH_RX_GRACE`, 750 refusals), which worked on the owner's device. Read in the code of the branch head:

- **[D]** `core/pattern-position`'s DSP receiver (`src/dsp/pattern_position.s`) refused a frame whose transmit sequence
  did not move on or went back, and on every refusal cleared the record's magic and flags (and the clock record's).
- **[D]** Overloop's engine (`_ol_track` in `overloop.s`) ended its run (`OL_RUN = 0`, `OL_NEXT = 0`) whenever the send
  count of the record had not moved by exactly 1, and a missing magic as lost (`OL_HAVE = 0`, the next record a first one).
- So with the old receiver a frame that came twice was, for Overloop, a record without magic (lost), and the next one a
  first record: two blocks without an edge and the run gone. Every repeat or skip threw away the cycle: `OL_NEXT` (what an
  armed change keeps) went to 0, a cycle had to be heard whole again before an armed change could take anything, and the
  page's step mark and `CYCLE` line went back to nothing each time. That matches "the loop is not kept at a pattern change"
  and a display that skips. **[O]** The rate of repeats on the device is not measured: the emulator locks the frames one
  a block, so no device test sees them.
- **[D]** The second cause of a skipping display: `loop_ui_tick` read the loop status from the reply cells once a UI pass,
  but `core/phrase-control`'s reply has the same cells in three blocks of four while PRISM runs (finding 340's carrier is
  shared by magic). The pass saw Overloop's status only when it met the fourth block, at irregular times. No stock write of
  those cells is involved: the sharing is by our own two producers. Finding 340's open point (every stock CF request for
  parameters 36 to 39) stays open and is not needed to explain this.
- **[D]** ARM: Overloop's page drew `ARM OFF` centred at x 32, above encoder E, while ARM was the press of H. A remote press
  of H armed on the device, so control and status worked; label and control were on different encoders.
- **[D]** PRISM's INPT switched only while there was a loop (a short press or a turn of G), and went back to LIVE in every
  UI pass without one: it could not be set beforehand.

## A. The tolerant receiver and engine

**The receiver.** A refused frame (bad header or check, a sequence that did not move on or went back) keeps the last good
record and clock record; words 16 to 19 count as before (every block, every refusal, every accepted frame, the accepted
frame's delta). Only `PP_RX_GRACE` = 750 refusals in a row (half a second, as `PH_RX_GRACE`) clear the magic and flags of
both records, once; after that any frame with another sequence, also an earlier one (a sender that started again), is a
first frame (delta 0), and the same frame again stays refused. A reader that sees the same record again sees the same send
count: nothing new in this block.

**Overloop's engine** (`OL_SKIP` = 4, `OL_SAME` the records again in a row):

| Record | Before | Now |
| --- | --- | --- |
| the same send count again | the run ended | no news: the run goes on, nothing is placed |
| `OL_SKIP` records again in a row | | the run ends |
| send count up by 2 to 4, at most 4 blocks after the last new record | the run ended | the run goes on; a boundary in it begins with this block |
| send count up by more than 4, or after more than 4 blocks | the run ended | the run ends; this record places no edge |
| send count went back | the run ended | the run ends |
| boundary count up by 2 or more | the run ended | the run ends |
| no magic, stopped, invalid length or step | as today | as today |

**The loop's length error.** A boundary begins with the block in which the engine sees the first record that counts it.
Each frame's notes and its position travel together, so as long as the frame that carries a boundary arrives, the boundary
is placed with its notes. When that frame never arrives and the next one carries the count after a gap of d frames, the
boundary is placed up to d - 1 blocks late (or one block early when a frame was overwritten by the next one before the
DSP read it). The loop's length is the distance of two placements, the cycle's first step and the change: it is exact when
no gap touches either of them, one block (32 frames, 0.67 ms) off when a single missing frame held one of them, and at
most `OL_SKIP` - 1 = 3 blocks off for the largest gap that is still followed. A gap that touches other steps changes
nothing of the length.

## Why the other readers need nothing more

- **CANOPY, through `core/instrument-play`** (`_iv_position`): it reads the record's magic, version, word 19 (the delta)
  and the clock record. With the old receiver a refused frame was one block without a position; now it is the same record
  again, with the same boundary count and phase and the receive count (the block clock, word 16) moving. CANOPY's arp in
  tempo mode, the current provider's (wire v4), runs on its own audio-clock accumulator and takes only the BPM ("provider
  sequence, pattern phase and transport flags never determine elapsed time"); the same BPM again is no change. Its legacy
  phase path sees a phase that did not move and catches up with the next record, as it did after a missing block, bounded
  by its own discontinuity checks. A record after a gap (delta 2 or more) still gives tempo only for that block, as before.
  No change was made; CANOPY's device tests (`A:core/instrument-play`) are in the gate below. **[O]** Whether the same
  record twice is audible in CANOPY on the device is not measured.
- **PRISM's SYNC time** comes from `core/phrase-control` on the ColdFire, which reads the tempo from the position suffix in
  the frame it is about to send (`frame_words`), not from the DSP record; the phrase receiver keeps the last good mailbox.
  It needs nothing.
- `_pattern_clock_info` has no other reader.

## B. ARM on encoder E

Overloop's page (`core/loop-display`): a press of E arms or takes the arm back; E turned to the right arms, to the left
disarms. The press of H arms too, as on PRISM's page; H and LEVEL stay the crossfader. ARM is a counter in the home (each
change toggles the engine), so a turn of several ticks must move it once: the page counts the state it asked for until the
status shows it (`ld_observe`, each time it reads the status), the status is not fresh, or 750 blocks of the heartbeat
pass. The bottom row is laid out as PRISM's cells: `ARM` above E (x 32) with `OFF` or `ON` (inverted) under it, `XFAD`
above H (x 110) with its value and the crossfader as it sounds under it; x 124 to 127 below the header stay the FX view's.
The timeline moved up 3 pixels; the wide crossfader drawing of the old page is the H cell's.

## C. Overloop's status in the block interrupt

`core/dsp-link`'s `dl_tick` calls a new weak hook `loop_reply_frame(rec)` of `core/loop-control` once a block, at its
start, with the reply copy, as it calls `phrase_write_frame`. The hook reads the fourteen carrier cells and keeps the
newest `OVL2` carrier (magic, packed word 7 zero) and a count; `loop_ui_tick` copies it with the interrupt masked for that
bounded copy and takes it when the count moved. The hook only reads: what every other adapter gets from `dl_tick` is
unchanged, and on a device without `core/loop-control` (DN2) the call is a pointer test. `loop_reply_literal` is no longer
needed.

## D. INPT, a switch turned by G

PRISM's page: G turned to the right is LOOP and to the left LIVE, at any time; without a loop PRISM hears silence (the
engine's loop side is silence while there is no loop, unchanged) and the INPT cell says `SILENT`. The press of G switches
nothing; holding G with the TRIG keys stays the track selection. INPT goes to LIVE by itself only when a loop goes away (a
fresh status with a loop, then one without: a drop, or a loop lost with a moved lease) or when Overloop, having reported,
stops reporting for 60 passes after the home's own 60 (once, so that G can set LOOP again); it goes to LOOP by itself at an
armed change that takes a loop, as before.

## Proofs

Lowest layer first, at the branch head in the WSL checkout; device tests on the Digitakt II 1.17 image on the emulator with
the SHARC core.

| Layer | Check | Result |
| --- | --- | --- |
| Probe, SHARC | `tests/patchkit/test_pattern_position_transport.py` runs the assembled receiver: a bad header or check, a repeated and a backward frame keep the record and the clock record, counters 16 and 17 count, the run of refusals counts; the 749th refusal keeps, the 750th clears magic and flags of both records once (and sets the after-grace state), more refusals change only the counters; after the grace an earlier sequence is a first frame (delta 0) and the refusals start again from 0; a good frame after refusals is taken with its gap in word 19; context, wrap and the v3/v4 normalization as before | 43 passed |
| Rig | `digikit/overloop:test-rig`, 36 cases, the 26 before unchanged. Jitter over 720 blocks (a record again every 37 blocks, one a block early every 53, a burst of three repeats): 33 records seen again, 31 gaps (largest 4), steps seen late or early at blocks 170, 260, 300 (3 late) and 540; an armed change keeps the last cycle exactly, 2560 frames, and five turns equal the cycle before sample for sample. A missing frame at the cycle's first step: 2528 frames; at the change: 2592; the change a block early: 2528. A send count up by 5, a send count that went back, two boundaries in one record, 4 repeats in a row at the change: the run ends, the change takes nothing, the arm waits, and the run follows the next pattern; up by 4, or 3 repeats: the change keeps the cycle exactly. Heaviest block 5028 instructions (`dsp_cost_fx`, 8 more) | pass |
| Host, C | `core/loop-control:test-host`: the hook keeps the newest `OVL2` carrier between UI passes, the phrase reply in the same cells, a carrier with packed bits or the old magic and no reply copy are refused, a pass without a new reply takes nothing, the mask is restored after each copy, staleness after 60 passes. `core/loop-display:test-host`: E's press toggles, E right arms and left disarms once however many ticks before the engine shows it, a turn of 0 does nothing, H's press arms, an arm used up by a change is armed again by a turn, what the page asked for lapses after 750 blocks, without a fresh status every press counts; the texts; ARM's label and state centred at x 32 (E) and XFAD's at x 110 (H), nothing below the header at x 124 to 127. `core/phrase-ui:test-host`: G right LOOP and left LIVE with and without a loop, no switch on a short or a long press, with a TRIG key or after a turn while held, INPT staying at LOOP without a loop over 180 passes, LIVE once at a drop and once when Overloop stops reporting, LOOP at an armed change, `SILENT` | pass |
| Host, Python | `test_dsplink.py` and `test_instrument_registry.py` link `dsp-link.c`: the new weak symbol is named in their links; `test_midi_events.py` (the composed frame with the receiver) | 178 passed, 5 skipped |
| Device | Overloop `transition`: ARM with the press of E (`ARM ON` inverted), the armed change keeps the 2 s cycle (96000 frames), the output across the change and the loop equals one cycle earlier (largest difference 0), at LIVE pattern 2; E turned right arms and further keeps it armed, turned left disarms and further keeps it off; H's press arms; the cut loop as before | pass |
| Device | PRISM `ui` (G right: INPT LOOP, `PH_C_INPUT` 1, the engine's input 1, after 1.5 s still LOOP, `SILENT`; its press switches nothing; G left LIVE), `transition` (G turned left gives pattern 2 to PRISM, its press switches nothing; G right while the loop plays, FUNC + NO: INPT LIVE by itself; G right without a loop stays LOOP for 1.5 s with `SILENT`; through the change 0, sends at LOOP 1.5e-7), `coexistence` (`stallMs` 1500, CANOPY, ARM with H on Overloop's page) | pass |
| Static | lint, fmt-check, rust-fmt, typecheck, python-checks; `check:layers`; `patchkit:objects-check`; `patchkit gen --check`; device-literals; `core/runtime:test-assembly`; `test-conformance` of PRISM and Overloop; `@digi/sharc-rig-kit:test-conformance-rig` (189) | pass |
| Python | `py_select run --base 43c9e23c2` | all selected files pass but `test_pk_current.py` (`test_ports_are_current`), the known failure |
| Gate | `mise run test:subset -- A:core/phrase-ui A:core/loop-display A:core/dynfx A:core/fxpages A:core/fx A:core/instrument-play C:transport --run`: 52 device test halves on DT2 and DN2 (PRISM's four and Overloop's transition; the 13 files of `core/fxpages`, `core/dynfx` and `core/fx`; CANOPY's seven on DT2; the transport files: `transfer-live`, `upload`, `upload-depth`, `upload-faults`, `platform-contract`, `service-commands`, `usb-bulk`, `usb-bulk-audio`, `usb-fast`), `gate-smoke`, `gate-pins` | 128 of 129 tasks passed. PRISM's `send` timed out at Vitest's 600 s wall limit after 656 s while it was still going on (eleven emulators at once; alone it took 332 s before this change); run alone at the same head it passed in 399 s |
| Hardware set | `hw_build10.sh`: `patchkit apply --set canopy-overloop-prism`, `preflight`, `verify`, `pack-all`, `services --bind-dir` | every step exit 0; nothing installed. **Image id `8278188469b2dc9f`**, 2,143,776 bytes; `cf.expansion` 1,318,396 of 1,396,736 bytes, `dsp.l2` 566,460 of 922,756; `overloop.dkm` 5,804 bytes, `prism.dkm` 9,994 (the engine unchanged); the packages, services and image are the ones the device tests loaded |

The gate skips every device test without these tags: the runtime's boot, storage, loading, layout and fault tests, the LFO,
sample and machine tests, the tools. Not run: `mise run test:full`, anything on hardware.

**No device case under jitter.** A test-only switch in `dl_tick` (skip a call, or write the position twice) would delay or
skip only our tail of the frame while the stock part (the notes) stays on time, which is not what a late block interrupt
does on the device (the whole frame is late), and it would put test code into the block interrupt of every image. The
engine's tolerance is proven on the rig, the receiver's in the probe, and the device tests show nothing changed when frames
come one a block.

## Open

- **[O]** Hardware: nothing of this ran on the device. Whether the frames on the device come as the rig's jitter models
  them (a repeat, then a gap of two; bursts) is not measured.
- **[O]** The 750-block settle of the page's ARM and the `OL_SKIP` of 4 are first values.
