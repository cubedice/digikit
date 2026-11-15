# CANOPY copy guard experiment

This fixture is not linked to the device. It explores one synchronization
option: mask interrupts, freeze each applicable Sound's copy-on-share snapshot,
call the DTII stock `memcpy` for the complete Sound or kit, then restore the
previous mask. A failed freeze returns before copying. A Sound copy checks one
source; a kit copy checks all 16. Earlier successful freezes remain frozen if a
later check fails, which consumes no new snapshot and preserves data.

The host test requests a MIDI interrupt at every 16-byte copy boundary, then
delivers a selector note through the proof MIDI mapper when the mask is
restored. It checks that the copied Sound keeps the old chord, the source
detaches to the new chord, and the other note releases. This models deferred
interrupts; it does not execute the firmware scheduler or prove behavior on a
device. The ColdFire component test executes the pinned 1.17 stock copy routine
with 1 or 16 pre-created snapshots and enforces an instruction-count ceiling.
Guest instruction count is not hardware time or a complete latency budget.
The component run measured 588 guest instructions for the Sound case and
9,369 for the kit case, including the test wrapper and stock copy. These are
emulator instruction counts, not measured interrupt-off time on the DTII.

The wrapper assumes its callbacks are bounded, nonblocking and allocation-free;
it cannot enforce that contract. Callers must validate RAM, Sound identities,
admission and snapshot consistency; serialize all source edits with the same
mask; ensure no DMA or unmaskable writer can change the region; and ensure a
failed copy is actually propagated to the user. No user notification,
reservation, recovery, code patch, or copy-site discovery is implemented.

Coverage is incomplete. On the DTII 1.17 image, the previously observed stock
copy calls at `0x401aacc6` (Sound) and `0x40047a14` (kit) are not exhaustive.
Many other direct and indirect calls copy the same byte lengths. Some same-size
operations are not Sounds or kits, so a blanket length check cannot establish
source identity. Inline or dynamically sized copies and indirect entry points
also remain unclassified. A production system must enumerate and verify every
relevant path, including clipboard, undo, in-place replacement, project load
and serialization; then measure the maximum masked interval on hardware under
load. Until those conditions are met this fixture is not safe for hardware.

Pinned representative 1.17 instruction windows live in
`tests/patchkit/test_canopy_copy_sites.py`. They include direct and indirect
Sound and kit copies as well as an unrelated same-size memset. Conservatively
freezing any published handle found at an expected offset in an arbitrary
same-size copy would avoid writing that object, but false matches can freeze
unrelated snapshots and eventually exhaust the non-reclaiming pool. It also
would not catch inline, partial, larger aggregate or alternate-entry copies.

`canopy-boundary-capture.test.ts` explores a lower-churn variant using the
immutable snapshot pool and real SRC encoder edits. A dirty admitted Sound has
one pre-reserved handle, and the host harness publishes its snapshot only when
a modeled copy reads the complete handle. An aggregate copy captures both
modeled instances before copying; stale ownership, a split-handle copy and an
active destination overwrite are refused without changing the destination.
The test also covers allocator failure and a simulated deferred edit. It does
not provide a stock-firmware refusal path, serializer or replacement hook,
complete copy coverage, a hardware interrupt-latency bound, or reclamation of
published handles. It is an isolated host experiment, not device integration.

The separate `canopy-boundary-credits` host experiment supplies one
pre-reserved unpublished entry to each of four admitted Sounds. It batches
task-side replenishment, retains unused credits across clean rebinding, and
refuses edits without a credit or unbinding a dirty Sound. The host test covers
real encoder edits, reuse across repeated edits, allocator failure and a failed
batch without partial reservations. Its external serialization contract is
not implemented for the device: slab allocation can block and must not run in
the MIDI interrupt or under an unbounded interrupt mask.

The host-only `canopy-two-phase` experiment stages slab allocations with
interrupts enabled, then verifies the pool and publishes up to four reserved
entries in a short modeled interrupt-masked section. It tests an interrupt
publishing an existing reservation during allocation, re-entry, allocation
failure, and stale staging. Its compiler fences and mocked locks do not prove
ColdFire memory ordering, real task exclusion or hardware interrupt latency;
all task and interrupt users would have to adopt the same verified protocol.
The ColdFire component fixture measured 59 guest instructions for masked
preflight and 114 for the four-entry commit. Its synchronous simulated
interrupt's publication took 1,428 guest instructions while masked. These are
emulator instruction intervals, not physical timings or proof of preemption.

`canopy-credit-refill` combines the two experiments in a host-only model. It
rechecks owners after allocation and retains surplus unpublished reservations
when a Sound changes or unbinds during the allocation. Its test exercises a
simulated MIDI selector and note release during allocation, owner replacement,
spare reuse, allocation failure and retry. The model still requires the same
unproven task and interrupt exclusion as the two-phase experiment.

The opt-in `canopy-worker-refill` fixture also posts a refill to DigiKit's
actual worker in the emulator. With a forced allocator failure followed by a
retry, it observed different UI and worker task identities, no interrupt mask
at allocator entry, and four credits after the retry. This uses a disposable
fixture and does not test the allocator's internals, concurrent physical MIDI,
complete Sound ownership or hardware timing.
