# Pattern position bridge

Experimental DT2 OS 1.17 framework provider for `_pattern_position_info`. ABI version 2 publishes nominal master-step boundaries when the existing timed firmware event is consumed, before the corresponding frame is sent to the DSP. It does not use a fixed scheduler-to-audio delay.

The start and sequencer hooks snapshot a bounded 16-entry marker queue. Type 7 dispatch commits the PLAY boundary; type 5 dispatch commits later step boundaries. Each marker freezes its deadline, generation, scheduling-pattern identity, step and master length. No stock event is allocated or inserted. Missing deadlines, queue overflow and multiple boundaries in one audio block invalidate timing. STOP clears pending timing.

The per-send writer fills the existing 16-word reserved suffix; the early DSP receiver checks packet integrity and sequence before publishing its 20-word record. ColdFire accesses are interrupt-masked. SDK definitions are in `sdk/include/dk/pattern_position.h` and `sdk/asm/dk_pattern_position.inc`. This record is read-only to effects. It describes the nominal master grid, not per-track swing or microtimed notes.

The same early DSP hook optionally calls `_instrument_block` after processing the
position packet, including rejected packets, and before returning to stock render.
The MIDI-events adapter owns that callback and preserves its scratch context.
Without the callback, the position provider operates alone. Composed component
tests cover valid/invalid position packets, missing instrument consumers and
SIMD/saturation modes; the combined running-firmware proof remains open.

Firmware declarations are in `devices/fw/pattern.toml`; their hashes pin the DT2 1.17 sites. `tests/patchkit/test_pattern_position_cf.py` exercises queue/packet behaviour on the ColdFire core and checks the windows against the pinned firmware. `tests/patchkit/test_pattern_position_transport.py` checks the DSP receiver and context preservation.

The ABI 2 paired running-firmware proof preserved 241,184 stereo frames bit for bit and aligned committed boundaries with tested note events, including first PLAY. The actual overloop effect also captured a whole cycle and a selected-step interval exactly. These emulator results cover a 16-step pattern at 120 BPM; non-default lengths/scales, tempo changes in running firmware, external clock and hardware remain open. See `docs/findings/76-pattern-position.md` for evidence; its earlier ABI 1 results are historical.


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
