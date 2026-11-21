# Native notes and a pre-send helper voice fold

Digitakt II OS 1.17. **[D]** records a source trace or emulator observation. **[O]** records an open proof. The reference design is [Track arpeggiator and keyboard](../design/dt2-track-arpeggiator.md).

## Stock note output [D]

The stock keyboard lifecycle wrappers at ColdFire `0x4005cee2` and `0x4005c72a` update the keyboard view's own held-key collection. The shared service enters below those wrappers. An audio note starts through `0x401200f2(track, note, velocity, 0x40, 0, -1, -1, -1, -1)` and releases through `0x401204ec(track, note, 0x40)`. In the isolated 1.17 ColdFire fixture, notes on tracks 1, 9 and 10 reached the stock audio dispatcher `0x40139b54`. Releasing track 9 changed the stock held counts from 1, 1, 1 to 1, 0, 1. The stock coupled audio fixture also rendered a track 1 note with a 1000.5 Hz spectral peak.

The stock MIDI worker `0x4012d4ce` accepts a 64-byte native event record. Word 3 selects an onset (1) or release (2). Word 4 bit 7 enables output. With track 10's kit MIDI bit set and its cached channel at 4, an E4 onset at velocity 96 yielded native TX bytes `03 93 40 60`. The native channel/pitch owner changed from 0 to 10 and returned to 0 after release. The event pool head returned to its initial value. The same release worked after the track's kit MIDI bit was cleared, because the stock MIDI worker matches the saved track owner for an off.

The shared backend submits bounded work in the main task. It orders offs ahead of ons, cancels an onset that has not left the queue, and checks room in the stock worker mail and event pools. Admission checks the kit machine and live MIDI bitmap. Stock audio is assigned one voice. MIDI can use several pitches from a single track and uses the configured stock channel. The stock worker has a single owner per channel and pitch, so the generated-note queue keeps the first owner across a channel/pitch collision. Physical MIDI port tests and saturation measurements remain open.

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

The interactive pool admits stock ONESHOT owners and helpers. POOL checks the
selected pattern, the live working pattern, the track machine, stock note and
trig data, and per-step parameter-lock counts. Start a new lease while stopped
with the recorder idle. An eligible group can retain the same lease when the
same pattern starts playing. A conflicting assignment fails as a group. The
reserved destination uses the owner's active Sound and native lock context.
Its original kit and pattern records are left intact.

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

## Remaining qualification [O]

Validate WERP, STRETCH, REPITCH, SLICE, GRID and stateful machines before
expanding audio-track eligibility. Check audible owner mute, pan, filter,
overdrive and compressor routing with the device's controls. Qualification
also needs a maximum-group DSP budget and a higher-load capture covering
PRISM, RELAY, USB audio, rapid group changes, and pattern changes that
immediately need a retiring helper. The pool keeps the old route during a
sustained loop for as long as its cursor remains active.
