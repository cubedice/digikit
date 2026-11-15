# Public-input transaction experiment

Disposable DTII 1.17 emulator only; never install this fixture on hardware.
Set `DIGI_CANOPY_LIVE_PROOF=1` when running the named device test
`modules/machines/canopy/tests/live-transaction-proof.device.test.ts` with the
existing regression Vitest configuration and DTII runtime inputs.

The test selects CANOPY on two tracks, then registers the proof-only public
encoder/MIDI transactions. Its own immutable `SpPool` and owner table hold
snapshots. No new Sound fields or card records are written by this fixture.
The existing experimental `canopy-sound-proof` dependency supplies its UI tick
trampoline and shared codec/pool symbols, so this retains that fixture's unsafe
experimental hooks and must remain explicitly opt-in.

`CreditRefill` and `tp_reserve_batch` are reused unchanged. The UI posts refill
jobs with `dkrt_job_post`; allocation runs on the worker with interrupts enabled.
Prepare/attach only publish already-reserved entries under mapper exclusion.
The test first injects allocation failure, retries, then checks panel encoder
edits and public mapper batches while the other track owns the UI. Exhausted
credits refuse changes while mapped notes and their matching releases continue.
Refill permits the same edits, and every previous published snapshot is checked
for immutability. Unregistering ends the experiment.

The mapper batches call `instrument_keyboard_events`; they do not enter through
USB/DIN routing or reach the DSP. This proves neither physical MIDI/audio nor
complete Sound lifetime, storage, save/copy integration, resource recovery,
critical-section latency, or hardware safety. There is no automatic refill in
this fixture: the test commands it deliberately to make exhaustion observable.
