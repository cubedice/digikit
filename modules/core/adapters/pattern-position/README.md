# Pattern position bridge

Experimental DT2 OS 1.17 framework provider for `_pattern_position_info`. ABI version 2 publishes nominal master-step boundaries when the existing timed firmware event is consumed, before the corresponding frame is sent to the DSP. It does not use a fixed scheduler-to-audio delay.

The start and sequencer hooks snapshot a bounded 16-entry marker queue. Type 7 dispatch commits the PLAY boundary; type 5 dispatch commits later step boundaries. Each marker freezes its deadline, generation, scheduling-pattern identity, step and master length. No stock event is allocated or inserted. Missing deadlines, queue overflow and multiple boundaries in one audio block invalidate timing. STOP clears pending timing.

The per-send writer fills the existing 16-word reserved suffix; the early DSP receiver checks packet integrity and sequence before publishing its 20-word record. ColdFire accesses are interrupt-masked.

A refused frame (bad header or check, or a transmit sequence that did not move on or went back) leaves the last good record and clock record in place: on the device a frame comes twice or late when the ColdFire's block interrupt is held off, and a reader then sees the same record again (its transmit sequence unchanged: nothing new this block). Only `PP_RX_GRACE` (750, half a second, as the phrase receiver's `PH_RX_GRACE`) refusals in a row clear the magic and flags of both records; after that any frame with another transmit sequence, also an earlier one (a sender that started again), is taken as a first frame (delta 0). Words 16 to 19 count as before: every block, every refusal, every accepted frame, and the accepted frame's transmit-sequence delta. See [finding 363](../../../../docs/findings/363-loop-link-jitter.md), which also says why the other readers (Overloop, CANOPY through `core/instrument-play`, PRISM's SYNC time) are tolerant. SDK definitions are in `sdk/include/dk/pattern_position.h` and `sdk/dsp/contract/dk_pattern_position.inc`. This record is read-only to effects. It describes the nominal master grid, not per-track swing or microtimed notes.

The same early DSP hook optionally calls `_instrument_block` after processing the
position packet, including rejected packets, and before returning to stock render.
The MIDI-events adapter owns that callback and preserves its scratch context.
Without the callback, the position provider operates alone. Composed component
tests cover valid/invalid position packets, missing instrument consumers and
SIMD/saturation modes; the combined running-firmware proof remains open.

Firmware declarations are in `devices/fw/pattern.toml`; their hashes pin the DT2 1.17 sites. `tests/patchkit/test_pattern_position_cf.py` exercises queue/packet behaviour on the ColdFire core and checks the windows against the pinned firmware. `tests/patchkit/test_pattern_position_transport.py` checks the DSP receiver and context preservation.

The ABI 2 paired running-firmware proof preserved 241,184 stereo frames bit for bit and aligned committed boundaries with tested note events, including first PLAY. The actual overloop effect also captured a whole cycle and a selected-step interval exactly. These emulator results cover a 16-step pattern at 120 BPM; non-default lengths/scales, tempo changes in running firmware, external clock and hardware remain open. See `docs/findings/133-pattern-position.md` for evidence; its earlier ABI 1 results are historical.


## Fine clock capability (wire v3)

The unchanged 16-word wire suffix now carries firmware musical phase in word 9.
The receiver accepts v2 and v3 and normalizes `_pattern_position_info` to ABI 2,
including its reserved word and checksum, so existing Overloop consumers retain
their contract. Static instrument adapters may additionally read the 32-byte
`_pattern_clock_info`: magic PCLK, version 1, phase, boundary deadline, flags,
receive count, boundary sequence and run generation. Invalid frames clear its
magic/flags; v2 frames supply no fine-clock capability. Phase uses 21600000 ticks
per quarter note and wraps modulo 2^32. It is distinct from master-step length
and scale. External MIDI clock may correct phase backward; consumers must not
count corrected time twice. Instrument input V4 forwards this optional pair
without changing the instrument descriptor or reset ABI.

Wire-v3 flag bit 2 marks an expired external instrument clock. The receiver
removes that bit and adjusts the legacy checksum, preserving the public position
record. Only the separate clock flags become zero. Accepted MIDI pulse age
expires after three estimated periods, bounded to 40..500 ms. An external-source
latch prevents stock freewheel/fallback from silently reviving instrument arp;
STOP or clock receive OFF clears it. Pulse return allows fresh observation.

## Selected tempo while stopped (wire v4)

Wire v4 also carries the selected DT2 stock tempo in flag bits 16..31, in units
of 1/120 BPM. The value is sampled from the same interrupt-masked audio frame as
phase and transport state, and is available before PLAY and while stopped.
Only values from 3600 to 36000 (30 to 300 BPM) are published; invalid values
produce no tempo capability. This is the stock pattern/master tempo, not a
separate instrument setting.

The receiver accepts wire v2/v3/v4. It removes the tempo bits and normalizes
the version, reserved word and checksum of the public ABI 2 position record,
preserving existing effects. `_pattern_clock_info` retains its 32-byte size;
version 2 carries tempo in its flags' high 16 bits. Its low flags continue to
describe phase health independently: a lost external clock still clears them.
A run of `PP_RX_GRACE` refused frames clears the record's magic and flags. The instrument adapter
additionally rejects skipped frames, and forwards a validated tempo to V5+
inputs through timing flag bit 11 and bits 16..31. Explicit STOP is distinct
from missing or stale running clock, so CANOPY can play held-key arpeggios while
stopped without silently falling back after external pulse loss.
