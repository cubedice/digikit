# Pattern position bridge

Experimental DT2 OS 1.17 framework provider for `_pattern_position_info`. ABI version 2 publishes nominal master-step boundaries when the existing timed firmware event is consumed, before the corresponding frame is sent to the DSP. It does not use a fixed scheduler-to-audio delay.

The start and sequencer hooks snapshot a bounded 16-entry marker queue. Type 7 dispatch commits the PLAY boundary; type 5 dispatch commits later step boundaries. Each marker freezes its deadline, generation, scheduling-pattern identity, step and master length. No stock event is allocated or inserted. Missing deadlines, queue overflow and multiple boundaries in one audio block invalidate timing. STOP clears pending timing.

The per-send writer fills the existing 16-word reserved suffix; the early DSP receiver checks packet integrity and sequence before publishing its 20-word record. ColdFire accesses are interrupt-masked. SDK definitions are in `sdk/include/dk/pattern_position.h` and `sdk/asm/dk_pattern_position.inc`. This record is read-only to effects. It describes the nominal master grid, not per-track swing or microtimed notes.

Firmware declarations are in `devices/fw/pattern.toml`; their hashes pin the DT2 1.17 sites. `tests/patchkit/test_pattern_position_cf.py` exercises queue/packet behaviour on the ColdFire core and checks the windows against the pinned firmware. `tests/patchkit/test_pattern_position_transport.py` checks the DSP receiver and context preservation.

The ABI 2 paired running-firmware proof preserved 241,184 stereo frames bit for bit and aligned committed boundaries with tested note events, including first PLAY. The actual overloop effect also captured a whole cycle and a selected-step interval exactly. These emulator results cover a 16-step pattern at 120 BPM; non-default lengths/scales, tempo changes in running firmware, external clock and hardware remain open. See `docs/findings/76-pattern-position.md` for evidence; its earlier ABI 1 results are historical.
