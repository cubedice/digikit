# CANOPY Sound association without an in-Sound handle

This is a design candidate for DTII 1.17, not a production persistence
implementation. The previously tested `Sound + 0x13a` handle is unsafe: the
stock initializer at `0x400e65c4` calls `0x400e6464` with `Sound + 0xa2`,
and that helper writes byte value 1 at argument `+0x98`, i.e. `Sound +
0x13a`. The earlier emulator fixture overwrites that stock byte. Its
roundtrips establish neither stock compatibility nor hardware safety.

## Candidate dense table

Store immutable raw-record handles outside Sound memory. Known 1.17 live
address observations provide a possible dense index:

| Candidate owner | Sound address | Count | Sound stride |
| --- | --- | ---: | ---: |
| Kit table | `0x426532b8 + kit * 0x5574 + 0x34 + track * 0x450` | 129 kits × 16 tracks | `0x450` |
| Sound pool | `0x4291377a + index * 0x450` | 128 | `0x450` |

Validate bounds, quotient, remainder and exact Sound start; never round an
interior pointer to a Sound. The hypothesized full kit range ends at
`0x4290422c`, and the pool range ends at `0x42935f7a` (exclusive). Verify the
entire 129-kit runtime geometry before relying on this mapping. A 16-bit handle
per candidate slot costs 4,384 bytes, excluding record storage, allocation
metadata, synchronization, and non-table owners. A zero slot means no record.

Each entry represents the current *lifetime* of that slot. A load, default,
machine change, swap, copy, destruction, or reuse must update or retire the
association at the same atomic boundary as the Sound. An address match alone
cannot establish ownership after reuse. Hashing the stock Sound contents is
also insufficient: two Sounds can have identical stock bytes but distinct
CANOPY records. Native loads must retain raw records before the CANOPY module
is admitted, including unknown and corrupt records.

## Owners outside the table

Known inline ranges are not exhaustive. In the 1.17 image, the path at
`0x400533ca` allocates a `0x460`-byte refcounted object, clears its `0x450`
Sound data at `0x40053406`, and copies a Sound at `0x4005345e`. The observed
clipboard path uses temporary Sounds, with full copies at `0x401aaccc` and
`0x400521aa`. The undo path copies a whole `0x5574`-byte kit at `0x400443d0`.
None can be represented merely by indexing the fixed live ranges.

The operation at `0x4004568e` and `0x400456b6` copies two live Sounds into
separate stack buffers, then copies them back to opposite destinations at
`0x40045774` and `0x40045794`. A side table tracking only live-to-live copies
misses this swap. One option is a scoped association for each temporary;
another is an outer, preflighted swap transaction that transfers both
associations. Either approach must account for every exit and failure.

The heap object's vtable at `0x401f248c` includes the deleting destructor
`0x401a2306`, which changes the vtable and jumps to generic free at
`0x4011ebdc`. This is one observed path, not a unique lifetime event for all
Sounds: inline kit and pool slots, stack locals, other heap objects and raw
copies have different lifetimes. An overflow address map needs proven birth,
retirement and reuse events, not a generic-free hook alone.

## Required proof before integration

Enumerate stock Sound owners and all mutations, including full and partial
copies, bulk kit operations, reset/type changes, deserialization, and Sound
save. Prove how a temporary acquires and releases an association and how a
reused address avoids stale state. Reserve both association capacity and raw
record capacity before the first stock mutation, and verify each outer caller
actually handles refusal; an inner kit refusal cannot undo a pattern already
written by the pattern-plus-kit path. Serialize updates with live edits and
pool publication, without allocating inside an interrupt or mapper lock.

In the opt-in emulator run recorded at
`out/canopy-bank-preflight/digi-session-SGRR5u/probe.json`, a synthetic worker
call to the stock 130-Sound bank loader returned failure after an injected
reservation allocation failure. The full synthetic destination hash and pool
count were unchanged, and no inner Sound load ran. The fixture uses a private
pool and does not write the unsafe in-Sound handle. This exercises one guarded
entry and a synthetic caller; it does not prove a real caller handles refusal,
validate a side table, cover pattern-plus-kit mutation, or enumerate callers.
Production wiring must remain disabled until these lifecycle, failure,
concurrency, boot, and hardware-compatibility obligations are met.
