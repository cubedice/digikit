# PRISM phrase FX: selective routing and shared storage

Target: Digitakt II OS 1.17. PRISM implements the approved proposal in
`docs/design/prism-granular-fx.md`. This finding records measured
software boundaries separately from outstanding coupled and physical checks.

## Selected-source routing

**[D]** Native DTII rendering reaches the sends driver at SHARC `0x1c18a6`
after the sixteen stereo track buffers have been rendered. Each track occupies
256 bytes starting at `0x252df8`: 32 left float samples followed by 32 right.
The sends driver reads track zero through the literal at `0x1c1906`, and the
other fifteen through the base addition at `0x1c19d0`. The driver accumulates
three native send input buses at `0x253e78`, `0x254178`, and `0x254478`.

**[D]** The driver receives R8 pointing to two raw external-channel pointers.
It copies R8 into I12 at `0x1c18f0`; the monitor/send pan and gain path reads
those pointers at `0x1c1a44` and `0x1c1a49`. Its alternate monitor mode also
uses the same pair, at `0x1c1f90` and `0x1c1f99`. Native processing derives the
external monitor buses at `0x254b78` and `0x254c78` and their native send feeds
from those inputs. Thus raw capture can precede native monitor gain without
changing the saved monitor settings.

**[D]** The normal master branch initializes its summing buses, then loads
the track base into I13 at `0x1c20ea`. The native exclusion mask at `0x252728`
skips tracks by bits 0–15. The compressor routing mask at `0x252538` chooses
the compressor input (`0x254878`) or bypass bus (`0x254a78`) independently for
each included track. External sources and the shared FX return have separate
routing bits at `0x25253c`. Subtracting selected dry audio from the FX return
would be incorrect when a track and that return choose different branches.

The implemented adapter instead makes private track and raw external-input
copies. It scales selected held-source copies by the equal-power dry gain,
redirects the two native send readers and the master reader to those copies,
and substitutes the external pointer pair. It adds validated, already-scaled
phrase wet audio to the completed native return at the master reader hook.
All existing native compressor/exclusion routing then executes unchanged.
Original track/raw buffers, saved levels and native send amounts remain
untouched. Ordinary custom sends continue to read originals. Source feeds into
native effects follow FADE; already-existing shared native effect tails do not
get cleared or multiplied.

The capture mix reads the originals before these attenuations. The current
selection controls capture; held old/new masks control dry ownership. During
a recapture handoff, common sources retain their attenuation while old-only
sources recover and new-only sources enter. Both changes follow the engine's
handoff weight with a per-block sample ramp. A single external channel is
captured as centered mono; selecting both preserves stereo. Internal capture
observes native main exclusions; external capture ignores direct-monitor gain.

The generic loader assigns the exclusive capability an internal route 2. The
owner runs once, before native sends, using the established FX callback guard.
No new public route is added. `_phrase_route_info` proves the firmware includes
the actual audio-routing adapter before an owner can bind.

**[V]** A second agent checked the five static routing anchors against the
current image bytes and relocation hashes: send entry `0x1c18a6`, first-track
pointer `0x1c1906`, remaining-track base `0x1c19d0`, master-track pointer
`0x1c20ea`, and exclusion-mask load `0x1c20e7` (`DM(0x252728)`). This verifies
the anchor locations and decoded operands, independently of runtime behavior.

## Fault and silent-state behavior

Empty/unavailable owners cannot attenuate live sources. A callback fault,
invalid generation/version, stale heartbeat or invalid output restores LIVE.
Fault recovery is latched: a healthy callback alone cannot re-duck sources.
A new command, new owner, or FADE brought to zero and re-engaged is required.
The router publishes the current owner's latch in status word 30,
`PH_S_ROUTE_FAULT`, independently of the actual fade value. This lets the
control provider return the visible knob to LIVE without mistaking ordinary
control/reply latency for a fault. An old owner's generation cannot publish
its latch against a replacement owner; the engine leaves this word untouched.
An intentional silent held phrase, including RATE OFF, remains a valid held
effect and keeps source ownership at nonzero FADE.

**[D]** The control provider's host run passes all 13 tests, including
the compiled C control harness and twelve transport assertions. The harness
checks the interaction between the displayed FADE and a still-latched router
reply: an explicit zero-to-raised gesture starts a 64-audio-block deadline
(42.67 ms at 48 kHz) for a healthy reply to clear the latch. Further nonzero
turns do not extend that deadline. A persistent fault returns the displayed
FADE to zero on the first control tick at or after expiry; a new fault episode
resets it on its first observed control tick. CLEAR
cancels a pending retry, storage/link errors bypass its grace period, and an
owner rebind clears all retry state. The existing 750-block status-expiry
check still applies. These are host control/transport checks, separate from
the concrete router's fail-open and explicit-rearm checks below; the grace
window does not permit failed audio processing to attenuate LIVE.

## Master-step timing

**[V]** Independent reads of the DTII 1.17 master scheduler at
`0x400d8540..0x400d85ba` and table `0x4020e28c` establish the nominal step
period. Pattern byte `+0x1db79` selects advanced mode when equal to 1; its
master period is six pulses of 900,000 ticks. Common mode uses scale byte
`+0x1db7a` to index pulse counts `[3, 4, 6, 8, 12, 24, 48]`, also multiplied
by 900,000. Thus the common-mode step periods range from 2,700,000 to
43,200,000 musical ticks; these are clock units, not audio sample counts.

The position provider stages this period with the nominal pattern boundary
and hardware-phase origin. A pending pattern/scale change therefore cannot
alter capture timing before that boundary, and the first take does not need
to infer its step period by waiting for a second boundary. The exact timing
integration tests are tracked separately from this static byte verification.

**[V: static ordering]** Stock START sets transport to running at `0x400d6650`
under the interrupt mask, before the position observer at `0x400d66d8` clears
validity and the committed period. Only the subsequently due timed boundary
publishes the new generation and period. Therefore a control frame can see
running transport with invalid position, zero period, and the old generation.

The phrase clock bridges only that specific precommit handoff from an already
valid stopped grid: unchanged generation/pattern, non-stale native clock,
valid selected tempo, and zero committed period. It keeps the stopped grid
for at most 64 audio blocks (about 43 ms). Other invalid clocks and a timeout
fail invalid immediately; a timeout cannot renew itself. The committed valid
boundary must advance exactly one generation and retain the same pattern
and scale before the new epoch gets a retained `PH_TIMING_START` reason.
The engine uses that reason only to preserve a numbered cue waiting in
ARMED; a recording, a NOW capture, or another discontinuity still cancels.
Every new epoch resets rolling history and grain launch scheduling. Host and
native stopped-cue tests cover this handoff; their pass evidence is recorded
with the corresponding acceptance run, rather than inferred from these bytes.

## Coordinated storage

**[D]** `core/loop-storage` now makes the same six reviewed DTII allocator
capacity/end writes reserve 24 MiB rather than 8 MiB. The allocatable pool is
376 MiB. The original Overloop descriptor and 8 MiB address remain unchanged.

| Region | DSP byte address | Bytes |
| --- | --- | --- |
| Phrase bank 0 | `0x9ba2b9c8` | `0x00800000` |
| Phrase bank 1 | `0x9c22b9c8` | `0x00800000` |
| Existing Overloop buffer | `0x9ca2b9c8` | `0x00800000` |
| Unmoved stock preview start | `0x9d22b9c8` | Stock region |

Each phrase bank holds 1,032,192 stereo float frames (21.504 seconds at 48 kHz),
one Q16 master-phase stamp per 32 frames, and 2048 reserved guard bytes. The
engine owns both banks and must finish all readers before a held bank becomes
the rolling capture bank. The phase history supports musical-window lookup
across tempo changes without allocating another large L2 history.

The CF logical sample offset includes a `0x400` bias. Its new end
`0x17800400` maps to DSP `0x9ba2b9c8`, not 1024 bytes later. The stock preview
destination operands and global LP0 bound stay unchanged. The provider emits
separate immutable legacy and phrase descriptors; runtime consumers cannot
allocate overlapping regions independently.

**[D]** The real ColdFire allocator-helper probe passes for stock and reserved
DTII 1.17 images (`out/runs/prism-storage/report.json`). With synthetic sample
records, the reserved image reports 394,264,576 bytes (376 MiB) for total and
empty-pool free capacity, accepts the new exact capacity, and rejects an
over-limit request and the old 400 MiB capacity. Tail allocation obeys the new
end: the final 8192-byte free span is usable, an oversized request leaves its
output sentinel unchanged, and an interior 8192-byte gap adds to the reported
free space. These are calls through the original firmware helpers, not a
replacement allocator model. They do not exercise actual sample loading,
recording, compaction or project changes.

## ColdFire cache-line integration

**[D]** The first native UI run exposed a patch allocation boundary problem.
Its write-watch reported a fetch at `0x47fc36cc` on cache line `0x47fc36c0`,
following a write from `0x47fc2cb4`. The captured patch plan places the
300-byte `PhraseUi` state at `0x47fc35a0`; its writable `title` field is at
`0x47fc36c8`. The generated LED-hook tail begins at `0x47fc36cc`, so a normal
title update dirties a line also containing executable code. No instruction
bytes overlap the field. The same plan has another shared boundary: the
menu's writable `STORE` at `0x47fc0268` precedes phrase UI code beginning at
`0x47fc026c`. These addresses describe that captured patch layout, not stable
firmware anchors.

Patchkit now requires 16-byte bases and reserved footprints for executable
ColdFire objects, starts their first non-text section on a new cache line,
and reserves complete cache lines for generated ColdFire hook tails. Function
sections remain packed at their own declared alignment. Read-only constants
embedded in executable sections stay with their code; separate ELF read-only
sections become DKO data and are isolated along with writable data/BSS.
SHARC placement and text/data packing remain unchanged. The four focused
linker unit tests pass, including SHARC placement at an address aligned to
eight but not sixteen bytes. All 25 primitive cases pass across the full run
and corrected fixture's named rerun. The patched native capture and coexistence
runs also pass their lifetime safety checks with zero write-watch events and
zero cache-flush violations. The corrected native UI run also passes its
lifetime safety checks, as described below.

**[C]** The initial pre-dispatch candidate `0x4011c4e8`, labelled key dispatch
in finding 71, received no callbacks in the instrumented native UI run. Its
unique byte window verified a location, not that semantic label. The binding
now uses the primary-key dispatcher at `0x4011c79c`; the old hook is removed.

**[V]** Independent reads of stock DTII 1.17 MAIN bytes verify the corrected
dispatcher's unique 48-byte window (SHA-256
`dbc9176ef60626f2b66cec5f4736946cd8e720907b568a69daa9bca0fdb9ba76`).
The first eight bytes comprise `lea.l -0x60(sp),sp` and
`movem.l d2-d7/a2-a6,(sp)`. Subsequent loads read the event from new `sp+0x68`
and manager from `sp+0x64`, preserving the ordinary two-argument ABI. The
primary-key callback literal `0x4011b568` occurs only at `0x4011c87c`, and the
wrapper calls the generic view walk at `0x4011c8ac`. The 48-byte HelpBubble
table window at `0x40216f14` is also unique; its draw slot at `+16` contains
`0x400f6c82`. That draw entry has a unique 32-byte window and a complete
eight-byte `link.w`/`movem.l` prologue. These checks establish static locations
and instruction boundaries; the runtime evidence is recorded separately below.

## Native performance UI acceptance

**[D]** The corrected DTII 1.17 native UI acceptance case passes (60.4 seconds,
`kB59Qp` run, 2026-11-13). The primary-key manager hook now receives and owns
IN's TRIG gestures before native grid/keyboard handlers. The test proves
source selection, held-TRIG capture-gain editing without a release toggle,
next-source LED ownership and restoration, and the normal NO gesture closing
only IN. SETTINGS removes control ownership; LEVEL under that modal cannot
change FADE. Covering the page with H or a TRIG still held cancels the old
gesture, and subsequent navigation and fresh input gestures work normally.

The same case checks all four local pages share one global FX code, both wrap
directions, last-page recall after leaving the effect, native held FUNC+FX
show-values without an extra advance, deliberate FUNC+NO CLEAR while keeping
IN open, and FUNC+FX exit from IN. Normal and show-values screenshots use
native text and knob rendering. Local visual review confirms the PRISM header
survives the passive native page-name overlay and IN labels EXT and GAIN.
All four normal page captures differ. This is scoped native emulator evidence,
not a golden-pixel comparison or physical control qualification.

The run passes exported-overlay, settings-write and lifetime safety checks.
Host tests separately cover render bounds, header-only repaint bounds,
capture locking, enum travel, and cancelled-gesture reconciliation: a fresh
chord can belong to a new view, while a chord notification during the old
still-held cancelled gesture cannot leak its release.

## Coupled native capture and coexistence

**[D]** The DTII 1.17 native emulator capture test passes through the patched
firmware's real audio path. It selects a test sine instrument on two tracks
through the native chooser, then uses panel gestures and encoders to capture
one actual master-step interval from track 1. The completed take agrees with the stock
tempo within one 32-frame block, returns REC to OFF, and remains audible after
transport stops and the live note has ended. At full PRISM with RATE OFF,
track 1's dry RMS is less than 0.1% of the audible unselected track 2 RMS;
returning FADE to LIVE restores track 1. Captured output is finite and the
production FX guard stays healthy. No CF/DSP memory writes fabricate the take.

**[D]** A second native test passes with CANOPY, Overloop and PRISM loaded
together. Native controls select CANOPY, record a short Overloop take, then
record one PRISM master step from that track. Both loop effects contribute at
partial fades while CANOPY runs. The captured mixture is finite and audible;
both held lengths remain stable, their processing counters advance, and the
instrument and FX fault guards remain healthy. The installed descriptors show
three disjoint reservations, and both legacy and phrase descriptors remain
unchanged throughout. This is one active capture/blend scenario, not a
worst-case cost measurement or comprehensive sample-bank integrity test.

Both cases pass their exported-overlay and lifetime write-watch checks. An
initial capture-policy failure was traced to valid settings-copy writes at
`0x2e19fde` and `0x2e19fee`: both sectors had complete masks and valid DKST CRCs,
sequences 1 and 2, and only the expected `phrase/digikit/prism` key (160-byte
value, 201-byte record). PRISM-only tests now use a narrow settings policy
allowing those two copy starts, with kit sectors and neighboring store sectors
still forbidden. The coexistence case uses the established FX-routing policy
because its native Overloop edits also write kit data. No production store
validation was relaxed.

## Evidence and remaining qualification

The completed routing run passes all 39 colocated TypeScript assertions,
including all eighteen sources, explicit fault-recovery rearming and fault
flag generation isolation. It also executes the real native send,
external-monitor, master-track and USB-packing boundaries. The
engine suite passes 35 assertions covering exact NEXT/PAST audio,
cancel/overflow/epoch preservation, tempo changes and discontinuities,
ownership handoff, live crop bounds, four audible grain shapes, SPACE
tail/CLEAR, grain telemetry, hostile contexts, bad storage/lease/non-finite
controls, combined extremes and a saturated eight-reader handoff.
The final cases also prove that a stopped numbered cue survives a qualified
START, held grains resume after a beat-origin reset, invalid-clock CLEAR and
CANCEL execute immediately, a rejected ARM cannot replay after clock recovery,
and full-bank PAST exhaustion reports CAPACITY while preserving the held take.

The engine descriptor reserves 48,000 for its block budget. The largest
observed concrete-core block uses 39,849 instructions, including the production
FX callback guard, during saturated handoff. Combined extreme settings use
36,325; the steady eight-grain limit uses 36,196. These are bounded emulator
instruction counts, not measured hardware cycles or a complete firmware load
budget. The generic cross-device conformance ceiling near 31,000, coupled
CANOPY/Overloop cost and physical timing remain separate qualification work.

The concrete SHARC transport suite passes 12 assertions: it copies the full
64-word control packet, rejects malformed headers, torn sequence/checksum,
duplicate/backward sequence and ambiguous half-range advances, and accepts
forward wrap and later recovery. Three reply pages carry one coherent status
snapshot; the fourth preserves the complete legacy Overloop carrier. Disabled
transport and all unrelated reply words stay untouched. The production reply
preserves live I4/R1/R2 and inherited DAG context, including nonzero L5.

The colocated TypeScript routing tests drive a bounded concrete-SHARC analysis
probe. They execute the production adapter under hostile register contexts,
verify masks/gain ramps/fault fallback, and run the original hash-checked
firmware send, external-monitor, and master track accumulation with the actual
reader hooks. The native boundary checks compare original and substituted
inputs and confirm original track/raw buffers stay bit-identical. No firmware
bytes are committed. Storage tests audit the six operand writes, preview
boundaries and packed descriptors; the real ColdFire allocator-helper probe
also passes with the reduced capacity, within the synthetic-record scope
described above.

The USB cases run the production router twice, then execute the original
`FUN_1c28b5` from entry through both output loops, stopping at `0x1c29e0`
before its epilogue. For both native source-selection modes, the 512-word
interleaved region and 672-word packed region are nonzero and bit-identical
between LIVE and full FADE, while all selected dry mirrors become silent.
These tests seed valid source pointers to the original track buffers in
the native table at `0x252d78`. They verify the actual packing instructions
consume original buffers; they do not exercise USB transport, a host audio
driver, or the earlier initialization of that source table.

The complete affected gate ran on 2026-11-14 against base `590d65b8` and
finished with 96 successful tasks and failures. PRISM-related failures exposed
two ColdFire alignment fixtures, optional-provider test bindings, an empty
DNII conformance collection for a DTII-only module, and insufficient room for
the combined image's patch record. Those issues were corrected and checked
with scoped follow-ups. An isolated retry of the three Nx discovery suites
still hit their 30-second subprocess deadlines before selection/release
assertions could run; those timeouts remain unresolved and their assertions
were not weakened.

The patch-record shortage was 476 bytes. Reusing the clock division quotient
to derive its exact remainder removes the separate 64-bit remainder helper;
temporary UI key-dispatch tracing was also removed. No firmware RAM boundary,
runtime quota or patch-record validation was relaxed. A 4,096-step host test
compares the resulting clock accumulation against full-width division/modulo,
including phase wrap. The two isolated link-fixture suites and generic FX-rig
tests pass 102 tests (two optional skips), and all six PRISM generic FX ABI/
safety cases pass using explicit synthetic phrase leases. The latter are not
a substitute for native capture or timing measurements.

The final revised DSP passes all three DTII native acceptance cases (UI,
capture and coexistence) and the DTII load-conformance case. Both Wasm build
targets also pass. These results do not make the complete affected gate green.
A read-only audit of its individual Python outcomes against base `590d65b8`,
followed by focused reruns, separates the causes:

- Missing `Digitone_II_OS1.12.syx` causes fourteen sample-binding-close failures
  at `cf_call`'s firmware-file read, before tested firmware code executes.
  `test_pk_current` separately found stale DDR-proof and LP0-proof objects.
  After source-hash and `selache_rev` metadata refresh, its focused rerun
  passes one test with one skip. The dynamic-FX suite also passes all eight
  cases after a forced cache refresh. These were build prerequisites rather
  than evidence of a runtime regression.
- Thirty-three MIDI fixture cases initially omitted newly optional phrase
  bindings. The eighteen composed-frame cases now pass with `_phrase_receive`
  absent explicitly. Both loader dictionaries now bind `_phrase_control_info`
  and `_phrase_reply` to zero, covering all undefined imports in the built
  DTII loader object; their fifteen-case rerun passes. All thirty-three
  introduced binding failures are resolved with existing assertions unchanged.
- Confirmed baseline fixture mismatches include CANOPY's expected DSP section
  version 6 versus the existing version-8 packer, a machine-rule filter that
  excludes the generic `cf.mem.fn.free` extern it then checks, and a Darwin
  toolchain test that changes `sys.platform` without changing cached `DARWIN`.
  Three sample-pool failures read output fields or counters in the same C
  `printf` argument list that invokes their writer, leaving evaluation order
  unspecified. These tests and the relevant production sources match base.
- Eight CANOPY note-curve comparisons expect the stock fast attack while the
  existing CANOPY code limits onset to at least 256 samples. The CANOPY DSP,
  fixture and SHARC execution/build sources match base; no PRISM routing,
  storage or runtime is linked into these cases. The same file's twelve other
  cases pass. This explains the mismatch without claiming physical envelope
  qualification or changing the CANOPY assertions.

The release firmware image builds to 2,046,880 bytes and the PRISM module to
8,756 bytes. Release preflight passes with the exact emitted plan. The original
linear scan misclassifies a phrase-storage `.data` word as a PM store of B8;
the plan declares only the provider's two-byte NOP as executable. An explicit
`--plan` path now verifies the whole-image SHA-256, base and module identity,
complete appended placements and their loaded-byte hashes before scanning
every declared instruction. Unknown or truncated instructions are rejected,
including at the beginning of short code ranges. Stock/in-place checks and
the strict no-plan behavior remain intact. The plan is compiler/linker
provenance, not a cryptographic signature; use the unedited emitted plan.
All 14 scanner/preflight tests pass, including the real descriptor, illegal
instructions, malformed ranges, missing placements, mismatched images and a changed
bootstrap. No firmware bytes changed for this scanner correction.

Release artifact SHA-256 values:

- `phrase-dt2.syx`: `27be9d6f88f06b2eb9107c64d524c54bc373ee29a734b99123fefdda4cdb9d00`
- `prism-dt2.dkm`: `00d305fa5e67cf806fefe6e299f828af5163ba3b9d2f3c8aa28d666d9969ba41`

The broader gate remains red: DNII firmware/generated-core prerequisites are
absent, CANOPY preset and older fixture failures remain outside this change,
and existing formatting/layer violations remain. The DTII typed-image pin
was remeasured for cache-line alignment; the DNII pin is explicitly unmeasured.
No all-repository or cross-device acceptance is claimed.

**[O]** Broader integration remains open beyond the scoped native audio,
UI and load-conformance cases.
Runtime reload, per-track USB delivery
to a host, native shared-effect tail coexistence, sample load/delete/compaction/
recording/project changes with bank canaries, and worst-case real-time cost
require further evidence. No physical PRISM installation or hardware timing
qualification is claimed here.
