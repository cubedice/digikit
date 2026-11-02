# DT2 1.17: explicit SRC page capability and input interfaces

2026-11-02. **[D]** Static MAIN OS inspection and ColdFire component execution;
no physical device changes or booted SRC UI acceptance.

## Page count dependency

The existing `mf_srcpage` hook at SourcePageView draw +0x10 resets every added
machine to page zero and truncates its native vector to one entry. A custom
second painter alone therefore cannot provide a usable second page.

`core/machines` now has a core-facing version-1 registration contract in
`src/cf/dt2/src-pages.h`. Its descriptor is four words: ABI version, byte size,
page count and nonzero instance generation. Registration is restricted to
added types 7..31 and one or two pages. A different live generation cannot
replace an owner; revocation requires the current generation. Descriptors are
copied, and no caller or firmware-view pointer is retained.

For an explicitly registered two-page machine, the hook preserves a valid
native page index and restores the vector to two entries only if its original
capacity permits it. Out-of-range indices reset to zero. Both pages use the
knob-page branch, leaving the painter/input adapter responsible for their
distinct contents. No new native parameter slots are allocated. V1 packer
rules are unchanged. Unregistered added types keep one page; stock types and
the no-track sentinel retain their original two-page behavior.

This capability is intentionally **not registered by CANOPY yet**. Registering
before its painter and encoder handler are ready would expose duplicate or
incorrect controls. The single-instance adapter must revoke its old generation
before registering a replacement.

## View and encoder layout [D]

On DT2 1.17, SourcePageView constructor `0x4001753a` stores:

- Primary View vptr at object+0, table `0x401edc98` with 60 function entries.
  The two preceding words are offset-to-top and type information. Entry 60 is
  the next subobject's negative offset, not another primary function.
- Encoder interface vptr at object+4, table `0x401edd90` with three entries.
  Entry 2 is `0x400187bc`, a thunk subtracting four from the receiver before
  branching to SourcePageView encoder handler `0x400184ea`.
- Primary slot 2 is key handler `0x400181fe`; slot 4 is draw `0x40016eac`;
  slot 11 is update `0x40017a40`. Slot 3 returns a type/name pointer and **is
  not an encoder handler**.

ViewController encoder dispatch `0x4011c674` uses the interface at View+4.
Its invoke shim `0x4011b5da` calls that subobject's vtable slot 2. Any future
wrapper must preserve the interface receiver adjustment and untouched virtual
entries; copying only the generic 17-slot View table would truncate this class.

EncoderEvent constructor `0x401119b8` stores encoder number at +0x0c, signed
movement at +0x10 and a coarse/fine flag at +0x14. Getter `0x40111a3a` maps
numbers 1..8 to indices 0..7, otherwise -1. Scaling helper `0x40111aaa` chooses
one of its two multipliers according to the flag and multiplies signed movement.
These fields do not establish source-page ownership, generation, modifier or
queued-event validity; those remain the adapter's responsibility.

## Evidence and limits

The machine component tests and page-capability scenarios pass (26 total).
They execute the C registration and assembly hook on the
ColdFire emulator, including stock behavior, index/storage bounds, invalid
descriptors, stale revocation, replacement refusal and per-type isolation.
The DT2 module builds with 404 decoded instructions; the DN2 object is unchanged.

CANOPY's two rendered pages, native header/dot geometry, focus, all encoder
gestures, firmware-backed controls and actual DSP telemetry are still open.
This finding records a Stage 5 dependency, not Stage 5 completion.

## Independent elapsed-time source [D]

DT2 1.17 function `0x400cc8b2` extends the wrapping DTCN0 counter
`0xfc07000c` into a 64-bit accumulator at `0x42940a4c` (high) and
`0x42940a50` (low), retaining the last counter at `0x42940a54`.
`0x400cc970` divides that accumulator by 132000000 to obtain seconds.
Registration at `0x400cca0c` passes the update callback and interval128 to
`0x40002aa4`. Prior passive physical measurements are recorded in
[CANOPY carrier](78-canopy-carrier.md).

The instrument adapter reads all four words under the IRQ mask, includes the
unaccumulated counter delta, and converts outside the mask. It does not write
timer configuration or firmware state. Bounded division avoids a freestanding
64-bit division import. Native SRC bridge calls now use this source; host
fixtures inject time. Counter wrap, accumulated carry, millisecond wrap and
elapsed time without audio callbacks are covered by the SRC host tests.
This does not establish native painter/input integration or booted acceptance.

### Selected track scalar [D]

`0x4004abb8` reads the word at offset8 of the selector's selected record; it
returns that word, not the address of the record. In the normal SRC context,
`0x40035cca` consumes it as a signed track index, clamps it to0..15 and returns
`kit + 0x6c + index*0x3c0`. `0x400cd88a` independently consumes it as an index
into the track-level array. This resolves the earlier ambiguity for the normal
selection path. The alternate-context closure and queued-event ownership still
need explicit treatment before attaching native input.
