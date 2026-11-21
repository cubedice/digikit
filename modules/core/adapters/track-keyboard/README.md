# Shared DTII track keyboard

This experimental DTII OS 1.17 adapter adds ARP and KEYS to the native TRIG
page cycle for qualified stock track types. It reuses the native Level/Data
control, page indicators, navigation, and held-value gesture. See the
[proposal](../../../../docs/design/dt2-track-arpeggiator.md) for the full design.

The current integration is an emulator audition checkpoint. Include
`core/track-keyboard` in the emulator patch. Open a
stock sample or MIDI track's TRIG pages, advance to ARP, turn A to ON, then play
the panel keyboard. You can hold multiple input pitches with a running or
stopped pattern. The sample backend plays one native audio voice. The MIDI
backend sends through the native MIDI task and configured channel. A MIDI
track with CHAN turned off sends nothing.

On ARP, H selects a chord and opens NOTES on click. On KEYS, B opens the same
editor. Choose LEARN, play notes on the panel, then click CHRD again and choose
APPLY USER. Open a learned note row to adjust its interval or velocity. The
right side panel can move or remove a note. Learned order and velocities are
kept in the scratch pattern.
The editor and held-step overrides have separate drafts and hold-to-clear
behavior. Scratch settings start over when the audible pattern changes.

The host engine applies individual gates, press order, signed octave spread,
tempo and swing, and handles per-source key ownership and sustain. The
sequencer model can resolve an already validated seed with step locks. The
pattern codec serializes keyboard defaults, learned recipes and sparse locks.
Neither path currently attaches those records to a DTII project. The firmware
adapter dispatches auditions on the UI task, so the highest rates and shortest
gates remain unqualified under load.

The compact CANOPY SOURCE/TONE/ENV layout and a resolved-note input have
separate opt-in endpoints in instrument-play. CANOPY keeps its legacy SRC
and sound behavior in the combined checkpoint image. Migration of old
performance locks, separate pad and arp lanes, live USB/DIN input,
sequencer capture, durable save/copy/undo, and helper voices summed through
their owner remain open.

## Verification

- `track-keyboard.test.ts` checks direct and arpeggiated notes, release ownership,
  sustain, strum, live priority, step recipes, and the pattern codec.
- `track-keyboard-trig.test.ts` checks the TRIG view's stock and added pages,
  control ownership, and held-step editing.
- `tests/panel.manual.device.test.ts` boots the combined experimental image,
  opens ARP from the stock panel, and checks that the shared keyboard owns a
  physical key and produces a native note-on and paired note-off. It learns a
  chord and writes the three OLED frames under `dist/track-keyboard/` for
  review. The regression also rejects a control area with missing text.
- [TRIG view integration and evidence](../../../../docs/findings/368-track-keyboard-trig-view.md)
