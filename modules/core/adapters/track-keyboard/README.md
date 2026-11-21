# Shared DTII track keyboard

The experimental DTII OS 1.17 adapter adds ARP and KEYS after the original
TRIG pages. Build the firmware with `core/track-keyboard`. It uses the stock
pattern keyboard and track routing for panel, USB and DIN input. The
arpeggiator follows the selected pattern tempo when the sequencer is stopped
or playing. MIDI tracks use the configured output channel.

Turn A on ARP to enable the arpeggiator. Its controls set traversal order,
note gate, rate, signed octave spread, swing and restart behavior. H selects
the chord. On KEYS, A enables polyphony and B selects the same chord. C and
D change the voicing and strum. The keyboard drawing distinguishes held
pitch classes from emitted pitches. Stock audio voice groups use H POOL.
With transport stopped, select available, empty ONESHOT tracks as helpers.
Requested helpers belong to the pattern. The menu shows effective
reservations and the resulting voice count. A qualified reservation stays
active when transport starts on the same pattern.

Click CHRD to open NOTES. Choose LEARN, play notes from the panel or an
external keyboard, then click CHRD again and choose APPLY USER. Open a
note row to edit its interval or velocity. Use RIGHT to move or remove an
entry or configure a silent chord selector for a track. In grid recording,
hold a step and use FUNC with the CHRD encoder click to edit its private
recipe. Turning an encoder with steps held sets a lock. Clicking an
encoder without turning clears its lock. The stock recorder chooses the
step for played notes. The extension keeps up to 16 ordered pitches and
their individual timing within each of 32 captured steps per pattern.

The pattern extension also carries track defaults, helper requests and
sparse step locks. It is saved alongside the stock pattern and follows
pattern copy and undo. Native grid track copy, paste, clear and undo also
preserve the corresponding track settings, locks and recorded notes. The
extension uses the v4 record tail and data space in unused native lock records. The native writer fails if the extension
cannot fit with its current native lock count, or if the record tail
belongs to an unknown format.

Use the matching CANOPY module build with this firmware. An inherited
sound continues to use its legacy performer. Its effective controls appear
on TRIG and go into the pattern on the first performance edit. CANOPY's
SOURCE page retains MIX and its native TUNE and LEVEL/DATA handling.
Wavetable and FM sources add their respective TONE and ENV pages. When
both ARP and POLY are enabled, MIX balances four foreground pad voices
and the separately timed CANOPY voice. The shared note scheduler keeps the
legacy 5/Q and 7/Q rates and the original swing percentage on migration.
Patterns with existing native CANOPY sound or parameter locks keep legacy
performance routing so those trigs retain their original behavior.

The full design and release checks are in the
[proposal](../../../../docs/design/dt2-track-arpeggiator.md). The adapter
remains experimental. Stock helper eligibility is restricted to ONESHOT
tracks with transport stopped. Timing under heavy MIDI load and physical
device measurements need separate qualification.

## Verification

- The host keyboard and sequencer tests cover MIDI ownership, sustain,
  ordered notes, strum, timing, pattern records and pool assignment.
- The manual DTII device tests exercise panel controls and capture native
  firmware frames, test simultaneous USB/DIN routing and USB live
  recording, verify that stock audio locks reach generated notes, and
  cover cold project reload, track clipboard and helper reservations.
- CANOPY's SHARC tests check four held pad voices and a separate
  arpeggiated voice with the original blend across pad, wavetable and FM.
