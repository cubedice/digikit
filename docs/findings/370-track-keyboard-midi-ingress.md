# Shared keyboard MIDI ingress

Digitakt II OS 1.17. **[D]** marks findings from the firmware image, host
tests, and the coupled emulator. **[O]** marks open hardware verification.

## Native entry points [D]

The USB and DIN gatherer at `0x4011f08e` checks its receive mask at
`0x429368ca` before entering the MIDI router. DIN uses bit 0 and USB uses
bit 1. A parsed channel message has source 2 (USB) or 8 (DIN). For
Auto Channel those values are 4 and 16. The note-on handler at
`0x40120944` checks Receive Notes at `0x429368ee`, and the note-off handler
at `0x40120920` checks the same setting.

The note-on router at `0x40120770` uses the selected track word at
`0x42936826` for Auto Channel. Other channels traverse the signed channel
table at `0x42936876`, with 16 track entries and one auxiliary entry.
The stock route reserves MIDI notes 0–15 as track trig shortcuts. The
shared keyboard leaves those shortcuts available, unless an explicitly
configured selector claims the key on a wholly shared route.

| Firmware window | Role |
| --- | --- |
| `0x401208fa` | Per-track note-on call. The stock stack holds seven arguments. D2 carries the original pitch and D3 the destination track. |
| `0x40120754` | Per-track note-off call. The stock stack already holds the source. D2 carries the pitch and D4 the destination track. |

The shared keyboard makes a per-destination decision in these two windows.
On mixed channels the stock router still delivers to its original stock
destinations while shared tracks receive their own events. Native code
keeps the original channel, pitch, and velocity. The adapter queues
shared events for the UI task. Both instruction windows and the selected
track, channel table, and receive flag operands are byte-checked before
patching. [Firmware declarations](../../devices/fw/track-keys.toml)

For live recording an admitted physical note reaches the original
`0x401200f2` call with its fifth argument set. This skips native audition
and keeps the native writer event. The shared queue timestamps that
physical press and release at MIDI ingress. The capture stage pairs these
timestamps with the note committed by the firmware, using its original
step placement. Synthetic notes enter the audio or MIDI worker below
the recorder.

## Ownership and selector [D]

A physical press is identified by port, MIDI channel, and pitch. Its
release follows the original track after changing the active track.
CC64 extends that source's ownership. CC121 resets its pedal, and
CC120 or CC123 clears its outstanding notes. Turning off receive
releases ownership only for the corresponding source. On a track
or project reset the ledger retains the physical release pairing.
The former CANOPY path can still retire its existing key releases
when a track moves to shared keyboard control.

Open NOTES, press RIGHT, and choose SELECTOR. A selector octave uses
the native input route and works on the panel keyboard. A dedicated
MIDI channel addresses the selected track on an unused channel or a
channel already routed to that track alone. Selector presses choose a named
chord by pitch class, and held common tones keep their voices. PLAY
interprets all keyboard input as pitches. LEARN captures the resolved
pitches and velocities from panel, USB, or DIN on the edited track.

## Verification [D]

The host tests exercise paired USB/DIN owners at the 16-pitch limit,
mixed channel fan-out, pedal ownership across an Auto Channel change,
a silent selector that preserves held common pitches, receive disable
and restoration, reserved note-off capacity, and bounded overflow cleanup.
MIDI BUSY appears when the onset queue reaches that reserve. The coupled DTII test
checks that mixed routing creates one stock and one shared audition,
that each input's release reaches its correct track, and that both
ports can own the same pitch. Its USB live recording check observes
the stock writer committing a shared performance recipe.
[MIDI emulator test](../../modules/core/adapters/track-keyboard/tests/midi.device.test.ts)

## Open [O]

Physical USB/DIN routing, latency, and recording verification.
