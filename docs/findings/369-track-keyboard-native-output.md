# Native notes and a pre-send helper voice fold

Digitakt II OS 1.17. **[D]** records a source trace or emulator observation. **[O]** records an open proof. The reference design is [Track arpeggiator and keyboard](../design/dt2-track-arpeggiator.md).

## Stock note output [D]

The stock keyboard lifecycle wrappers at ColdFire `0x4005cee2` and `0x4005c72a` update the keyboard view's own held-key collection. The shared service enters below those wrappers. An audio note starts through `0x401200f2(track, note, velocity, 0x40, 0, -1, -1, -1, -1)` and releases through `0x401204ec(track, note, 0x40)`. In the isolated 1.17 ColdFire fixture, notes on tracks 1, 9 and 10 reached the stock audio dispatcher `0x40139b54`. Releasing track 9 changed the stock held counts from 1, 1, 1 to 1, 0, 1. The stock coupled audio fixture also rendered a track 1 note with a 1000.5 Hz spectral peak.

The stock MIDI worker `0x4012d4ce` accepts a 64-byte native event record. Word 3 selects an onset (1) or release (2). Word 4 bit 7 enables output. With track 10's kit MIDI bit set and its cached channel at 4, an E4 onset at velocity 96 yielded native TX bytes `03 93 40 60`. The native channel/pitch owner changed from 0 to 10 and returned to 0 after release. The event pool head returned to its initial value. The same release worked after the track's kit MIDI bit was cleared, because the stock MIDI worker matches the saved track owner for an off.

The shared backend submits bounded work in the main task. It orders offs ahead of ons, cancels an onset that has not left the queue, and checks room in the stock worker mail and event pools. Admission checks the kit machine and live MIDI bitmap. Stock audio is assigned one voice. MIDI can use several pitches from a single track and uses the configured stock channel. The stock worker has a single owner per channel and pitch, so the generated-note queue keeps the first owner across a channel/pitch collision.

The MIDI worker's native route mask is at `0x44e3b9e0`, with DIN on bit 0 and USB on bit 1. Generated onsets reserve both messages of their pair before entering the worker. DIN accrues 2,700 bytes/s with a 48-byte burst allowance. USB accrues 12,000 framing bytes/s with a 256-byte burst allowance. A note sent to both ports spends both allowances. Over-capacity onsets are rejected with MIDI BUSY. A release keeps the outgoing mask recorded at onset and does not need more allowance.

The native worker retains pitch/channel ownership and constructs both messages. The append hooks at `0x4012ce44` and `0x4012d00e` use the saved mask for tagged generated messages. Untagged messages take the stock append route. A pending native append is flushed before an explicitly routed generated note to preserve same-pitch ordering. Host tests cover exhausting DIN while USB remains available, pairing releases after an output change, and refilling both allowances. The coupled sequence fixture switched DIN to USB and USB to DIN while a note was held. Each release appeared on its original output only.

### Physical DIN under load [D]

On image `9eb931fe23821e2f`, a disposable 275 BPM pattern played one
sample track and nine independent MIDI arps at 1/80. A temporary
observer read the stock DIN transmitter's queue cursors, eDMA completion
and UART-empty status. We connected MIDI OUT directly to MIDI IN and set
the sequencer input to USB, leaving the DIN receive DMA available for
passive capture.

Over seven seconds of playback, all 15,180 bytes from the completed DIN
transfers reached the receiver. They matched USB byte for byte in the
same order, including all 2,530 complete note pairs. A separate 12-second
run with native clock send enabled drained 26,526 bulk note bytes. USB
received all 4,421 pairs, the DIN queue emptied after Stop, and the
maximum sampled bulk wait count was six bytes.

Six stopped USB input bursts each requested 16 simultaneous pitches on
one MIDI track with POLY and a 16-voice limit. USB output alone delivered
96 complete pairs. With USB and DIN enabled, 80 pairs were admitted
across the same six bursts. The 480 bytes captured at MIDI IN matched
the USB output exactly. After each burst, all admitted notes had matching
releases. The physical transmit queue drained in both route settings.

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
sample slot. The expanded coupled POOL test used the actual owner and
helper and measured both pitches following native PAN, filter type and
frequency, and track overdrive. Excluding the owner from main and send
FX silenced both pitches. Re-enabling only the owner send returned the
group through reverb. The same test exercises native sequencer mute and
readmission after a stock machine change.

The physical DTII USB test leased T3–T5 to T1 and played C4, E♭4, G4, and
B♭4. Each pitch was also captured separately. The four isolated T1
waveforms fit the chord with relative amplitude factors 1.009, 0.994,
0.996, and 0.986, and an 11.3% residual. The isolated T3 output was
digitally silent. With the owner's track level at zero, isolated POST had
zero peak and PRE still peaked at 0.306 / 0.285. With compressor MIX at
127, the same chord peaked at 0.540 / 0.514 with the owner routed to the
compressor and 0.206 / 0.195 with only the owner excluded. The helper
routing settings were held fixed. These captures used DTII USB audio at
48 kHz.

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
its source ends. A stopped ONESHOT cursor with every sample below a half
24-bit sample step is quiescent even if the AMP still holds its last release
coefficient. The quiet mask and its
sequence and check travel in the idle DSP-link ACK cells. An active upload
gets its normal ACK. Admission and reuse require a fresh valid quiet reply.
The draining route remains tied to its owner while a
matched physical note-off is delivered and the voice becomes quiet.

The compiled panel test `pool.device.test.ts` selected T1, enabled
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

Reassigning a helper repeatedly during a held RELAY note on 0.5 left the
new request selected with one effective voice. Releasing the note and
stopping transport left that state in place even after the wet output had
decayed. The read head could render indefinitely because its tape cursor
was independent of the native AMP release. The pool correctly retained the
old route while that cursor remained active.

RELAY 0.5.1 tests the physical track's native AMP record after key-up. A
finite phase-4 tail below a normalized amplitude of 0.0000001 lets the head
return DONE. Stock filtering still runs, and the pool waits for the cursor
and folded bus to go idle. The rig exercises the releasing head and its
held sibling independently, including audible decay, an AHD hold, and an
infinite release. The matched 0.5.1 pack and firmware are required for
borrowed RELAY voices.

The first 0.5.1 physical retest still left T7 reserved. The ColdFire
readback showed that both note releases had completed and T8 was quiet,
eligible, and requested. The DSP quiet acknowledgement was five blocks
old and still excluded T7 more than five seconds after key-up. The stock
arm writes ACTIVE and a pending sample seed. The SDK's old DONE code
cleared ACTIVE and its note-on mark but left the sample seed. A dynamic
machine bypasses the sample stage that would normally consume that seed.

The bound DTII machine service now clears the marked even cursor's stale
seed on DONE and retires the paired cursor for stereo output. It preserves
the native direction byte, neighboring tracks, and an unmarked next
note. All nine SHARC stereo cases passed. The service's ColdFire code and
the OS image remained byte-identical in the hardware A/B.

With image `7feac6215dbc8d90`, RELAY 0.5.1 and the updated
`services/machines.dks`, T6 held a two-voice chord while changing helpers
T7 → T8 → T7 → T8. Each change retired the old helper before granting
the new one. The first readbacks that showed readmission occurred
0.55, 0.55 and 0.65 seconds after the key-up checkpoint. All three runs
used both destinations and the 48 kHz USB capture reported no stream
errors.

A01 then used RELAY T6 with helper T8, while A02 stored a request for T8
on sample track T2. A queued A02 change while the A01 chord was held
preserved T8 as a draining A01 route. T8 became free during A02 with no
new lease. Stopping transport admitted it to T2. The hardware readback
captured all three states.


## Absolute ColdFire idle measurements [D]

The physical observer sampled the idle counter against the free-running
DTCN0 timer while the DSP block count advanced at 1,500 Hz. A bounded
calibration on the same counter and memory alignment timed warm chains
of 1, 8 and 16 copies of the idle loop's increment and branch. The
estimated cost was 2.50–2.57 hardware timer ticks per iteration. The chain
branches across cache lines, so nonzero idle percentages are estimates.
Zero idle iterations directly indicate that the idle task was never
scheduled in that window.

The same 275 BPM test pattern with a sample track, USB audio capture,
MIDI clock send and repeated USB CC1 input produced these results.

| Workload | Estimated idle share in steady windows | Emitted note pairs in ten seconds |
| --- | --- | --- |
| Nine MIDI tracks, ARP off | 21.3–21.8% | 207 |
| Three MIDI arps at 1/80 | 0% | 2,752 |
| Nine MIDI arps at 1/80 | 0% | 3,497 |

All pairs completed and the audio capture reported no stream errors.
In a separate USB-only run with MIDI clock send disabled, lowering the
three arps to 1/16 left an estimated 13.2–15.5% idle time. All 534 note
pairs completed in eight seconds and the audio stream reported no errors.
The 1/80 three-arp run returned to about 29% idle within roughly five seconds
of USB MIDI Stop. In the nine-arp trial, the first five seconds of the
stopped follow-up still showed zero idle. A later stopped sample recovered
to about 29% without a reboot or ARP change. These measurements put the
1/80 stress cases at the ColdFire ceiling even though the DSP and MIDI
release paths kept up.

## KEYS display refresh [D]

The keyboard drawing read the current pitches when the native TRIG page
repainted. In the DTII 1.17 coupled emulator, accepted panel, USB and DIN
notes produced no key highlight during the first 150 ms on an idle KEYS
page. Moving to the active pitch state did not schedule a repaint.

The UI task now compares the held and sounding pitch-class masks after the
note worker runs. It marks the visible KEYS view dirty when either mask
changes. The native drawable list resolves the view for that request, so
opening a modal does not leave a pointer to the underlying page.

The same emulator fixture measured these times from input to the first
observed frame with the expected key highlights.

| Input | Note-on | Note-off |
| --- | ---: | ---: |
| Panel | 15.2 ms | 30.3 ms |
| USB MIDI | 64.4 ms | 60.6 ms |
| DIN MIDI | 70.1 ms | 60.6 ms |

The test reads the keyboard pixels and the native backend's note ownership
without forcing a paint. These measurements are in emulated time.

## Manual RETRIGS priority [D]

On DTII 1.17, the stock performance call at `0x4005d4a6` reaches
`0x4005cee2` and returns to `0x4005d4aa`. The shared adapter checks that
return address and stock Trig Mode 2 from `0x4004d8da`. Other callers can
still audition notes or record the step through the same stock entry. The
original key-up closure at `0x4005c8cc` calls `0x4005c72a`. Both note entry
points already pass through Instrument Play.

The physical key and its owner define the gesture. While the key is held,
the owner's shared arp releases its current note and skips new onsets.
Its clock keeps advancing. The original key-up restores output at the
next scheduled onset, even if track focus has changed. Project reload or
a service interruption clears stale ownership. Host tests verify that a
second track's arp and the owner's direct note lane continue. The coupled
DTII fixture sets the stock RETRIGS mode directly, then holds and releases
native TRIG 9. It observed the owner release its prior arp note, suppress
new output for 2.1 seconds, and resume after the original key-up. The
second audio arp continued during the hold.

## Combined DSP load [D]

A disposable hardware project ran a four-pitch sample group, a MIDI arp,
RELAY playing a committed take, and PRISM in LIVE with all 16 tracks
selected, 125 ms, and MIX 58. Health reported 68% DSP time on average and
70–72% peak across four consecutive windows. The 6.7-second USB audio
capture reported no stream errors. The percentage estimates time in the
DSP block handler at the configured 1 GHz clock. The stress test above
measures ColdFire idle time separately. The USB PCM, Health frames,
and stopped input timing are under the ignored
`dist/track-keyboard/hardware-20261121` directory with the `closeout-`
prefix.

## Automatic helper pool [D]

On the DTII 1.17 coupled emulator, enabling POLY and playing one MAJ root
started the owner on T1 and borrowed T6 and T10 for the other two pitches
with the default POOL settings. The test placed a sequencer trig on T2 and
verified that the allocator left that track independent. The same three
DSP sample lanes activated with transport stopped and running. After key-up,
the helpers returned once the DSP reported their audio buses quiet.

The preparation check uses DSP quiet to keep a pending group together.
Admission still checks the stock event queue before claiming a track. The
onsets wait for the DSP to echo the new route. The dispatch test covers the
stock owner fallback when the route cannot be confirmed within 120 blocks.
In the RELAY SHARC rig, the automatic owner played a pitched take from its
own head and accepted the second head when a helper route was present.

## ARP phase under short gates [D]

The scheduler used the note's gate deadline as its threshold for moving
the next onset to the current poll time. In a 120 BPM host test at 1/16
and GATE 1%, a poll 2 ms late shifted the following onset from 250 to
252 ms. The scheduler now keeps the original time grid and swing phase.
It advances across elapsed slots without replaying them. A late onset
receives a gate bounded by the following slot.

The coupled DTII 1.17 fixture programmed a single long trig on T10 and
counted its 1/32 arpeggio against 16 committed native steps at 102 BPM
with GATE 1%. The older scheduler dispatched 25 of the 32 subdivisions
in the final A/B run.
The fixed scheduler dispatched all 32 in order and emitted 32 releases.
The native send entry arrived 0.75–33.10 ms after its subdivision, with
no growing offset across the pattern.
These measurements use the emulated device clock at dispatch.

## Qualification scope [D]

CANOPY supplies its own internal polyphony. Other dynamic machines need
their own playback and quiet contracts before POOL can reserve helpers for
them.

Owner pan, filter, overdrive, mute and sends were measured in the
coupled emulator. The physical unit covered isolated PRE/POST USB
output, compressor routing, RELAY helper reassignment, and a queued
pattern change that requested the same retiring helper.

## Short recorded chords and live recording

**[D]** In the host run using the production native dispatcher and pool allocator, a recorded PLAY seed stored
60, 64 and 67. The first pitch started immediately. The other pitches were scheduled 14 ms later with gates ending
25 ms after the seed. Preparing quiet helper tracks takes at least 24 blocks (16 ms). The scheduled note-offs
cancelled both pending pitches before their helpers were ready. A longer version of the same roll reached all three
voice destinations, confirming that recording had stored the additional notes.

The direct-poly sequence clock now waits while a recorded audio onset has an available helper in preparation.
The wait ends when the candidate is lost, the onset is dispatched, or its existing 120-block deadline expires.
Stop still cancels pending notes. Host checks cover the short roll, no eligible helper, transport stop and ARP timing.

The AUTO pool can now borrow empty, quiet tracks during live recording. Candidate filtering excludes tracks with
pending stock note traffic and preserves the selected, active, musically configured and reserved tracks. This lets
incoming MIDI audition a chord while stock recording retains its placement.

**[D]** On the coupled emulator running image `6fd330661efae5b0`, AUTO used two eligible helpers in each of
three cases: stopped panel input, running panel input and MIDI input during live recording. The populated
track stayed independent and the live recorder committed a trig before the helpers returned to the pool.

The DTII panel keyboard's live recorder and audible audition travel through separate calls within the stock note
action. The panel adapter retains the original key-down and key-up code for recording. During those calls, it
filters only the audition that matches the claimed track, pitch and panel source. The shared note engine delivers
the audible chord and releases before the matched stock callbacks run. Host tests cover note-up after changing
recording mode or moving to a different track.

**[D]** The coupled emulator on image `821ef4c23c807b81` confirmed three actual voices and a recorder commit
during panel live recording. Stopped and running panel play and USB live recording also passed. The populated
track remained independent, and the helpers drained after release in all four cases.

## Rapid One Shot chord retriggers [D]

In the host test, a second chord arrived while both released sample helpers were still ringing. The dispatcher
sent three releases and the next root in one batch. The following pool update moved both helpers to draining,
which prevented the next two attacks. AUTO lets released helpers drain and can reuse a still-routed sample
lane for a fresh chord from the same owner in the same pattern and kit. Selecting a borrowed track takes it
out of reuse while its previous tail drains.

The running test also exposed a DSP quiet check that required exact zero after the sample cursor stopped.
A pre-fold trace captured both inactive helper buses with residual peaks of `2.11e-6` and `4.77e-8` after
the old test's six-second deadline. Over the next nine snapshots both dropped below `1e-9`. The DSP now
accepts magnitudes below `2^-24` while still checking both native sample and pending-seed flags.

In the coupled DTII emulator, image `430154c8ca5a4367` played only the root on the second panel press while both
helper tails were still active. Image `7483a69156b60e0b` reused the same two helpers and activated all three stock
voice destinations for the second chord. After release, both quiet helpers returned to the pool. The running
transport case also returned both helpers.
