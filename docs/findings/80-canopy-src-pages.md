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
