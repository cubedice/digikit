# Pattern-position transport

Experimental DT2 OS 1.17 timing transport. This module publishes a snapshot to the DSP before the stock block handler
dispatches its command. It does not itself implement selected-step capture or change the loop effect.

The ColdFire hooks are `pp.cf.start` at `0x400d66d8` (`pp_start_hook`) and `pp.cf.tick` at `0x400d8dca`
(`pp_seq_hook`). Their named anchors use unique instruction-window hashes, and the port pins the complete firmware.
Both sites are inside interrupt-masked regions. Each hook replays the stock countdown store and takes a bounded
snapshot before returning to the stock SR restoration. The start hook also increments the successful start/resume
generation. The event sequence counts both hooks' snapshots, including substeps; it is not a musical-step counter.

`core/dsp-link` calls the optional `pp_write_frame(tx_queue, suffix_entry)` from its existing audio-send interrupt hook,
including while its acknowledged code channel is idle. The writer owns exactly sixteen 32-bit words in receive-frame
bytes `[0xa7c, 0xabc)`. The code channel retains bytes `[0x804, 0xa7c)`: 158 words, of which 155 are payload. Both its sender
and receiver reduce their bound when the provider is present. Without the provider, the original capacity remains 171
payload words. DN2 has no position export or reservation.

The writer receives a PUSHR queue address and a **halfword entry index**, not a DSP address. Entry `e` has its data halfword
at `tx_queue + 4*e + 2`; each DSP word is sent low half first. The callback must write only those 32 data halves, must not
alter command halves, and must return without allocation, waiting, or UI work. The core checks the exact DT2 geometry
before calling it. The writer briefly masks interrupts while refreshing transport state, computing the checksum, and
copying the snapshot, so these operations cannot mix two snapshot generations.

The DSP hook is at stock handler `0x1c7501`, located by the existing handler's relocation-insensitive hash plus `0x34`.
At `0x1c74ff`, the handler has loaded its current receive-ring pointer into I3. The displaced instruction loads I4 with
the command table; dispatch at `0x1c7521` and audio rendering follow. The receiver saves its touched R/DAG registers,
MODE1 and arithmetic status. Its fixed unrolled decoder has no calls or hardware loops. Eight-byte save slots also
cover the paired S-register transfers when PEYEN was set.

The direct optional runtime import `_pattern_position_info` points to an 80-byte DSP record. A missing provider resolves
the import to zero and the runtime refuses modules that require it. There is no fallback address. The C record layout is
in [dk_pattern_position.h](../../../sdk/dk_pattern_position.h); SHARC word offsets for `DM(n, Ix)` are in
[dk_pattern_position.inc](../../../sdk/dk_pattern_position.inc).

| Word | Meaning |
| --- | --- |
| 0 | Magic `0x50504f53` (`PPOS`); zero after a rejected frame |
| 1 | Version 1 |
| 2 | CF transmit sequence, wrapping unsigned 32-bit |
| 3 | CF snapshot event sequence, including substeps and the start/resume snapshot |
| 4 | Successful start/resume generation; increments at `pp.cf.start`, not on every transport or pattern change |
| 5 | Flags: bit 0 valid snapshot, bit 1 running |
| 6 | Opaque scheduled-pattern pointer; may look ahead of the currently audible pattern |
| 7 | Native master-step value reported by the firmware's position getter |
| 8 | Effective master-counter wrap length; zero means unknown or special |
| 9 | Native substep |
| 10 | CF audio-block counter when the snapshot was taken |
| 11 | Raw 32-bit scheduler clock, in the firmware's tick domain rather than samples |
| 12 | CF audio-block counter when the snapshot was transmitted |
| 13 | Native next-step value used by scheduling ahead |
| 14 | Raw transport state: 0 reset, 1 running, 2 paused |
| 15 | XOR of words 0–14 with salt `0x7b91d6e3` |
| 16 | DSP receiver invocations, including rejected frames |
| 17 | Rejected frames |
| 18 | Accepted frames |
| 19 | Unsigned transmit-sequence delta; zero for the first accepted frame |

Magic and version validate the transport format, not sequencer semantics. Clients must also require the valid flag.
Bad magic, version, checksum, or a repeated/backward transmit sequence increments the rejection counter and clears magic and
flags. The last accepted transmit sequence remains for the next comparison. Gaps are visible through word 19; counter
wrap is intentional; a forward gap must be less than half the 32-bit range. Accepted wire words are copied with magic
published last. The record is initialized to zero.

Word 8 follows the firmware's signed-length selection: use the master override when it is greater than 1; otherwise
use the scheduled pattern's fallback when that is greater than 1. Other values are reported as zero. It describes the
master counter's effective wrap length, not an individual track length or a universally meaningful bar length. Word 6
is an address used by the scheduler, not a bank/pattern number. A pointer change alone does not establish that its
pattern is already audible.

The writer reads transport state on every audio-send frame. Reset or pause clears both valid/running flags even if
the sequencer no longer takes snapshots. While transport is running, valid means that the latest snapshot passed its
source checks; it does not guarantee that the sequencer has advanced recently. There is no built-in snapshot-age
timeout. With a stalled external clock, transmit and DSP receive counters can keep advancing while event sequence,
master step, and snapshot block remain unchanged. Consumers must inspect event sequence and unsigned
`transmit_block - snapshot_block`, using an age policy appropriate to their clock and tempo, before acting on a step.

CF transmit sequence, DSP receive count, and `dk_fx_in.block` are distinct counter domains. The FX counter advances only
while an effect row exists. Their numerical equality is not a synchronization contract. A same-frame observer and
audio comparison must establish the source event's association with rendering before quantized recording uses it.

Receiver execution checks are in `tests/patchkit/test_pattern_position_transport.py`; code-channel suffix exclusion
and idle delivery checks are in `tests/patchkit/test_dsplink.py`. These are separate from a running-sequencer firmware
proof and from instrument validation.

The first running-sequencer comparison passes with unchanged PCM and exact same-frame DSP mirrors, but shows the
current-step edge **32 audio blocks before the tested note-ons** (21.333 ms at 48 kHz). This timing is specific to that
internal-clock test, not a fixed compensation rule. See [finding 68](../../../docs/findings/68-pattern-position.md) for
the measurements and remaining tests. An effect must not start recording merely because the current-step field changes.
