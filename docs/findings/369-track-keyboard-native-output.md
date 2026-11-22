# Native notes and a pre-send helper voice fold

Digitakt II OS 1.17. **[D]** records a source trace or emulator observation. **[O]** records an open proof. The reference design is [Track arpeggiator and keyboard](../design/dt2-track-arpeggiator.md).

## Stock note output [D]

The stock keyboard lifecycle wrappers at ColdFire `0x4005cee2` and `0x4005c72a` update the keyboard view's own held-key collection. The shared service enters below those wrappers. An audio note starts through `0x401200f2(track, note, velocity, 0x40, 0, -1, -1, -1, -1)` and releases through `0x401204ec(track, note, 0x40)`. In the isolated 1.17 ColdFire fixture, notes on tracks 1, 9 and 10 reached the stock audio dispatcher `0x40139b54`. Releasing track 9 changed the stock held counts from 1, 1, 1 to 1, 0, 1. The stock coupled audio fixture also rendered a track 1 note with a 1000.5 Hz spectral peak.

The stock MIDI worker `0x4012d4ce` accepts a 64-byte native event record. Word 3 selects an onset (1) or release (2). Word 4 bit 7 enables output. With track 10's kit MIDI bit set and its cached channel at 4, an E4 onset at velocity 96 yielded native TX bytes `03 93 40 60`. The native channel/pitch owner changed from 0 to 10 and returned to 0 after release. The event pool head returned to its initial value. The same release worked after the track's kit MIDI bit was cleared, because the stock MIDI worker matches the saved track owner for an off.

The shared backend submits bounded work in the main task. It orders offs ahead of ons, cancels an onset that has not left the queue, and checks room in the stock worker mail and event pools. Admission checks the kit machine and live MIDI bitmap. Stock audio is assigned one voice. MIDI can use several pitches from a single track and uses the configured stock channel. The stock worker has a single owner per channel and pitch, so the generated-note queue keeps the first owner across a channel/pitch collision.

The MIDI worker's native route mask is at `0x44e3b9e0`, with DIN on bit 0 and USB on bit 1. Generated onsets reserve both messages of their pair before entering the worker. DIN accrues 2,700 bytes/s with a 48-byte burst allowance. USB accrues 12,000 framing bytes/s with a 256-byte burst allowance. A note sent to both ports spends both allowances. Over-capacity onsets are rejected with MIDI BUSY. A release keeps the outgoing mask recorded at onset and does not need more allowance.

The native worker retains pitch/channel ownership and constructs both messages. The append hooks at `0x4012ce44` and `0x4012d00e` use the saved mask for tagged generated messages. Untagged messages take the stock append route. A pending native append is flushed before an explicitly routed generated note to preserve same-pitch ordering. Host tests cover exhausting DIN while USB remains available, pairing releases after an output change, and refilling both allowances. The coupled sequence fixture switched DIN to USB and USB to DIN while a note was held. Each release appeared on its original output only. Hardware DIN transmit saturation is still an open proof.

## Helper voices and owner routing [D]

The audio worker also accepts a direct 64-byte event with a physical destination in word 0 and a Sound pointer in word 12. In the stock 1.17 coupled test-card fixture, the stock track 1 note played alongside track 9 and 10 dispatches pointing at track 1's existing sine-sample Sound. The result had three simultaneous peaks at 1000.5 Hz, 1259.8 Hz and 1498.5 Hz. Using the helper destination leaves the original kit Sound values intact. The stock processing pipeline still treats the destination's track as the owner of the helper voice.

A temporary emulator-only overlay folded the track 9 and 10 buses into track 1 before the stock sends driver at SHARC `0x1c18a6` and zeroed the helper buses after adding. Each bus at `0x252df8 + 0x100 * track` is 64 floats, 32 left then 32 right, using a zero-based track index. The stock send readers then used the owner bus at `0x1c1906` and `0x1c19d0`. The master uses the same buses and applies its own exclusions at `0x252728` and compressor routing at `0x252538`. This overlay was local to the emulator and never added to a shipped image.

A 0.32-second comparison gave identical stereo PCM for a track 10 helper alone before and after a unity-gain fold under the fixture's default routing. The 1.2-second comparisons also used only that helper:

| Condition | Left RMS | Right RMS |
| --- | ---: | ---: |
| Track 1 dry excluded, track 10 left separate, owner reverb off | 0.01780 | 0.01780 |
| Helper folded at half gain, owner dry included, reverb off | 0.00890 | 0.00890 |
| Helper folded, owner dry excluded, reverb off | 0 | 0 |
| Track 1 dry excluded, track 10 left separate, owner reverb full | 0.01780 | 0.01780 |
| Helper folded, owner dry excluded, owner reverb full | 0.00803 | 0.00355 |

With the last condition, the 1.0–1.2 s tail measured 0.00153 / 0.00170 RMS. It was silent in the other cases. The owner reverb amount was set through `0x4002d7a4(0x7f00, 0, 56)` (code `0x45`). The dry exclusion was set in DSP memory after the frame decode. It verifies native output route selection and reverb input independently. The half-gain case verifies stereo fold math. The front-panel mute gesture and dedicated LEVEL control have their own upstream paths.

## Owner map, drain and LEVEL proof [D]

The reservation model admits an entire group only when the owner is qualified
and each destination can be leased exclusively and is quiescent. A pattern
epoch change or loss of eligibility stops new helper notes immediately. The
old route stays assigned to its mixer until the worker queue, cursor and AMP
tail are confirmed idle. The model leaves all stored helper requests intact.
Host tests cover conflicts, refusing an occupied destination, atomic group
assignment and holding a retired route across a pattern change.

The map fits in four words at frame byte `0x854`, before the RELAY, phrase,
lock, instrument and position packets. The SHARC receiver accepts a tagged,
checksummed map and rejects helper-to-helper routes. A corrupt frame retains
the last route, and a valid empty map restores stock routing. A standalone
execution test checks individual left/right samples, independent owners and
sparse helper tracks across both packed map words. In the coupled DTII 1.17
fixture, a temporary overlay calls the assembled fold before stock sends.
One helper produces identical 480-block PCM in stock, disabled and folded
cases (`7d3a3af8786ea57f…`). The assembled two-helper chord matches the fixed
overlay above (`fd6bdb6e4278a9dc…`). This initial test injects the map into
private DSP state.

The composed build publishes the double-buffered map from the UI task.
DSP-link writes those four words and projects owner controls into the matching
native frame copy. Pattern-position receives the map before rendering. The
fold runs in relay-input before the custom input callback and native sends.
With a private test lease published on the ColdFire after a cold boot, the
DSP received mask `0x0200` for T10 owned by T1. A held T10 note followed
two native LEVEL/DATA turns on T1. Its window RMS rose from 0.0219 to 0.0266,
while the direct T1 reference rose from 0.0254 to 0.0308. The map in an
unleased reference was empty and its T10 RMS stayed at 0.0239. This verifies
the negotiated frame path under real firmware scheduling. The lease is still
injected by the emulator in this measurement.

Turning the native LEVEL/DATA knob on T1 changed kit track halfword `+0x14`
and wire halfword `0x74` together, from `0x6a00` to `0x7400` or `0x6000`.
The late RMS of the held T1 note changed from 0.0257 to 0.0308 or 0.0211.
The track LEVEL word is distinct from SRC LEV, carried at `0xec + 0x60*t`.
The fold of a T10 helper alone was bit-identical when only the owner wire
halfword at `0x74` was lowered. Applying `0x6000`, `0x7400` and zero to the
helper word at `0x86` changed helper-only folded RMS to 0.01814, 0.02648 and
zero respectively. Track LEVEL must therefore be projected before the helper
bus reaches the fold.

The frame projection copies owner track LEVEL, filter type, SRC LEV and the
continuous filter/AMP/FX control section. A host comparison verifies all other
halfwords are preserved, including pitch, velocity, gate masks, machine and
sample slot. Further coupled tests need to qualify the individual live AMP,
filter and FX controls and confirm that these words suffice across the
admitted stock Sound types.

In a second booted emulator fixture, T1, T9 and T10 played the same sample
as different notes and routed together to T1. A UI-task callback sent the
stock event `[track=9, pitch=67, type=2, source=0x40]` after 456 audio blocks.
In the 0.32–0.49 s window, T10's 1498.5 Hz peak fell from 0.025684 in the
held reference to 0.022724 after release. The 1000.5 Hz and 1259.8 Hz peaks
retained amplitudes 0.036316 and 0.030674 in both cases. This checks a native
selective release with both siblings held. Reassignment with a sustained loop
or long tail requires the separate idle witness below.

## POOL admission and playback [D]

The interactive pool admits the stock ONESHOT, WERP, STRETCH, REPITCH, GRID and
SLICE machines. POOL checks the selected pattern, the live working pattern,
the track machine, stock note and trig data, and per-step parameter-lock
counts. Start a new lease while stopped with the recorder idle. An eligible
group can retain the same lease when the same pattern starts playing. A
machine change on the owner or a helper retires the old lease. A conflicting
assignment fails as a group. The first onset goes to the owner and installs
its Sound and locks. The remaining destinations use the owner's active Sound
and native lock context. Each helper retains its original kit and pattern
records.

Closing the native onset gate begins a 24-block guard, after which admission
requires the requested physical track to have no queued note, no generated
note still owned by the worker, and a fresh DSP quiet witness. The native
queue is scanned under the interrupt mask with a fixed bound. A native type-5
control record was present at every probed UI pass even with all 16 audio
tracks quiet. A check for an entirely empty queue never granted the lease.
The scanner now checks individual note destinations and treats a pending kit
transition or unrecognized record as busy. The native audition dispatcher
and sequencer due hook exclude independent notes on leased destinations.

Every eight blocks the DSP checks both sample cursors and the complete
pre-fold stereo track bus. This handles a native AMP phase that parks after
its source ends. A stopped ONESHOT cursor with a silent bus is quiescent even
if the AMP still holds its last release coefficient. The quiet mask and its
sequence and check travel in the idle DSP-link ACK cells. An active upload
gets its normal ACK. Admission and reuse require a fresh valid quiet reply.
The draining route remains tied to its owner while a
matched physical note-off is delivered and the voice becomes quiet.

The compiled panel test `pool.manual.device.test.ts` selected T1, enabled
POLY, chose an empty helper in POOL and waited for the native lease to publish.
Two held keyboard pitches produced accepted native events on T1 and the
helper. The reserve mask survived PLAY on the same pattern, and two new held
pitches again used distinct native physical destinations. The test uses panel
input and reads the resulting device state. It passed again in 77.90 seconds
with the final trigless-lock exclusion. The earlier coupled measurements
establish stereo PCM, owner LEVEL, reverb and independent release.
The expanded panel test then changed the owner's machine to WERP and verified
that the saved helper request was admitted again. Both physical destinations
carried the WERP machine type during the next held chord.

The test programmed an owner trig, started PLAY and observed it enter the
shared sequence. The stock FUNC+TRIG shortcut set the native mute bit. Over
the next 2.5 emulated seconds, the sequence hit count stayed fixed while a
new keyboard chord dispatched on the owner and the helper. The expanded
test passed in 106.25 seconds including fixture setup. [Elektron Digitakt II manual, §8.5.3 and Mutes shortcuts](https://www.elektron.se/wp-content/uploads/2025/06/Digitakt-2-User-Manual_ENG_OS1.15_250625.pdf)

## Stock sample machine qualification [D]

In the coupled 1.17 emulator, each stock sample machine produced bit-identical
stereo PCM when the owner Sound was sent to the owner or to a borrowed
destination and folded back. The paired renders covered C4 and G4. WERP and
STRETCH rendered separate pitch peaks when three notes were played together.
Changing BARS changed the rendered PCM in WERP, STRETCH and REPITCH while
preserving the owner/helper match. REPITCH's default BARS gave the same
timing-derived pitch for the two notes at both destinations. GRID changed
the played region when its slice setting changed. SLICE transferred the
owner's manually defined slice table, with each tested note selecting the same
start and end on either destination. A native note-off on one of three
physical voices reduced group RMS for each machine while the sibling gates
stayed held. Long BARS kept STRETCH and REPITCH sample cursors busy after their
isolated AMP output fell silent, so quiet-bus evidence alone cannot retire
these leases.

A separate paired render ran all 16 native audio tracks over 781 blocks
per case and enabled the fold for three helpers. The average increase in
active SHARC instructions per block was 1,325 for ONESHOT, 1,325 for WERP,
1,331 for STRETCH, 1,382 for REPITCH, 1,361 for GRID and 1,354 for SLICE.
These counts measure the fold in isolation on the coupled emulator.

## The first trig at START [D]

The DTII 1.17 timer creates subsequent audio and MIDI events at
`0x400d8200` and `0x400d821a`. The first events instead come from a per-track
cache beginning at `0x44f37c94`. Its builders return at `0x400d73b8` for
audio and `0x400d7400` for MIDI. Local START copies 0x6c bytes into each
allocated event at `0x400d6598`. Synchronized START uses the same cache and
copies it at `0x400d6120`. The two loops carry the track index times four in
`D3` and `D2` respectively. Both replay the stack pop and byte load after the
copy. The cached tag belongs to the source track, resolved step and native
pattern, while ownership follows the allocated destination at due time.
The stock cached audio builder stores type 1 at offset zero in
`0x400d6ca4`–`0x400d6caa`; the allocating timer path uses type 0.
The copy and enqueue retain that type. The shared due hook admits type 1
only when it has the transferred START tag.

On the physical DTII, the intermediate image had an audio START tag for
track 8, step 1, pointing to its scheduled pattern and note table. Reading
the cached stock event confirmed type 1 and matching native track data.
The native CANOPY capture observed the first hit, while the shared hit
counter only advanced at step 9. This identifies the due-event type check
as a separate requirement from the cache transfer.

The stock running word at `0x43164f94` is set before the first events become
due. Its published `pp_position` frame can still show stopped. The shared
scheduler samples the native transport and each queue's write position
before draining. A first hit that arrives after a stopped snapshot remains
queued for the running pass. An observed stop still discards outgoing notes.
The existing native due check verifies track and pattern pointers on the
copied record before accepting the tag.

The final physical test isolated CANOPY on track 8 by setting the other
populated track's level to zero. The two panel starts and one USB-clock
start produced their first onsets at 0.5179, 0.5134, and 0.5103 s after
commands scheduled at 0.500 s in each recording. In all three captures,
the first step and the wrapped step 1 had a dominant pitch of 232.18 Hz
from the base Velvet Sound. Step 9 recalled FM preset 001 from the project
pool and produced a dominant pitch of 468.02 Hz on the panel starts and
468.75 Hz on the USB-clock start. The recordings and timing measurements
are under the ignored `dist/track-keyboard/hardware-20261121/final-combined`
directory.

## RELAY playback and recording [D]

RELAY 0.5 gives the owner and one reserved helper separate pitched wet
playback heads. An explicit RELAY group marker in the voice map authenticates
the owner of both heads. Both read the owner's committed tape. The helper
never receives a recorder or an undo position. A take revision retires its
old heads. The committed read path leaves copy-on-write pages unallocated,
including when it crosses implicit silence during an open punch.

On the physical DTII, image `9eb931fe23821e2f` loaded RELAY 0.5 alongside
CANOPY, Overloop, PRISM and remote with zero reported module faults. RELAY
on T2 recorded a two-second take from T1 in a disposable project. T9
selected directly in POOL raised the effective voice count to two. Three
48 kHz USB captures held each note individually and both together. The two
individual waveforms each appeared at a consistent offset in multiple
overlapping windows of the paired capture. The peak absolute sample rose
from about 0.005 for either note alone to 0.0065 for the pair.

## Remaining qualification [O]

CANOPY supplies its own internal polyphony. Other dynamic machines need
their own playback and quiet contracts before POOL can reserve helpers for them.

Check audible owner mute, pan, filter, overdrive and compressor routing with
the device's controls. The physical DSP budget still needs a capture covering
PRISM, RELAY, USB audio, rapid group changes and pattern changes that
immediately need a retiring helper. A sustained loop keeps its old route
for as long as its cursor remains active.
