# Overloop status and immediate FX diagnostics

DT2 OS 1.17, 2026-11-08. **[D]** Static diagnosis, component regression and full
running-firmware emulator coexistence passed. No hardware installation.

The CANOPY wavetable coexistence run reached four-note audio and the Overloop
page, but LENGTH never appeared in the DSP-derived UI snapshot. Forty real
encoder turns changed the kit value to 76 while the display showed `NO STATUS`.
The instrument guard remained clear. The failure occurred before REC was armed.

## Conflicting ownership [D]

`core/dsp-link/src/dsp/loop-status.inc` formerly copied the 16-word Overloop
mailbox to spare words in reply rows 13..20. Its magic occupied record word 297.
The FX diagnostic table already owns rows 1..16: its word 24, the immediate FX
breadcrumb, also occupies record word 297. `core/fx/src/dsp/fx.s` `_fx_mark`
writes that breadcrumb into **both** reply rings immediately before and after
each effect call. A tail rewrite therefore cannot make the old allocation
safe: the next effect call can invalidate the previous ring's Overloop magic
before the ColdFire reads it. The CF reader rejects the record and eventually
marks the retained snapshot stale.

The expanded CANOPY control schema did not expand its physical reply carrier.
Instrument replies still occupy exactly 16 words in rows 21..28. The collision
predates that expansion; a changed execution schedule can expose it.

## Corrected carrier [D]

Keep the immediate FX diagnostics and their wire format unchanged. The optional
Overloop carrier uses 14 words from the same established spare cells, offsets
`+0x44/+0x48` within each slot row:

| Consumer | Reply rows |
| --- | --- |
| Transfer ACK | 0 |
| Diagnostics, including immediate FX breadcrumbs | 1..16 |
| Overloop OVL2 | 17..20 and 29..31 |
| Instrument replies | 21..28 |

Overloop's module mailbox and public CF snapshot remain 16-word `OVL1` records.
The carrier uses magic `OVL2`. Its word 7 packs logical capture START, current
START and current LENGTH (logical words 7/14/15) into bytes 0/1/2 respectively.
Each value is bounded to 0..127. The producer refuses out-of-range values; the
CF reader rejects reserved bits and reconstructs the original logical record.
Logical words 0..13 otherwise retain their positions. This requires a matching
firmware producer/consumer update, but no new Overloop module ABI or package.

The final carrier word spans record bytes `0xa90..0xa93`; the stock trailer
starts at `0xa9c`. An independent agent checked the geometry, module writer
census, and DT2 1.17 frame-call bytes at `0x4002dd60..0x4002dd74`, including the
full `0xabc` reply-copy argument. This uses the existing spare-entry evidence in
[finding 31](31-dsp-link.md); that finding's open question about every possible
stock CF request for parameters 36..39 remains open. No new claim of exhaustive
stock-reader exclusion is made.

An independent audit of the rebuilt image
`78a6cc77597dd5f60c065fdd41ed310288f971115b4d5bd70044d3a55d7a041b`
confirmed all 212 inline plan writes and 640 relocations across DSP-link,
loop-display and instrument-play. Their source digests, placed image hashes,
symbols and non-relocation bytes agree with the current objects. The decoded
DSP instructions contain OVL2 magic and all 14 new carrier cells. Instrument-play
is unchanged from the prior stack-fixed image outside relocation operands;
the duplicate lookup uses I0 without borrowing I7.

## Focused regression [D]

`tests/patchkit/test_loop_display.py` executes the real SHARC carrier routine,
then the real FX adapter's call/return markers with both reply rings populated.
It requires both immediate diagnostic breadcrumbs and the whole loop carrier
to survive. Other checks preserve every non-carrier byte, exercise absent and
invalid providers, run the produced bytes through the CF reader, verify byte
order and field limits, and reject reserved packed bits. Existing FX diagnostic
and instrument-carrier tests remain required. The focused carrier/CF-reader
suite and the existing FX and instrument preservation cases passed all 25
checks: 24 in `out/canopy-wavetable-loop-carrier-tests.log`, then the remaining
collision case in `out/canopy-wavetable-loop-fixture-retest.log` after correcting
its fixture allocation. The original fixture put its first reply ring over the
FX adapter's initialized data; both rings now follow that allocation and remain
inside the fixture's RAM bounds. No production assertion was removed.
The full Stage 6 coexistence run passed on this image in 1,771 seconds:
`out/canopy-wavetable-replacement-coexist.log`. It recorded 56,448 stereo frames
while four CANOPY wavetable notes and arp sounded, verified a concurrent 32-KiB
transfer, edited START/LENGTH, crossed through half/full fades, changed tables,
and proved actual pad replacement while both shared tails and arp were active.
The loop continued after all live notes released. Final diagnostics reported no
instrument guard fault, DSP trap or missing audio block; the Overloop snapshot
was fresh (age zero, 3,134 updates). This is functional emulator evidence, not
physical timing or hardware-capacity qualification.
