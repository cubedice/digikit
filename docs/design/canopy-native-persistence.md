# CANOPY Sound persistence investigation

CANOPY's custom SRC state currently resides in the admitted instrument instances,
not in the Elektron Sound. Saving a CANOPY preset is explicit; a project save or
restart does not serialize those values. A pattern change deliberately retains
the previous instance configuration, even if the destination Sound is different.
Consequently a boot-only restore would not supply ordinary Sound semantics.

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
permutations against the image hash. Neither test exercises stock Sound
ownership, saving, copying, or boot. The experiment does not implement
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
