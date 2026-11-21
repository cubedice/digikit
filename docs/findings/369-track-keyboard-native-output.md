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

## Gates for enabling a stock audio helper group [O]

1. Lease only eligible unused tracks through POOL. Keep their stored Sound, mixer and sequencer data intact. Reject a group with a pending recording, a competing owner, or any pattern event that can reach a helper. Release and drain helper tails before old leases are cleared or reassigned. Evaluate track, kit and pattern changes alongside the note ownership transitions.
2. Give the helper the owner's live DSP control values, including resolved locks, before the fixed per-track AMP, level, filter, overdrive, and pan chain. Keep the helper's note, gate, envelopes, and filter history independent. A stereo fold after that chain cannot reproduce changes to the owner's LEVEL, pan or upstream controls by itself. Prove these controls on both owner and helper notes while sustained, and cover the relevant stock Sound types.
3. Publish one complete owner map per audio frame. Fold each valid helper into its owner exactly once before custom send captures and stock sends use the buses. Clear that helper contribution so it cannot enter the master, compressor bypass, or post-master track readers as an independent track. The temporary overlay uses the send entry already claimed by `core/relay-input`. Integrate before its capture, or test the caller at `0x1c3090` with its delay slots preserved. Use the same order with PRISM's private attenuated send and master copies. The master entry alone is too late for capture and sends.
4. Verify live owner mute, native sends, compressor route, and recording/USB output with the device's actual controls. A main-out exclusion can preserve sends by design and does not establish the front-panel mute contract. Test disable, transport stop, rapid group and route changes, project reload, and pre-existing notes and tails. Check unchanged PCM with helper groups disabled and measure DSP headroom with the maximum qualified group.

The backend stays monophonic for stock audio until these gates are met.
