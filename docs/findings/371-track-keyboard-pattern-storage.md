# 371. Track keyboard pattern storage on DTII 1.17

## Record and ownership [D]

The v4 pattern serializer at `0x400dc894` leaves 308 bytes at the end of
each `0x15c00` record, beginning at `0x15acc`. It writes 80 native
parameter-lock entries at `0x10a04`, each 258 bytes. For unused entries
it writes two `0xff` header bytes and leaves the payload alone
(`0x400dc804`). The native loader (`0x400dc60a`) never reads the payload
of an entry with that header. See [the native project record](121-project-kit-storage-dt2-1-17.md).

The adapter uses a `TKR1` signature and length in the tail followed by
the pattern record. If needed, the payload continues in the skipped
native lock entries in their native order. The two header bytes and
every populated native lock are preserved. Writes check available space
and respect a foreign record tail before changing any extension bytes.

The enclosed `TKPT` record contains only saved tracks and nonempty step
entries. Version 2 carries ordered pitches, velocities, delays and
gates. A complete record is validated before it replaces live settings.
The record checksum covers the track mask, recorded-step count and
payload. A corrupt record is ignored and the stock project stays usable.

An association outside native pattern memory separates the editor from
the committed audible pattern. The pattern writer and loader sync the
association after a successful native operation. Full-pattern copies
through the existing Sound memcpy observer carry the source snapshot
with the native clipboard and undo buffers. The initializer clears the
association on native pattern reset. The 0x6a5-byte stock track clipboard
copy carries an independent track snapshot. Track clear uses the native
initializer at `0x400e5af8`. Both scopes use the same live replacement
notification. Destination step indices are remapped only after a capacity
check.

## Track sequence clipboard [D]

Native track copies move a `0x6a5`-byte chunk. The adapter carries the
track settings, filtered step locks, and recorded notes alongside each
native clipboard or undo copy. A paste remaps the step identities onto
the destination track and checks its available attachment slots.
Native track clear at `0x400e5af8` clears that track's attachment.
The same copy observer handles the restore from the native undo buffer.
The firmware validates a canonical track offset or a matching temporary
snapshot before applying the attachment.

Open grid recording for the track sequence clipboard. FUNC with RECORD,
STOP, or PLAY copies, pastes, or clears the active track sequence.
Repeat the paste or clear gesture to undo it.
[Digitakt II manual, sections 6.4 and 17](https://www.elektron.se/wp-content/uploads/2025/06/Digitakt-2-User-Manual_ENG_OS1.15_250625.pdf)

## Individual trigs [D]

The held-TRIG commands enter the native copy, paste, and clear routines
at `0x4005889a`, `0x4005a1e0`, and `0x400594c8`. The shared clipboard
retains the native clipboard reference and stores the step locks and
learned or recorded note recipe in the order accepted by the native
copy. Paste follows the native step offset and track-length wrap,
and keeps the original clipboard spacing for later pastes. It checks
for overlapping destinations and enough lock and note slots for the
whole edit before applying the native paste.

Repeat the paste or clear gesture on the same selected trig or trigs
within four seconds to undo a shared attachment edit. The adapter
uses the native clipboard routines to capture and restore the touched
trigs. It checks the current native fields, shared attachments, and
pattern revision before restoring. Native clipboard and undo slots
retain their own entries during the snapshot. Tapping an active note
trig off clears its attachment through the note flag setter hook.

## Emulator results [D]

The manual storage test on a disposable DTII 1.17 card set A01 and A02
independently, pasted and undid the clipboard copy and inspected the
saved extension and native project sectors. It then booted a fresh
emulator from the exported card and loaded the stock project. Both
patterns recovered their separate keyboard routing settings.

The undo check reads the pasted value before pressing another panel key.
An intervening panel gesture can reset the stock clipboard undo context.

The track clipboard test selected track 2, enabled ARP, placed a native trig,
and copied its sequence in grid recording. Pasting onto track 1 applied the
saved ARP state. Undo restored the destination state. Clear and undo clear
changed track 1 while track 2 retained its setting.
[Track clipboard emulator test](../../modules/core/adapters/track-keyboard/tests/track-clipboard.device.test.ts)

The individual trig fixture copied an ARP lock to another trig,
undid the paste to recover its previous GATE lock, cleared and
restored the source lock, and deleted the source without disturbing
the destination. A two-trig clipboard wrapped from step 16 to step 2
and undid both changes, then pasted to steps 6 and 8 using the same
source spacing. [Trig clipboard emulator test](../../modules/core/adapters/track-keyboard/tests/trig-clipboard.device.test.ts)

The portable tests also cover ordered note durations, selector state,
corrupt payload rejection, preserving populated native lock records and
rejecting a write that cannot fit. They check track copies and clears with
selector settings, step locks, and timed notes. The native project and
track clipboard paths have emulator coverage. Physical verification and
transfer between units remain open.
