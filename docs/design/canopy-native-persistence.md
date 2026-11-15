# CANOPY Sound persistence investigation

CANOPY's custom SRC state currently resides in the admitted instrument instances,
not in the Elektron Sound. Saving a CANOPY preset is explicit; a project save or
restart does not serialize those values. A pattern change deliberately retains
the previous instance configuration, even if the destination Sound is different.
Consequently a boot-only restore would not supply ordinary Sound semantics.

**Correction:** the experimental 16-bit handle at Sound `+0x13a` is not safe.
The stock initializer writes `1` to `+0x13a`, and the machine refresh copies
both `+0x13a` and `+0x13b` into its outgoing row. All emulator fixtures using
this field remain useful only as isolated functional experiments; their passing
results do not justify installing the handle scheme on a device. **[C][D]**
Runtime parameter 0 is not a substitute: the generic setter, two descriptor
rows ("None" and "Trig Note") and the lock loops all reach it
(`tests/patchkit/test_canopy_sound_param0_sites.py`). An association held
outside Sound memory is sketched in
[canopy-sound-association-candidate.md](canopy-sound-association-candidate.md). **[D][O]**

## Reproducing the persistence failure

The unregistered DTII device test
[`persistence.device.test.ts`](../../modules/machines/canopy/tests/persistence.device.test.ts)
edits CHRD, VOIC and STRUM on two independently admitted CANOPY tracks, uses the
stock panel to save the project, and starts a new emulator on the resulting
disposable card. It checks that the saved card selects CANOPY and that CANOPY
loads successfully, then uses the stock panel to load the saved project and
compares the two tracks. The test uses the regular runtime prerequisites,
including the built CANOPY module and DTII 1.17 firmware. From the repository
root, run:

```sh
DIGI_CANOPY_PERSIST_TEST=1 mise exec -- pnpm --filter @digi/regression exec vitest run --project runtime ../../modules/machines/canopy/tests/persistence.device.test.ts
```

On the unfixed implementation the comparison fails: track 1's
`(mode, CHRD, VOIC, STRUM)` of `(1, 4, 3, 16)` and track 2's `(1, 2, 2, 8)`
both return as `(0, 0, 0, 0)` after the cold boot and explicit project load.
The two tracks are admitted after that load, so the comparison does not rely
on a missing CANOPY module or an unselected machine. The test currently logs
the automatic boot state separately; in this run neither track was admitted
before the explicit load. It therefore does not prove automatic resumption of
the last project on boot. It also does not cover copies, pattern changes, or
the remaining Sound lifecycle. Keep it outside `runtimeAcceptance` until the
implementation and this regression pass.

## Constraints

The DTII 1.17 Sound record and its positional parameter mapping are documented in
[finding 02](../findings/02-machines-and-parameters.md#storage-records). The
runtime-to-storage mapping saves 69 parameter values; the other record slots are
zero-filled, and the last two runtime parameters are not saved. The six CANOPY
MOD targets already use six of the eight SRC mirror slots as neutral offsets.
Neither these offsets nor the stock TUNE/LEV fields are available as arbitrary
payload without changing their behavior.

An individual fixed-width encoding of the current 32 custom controls uses 176
bits; the remembered FM bank uses another 83 bits. Together with the selected
family, keyboard mode and an embedded shape, a snapshot uses 293 bits. On the
DTII 1.17 image, the 69 unique entries of the save permutation at `0x40213a50`
leave 21 zero-filled parameter slots among positions 0..89: 4, 8, 12, 16, 20,
24, 28, 32 and 77..89. The first eight map to dummy runtime parameter 0 in the
load permutation at `0x40213b64`; the last 13 are outside the load loop. These
42 serialized bytes can hold a versioned compact snapshot with an integrity
check. They do not provide backing storage for the running Sound: the firmware
overwrites the eight dummy entries while loading and zeroes all 21 while saving.
Writing and reading these positions would require serializer hooks, and their
effect on stock firmware and every Sound lifecycle still needs proof.

`sound-codec.c` is an unlinked feasibility experiment: it round-trips all
current controls, remembered FM settings and the embedded shape in 42 bytes.
The host test covers boundary values, independently varied configurations and
every single-bit corruption; the firmware-map test checks the actual 1.17
permutations against the image hash. The host test also scatters and gathers
the snapshot at the 21 proposed record positions, checks that other bytes stay
unchanged, and rejects corrupt or truncated input. Neither test exercises stock
Sound ownership, saving, copying, or boot. The experiment does not implement
persistence or change the device's Sound format.

One possible RAM association is a 16-bit shadow-pool handle in runtime Sound
parameter 0, the dummy written by the eight padding entries. A loader hook would
attach it only after stock deserialization completes, and a save hook would
replace the transient handle in serialized slot 0 before filling the padding.
This needs proof that generic fallback setters, resets and every clipboard or
Sound-copy path cannot clobber or lose the handle. Copies need immutable shared
snapshots and copy-on-write; pool exhaustion and reclamation must not discard
state still referenced by any kit, Sound pool, or clipboard. Decode can occur
before CANOPY's dynamic type is registered, so validation and publication
cannot assume the type is available during the boot deserializer call. Runtime
indices 69 and 70 are unavailable because `core/fxsend` uses them.

The 64 by 12-byte sample-related area in a Sound
record is not proven unused or safe to repurpose; doing so could corrupt sample
or slice settings when the Sound changes machine.

On the DTII 1.17 image, the stock Sound writer at `0x400dbccc` copies each of
these 12-byte entries from Sound RAM `+0x140` to record `+0x139`. The reader at
`0x400db726` copies the first two words, range-checks the third, and counts
nonzero entries. Thus this area is not an arbitrary byte-preserving extension:
unrestricted bytes can be altered on load and also change the stock slice data.
These observations are static and have not been independently reviewed.

Further static inspection found that `0x40050f32` identifies occupied slice
entries by the first two words without checking machine type. `0x4005104e`
enumerates those entries and the caller at `0x400d39b8` emits a separate slice
list, so hiding arbitrary payload there would expose fake slices even if the
Sound writer were sanitized. `0x400510f4` calls `0x40050f98` to clear and
rebuild the whole slice table; observed direct callers include `0x4001a730`,
`0x40060166`, and `0x400d4de2`. A loan in otherwise empty entries could be
destroyed by this operation. A variation using third words in empty trailing
entries avoids the observed occupancy check but not the table rebuild; its
other consumers, concurrent updates, availability, and machine transitions
remain unproved. Neither variation is suitable for production yet. **[D][O]**

A narrower tail proposal would place 42 payload bytes and a two-byte marker in
the third words of entries 53..63 only if all three words of each entry were
initially zero, and sanitize those words in the stock serialized slice table.
It still has no representation for Sounds with occupied tail entries. Moreover,
`0x400510f4` clears the entire table and rewrites up to 64 entries; one direct
caller, command case 7 at `0x400d4de2`, accepts a track without an observed
machine-type check. A guard would need to handle that complete operation and
machine transitions. A stock Sound copy interrupted by a MIDI edit could also
copy a mixture of old and new payload bytes; an integrity check would detect
but not recover that loss. These are static observations, not a safe alternative
or a complete inventory of slice consumers. **[D][O]**

SRC mirror slots 29 and 30 are suppressed as sampler bank/slot aliases by
`instrument_mod_param`; they are a candidate location for a bounded sidecar
identifier, subject to proving that stock refresh, assignment and copy paths
preserve their bits and do not use them for other sampler behavior. A matching
sidecar still needs immutable Sound identities, transactional storage and a
strategy for copies, exports and reclamation. The writable settings API is a
shared 8 KiB double-buffered record, not an unbounded file store. Reusing a key
for later edits can make an older saved or copied Sound resolve to the wrong
state; retaining every revision there will eventually exhaust the shared store.
The runtime's existing device-side store file API supplies reads, but no general
create/write operation. A small boot-recovery cache could fail closed on a
missing ID, but would not provide native Sound persistence.

The current project slot is not a unique persistent identity: slots can be
overwritten, reused, or copied. A settings record keyed by track or by project
slot would therefore conflate different Sounds and cannot be presented as
native Sound persistence. See [finding 110](../findings/110-project-identity.md).

## Runtime ownership investigation

DTII 1.17 saves a Sound through `0x400dba94(record, sound, flag)`. Its first
parameter permutation entry is zero, so it writes runtime parameter 0 to
record `+0x1c`; the 21 proposed extension slots do not include this slot.
The stock loader repeatedly writes parameter 0 from padding while reading a
record. A proposed in-memory handle there would therefore need to be installed
only after a successful load and removed from the serialized dummy slot after
a save. The handle must never be assumed valid merely because a Sound address
matches an earlier object.

The live 1.17 kit table begins at `0x426532b8` with `0x5574` bytes per kit;
its Sounds have `0x450`-byte stride. The live Sound pool is addressed at
`0x4291377a + index * 0x450`. These ranges are not a complete list of owners:
at `0x4005339c` the firmware allocates a heap object and copies a full Sound
into it, and the pattern undo path at `0x400443be` copies a complete kit into
heap-backed storage. Other paths use stack-local Sound copies, such as
`0x40045686` and `0x400456ae`. A Sound handle naturally follows those raw
copies, but immutable snapshots and copy-on-write alone do not say when a
snapshot can be reclaimed. Scanning only the live kit and pool tables could
reclaim a snapshot that an undo or clipboard object still holds.

One snapshot path (`0x40047dd0` through `0x40047eb2`) copies a Sound, then
conditionally substitutes locked values for indices 0 through 70. Its getter
at `0x400405ce` accepts index 0. A handle in parameter 0 is therefore not yet
proved to survive every stock Sound operation. Parameter indices 69 and 70
are not an alternative: DigiKit's FX-send implementation uses both. The
remaining design must identify and bound all copied owners, handle allocation
failure and reuse without aliases, and verify boot ordering; loading may occur
before the instrument's dynamic type is registered. These observations are
static reads of the 1.17 image, not runtime proof.

### Rejected two-byte Sound handle [C][D][O]

The DTII 1.17 MAIN image examined for this section has SHA-256
`a1e7b657b705eba1a19d81c33c1e11ba9c409816447ad74005d7bbf36da6d964`;
the source SysEx has SHA-256
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`.
These are static observations, not an independently verified or device-tested
claim that the proposed handle is safe.

`Sound + 0x13a..0x13b` was considered for a two-byte in-memory handle. The
stock 1.17 deserializer `0x400db3b2` writes the sixteen-byte field at
`+0x12a..+0x139`, the count at `+0x13c..+0x13f`, the sample entries at
`+0x140..+0x43f` and the sample reference at `+0x440..+0x44f`; it does not
write the intervening two bytes on that path. The stock writer `0x400dba94`
does not serialize these two bytes. A scan for direct 16-bit displacement
operands in the MAIN image found no established access to this gap in a live
Sound. This scan cannot rule out indexed pointers, bulk operations or mistaken
instruction boundaries and is not proof that the gap is unused.

The previous analysis overlooked another initializer helper. At `0x400e65be`,
the initializer passes `Sound + 0xa2` to `0x400e6464`; the helper writes
`1` to offset `0x98` of that pointer, i.e. `Sound + 0x13a`. In a ColdFire
comparison, an initial `0x4567` became `0x0167`. A test-only early `0xffff`
marker became `0x01ff`, which could alias handle 511; a late `0xffff` marker
overwrites the stock byte. Neither marker is safe. Machine refresh at
`0x4002d4a6..0x4002d4c0` also copies `0x9a` bytes from `Sound + 0xa2`,
including both candidate bytes, into the outgoing machine row. The meaning
of those row bytes is not yet established. The initializer is reached from
generic initialization
at `0x400e6634`, a temporary Sound used for serialization at `0x400511f6`,
failed project-load defaulting at `0x400dfc08` and the in-place operation below.
A linear reference scan over `0x40000400..0x401e0000` found these four direct
references with 99.89% decoded byte coverage; indirect callers remain open.

At `0x40051fd2..0x4005200c`, firmware obtains a live Sound through the track
vtable's `+0x28` getter, copies all `0x450` bytes into a separate object, and
calls the initializer on the live Sound with the requested type. The copy
includes `+0x13a..0x13b`, but the initializer overwrites the first byte of
the live Sound. A handle there could therefore become a different valid handle
on reset. Separately, the setter at
`0x40051780..0x400517a2` obtains a live Sound through the same getter and
writes its type byte at `+0xa2` directly. Hooking the initializer alone does
not establish that every type-change path invalidates the handle.

The snapshot path at `0x40047dd0` first copies all `0x450` Sound bytes;
its subsequent lock-substitution loop at `0x40047e90..0x40047eb2` targets the
71 parameter halfwords at `+0x14`, rather than the candidate gap. Another
heap allocation at `0x400533fc..0x40053406` clears all `0x450` bytes. These
examples show why the gap may survive some copies and start zero in one
allocation path; they do not enumerate all Sound owners, copies, resets or
allocation paths.

#### Observed Sound copy and paste [D][O]

A disposable emulator-only memcpy-entry probe filters for exactly `0x450`
bytes and exposes a non-mutating instruction label for the host's register
observer. It has no shared counter, callback, allocation or lock. From a
previously booted CANOPY state, the ColdFire-only `panel_drive` resumed the
snapshot with SSI timing and no live DSP peer. A panel turn changed the
CHRD/VOIC control word from `0x00040003` to `0x00050003`. The stock
TRK+TRIG+RECORD and TRK+TRIG+STOP operations then copied track 1 to track 2;
track 2's machine-type word changed from `0x00000000` to `0x00001000`.

Three matching memcpy calls were recorded, with no dropped observations:

| Return PC | Source | Destination | Observation |
| --- | --- | --- | --- |
| `0x401aaccc` | `0x426532ec` | `0x450d9c54` | Track 1 Sound to temporary object during copy |
| `0x401aaccc` | `0x4265373c` | `0x450da454` | Track 2 Sound to another temporary object during paste |
| `0x400521b0` | `0x450d9c54` | `0x4265373c` | First temporary object to track 2 during paste |

The observations, panel frames and initial state metadata are in
`out/canopy-copy-trace/digi-session-HHSchi/drive.json` and `probe.json`.
The probe records arguments, not copied contents or the validity of a CANOPY
handle. In that observation the fixture snapshots edited controls only at
serialization, so this trace alone does not establish that copied CANOPY settings
follow the Sound. The result does not cover bulk kit/project copies, inline
copies, undo, concurrent edits, live DSP behavior or hardware. The marker is
an isolated test fixture, not a production hook. **[O]**

A separate, explicitly selected emulator-only handoff fixture builds on that
observation. Its global memcpy entry intercepts only length `0x450` from return
PC `0x401aaccc`, snapshots an admitted active CANOPY source into the immutable
pool, and places its handle in the source object before stock performs the
copy. The normal proof tick later applies the copied handle to the admitted
destination. This callback does not run at `0x400521b0`, the traced paste copy;
that operation carries the previously attached handle as part of stock's
ordinary object copy. **[D]**

The opt-in `copy-handoff.device.test.ts` ran with the emulator's live DSP peer
and the real panel gestures. It edited track 1 to `mode=1, CHRD=4, VOIC=3,
STRUM=16`, copied it, changed CHRD on the source to 5, and pasted to track 2.
Track 2 restored the copied values with CHRD 4, and the callback counter was
1. The run passed without a reported emulator fault; evidence is at
`out/canopy-copy-handoff/digi-session-R3bSV5/probe.json` and
`out/canopy-copy-handoff-device.log`. This verifies one copy/paste sequence,
not native ownership or device safety. The hook changes a global routine and
the callback can allocate, race or be re-entered; it cannot cancel stock's
copy if allocation or snapshotting fails. No exhaustion, undo, inactive Sound,
bulk kit/project copy, inline copy, concurrency, complete stack/latency, or
hardware guarantees follow from this result. The fixture is never part of a
production image. **[O]**

Any implementation still needs to invalidate or replace the handle on every
relevant initialization, type change and successful deserialization, including
records without a CANOPY payload, while leaving a rejected load's intact Sound
alone. It must also prove that handles
cannot alias snapshots after exhaustion, reuse, copying or undo; that the gap
survives the actual stock editing and copy paths; and that boot-time loading
works before dynamic machine registration. Those questions remain open.

The `sound-lifetime.s` prototypes, selected only by the disposable emulator
fixture, demonstrate two narrowly scoped invalidation sites. At `0x400e65c8`,
the eight bytes `42aa013c 45ea0140`
clear the Sound count then move `a2` to the slice array. The fixture marks
the candidate handle before replaying both instructions; the stock clear
restores the original condition codes. At `0x4005179e`, the six bytes
`114200a2 b486` write the requested type from `d2` through the Sound pointer
in `a0` and compare it against the old signed type in `d6`. The fixture marks
the handle only when these types differ, then replays the write and
compare. A firmware-byte test pins these observations to the 1.17 image; it
does not prove that those sites cover every Sound or type mutation. Neither
prototype is registered in a production image.

The 64 slice entries cannot safely provide an implicit Sound payload merely
because CANOPY does not itself use a sample. The stock function at
`0x40050f32` checks an indexed entry for emptiness through a track's Sound
getter without an observed machine-type check. `0x4005104e` copies all 64
entries into an allocated list; `0x40050f98` clears all 64, and is called by
the list-to-Sound writer at `0x4005111c`, which then rewrites those entries.
Thus a payload in otherwise empty slice entries could be exposed or erased.
These are static observations, not an exhaustive reachability analysis.

### Serialization hooks and allocator constraints [D][O]

On the same DTII 1.17 MAIN image, the Sound loader at `0x400db3b2` takes
`(Sound *destination, const record *source)` on the C stack and returns its
boolean result in the low byte of D0. Its failure exit at `0x400db800` is
reached for a null argument or a record version other than 3, before the first
destination write; the other observed paths complete at `0x400db7fc` with
success. The existing `core/instrument-play` entry wrapper brackets the stock
loader and preserves its result. `core/fxpages` owns a distinct hook at the
loader's common exit, `0x400db802`, to count loads. Calling the stock body
through the existing entry wrapper still reaches that exit hook. Persistence
work should extend the existing entry wrapper rather than replace the exit
hook; coexistence must still be tested with the final image.

The Sound writer starts at `0x400dba94` with C arguments
`(record *destination, Sound *source, flag)` and returns its result in the low
byte of D0 at the common exit `0x400dbd38`. Its first eight bytes allocate
`0x24` stack bytes and save D2-D5 and A2-A6, so an entry wrapper would have to
replay those instructions before resuming. The disassembly includes direct
calls, PC-relative calls, and addresses loaded for indirect calls; wrapping
the entry reaches the observed uses. A complete scan for entry into the middle
of the function and a patch-site coexistence test remain open.

Returning failure from a persistence wrapper alone cannot guarantee that an
invalid record will be rejected. The caller at `0x4004fd90` ignores the writer
result and returns success at `0x4004fd9a`; calls at `0x400e3162` and
`0x400b5f2a` also proceed without testing it. Conversely, calls at
`0x400d3b62` and `0x40131d78` test the low byte. Preventing an invalid or
missing Sound association before serialization, and accounting for each save
path's failure handling, are therefore necessary to avoid silently emitting an
incomplete record. The full impact and reachability of these callers remain
unverified.

The Sound writer's result is also ignored by both containing writers. The
128-Sound bank writer `0x400dbd7c` calls it through `a3` at `0x400dbda0`,
advances to the next Sound without testing the result, and returns success.
The kit writer `0x400dbdca` calls it through `a4` at `0x400dbe30` for each
of 16 tracks, again without testing the result. The project serializer calls
the kit writer in a loop at `0x400df83c` and the bank writer at `0x400df8e8`.
A 99.89%-covered linear reference scan of `0x40000400..0x401e0000` found ten
direct references to the kit writer and two to the bank writer; this is not
proof that indirect entry points do not exist.

The persistence paths diverge after serialization. A kit path at `0x40043834`
tests the kit writer result and then calls the storage operation at
`0x40130800`; the kit transfer construction path at `0x400e2ee8` ignores the
kit result and proceeds to construct an object. A Sound transfer path at
`0x400e3162` ignores the Sound result and proceeds to the object constructor
at `0x401c8294`. The asynchronous Sound worker at `0x4004fd74` also returns
success after invoking the Sound writer. Therefore a guard in the Sound
writer, or in one downstream storage operation, cannot by itself stop all
observed incomplete project, kit and Sound output. A degraded-state write
guard would require an audited boundary for each output and worker, and an
explicit recovery mechanism. These findings are static and not an exhaustive
enumeration of export or save paths.

The exported `fw_alloc` resolves to the firmware allocator at `0x4011e954`.
It takes the stock heap mutex at `0x44e08654` through `0x40001608`, initializes
the heap lazily under that mutex, and takes the mutex again for allocation. A
nonzero request with no free block reaches the negative-index case of
`0x4011e7a8` and returns null; a zero-sized request executes `illegal` at
`0x4011e9f2`. Mutex availability and call context at every Sound load and save,
including early boot, have not been established. In particular this allocator
cannot be assumed safe in an interrupt, with interrupts masked, or while its
own mutex is held. A persistence design must prove the allocation context and
handle exhaustion without overwriting an existing Sound or saving the wrong
snapshot.

For the observed boot path, the boot task calls the heap allocator through
`0x400c0d90 → 0x400e1140 → 0x400e0d8a → 0x401c7974` before project Sound
deserialization and releases that temporary allocation beforehand. Project
reload path `0x40043f74` calls `fw_alloc` at `0x40043fc0` and then
`0x400e1140` at `0x40044016`; path `0x40046906` calls it at `0x40046958`
and then `0x400f521c` at `0x400469be`. Both reach the Sound loader loop at
`0x400df95a`. The Sound-file path at `0x40130f4e` locks its object at
`0x40130f74`, calls the allocator alias `0x4011ebd0` at `0x40130f98`,
`0x40130fa0`, and `0x40130fb0`, and reaches Sound deserialization at
`0x4013101c` under that object lock. These observations support heap calls
in those specific task paths, not other loader callers, interrupts or
arbitrary lock orderings. The project upgrader requires a roughly 16 MiB
contiguous allocation, so the pool's heap fragmentation and coexistence with
upgrades need measurement. **[D][O]**

An explicitly selected `test/canopy-sound-proof` module is a disposable
emulator-only vertical-slice experiment. It hooks the Sound writer and the
loader's successful exit, snapshots currently admitted instances during
serialization, and applies a decoded record after admission through a test-only
callback. It uses the candidate gap and immutable pool. The wrappers and callback
are compiled only with `DK_CANOPY_SOUND_PROOF`, through a generated alternate
adapter manifest; the production adapter does not include them. The unregistered
two-track reboot test selects the fixtures only when `DIGI_CANOPY_SOUND_PROOF=1`;
its normal run remains the production baseline.
The fixture does not handle concurrency, allocation failure, all Sound/type
changes, inactive-Sound edits, or write refusal and must never be installed on
hardware. Passing that test would demonstrate only the narrow experimental
path, not safe native persistence. **[O]**

The current fixture reserves `0xffff` for defaults pending after the observed
initialization, type-change and no-payload-load paths; `0xfffe` marks an
allocation or encoding failure. Pool snapshots stop at `0xfffd`, so these
states cannot alias handles. The fixture consumes the default marker only
after checking Sound and admission generation under the mapper lock, applies
the SRC initializer's defaults while retaining the shared learned-shape
library, and leaves the handle at zero. A later admission-generation change
with the same zero-handle Sound does not itself cause defaulting. Copies of a
pending marker default independently, while unobserved and partial copies,
zero-handle edits, concurrent failures and restore timing remain open. **[O]**

The marker variant passed the disposable emulator's project-load and machine
switch scenario: a loaded handle of 1 became `0xffff` on the stock machine,
and the new CANOPY returned to default CHRD, VOIC and STRUM. Evidence:
`out/canopy-switch-marker-device.log` and
`out/canopy-lifetime/digi-session-kk8L36/probe.json`. Focused host tests also
covered marker consumption, stale ownership and pool exhaustion. This test
does not reproduce a transient link reset or establish general Sound-copy
ownership. **[D][O]**

In an earlier fixture variant, the disposable DTII 1.17 emulator project
round trip gave an edited
CANOPY Sound a nonzero handle; switching that track to a stock machine cleared
the handle, and switching back gave the original default CHRD, VOIC and STRUM
values. Evidence: `out/canopy-switch-lifetime-device2.log` and
`out/canopy-lifetime/digi-session-zoxbnB/probe.json`. The focused firmware-site
tests also passed. This single machine-switch path does not establish copy,
undo, link-reset, pool-exhaustion or physical-device safety. **[D][O]**

The initial emulator experiment saved two independently edited tracks,
power-cycled, explicitly loaded the project, and restored their CHRD, VOIC and
STRUM values. Evidence is under `out/canopy-persistence/digi-session-Ub1uTJ/`
and `out/canopy-sound-proof-device.log`. After isolating the code behind the
test-only build flag, the same test passed with artifacts under
`out/canopy-persistence/digi-session-940jOY/` and
`out/canopy-sound-proof-isolated-device.log`. The initial post-save counters
recorded 22 Sound payload writes and zero experiment errors; its post-load
counters recorded two payloads decoded and two applications. These results do
not establish correctness for other Sound lifecycles, allocation failures, or
physical hardware. **[D][O]**

The isolated prototype also restored both tracks after a working-state
restart in `out/canopy-persistence/digi-session-rAwrl7/probe.json`. This test
explicitly sets `DKRT_SAVE_MODE=3`, causing the restart to invoke the stock
working-state serializer and boot-record writer; that is not DigiKit's default
restart behavior and does not prove a physical power-off. Run it with
`DIGI_CANOPY_WORKING_BOOT=1` and `DIGI_CANOPY_SOUND_PROOF=1`. A separate fresh
emulator boot from the card produced after that forced restart stalled before
instrument initialization in `digi-session-MvdVt2`; that combined sequence
is not a passing cold-power-cycle test. **[D][O]**

The opt-in `pattern-ownership.device.test.ts` exercises the related live
handoff: edit CANOPY independently on A01 and A02, save the project, then
return to A01. The production baseline retained A02's chord and strum on A01
(`out/canopy-pattern-ownership/digi-session-hTvgJd/probe.json`). The isolated
prototype also retained A02's chord, voicing and strum on A01
(`out/canopy-pattern-ownership/digi-session-ecgesq/probe.json`). Its writer
cannot recover a configuration that was already lost at the earlier pattern
switch. Run the baseline with `DIGI_CANOPY_PATTERN_TEST=1`, or the unsafe
emulator fixture with `DIGI_CANOPY_PATTERN_PROOF=1` and the same fixture roots
as the reboot proof. The baseline remains an intentionally failing experiment.

An additional fixture-only callback now snapshots an outgoing admitted Sound
before the UI scan replaces its track/Sound association. With that callback,
the emulator pattern test passed for the two edited patterns, including both
return transitions (`out/canopy-pattern-ownership/digi-session-DnAhhr/probe.json`).
It runs only in the generated proof adapter and can allocate; it observes the
UI scan rather than all stock mutations. It does not handle snapshots needed
before unrelated copies or saves, in-place replacement, stock changes preceding
the scan, allocation failure, reentrancy, the interrupt budget or exhaustion.
The regular adapter's rebuilt object is unchanged except its source hash.
The two-track project-save and explicit reload regression also passed with
this callback (`out/canopy-persistence/digi-session-RgaB6q/probe.json`).
This is a narrow experiment, not production ownership. **[D][O]**

The pattern clipboard also needs an up-to-date snapshot before copying. An
emulator test initially passed when A01's last stored snapshot matched its
current state, but editing A01 immediately before copying exposed a stale
handle: the destination received the earlier chord (evidence
`out/canopy-pattern-copy/digi-session-Q0CGFW/probe.json`). A trace of that
panel operation observed stock `memcpy` returning to `0x40047a16` with a
`0x5574`-byte kit source at `0x426532b8`; the live first Sound was at kit
`+0x34` (`out/canopy-pattern-copy-trace/kit-drive.json`). An emulator-only
extension to the existing global copy hook snapshots admitted Sounds in that
source kit at the observed return address before the copy. The tightened
copy, subsequent source edit, paste and undo scenario then passed, including
the copy-time chord (`out/canopy-pattern-copy/digi-session-cQOOUk/probe.json`).
It passed again after the pool reservation and rollback changes
(`out/canopy-pattern-copy/digi-session-x1kWPs/probe.json`).
An opt-in two-track variant also passed after independently editing both
CANOPY tracks before copy and again afterward; paste restored each copy-time
value and undo restored each destination value
(`out/canopy-pattern-copy/digi-session-DGAy4S/probe.json`).
This single observed copy site does not prove all kit/undo/clipboard paths,
and the global hook can allocate, is not reentrant-safe and has no failure
protocol. It must not be enabled on hardware. **[D][O]**

With both the Sound persistence and copy handoff fixtures selected, the
two-track project-save, cold emulator start and explicit project-load test
also passed (`out/canopy-persistence/digi-session-WXwdAS/probe.json`). This
does not prove an ordinary hardware power-off or cover an exhausted pool. **[D][O]**

### Candidate edit transaction [O]

The fixture snapshots active instances at save time and does not establish
native ownership. A production path would need to attach an immutable snapshot
to the Sound whenever a persisted setting changes, before a copy, switch or
save can observe the new state. A candidate is to reserve entries and allocate
slabs in a verified task context, never from the MIDI/mapper interrupt. An edit
would use a reserved entry, check Sound ownership and admission generation,
prepare the complete normalized snapshot, and atomically publish the snapshot
handle with the state change. If reservation or validation fails, the prior
state must remain intact and a task must report the failure; emitting an older
handle as if it contained the edited state is not acceptable. A provisional
entry must never become visible to a Sound or be recycled while any copied Sound
can still hold its handle.

The unlinked pool experiment now separates task-only `sp_reserve_task` from
allocation-free `sp_publish`: a reservation cannot be read until publication,
and a published entry cannot be rewritten. Published handles are never
recycled during the pool's lifetime. An unpublished reservation can only be
returned by its owner when the whole returned range is still the pool's tail;
other abandoned reservations remain consumed. The 89-entry
slabs fit in 4 KiB each, for a worst-case 737 slabs (about 2.88 MiB before
allocator and index overhead). These APIs require external serialization;
they do not themselves make edits atomic, prove a lock safe or reserve space
for future project loads. **[O]**

The proof-gated `instrument_sound_proof_edit` explores actual SRC encoder edits.
It previews an edit on a copy of the control state, reconstructs the Sound's
source banks and prepares a snapshot in a pre-reserved entry before applying
the actual encoder action and attaching the handle, while the mapper lock is
held. It is deliberately not wired to the instrument UI and does not cover the
MIDI mapper or learned shapes. The callback must not allocate or wait; encoding
and lock duration have not been measured. It is not a production transaction
implementation. **[O]**

The experimental encoder path retains partial turns on discrete lists without
publishing or consuming the pre-reserved entry; a completed choice publishes
the snapshot. Its host test also compares the attached payload with the live
instance after chord, voicing, arp, wavetable and FM edits. This host behavior
does not establish safety under a real concurrent Sound save or copy. **[O]**

The test-only MIDI selector experiment prepares a pre-reserved snapshot before
calling the real mapper. In its host test, an unavailable reservation refuses a
selector note-on while still routing a simultaneous root note and its release;
an available reservation publishes the chosen chord, and a temporary step
chord lock does not publish a persistent change. The revised proof shares the
mapper's input validation and selector predicate within an isolated test-only
keyboard variant. It uses one mapper-serialized preset scratch object instead
of a stack-local eight-event copy and consumes refused selectors while retaining
roots and releases, including a full eight-event batch. The production keyboard
is unchanged. Snapshot encoding still runs under the mapper lock; host success
does not prove interrupt safety or the remaining MIDI cases.
**[D][O]**

The existing mapper is reached by `dl_tick → instrument_write_frame →
mt_prepare_mapped → instrument_keyboard_events` from the 1,500 Hz block
interrupt. `instrument_write_frame` masks interrupts around this work, and
interrupts use the interrupted task's stack. The proof's encoder and mapper
must therefore be assessed against the worst interrupted-task stack
headroom and block deadline, rather than assuming the MIDI task's stack is the
only bound. **[D][O]**

An initial ColdFire GCC `-fstack-usage` build of the proof adapter reported
760 bytes (dynamic, bounded) for `instrument_sound_proof_midi`, 344 bytes for
`instrument_sound_proof_edit`, 52 bytes for `preset_snapshot`, and 28 bytes
for `instrument_keyboard_events`. These are individual function frames, not
the complete interrupted call chain; the compiler flags used in this initial
measurement were not verified identical to the module build. The existing
production object's static frame-depth scan reports 160 bytes for
`instrument_keyboard_events → kb_process_locked → kb_process`, excluding
unresolved and indirect calls and the interrupt's other frames. The rebuilt
proof object's scan observes a 740-byte MIDI frame (including the saved frame
pointer); it is not an upper bound for nested calls because the scanner does
not follow branches. The scanner was corrected to sign-extend ColdFire LINK.W
displacements after this investigation exposed an undercount. Neither result
establishes safe worst-case stack headroom or meets the audio deadline. The
MIDI experiment remains unlinked from the real mapper and must not be enabled
on a device on this evidence. **[D][O]**

A later GCC `-fstack-usage` proof build reports 84 bytes for the individual
`instrument_sound_proof_midi` frame after removing the event-array and preset
locals, compared with 760 bytes in the earlier measurement. The focused
keyboard and transaction run passed 54 tests before isolating the variant.
The 84-byte figure excludes all nested calls, the interrupted task and interrupt
frames; it is not a complete stack bound or a latency measurement. **[O]**

The current SRC encoder calls `cs_edit` before taking the mapper lock. It would
need a tentative edit and ownership/revision recheck before committing. The
MIDI selector modifies `KbState.type` inside `kb_process_locked` under that
lock; it cannot allocate or wait there. Its note/release ledger must continue
to behave correctly if the selector change is refused. Step p-locks temporarily
substitute and then restore mode/type/voicing in the same function, so only a
real persistent selector change may allocate or publish a Sound snapshot.
Learned-shape, setup-menu and preset-application paths need the same transaction.
The upper bound of encoding and publishing while interrupts are masked needs a
measured timing budget; deferring publication instead requires preventing
concurrent Sound copies and saves from seeing the old handle as current.

The observed project serializer writes 129 kits of 16 Sounds and a 128-Sound
bank, 2,192 Sound records per complete project. Reserving this many free handles
before an ordinary project load could cover one such load if every record were
distinct, but it does not solve repeated project switches, extra file loads,
hidden clipboard/undo owners or a capacity shortage discovered after loading
has started. Never-reused handles need a proven capacity policy for all these
paths, or safe reclamation based on complete ownership. Any load refusal must
occur before stock callers can replace a valid Sound with defaults; preventing
subsequent project, kit and transfer output also needs the write-boundary work
above. No complete production reservation or refusal protocol is implemented. **[O]**

The isolated pool now supports task-only batch reservation: it allocates the
required slabs before advancing its handle count, and the host test checks
that failed allocation leaves existing payloads and handles intact. An optional
matching deallocator can release only the slabs newly allocated by the failed
attempt; without one, those slabs remain available for retry. This does not
reclaim published handles: reservations consume the finite pool until used
or their still-unpublished tail is returned, and all operations need
external serialization. A 2,192-entry reservation can need 25 new 4 KiB
blocks; allocation and deallocation can wait on firmware locks and are not
suitable for an interrupt. The emulator fixture wires the firmware allocator
and deallocator, but their failure and locking behavior are not validated on
hardware, and no production load path uses the reservation. **[D][O]**

An entry-point check alone does not establish safe refusal. In the DTII 1.17
image, the project-card wrapper at `0x40046c38` checks the byte result of
`0x40046906`, but a caller of the separate memory-project loader at
`0x400d5232` invokes another function immediately afterward without testing
its return byte. A linear scan of `0x40000400..0x401e0000` found three direct
references to `0x40043f74` and six to the Sound deserializer `0x400db3b2`,
with 1,963,524 of 1,965,056 bytes decoded. Indirect callers remain open. In
particular, returning failure at a loader entry cannot be treated as a proven
transactional abort for its caller. **[D][O]**

The unchecked call is within the larger handler beginning at `0x400d45c4`.
Its dispatch path for value `0x1d` allocates and fills a `0xdd9604`-byte
buffer, invokes the memory-project loader at `0x400d5232`, and unconditionally
calls a state transition that changes state 11 to state 2 before joining a
common cleanup path for both the project buffer and an outer temporary.
An early exit before allocating the project buffer would leave state 11 active;
neither a safe abort transition nor an external failure response is known.
The command, transition and cleanup instruction bytes are pinned in
`tests/patchkit/test_canopy_load_preflight_sites.py`.
The two direct image references to this handler are tail jumps at
`0x4003372e` and `0x4003376e`. Whether a preflight refusal can safely report
failure to the external caller, and which earlier side effects require cleanup,
are still unproved; it must not be treated as the same checked card path. **[D][O]**

The aggregate loaders cannot simply propagate a low-level Sound refusal: the
1.17 bank, kit and project loops default failed entries and continue. Their
observed bounds are 130 Sounds in a bank, 16 in a kit, and 129 kits plus 128
pool Sounds (2,192 total) in a project. A pattern-plus-kit path writes the
pattern before calling the kit loader, so a kit-only reservation is too late
to protect the old pattern. A separate Sound-file caller ignores the load
result before emitting a notification. Representative instruction windows are
pinned by `tests/patchkit/test_canopy_aggregate_load_sites.py`; indirect
callers and a transactional refusal protocol remain open. **[D][O]**

An opt-in emulator fixture now dispatches a synthetic type `0x10`, subtype
`0x1d` command containing a valid tiny compressed frame on DigiKit's worker.
When the project reservation is deliberately refused, the fixture invokes
the stock CANCEL RECALL handler and resumes the stock buffer cleanup. In two
disposable runs, the state changed from 11 to 1, the dispatcher returned, a
nonzero buffer reached cleanup, and an edited CANOPY track retained its SRC
values and full Sound-byte hash (`out/canopy-memory-cancel/digi-session-sfLnbO/probe.json`).
This is a synthetic command on DigiKit's worker, not a real transfer on the
stock command task. It does not prove remote failure signalling, preservation
of the whole project, physical-device safety, or safe failure for the other
memory-loader callers. The fixture is not enabled in production. **[D][O]**

The opt-in card-load guard reserves the project bound before the observed
`0x40046c62` call and returns failure to that wrapper if reservation is refused.
In a disposable emulator run, a forced refusal preserved two edited working
Sounds; a later successful load consumed two snapshot handles and returned the
other 2,190 unpublished reservations (`out/canopy-persistence/digi-session-bqidB5/probe.json`).
In a subsequent run the fixture deliberately failed the second slab allocation;
the first slab was released through the firmware deallocator, the two working
Sounds stayed unchanged, and a later load again used two handles and returned
2,190 (`out/canopy-persistence/digi-session-VrPkwW/probe.json`).
The check does not establish preservation of every project field, actual
firmware-originated allocator failure, concurrent load/copy safety, other load entry points,
or a safe user-facing recovery. The guard remains emulator-only. **[D][O]**

An unlinked `canopy-raw-store` experiment tests a possible split between the
resident loader and the dynamically loaded machine. It retains all nonzero
42-byte extensions opaquely, including unrecognized or corrupt records, and
can copy them back to a record without decoding them. The host test covers
current, future, foreign and damaged records, missing handles, exhaustion and
reservation rollback. This does not establish that a stock Sound is CANOPY,
where the pool lives during boot, callback lifetime, safe copy ownership, or
transactional failure in the stock load/save paths. **[D][O]**

The opt-in integrated fixture shares one immutable pool between live SRC/MIDI
transactions and the Sound loader. It attaches a reserved snapshot to the
candidate Sound handle and rechecks that handle under mapper exclusion before
applying a decoded snapshot, so a delayed decode cannot overwrite a later edit.
In `out/canopy-integrated-proof/digi-session-N6NYpS/probe.json`, independently
edited tracks survived a native project save/load and an explicit project load
after starting a fresh emulator from the saved disposable card. It also checked
credit exhaustion, retry and stale/self-restore handling. This is a disposable
fixture, not the production adapter: complete copy/lifecycle coverage, real
load-failure handling, pool reclamation and hardware timing remain open. **[D][O]**

An opt-in stock Sound-function roundtrip also passed in the emulator with
CANOPY absent (`out/canopy-opaque-proof/digi-session-N7KdVS/probe.json`). It
used a candidate handle at Sound `+0x13a` and preserved current, future,
corrupt and foreign payload bytes without changing the other serialized
bytes. The fixture uses one synthetic Sound; it does not prove a full project
cycle, handle lifetime through copies or safe failure of stock callers. **[D][O]**

A separate compact-pool experiment omits the deduplication buckets and links
from that raw path. Each immutable entry is 43 bytes, so 95 fit in a 4 KiB
slab; the 2,192-entry project reservation needs 24 slabs instead of 25. Its
host test checks batch rollback, invalid handles, tail return and allocation
failure. It still needs verified synchronization, lifecycle handling and a
recovery strategy for published handles before it could be used on a device.
With the same ColdFire compiler flags, the standalone compact object has 642
text bytes versus 996 for the original pool, before final linking and alignment.

The isolated `canopy-detach` experiment models an in-place Sound replacement.
It captures an old snapshot and full Sound for undo, suspends the instance,
then either applies a validated replacement or restores the old bytes after a
partial write. A changed generation is quarantined. Host and ColdFire component
tests passed, but the experiment assumes exclusive destination ownership and an
atomic admit-and-apply callback that the stock integration does not yet have.
Its full-Sound backup and encoding under a modeled interrupt mask also need a
real latency and stack budget. It is not wired to firmware. **[D][O]**

A separate ColdFire component fixture exercised proof-only callbacks around
the actual `ip_sound_begin`/`ip_sound_end` bracket and stock Sound deserializer.
It checked successful replacement, a pre-write refusal and rollback after an
injected post-write failure, while a second admitted instance remained intact.
The callbacks and atomic readmission are modeled, and the normal stock wrapper
cannot interpret the proof's refusal cookie. No production hook is enabled.

## Required proof before integration

1. Identify Sound-owned RAM and serialized storage that can hold the complete
   versioned payload without altering stock machine data. Verify the DTII 1.17
   save, load, Sound copy/paste, kit copy and pattern switch paths against image
   bytes and bounded emulator runs. If no such region exists, design a native
   serialization extension rather than reusing unidentified bytes.
2. Define lifetime and ownership when a Sound is loaded in place, a track changes
   machine, a project changes, or two tracks hold copies of the same Sound.
   Publish a complete validated configuration at the existing suspend/reset
   boundary; never restore a stale snapshot based solely on a RAM address.
3. Carry both the active and remembered sound-family banks and the embedded USER
   chord shape. Version and validate the record before applying it. A missing or
   invalid record must have an explicit default/migration policy.
4. Test independently edited tracks and patterns, project save/load and boot,
   Sound and kit copy/paste, machine switching, old projects, corrupt records,
   and returning to stock firmware. Verify that TUNE, LEVEL, MOD, p-locks and
   sample data remain intact. Do not report boot persistence from a preset or
   global cache as proof of these paths.
