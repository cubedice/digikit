# Shared DTII track keyboard

The DTII OS 1.17 adapter adds ARP and KEYS after the original TRIG pages of
every audio and MIDI track. It is part of the fork's hardware image
(`canopy-overloop-prism`) and of any image built with `core/track-keyboard`.
The image keeps the pattern storage and the hook sites; the player runs from
`services/track-keyboard.dks`, which the image asks the boot for itself, so
it needs no module: it starts through its image tick once the file is
loaded and plays through `core/keyboard-panel`, the KEYBOARD view handler.
Without the file on the card the DT2 keeps its stock TRIG pages. It uses
the stock pattern keyboard and track routing for panel, USB and DIN input.
The arpeggiator follows the selected pattern tempo when the sequencer is stopped
or playing. MIDI tracks use the configured output channel.

Turn A on ARP to enable the arpeggiator. Its controls set traversal order,
note gate, rate, signed octave spread, swing and restart behavior. Changes
to rate, spread or order apply from the next onset and preserve an open
gate. A sequenced phrase keeps any of those values locked by its seed.
Holding a pad in stock RETRIGS mode pauses generated arp notes for the
selected audio track. Releasing it resumes on the next timed onset.
H selects the chord. On KEYS, A enables polyphony, B selects the same
chord, and C and D set voicing and strum. VCES sets the maximum number of
voices. For ONESHOT, WERP, STRETCH, REPITCH, GRID and SLICE, turn POLY on
and play. AUTO uses the selected track for the first voice and borrows
eligible empty audio tracks for the extra pitches, with transport running
or stopped. Each borrowed track returns to the pool after its release
tail finishes.

A chord of up to four notes without strum plays on the track's own voice
on ONESHOT, WERP, REPITCH, GRID and SLICE: the track keyboard sends its
first note, and the DSP plays the other notes as copies of that voice, so
the chord borrows no track. The notes share the track's filter and amp
envelope. STRETCH, strummed chords, chords of more notes and user chords
with their own velocities still take a voice per note
(docs/findings/375-stock-voice-chords.md).

On the same machines a track also has three hidden voices for further
keys. A key that no track of the group can take while the track's own
note sounds plays on a hidden voice, so the track plays up to four keys
by itself; a saved group's helpers still come first, and keys that
arrive together still let AUTO borrow tracks. Each hidden note fades in and out with its own key,
but the track's filter and amp envelope shape them all: the envelope
opens with the first key and releases after the last. Each hidden
note plays at the level its velocity gives a stock note on the track's
velocity curve (up to 8 times the first key's level).

H POOL shows the automatic voice count and borrowed tracks. A saved
manual group still uses its chosen helpers. To pin a group, stop transport,
tap the numbered TRIGs in POOL, then press YES. Blue is the owner, white
shows selected helpers, and dim shows available tracks. NO discards the
draft. Clear the selections and apply to return a saved group to AUTO.
A track the group cannot borrow shows IN USE (it has trigs or locks in the
pattern, or another claim), or MIDI, CANOPY or RELAY when its machine is
the reason: a MIDI track has no audio voice to lend, and CANOPY and RELAY
play their own.
MIDI tracks show their configured channel, output routes and transmit
budgets. CANOPY shows its internal voice limit.

Click CHRD to open NOTES. Choose LEARN, play notes from the panel or an
external keyboard, then click CHRD again and choose APPLY USER. Open a
note row to edit its interval or velocity. Use RIGHT to insert, move or remove
an entry, choose a reference note, clear the recipe or configure a silent
chord selector for a track. The APPLY row identifies the track and step
when editing a step recipe. In grid recording,
hold a step and use FUNC with the CHRD encoder click to edit its private
recipe. In NOTES, RIGHT then EDIT STEP lights the existing note trigs.
Tap a lit TRIG to edit its step. PAGE moves through banks of 16 steps.
You can also select a step with the arrow keys or LEVEL/DATA, then press YES.
In PLAY, read-only rows show absolute note names from the saved recording,
the native trig, or the held input. The NOTES title shows the track or step
being edited and the sounding arpeggiated note. USER rows show intervals
and velocities.
Apply or cancel an edited draft before selecting another step. Choosing a
step leaves the pattern unchanged until you apply its recipe. Turning an
encoder with steps held sets a lock. Clicking an
encoder without turning clears its lock. The stock recorder chooses the
step for played notes. The extension keeps up to 16 ordered pitches and
their individual timing within each of 32 captured steps per pattern.

The pattern extension also carries track defaults, helper requests and
sparse step locks. It is saved alongside the stock pattern and follows
pattern copy and undo. About two seconds after an edit it is also written
into the firmware's working copy of the project, which a power cycle
brings back, so the settings survive one without a project save
(finding 374). Native grid track copy, paste, clear and undo also
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

CANOPY tracks join in while CANOPY is loaded (its host runs from
`services/instrument.dks`); without it every track is a stock track to the
track keyboard. An inherited
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

RELAY 0.5.1 supports shared ARP and KEYS. Enable POLY to play two pitches
when an empty stock sample track is available. AUTO borrows it for the
second note. The playback heads read the owner's committed stereo take
from its start, pitched relative to C5.
They follow the tape's RATE and MIX, and only the recorded signal goes
through the grouped voices. The recorder still owns punch, undo, and
the manual tape controls. While recording, the heads read the last
committed MAIN. A new take, an undo, or a take change retires the old notes.
Released heads relinquish their helper after the finite native AMP tail.
Earlier RELAY builds retain their own motor and recorder. POOL requires
RELAY 0.5.1, its matching firmware, and the updated DTII machine service.

The full design and release checks are in the
[proposal](../../../docs/design/dt2-track-arpeggiator.md). CANOPY uses its internal
voices. RELAY is the
only qualified dynamic machine that can borrow an empty stock track.
Physical DIN, heavy MIDI load, isolated USB audio, routing and DSP
measurements are recorded in the [native output findings](../../../docs/findings/369-track-keyboard-native-output.md).

## Verification

- The host keyboard and sequencer tests cover MIDI ownership, sustain,
  ordered notes, strum, timing, pattern records and pool assignment.
- The manual DTII device tests exercise panel controls and capture native
  firmware frames, test simultaneous USB/DIN routing and USB live
  recording, verify that stock audio locks reach generated notes, and
  cover cold project reload, track clipboard and helper reservations.
- CANOPY's SHARC tests check four held pad voices and a separate
  arpeggiated voice with the original blend across pad, wavetable and FM.
