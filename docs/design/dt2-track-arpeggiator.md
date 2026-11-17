# DTII track arpeggiator proposal

Status: design for review, 2026-11-13. Implementation target: DTII OS 1.17.
This document specifies proposed behavior, not an implemented feature.

Add an arpeggiator to every track, before its selected audio machine or MIDI
output. Put its controls beside retrigs in the TRIG page family. Keyboard
playing, learned chords and sequenced notes all feed the same note generator.
Changing machines does not require learning another arp UI.

The main payoff is one playable arp for samples, CANOPY and external synths.
Confidence is high in the note-order and interaction design; confidence in
integration is moderate until the audio/MIDI event dispatch and pattern-storage
boundaries are proven on 1.17. CANOPY's working arp establishes musical behavior,
not a ready-made universal event scheduler.

## Play it

1. Select any track and open its **TRIG > ARP** page. Turn ARP on.
2. Hold notes on the DTII keyboard or a USB/DIN keyboard. The selected track
   arpeggiates those notes, including when transport is stopped.
3. Set RATE, ORDR and OCTS. In AS PLAYED, pressing E, C, G gives E, C, G;
   UP gives C, E, G. OCTS +1 repeats the sequence one octave above.
4. Press **H, NOTES** to select a chord shape or learn a custom ordered set.
   A saved USER shape can then be transposed by playing a single root key.
5. Record the input notes, or place a root/chord trig. Its native note length
   determines how long the sequenced arp phrase runs.

No DigiKit settings visit is needed for these actions. There is no latch or
local-tempo control. A saved shape does not sound without held input or an
active sequencer phrase.

## Relationship to retrigs

Keep the stock retrig/portamento page intact and add ARP immediately after it
in the TRIG page cycle. On audio tracks this is TRIG page 3. On MIDI tracks,
append ARP to their existing TRIG pages without inserting irrelevant audio
retrig controls. The page header names ARP, so access does not depend on
memorizing the page number.

This integrates the controls into the familiar repetition workflow without
displacing portamento. Turning ARP off restores normal stock behavior. Turning
it on makes the arp own repetition for each eligible input event: saved RTRG
settings remain intact, but are visibly inactive for that event (`ARP ACTIVE`
on the retrig page). Arp output
must never enter the retrig generator or the arp input again. A one-note arp
also provides MIDI note repetition.

The native RETRIGS performance gesture explicitly bypasses ARP and uses stock
retrigs. While held it temporarily takes over that track's audition, marked
`RETRIG`, without changing saved ARP settings. On release, resume a still-valid
arp phrase. Use KEYBOARD mode to play arp notes and the ARP page to change its
rate; do not silently turn the existing retrig-rate TRIG layout into a note
keyboard. This bypass is for the manual gesture, not sequencer seed events.

The public [DTII OS 1.15A manual, sections 10.5 and 11.3](https://www.elektron.se/wp-content/uploads/2025/07/Digitakt-2-User-Manual_ENG_OS1.15A_250708.pdf)
documents audio-only retrigs and the shared retrig/portamento page. This is an
interaction reference; all new hooks must be resolved on OS 1.17.

## Main ARP page

Use the native track header, parameter cells, font, focus behavior and
show-values presentation. Dedicated LEVEL/DATA retains its normal track-level
function. There is no duplicate volume or tuning control here.

| Encoder | Label | Behavior |
| --- | --- | --- |
| A | ARP | OFF / ON; can be locked per trig. |
| B | RATE | Pattern-tempo note division, using the familiar retrig divisions. |
| C | ORDR | AS PLAYED / UP / DOWN / UPDN / RANDOM. |
| D | OCTS | -3 through +3 additional octave layers; 0 = played register only. |
| E | GATE | 1–100% of the interval to the next arp onset. |
| F | SWNG | 50–75%; 50 is straight, applied to alternating arp intervals. |
| G | RST | PHRASE / PATTERN / CONTINUE. |
| H | NOTES | Press to open note/chord editor; value shows PLAY, chord name or USER. Turning does nothing. |

Layout schematic, not a pixel-accurate firmware render:

```text
 T01                      ARP 3/3
   ARP     RATE     ORDR     OCTS
    ON     1/16      UP       +1

     C3  E3  G3  C4  E4  G4
             ^

  GATE     SWNG      RST     NOTES
   60%      50     PHRASE    PLAY>
```

The central strip previews traversal, with a cursor at the generated note;
it is not a second editable step sequencer. Long sequences scroll around the
cursor. With no active phrase it shows the saved shape or `HOLD NOTES`.
Show-values mode uses the same native numeric presentation as other TRIG
pages; the illustration is secondary to legible controls.

Enumerated choices use CANOPY's deliberate travel (four raw encoder ticks per
choice, no acceleration). RATE shows musical fractions, OCTS signed integers,
and the focused ORDR value can show its full name. Encoder turns while holding
a sequencer step edit its locks; pressing NOTES while holding a step opens that
step's note recipe. An action press must never create a parameter lock.

Defaults: OFF, 1/16, AS PLAYED, 0 octaves, 50% gate, straight swing, PHRASE reset,
PLAY input.

## NOTES: play, learn and edit

The editor clearly separates the **input set** from the currently **generated
note**. It opens on the current choice, using native list/editor styling.

```text
 T01                NOTES DRAFT
 CHRD  PLAY          LIVE 3/16

  1:E3   2:C3   3:G3
        OUT E4       OCT +1

 A CHRD  B LEARN  C ITEM  D NOTE
 E VEL   F REF    G --   H --
```

This is a hierarchy/controls schematic; the finished editor must fit these
elements to the display using native widgets rather than shrinking the font.
In PLAY, ITEM/NOTE/VEL are read-only views of held inputs with absolute pitch
names. In USER, rows and NOTE show signed semitone offsets, for example
`1:+0  2:+4  3:+7`; the separate OUT readout shows the actual generated pitch.
Generated chord choices show their resulting intervals.

- **CHRD PLAY:** use the exact held or sequenced pitches, including their
  inversions. A chord-name readout may describe them, but recognition must not
  reorder or substitute them.
- **CHRD types:** major, minor, sus2, sus4, diminished, augmented, dominant 7,
  major 7 and minor 7 expand a root. With several root keys held, the most
  recently pressed key is the root; releasing it returns to the previous one.
- **CHRD USER:** transpose a stored ordered interval set from the root key.
  USER and the standard shapes are immediately selectable; no hidden PLAY
  mode may trap the CHRD control.
- **LEARN:** start an explicit capture draft. Play a chord together or enter
  notes successively. Key releases stop audition but do not erase draft
  entries. Order and velocities are captured; up to 16 entries are retained.
- **Review:** ITEM selects an entry, NOTE edits its semitone offset, VEL its
  velocity. REF changes the captured reference pitch while preserving captured
  pitches by rebasing their offsets. Initially this is the first captured
  note. REF is the stored learning anchor, not a second live root control:
  during USER playback the played root key replaces this reference.
  RIGHT opens native extra actions: insert, remove, reorder and clear.
- **Save:** YES saves the editor's choice/set and returns to ARP. A learned or
  manually edited set becomes USER. NO backs out and discards the draft,
  preserving the original choice/set. No extra confirmation.

The main ARP page's encoder edits are immediate, as on native parameter
pages. The NOTES editor always works on a draft, for track defaults and for
held steps alike. Edits can be auditioned immediately, but NO restores the
previous saved data. YES commits and closes the whole flow. Mark the editor
`DRAFT`, with `LIVE`, `LEARN` or `USER` as its source/status. A learned set is
ordered interval data, not an automatic hold. The learn screen does not need
a sustain pedal.

USER data is local to the pattern/track, with optional per-trig overrides.
Pattern/track copy and paste must include it. A new trig uses track defaults
unless it has its own recipe. Editing the track's USER set must not mutate
independently saved per-trig recipes.

## Exact musical behavior

### Ordering and range

- AS PLAYED preserves first-admission order. Releasing a pitch fully and
  pressing it again puts it at the end. Simultaneous recorded note entries
  retain their stored order rather than being sorted automatically.
- UP/DOWN sort the complete octave-expanded pitch set. UPDN travels that full
  span without repeating end points: C, E, G, E when OCTS is zero.
- RANDOM chooses across the expanded set, avoiding immediate repeats when
  more than one candidate exists. Use reproducible per-track random state
  for deterministic recording/replay tests.
- AS PLAYED finishes the press order in the original register, then repeats
  it in each added layer. UP/DOWN/UPDN follow pitch across all layers. UP on
  C3/E3/G3 with +1 is C3/E3/G3/C4/E4/G4; DOWN reverses that sequence; UPDN
  returns through E4/C4/G3/E3 before repeating. OCTS -1 instead gives the
  range C2 through G3, and UP starts at C2. Thus OCTS determines available
  registers, while ORDR determines their traversal.
- OCTS +3 adds three layers above; -3 adds three below. A single setting never
  adds layers on both sides. Omit added pitches outside the destination's
  legal note range instead of clamping several notes onto one boundary pitch.

Target capacity is 16 distinct live pitches or 16 ordered USER entries, at
most 64 candidates after octave expansion. Duplicate live pitches occupy one
candidate but retain separate input ownership; releasing one keyboard cannot
release a pitch still held by another. When full, ignore additional live
admissions with a visible FULL indication and still pair their eventual
releases correctly. Do not evict a held note silently.

### Clock, length and reset

RATE follows the selected pattern's BPM. There is no local BPM or clock-source
selector. The initial division list follows the native retrig choices:
1/1, 1/2, 1/3, 1/4, 1/5, 1/6, 1/8, 1/10, 1/12, 1/16, 1/20, 1/24,
1/32, 1/40, 1/48, 1/64 and 1/80. A rate of 1/4 means one note per quarter note.

While stopped, keyboard phrases run at the selected pattern tempo. Starting
transport synchronizes to the committed pattern clock boundary. Missing
external clock while transport is running must not masquerade as a stopped
transport and silently free-run. Stop scheduling new onsets and release owned
notes safely if a running clock becomes invalid.

RST controls the traversal cursor:

- **PHRASE:** restart at the first candidate when an empty input becomes
  active, or a new sequencer seed starts. Adding a held key does not reset it.
- **PATTERN:** reset on each committed pattern-cycle boundary. For a new seed,
  derive its candidate index from the number of nominal RATE intervals since
  that boundary, modulo the current traversal length. While stopped, with
  no committed pattern cycle, use PHRASE behavior.
- **CONTINUE:** preserve cursor across phrases and pattern changes, wrapping
  against the new candidate count. It does not preserve notes or latch input.

RST controls the note index, not the seed's actual onset time. A new phrase's
first note is due at the eligible seed/key event, including a microtimed trig;
its subsequent intervals start there and follow RATE/SWNG. Do not catch up
missed notes with a burst. CONTINUE remembers the next index while input is
empty; it does not advance it silently. PATTERN instead derives the index
from the running pattern grid on re-entry. For an E/C/G press-order set, a
fresh phrase starts E under PHRASE; CONTINUE resumes C if E was the last note;
PATTERN starts G if two nominal RATE intervals have elapsed since the cycle
boundary. A boundary reset during an active phrase sets the next index to
zero without creating an additional onset.

The native **TRIG page 1 note LEN** gates the whole sequenced phrase; this is
not the retrig page's velocity-curve LEN. Preserve native note-length/track
scale semantics. GATE gates individual generated notes. Exclude an onset at
the exact phrase endpoint and clip its preceding gate to that endpoint.
Infinite note LEN runs until replacement, sequencer mute/stop, pattern switch
or another explicit cleanup event. At 100% GATE, send the previous note-off
before the next note-on, including repeated instances of the same pitch.

Keyboard release ends that key's ownership. If the currently generated note
loses its last owner, send its note-off immediately rather than waiting for
the rest of its gate; the next surviving candidate waits for the next onset.
A normal sustain pedal can extend
that ownership, but no pedal is required. For ARP-owned keyboard input,
consume CC64 into the input ledger: do not also forward it blindly and sustain
every generated MIDI note on the receiving synth. Explicitly sequenced output
pedal CCs retain their native meaning and may intentionally override audible
gate length on that synth. The native AMP
envelope and external synth release still control how sound decays after
note-off. GATE cannot force a one-shot sample to shorten if that machine's
native playback ignores note-off.

Apply track swing/microtiming once to the seed. Apply SWNG only to subsequent
arp intervals, keeping each pair's total duration constant. Do not run the
children through track swing a second time. RATE/OCTS/ORDR edits take effect
at the next onset; a gate already in flight keeps its scheduled note-off.

## Sequencing and live performance together

An audio trig supplies its resolved note/chord recipe and native sound/lock
context. A MIDI chord trig supplies its recorded notes in PLAY mode. Each
eligible new trig replaces that track's previous sequencer phrase. The live
keyboard temporarily takes priority over that phrase; advance the sequencer
phrase's cursor and clock without output during this override. On final live
release, resume a still-valid sequencer phrase at its current phase, otherwise stop.
Raw keyboard audition and generated notes must not sound twice.

Evaluate trig conditions/probability once for the seed. Each child inherits
the seed's sound and resolved locks for that phrase; it does not reroll the
condition or resend program changes. Later lock-only events can update
supported parameters. On phrase end or replacement, retire the old context
without leaking its locks into unrelated events. ARP enable and its musical
controls are per-trig lock targets; NOTES is an action, not a lock value.

The arp emits one note event at a time. Existing machine behavior determines
voice stealing, sample tails, portamento and amplitude envelopes. This does
not add polyphony to stock sample machines. MIDI uses the track's configured
channel/output ports and its existing controller behavior. Per-key velocity
is retained; panel keys use their native trig velocity. Generated chord notes
inherit root velocity unless a USER entry supplies an explicit one.

Default recording records **input notes/chord recipes**, not every generated
note. Live recording, step recording and held-trig editing must all preserve
order and support playback without the keyboard attached. Turning ARP off
plays the ordinary native seed fields: an audio track's root note or a MIDI
track's native chord (up to four notes). The extended ordered recipe remains
saved but inactive; this does not create a 16-voice stock chord. The editor
must distinguish that native seed from the extended recipe.

The release target is 16-note ordered recipes, including recording. Native
four-note storage alone does not satisfy that target. Use native note fields
where compatible, plus an explicit versioned extension for ordered recipes
and references. Never silently truncate to four notes. A bounded recipe pool
must fail an oversized/full edit visibly and atomically, leaving the old
step intact. The capacity and copy/save behavior must be proven before
promising an implementation is complete.

Do not automatically bake the arp output into the sequencer. An explicit
future `RENDER TO TRIGS` action can do that, with preview and capacity checks.

## CANOPY and track lifecycle

Universal ARP must become the single note-generation owner when enabled on
CANOPY. Feed its resulting pitches into a direct single-note performance
path; bypass CANOPY's own chord expansion, strum and internal arp for those
events. This path must sound independently of CANOPY's saved PAD/ARP MIX;
otherwise a sound set fully to its internal arp could become silent.
Preserve the saved CANOPY sound settings and restore their ordinary behavior
when universal ARP is disabled. Show `TRACK ARP` on the internal CANOPY arp
view while it is overridden; never leave two apparently effective arps.

On ARP disable, source/route/channel/machine change, project load or reset,
release the notes owned by the old configuration before adopting the new one.
Transport stop ends sequencer-owned phrases; keys still held can continue in
stopped keyboard mode. Ordinary track mute follows the native distinction
between sequencer muting and keyboard audition. A native panic/stop-all action
flushes both kinds of ownership and requires fresh input to restart them.
Pattern switches use destination arp settings; physically held keyboard notes
remain input, subject to the selected RST mode. Never carry old sequencer
notes into a new pattern accidentally.

## Implementation boundaries

```text
panel keyboard   USB/DIN keyboard   sequencer note/chord + locks
       \              |                       /
          ownership and per-track input arbitration
                           |
                  shared track arp scheduler
                    /                    \
        native audio note dispatch    native MIDI event queue
                  |                         |
           selected machine          track channel / ports
```

1. **Ingress and ownership:** adapt the resolved keyboard note path, input
   ledger and sequencer events. Preserve native scale/octave mapping. Shared
   ownership must compose with CANOPY, not compete for the same messages.
2. **Clock and generator:** a bounded per-track state machine owns traversal,
   octave expansion, swung timing and paired note-offs. Reuse CANOPY's clock
   semantics rather than its synth-specific DSP voice generation.
3. **Dispatch:** establish current-image audio and MIDI event/task boundaries.
   Use native note scheduling and a bounded MIDI queue. Never call USB MIDI
   transmission from an audio interrupt: its low-level sender can block.
4. **Persistence and locks:** define versioned pattern/track/step data and a
   bounded table of ordered recipes. Prove save/load, copy/paste, clear,
   reload, undo and pattern-chain transitions. Allocate lock identifiers
   without collisions with CANOPY or stock parameters. Do not assume unused
   pattern bytes or the existing CANOPY chord packing can hold this feature.
5. **UI and CANOPY integration:** implement the one-page controls, editor and
   explicit single-generator ownership. Existing CANOPY USER learning sorts
   intervals, so its schema cannot preserve AS PLAYED without an extension.

DIN MIDI has a shared bandwidth ceiling. Sixteen tracks at 1/64 and 120 BPM
would use roughly 3,072 bytes/s for three-byte note-on/off pairs before clock
and controllers, nearly the 3,125 bytes/s wire capacity. Account for this
before queueing. Reserve note-off capacity, prioritize due note-offs, and
reject excess new onsets with a visible `MIDI BUSY` indication rather than
building an ever-growing queue. Do not advertise unrestricted maximum-rate
arps on all MIDI tracks at once. USB and DIN routes need independent budgets.

CANOPY's onset ramp is an instrument fix, not a universal audio de-click
solution. Test stock AMP retrigger behavior at zero attack and short gates;
any additional ramp must be designed for that path without changing every
machine's attack sound. External synth envelope behavior is outside DTII.

## Delivery and acceptance

First prove one audio track and one MIDI track using the same generator and
keyboard pool. This is the decision gate for the universal design; building
the editor first would not establish the hardest integration boundaries.

Then deliver persistence/recording, full UI, CANOPY coexistence and multi-track
scheduling. Release acceptance includes:

- Audio-machine and MIDI-track arp from panel, USB and DIN; no raw-note double
  audition; exact order/octave examples above, including duplicate ownership.
- Stopped play, transport start/stop, internal/external clock, clock loss,
  pattern switches and all reset modes; one onset at coincident boundaries.
- Short/long gate, repeated same pitch, release tails, conditions,
  microtiming, swing, portamento, sound locks and lock-only events.
- Learn/save/cancel, changing PLAY/USER/chord types in both directions, and
  deliberate encoder travel with stock show-values/focus behavior.
- Live and step recording, held-trig edits, more than four notes, pattern
  save/reload/copy/undo, recipe capacity exhaustion and no corrupt partial edit.
- CANOPY internal arp restored after universal ARP is disabled; no recursive
  note generation; route/channel/machine/project changes leave no stuck notes.
- Busy multi-track MIDI plus clock/controllers with bounded latency and
  guaranteed release capacity. Compare audio timing and CPU overhead against
  the feature disabled before making a performance claim.

## References and reusable evidence

- [Digitone II OS 1.12 manual, section 9.7](https://www.elektron.se/wp-content/uploads/2026/10/Digitone-2-User-Manual_ENG_OS1.12_261008.pdf):
  inspiration for press-order traversal, octave cycling and a direct arp
  editor. Its own arp is documented as unavailable on MIDI tracks; this
  proposal deliberately requires a different, track-wide dispatch design.
  Per-step offsets/rests like DNII can be a later phrase-editor extension;
  first implement the keyboard/USER pool and existing sequencer editing well.
- [CANOPY UI review](canopy-ui-review.md): deliberate enums, ARP controls and
  clear separation of played notes from generated notes.
- [CANOPY pattern-clock finding](../findings/148-canopy-pattern-arp.md):
  pattern BPM, stopped clock behavior and instrument-specific onset ramp.
- [CANOPY recording finding](../findings/149-canopy-recording.md): native
  four-note records, distinct recording paths and packed chord storage.
- [Native keyboard/UI finding](../findings/139-canopy-native-ui.md) and
  [instrument-play adapter](../../modules/core/adapters/instrument-play/README.md):
  resolved panel pitches, ownership and current learned-shape limitations.
- [UI rules](ui-rules.md): native lists, action completion and cancel behavior.

These references support the design choices. They do not establish the new
universal retrig/arp scheduler, MIDI queue integration or storage format as
implemented or hardware-tested.
