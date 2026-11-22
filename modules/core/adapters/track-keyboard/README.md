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
With transport stopped, choose available empty stock audio tracks as helpers
for ONESHOT, WERP, STRETCH, REPITCH, GRID or SLICE. Check the track rows, then
choose APPLY GROUP to save them together. NO discards the draft. Requested
helpers belong to the pattern. The menu shows effective reservations and the
resulting voice count. A qualified reservation stays active when transport
starts on the same pattern.

Click CHRD to open NOTES. Choose LEARN, play notes from the panel or an
external keyboard, then click CHRD again and choose APPLY USER. Open a
note row to edit its interval or velocity. Use RIGHT to insert, move or remove
an entry, choose a reference note, clear the recipe or configure a silent
chord selector for a track. The APPLY row identifies the track and step
when editing a step recipe. In grid recording,
hold a step and use FUNC with the CHRD encoder click to edit its private
recipe. In NOTES, RIGHT then EDIT STEP opens a list of existing note trigs.
Choose the absolute step with the arrow keys or LEVEL/DATA, then press YES.
Apply or cancel an edited draft before selecting another step. Choosing a
step leaves the pattern unchanged until you apply its recipe. Turning an
encoder with steps held sets a lock. Clicking an
encoder without turning clears its lock. The stock recorder chooses the
step for played notes. The extension keeps up to 16 ordered pitches and
their individual timing within each of 32 captured steps per pattern.

The pattern extension also carries track defaults, helper requests and
sparse step locks. It is saved alongside the stock pattern and follows
pattern copy and undo. Native grid track copy, paste, clear and undo also
preserve the corresponding track settings, locks and recorded notes.
Hold a trig in grid recording and press RECORD, STOP or PLAY to copy,
paste or clear its locks and recipe. Repeat paste or clear on the same
trig within four seconds to undo a shared edit. A clipboard operation that
would exceed recipe capacity leaves the destination intact. The extension
uses the v4 record tail and data space in unused native lock records. The
native writer refuses a save if the extension cannot fit alongside the
current native lock count or if another format owns the record tail.

While switching patterns, the scheduler prepares compatible destination tracks
before the first note is due. It uses that pattern's step settings and notes,
including for early steps with negative microtiming. Held keyboard input
retains ownership across ordinary pattern changes.

Use the matching CANOPY module build with this firmware. An inherited
sound continues to use its legacy performer. Its effective controls appear
on TRIG and go into the pattern on the first performance edit. CANOPY's
SOURCE page retains MIX and its native TUNE and LEVEL/DATA handling.
Wavetable and FM sources add their respective TONE and ENV pages. When
both ARP and POLY are enabled, MIX balances four foreground pad voices
and the separately timed CANOPY voice. The shared note scheduler keeps the
legacy 5/Q and 7/Q rates and the original swing percentage on migration.
Packed CANOPY trig locks travel with the generated phrase. Chord, timing and
blend locks apply to the shared scheduler, and timbre locks reach the DSP
before native modulation. The next unlocked phrase uses the Sound's base
values. Native AMP, filter and FX parameter locks still go through the stock
track. A native Sound lock can recall another CANOPY preset from the project
pool for the generated phrase, including its source family and TUNE. Trig
locks still take precedence. Tracks that reference a Sound without a
supported CANOPY record use legacy performance routing.

The full design and release checks are in the
[proposal](../../../../docs/design/dt2-track-arpeggiator.md). The adapter
remains experimental. CANOPY uses its internal voices, and dynamic machines
that need their own playback state are ineligible for borrowing tracks.
Timing under heavy MIDI load and physical device measurements need separate
qualification.

## Verification

- The host keyboard and sequencer tests cover MIDI ownership, sustain,
  ordered notes, strum, timing, pattern records and pool assignment.
- The manual DTII device tests exercise panel controls and capture native
  firmware frames, test simultaneous USB/DIN routing and USB live
  recording, verify that stock audio locks reach generated notes, and
  cover cold project reload, track clipboard and helper reservations.
- CANOPY's SHARC tests check four held pad voices and a separate
  arpeggiated voice with the original blend across pad, wavetable and FM.
