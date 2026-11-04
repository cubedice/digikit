# DT2 1.17: explicit SRC page capability and input interfaces

2026-11-02. **[D]** Static MAIN OS inspection, component execution and booted
DT2 1.17 SRC acceptance. No physical device changes.

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

CANOPY's native adapter now registers this capability only for its admitted
track and live generation. It revokes its old generation before registering a
replacement. Unregistered machines retain their original page policy.

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

### Native integration corrections [D]

The first running SRC adapter exposed two boundaries absent from its host
stand-ins. `0x40030b64` copies the constructed encoder event onto its own stack
before calling ViewController dispatch at `0x40030bac`. The pointer retained by
the dispatch closures is this copy, not the constructor's object. Capture page
ownership at that dispatch call; matching the constructor's pointer rejected
every real turn as stale. The dispatch ABI is `(controller, event) -> u32`.

`fill_rect` at `0x401131ae` treats negative color as XOR: the branch at
`0x40113280` reaches `eor.l` at `0x40113284`. Nonnegative colors use their low
bit to select OR/set or AND/clear. Consequently a bitmap overlay must use
0/1, not 0/-1; the latter visibly corrupts letters against existing stock
pixels. Native adapter tests now model XOR and compare every owned pixel with
the bitmap, while checking that the native header and page dots survive.
These corrections still require the running acceptance results below before
claiming the complete Stage 5 UI.

The stock widgets do not share the custom grid's cell geometry. Preserving
fragments of A/H left a sample widget at top-left and cropped LEVEL at the
bottom. The adapter now paints all eight cells. It obtains values through
`0x4006aa32(view, code, flags_out)` and calls the existing parameter formatters:
TUNE code `0xca`, formatter `0x400e177c`; LEVEL code `0xd1`, formatter
`0x400e1588`. Both formatters take `(raw_value, char_buffer)`. Validated native
ranges bound the output to fewer than 32 bytes. Six-character signed decimal
values use the small face so the complete value fits; the decimal glyph is
explicit. Input for SOUND A/H continues through the native encoder handler.

The SRC primary attach and detach slots are 5/7, originally `0x400693da` and
`0x4011b422`. The adapter preserves those calls and restores the admitted
track's page through the first draw after attachment, scoped to the registered
instrument generation. No firmware View pointer is retained.

The slot-11 callback alone did not keep the idle SRC screen current in the
booted proof: the applied bank reached the DSP while pending markers cleared
only after another captured redraw. The adapter now checks the UI-owned view
stack from its existing UI tick, bounded to 32 nodes, and invalidates the
visible owned SRC view at 40 ms intervals. It walks from the manager's resolved
first drawable node, so opaque covering views suppress animation, and resolves
pointers afresh each tick. Host checks cover that deadline,
modal suppression, stock-track exclusion and restoration without retaining a
View pointer. The running-firmware regression checks pending completion with
no additional keys or forced screen capture.

Telemetry draining must also be independent of painting. With polling only in
the painter, pending edits remained until a forced capture despite the applied
DSP bank being correct. The UI tick now drains the authenticated FIFO and
reconciles matching applied revisions before considering its redraw deadline.
This does not acknowledge pending values from transport receipts or weaken
freshness checks. The booted test passes idle pending completion and continued
redraw without a forced capture, then checks focus expiry.

The machine component tests and page-capability scenarios pass (26 total).
They execute the C registration and assembly hook on the
ColdFire emulator, including stock behavior, index/storage bounds, invalid
descriptors, stale revocation, replacement refusal and per-type isolation.
The DT2 module builds with 404 decoded instructions; the DN2 object is unchanged.

Booted acceptance passes both pages, their editable and read-only controls,
matching applied DSP revisions, SRC/UP/DOWN navigation, AMP/FLTR return, stock
track isolation, page restoration, idle refresh, focus expiry and MIDI notes.
The earlier native-page checkpoint image had SHA-256
`0f6f2c4db7f4b5ce906b3fd30d2758f8c850067a3482398529539549550d70dd`.
The passing run is `out/canopy-stage5-native-proof.log` (326.97 seconds).
The completed Stage 5 acceptance below extends this checkpoint with
persistence, LEARN and native TUNE/LEVEL. Neither run is hardware qualification.

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
selection path. The native adapter excludes alternate context and captures
queued-event ownership at dispatch, as described above.

## Native pitch and control-bank extension [D]

The SourcePageView parameter resolver is primary vtable offset0xbc, original
function0x40017cd4. Stock encoder dispatch obtains the zero-based knob index
through0x40111a3a and calls this resolver. The V2 adapter explicitly maps SOUND
A/H to0xca/0xd1; other contexts retain the original resolver. Both the slot and
original function have unique hash-window anchors. No legacy parameter table is
added to the V2 module.

The pinned parameter table at0x4020f59c has60-byte entries. TUNE code0xca selects
Sound index25, raw range0x0400..0x7c00 and default0x4000. LEVEL code0xd1 selects
index34, range0..0x7f00 and default0x6400. The admitted Sound array starts at+0x14,
so those halfwords are+0x46 and+0x58. The adapter reads the current admitted
track's Sound each pass; it retains no selected-view Sound pointer.

CPS7 carries native transpose alongside the existing32-word atomic bank. The
adapter must copy all32 words for both CPS6 and CPS7: accepting the new schema
but retaining the old10-word copy silently discards the presentation bank.
That mismatch now has a connected configuration regression test.

The kernel combines signed8.8 native transpose with the route's pitch bend.
It retunes foreground pad voices and the current arp without resetting their
identity or phase, retains raw arp pitch/bend across legacy-bank transitions,
and clamps integer pitch lookup to0..127. Pending onsets use the new tuning;
already detached short arp tails retain their previous pitch until they end.
Applied telemetry detail word16 carries the exact TUNE raw value, bound to the
same generation/revision as the custom controls. Old CPS6 detail remains zero
in that position. Range rejection leaves the instance unchanged.

Focused direct/protected tests cover octave and fractional pitch, both range
ends, held-note return to center, combined route bend, active arp retuning,
legacy bank transitions, complete telemetry acknowledgement and invalid banks.
These are instruction-core checks; they do not establish a hardware CPU budget.

## Completed Stage 5 acceptance [D]

The expanded running-firmware test passes on the final image SHA256
`65520ae2ddc0086e3e4ba564180a92fc42d95f36f4db169d7651c3d4414eee21`.
Log: `out/canopy-stage5-final-runtime.log`,394.95seconds. It extends the earlier
page proof with LEARN modal ownership, durable preset save/load through the
settings service, native TUNE on a held voice, matching applied revision, and
native LEVEL edits. Native encoder movement is repeated through the original
accumulator; one initial event alone need not produce a parameter increment.
The full host lane passes333 tests. See the current
[Stage 5 handover](../handovers/HANDOVER-2026-11-01-canopy-stage5.md) for final
artifact hashes, scope decisions and the separate Stage 6 hardware gates.
