# CANOPY copy-on-share pool experiment

`sound-share-pool.c` is an unlinked host experiment. It does not implement
Sound persistence or connect to the Digitakt. It explores reducing snapshot
consumption during repeated edits while retaining copied revisions.

Publishing a pre-reserved entry creates a snapshot exclusive to one
boot-lifetime owner token. That owner may update it without consuming another
entry. Before
any Sound or kit copy is visible, the copy path freezes each source entry.
Frozen entries never change and can be shared by copied Sounds. The next edit
of any shared Sound must use a pre-reserved entry and atomically replace that
Sound's handle. Without a reservation, the edit is refused with its old handle
and snapshot unchanged. A failed edit does not consume the reservation.

Published handles and reservations are never reused for the pool's lifetime.
This makes the experiment bounded and avoids aliasing an old copied Sound, but
it does **not** reclaim memory. Sufficient copy/edit cycles exhaust the pool;
only edits to already exclusive entries can then continue. No save, reset, user
notification, or recovery operation is implemented here. A production design
would need a verified, explicit recovery for exhaustion and heap failure.

The caller supplies all storage, pre-reservation and synchronization. The
owner token must not be a recyclable Sound pointer. A single proven exclusion
protocol must cover Sound identity and generation validation, every snapshot
read and write, publication of the Sound handle and the full stock copy. In
particular, freezing a handle and releasing the lock before the stock copy is
unsafe: an intervening edit can detach the source, causing the wrong revision
to be copied. The pool cannot verify that a frozen handle belongs to the Sound
given by a caller; the caller must do so. The protocol must also prevent
initialization, machine changes, load, save, undo and in-place replacement
from bypassing ownership checks. Holding an interrupt mask across a stock kit
copy would need its own latency proof; this experiment proposes no such mask.

A host-only transaction test now combines this pool with the real SRC encoder
preview and commit functions. It checks repeated exclusive edits, a frozen
copy that detaches on the next edit, and refusal without changing the control
or copied snapshot when no reservation is available. Its prepare callback
only validates and encodes into scratch; it changes the pool in the attach
callback after the real edit succeeds, under the same simulated mapper lock.
The same host harness now exercises the actual MIDI selector mapper with
exclusive and frozen snapshots. It checks that a pre-reserved slot detaches
the edited Sound, that an unreserved edit leaves both snapshots unchanged,
and that simultaneous musical notes and note-offs continue through a refused
selector edit, including a full eight-event batch. This remains a simulated
lock and pool; it does not wire the transaction to the device UI or MIDI path,
measure the real interrupt duration, or prove the attach step infallible
across all production callers.

All relevant Sound and kit copy paths, including temporary, clipboard and undo
copies, would have to freeze their sources before this method could ship. No
complete inventory or concurrency proof exists. The host test checks the pool
state transitions and refusal only, not firmware boundaries or timing.

Additional writers must participate before a Sound-owned snapshot can replace
the instance state. `scan_clock_done` changes both wavetable clock and rate;
`scan_restart_done` and `amount_changed` update remembered source-bank values.
Changing SHAPE SLOT can replace the effective embedded USER shape, and its UI
backing value has already changed when the callback runs. A failed transaction
must restore that backing value as well as the keyboard state.

Learning a shared shape is a multi-owner transaction: `shapes_share` changes
the effective shape of every live instance using the selected library slot
without a preset override. `preset_snapshot` embeds that shape even if USER is
not currently selected. All affected owners must be prepared before publishing
any of them, with old copies left unchanged. LEARN begin, capture and cancel
are transient; the pre-LEARN Sound remains the persistent one until save.
The existing host transaction tests do not prove these menu or shared-shape
paths. These observations are from the current source, not device proof.

`instrument_sound_proof_menu` is an unregistered test helper for those
individual menu edits. It stages one preset and commits the actual shared
setters only after preparation succeeds. Six host cases cover clock/rate as
one edit, remembered WT/FM/WTFM wheel banks, refusal and retry, stale
admission and callback re-entry. The real menu callbacks do not use this
helper, and no device timing or Sound-lifetime guarantee follows from it.
The separate test-only `instrument_sound_proof_shape_slot` prepares the
effective shape before invoking the existing slot-change callback. Seven host
cases cover reselecting an overridden slot, switching slots, copied snapshots,
refused or stale requests and preserving a newer staged UI value. It is not
connected to the device menu.

In proof builds only, `instrument_sound_proof_set_transactions` can register
preparation and publication callbacks on the public encoder and keyboard
entry points. Nine host cases exercise accepted and refused edits, note
releases, an unfocused MIDI track, stale admission and callback re-entry.
Production builds do not register or call this dispatch. A fixture must still
provide pre-reserved storage, verify ownership, and retire its callbacks safely.
An opt-in two-track emulator fixture exercised real panel encoder edits and
public MIDI mapper batches against immutable snapshots after worker refill
(`out/canopy-live-proof/digi-session-s6ksgY/probe.json`). It checked exhausted
credits, retry, matching note releases and unchanged old snapshots. Its handles
remain fixture-owned, and it is not a physical MIDI or production persistence
test.

The proof-only `instrument_sound_proof_learn` helper stages prospective
snapshots for affected instances before calling the real LEARN save and
shared-library routines. It uses a single static preset and shape as scratch;
the caller handles the UI after the lock is released. Eight host cases cover
PLAY and ROOT peers, preset overrides, copied owners, an exhausted reservation,
retry, an aliased reservation, a stale capture, changed owner generation,
re-entry and a divergent peer library. A refusal leaves the learned library,
live settings, menu, pool and reservations unchanged. This helper is not wired
to the device: stack, interrupt timing and actual ownership remain unproved.
