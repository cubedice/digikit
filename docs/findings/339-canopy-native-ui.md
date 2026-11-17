# CANOPY native SRC and panel keyboard (DTII 1.17)

## Native drawing boundary [D]

The OS1.17 SRC draw path is `0x40016eac`, through generic parameter drawing at
`0x4006b0b0`. The encoder resolver receives zero-based parameter indexes, so
the dedicated LEVEL/DATA index is 8. CANOPY now returns native track-level code
10 for that index on both pages. SOUND A/H retain native codes `0xca`/`0xd1`.
The ordinary stock input and draw functions implement their values, modifiers,
focus strings and geometry. No replacement gain multiplier is introduced.

Custom cells have origins x=24+26*column, bottom-up y=27/0. Clears stay within
each custom cell; SOUND A/H, the track column, header and page dots stay native.
The font operand at `0x40069d4e` points to the native parameter font. Continuous
custom values use the device's native encoder template (`0xcc`, CFADE) with a
supplied normalized value, without reading or writing its Sound value.
Discrete values use the native text-box renderer. View byte `+0x190` is the
firmware's momentary show-values state. Native focus helpers are `0x40111e92`
(touch) and `0x40111ec4` (active); custom controls do not own another hold timer.

The initial actual emulator frame compared with the stock Oneshot oracle at
matching default values had zero differing pixels in logical 128x64 regions:
TUNE x23..45/y11..36; source LEV x101..123/y38..63; track column x0..21/y11..63.
This is default-state evidence, not the full min/max/hold/focus matrix. Firmware
and derived frames remain ignored under `out/design/`.

The held-SRC comparison also has zero differing logical pixels in TUNE and
source LEV at matching 0.00/100 values. The left track values in these two
captures differ (105 versus 100), so that pair cannot establish track-level
parity. No tolerance or antialiasing is used when extracting the 128x64 pixels
from the integer-scaled capture PNGs.

## Native Status page [D]

The SDK text page uses stock VerticalMenuView: a narrow title column at left,
read-only rows at right, and native scrolling. Use the short title `STATUS`;
`CANOPY STATUS` collides with the first row. Normally six rows retain the mode/chord,
two complete-note pairs, latch, pedal and arp state; UP/DOWN reveals the last
row. Long mode/chord pairs split across two rows instead of exceeding the native
right column's 18-character width. The menu entry still identifies CANOPY.

Telemetry draining is independent of the 100 ms repaint interval. Waiting
until repaint to drain the bounded FIFO produced persistent LINK LOST in the
first running-firmware screenshot. While Status is open, poll at the UI tick
cadence, just as the SRC path does. Keep stale-data rejection; do not extend the
freshness timeout to mask an undrained queue.

The corrected compiled image shows `PLAY C`, C4/E4/G4, LATCH HOLDING and PEDAL
UP with three keys sounding. The runtime test checks authenticated freshness and
the three-note count after opening the modal, and verifies that SRC does not
repaint underneath it. Actual frame:
`out/design/canopy-native-ui/canopy-native-status.png`.

## Resolved panel notes [D]

KeyboardView's vtable is `0x401f68e0`. Resolved note-on helper `0x4005cee2`
receives `(view, physical_key, track, pitch, velocity, a, b, c)`. Its caller has
already applied the native octave/scale mapping. The native action sweeps the
held-note vector to enforce stock mono audition before calling this helper;
the helper also populates that vector. CANOPY consumes only its admitted track
before those side effects, leaving the vector empty for CANOPY. The caller
still installs its native physical-key release closure to `0x4005c72a`.
No raw TRIG-to-MIDI-note map is added. The native Chromatic upper row has gaps;
the three-note acceptance uses lower-row TRIG 9/11/13.

Track object/type helpers are `0x40035cca`/`0x4005063e`. The selected-track
getter is `0x4004abe8`. Key, detach and update vtable slots are
`0x401f68e8`, `0x401f68fc`, `0x401f690c`; the pinned stock update is an empty
return at `0x4011afd2`. The update observes selection changes handled by other
views. Exit or track change cancels only PANEL ownership. Original handlers
still own stock auditions and their release vector. Hook windows are pinned
by exact current-image hashes in `devices/ports/dt2-1.17.toml`.

KeyboardView remains alive after leaving KEYBOARD. The main controller's call
at `0x40065e12` toggles active state via `0x4004b006`; getter `0x4004af88`
reads that state. The adapter wraps that call and cancels PANEL when it turns
off. The update also observes active-state loss. Detach alone proved insufficient
in the live acceptance test, despite independent key releases working.

PANEL is normalized source 2/channel 0, route 32. It shares the bounded token
allocator and event queue with USB/DIN but stores physical key identity and
captures pitch until release. Only notes and scoped CC120 cross this boundary.
External pedal/bend cannot alias PANEL: route masks exclude bit 32 and panel
bend uses a reserved zero word. The normalized event validator must accept this
source as well as the wire validator; omitting that second change caused the
first end-to-end run to fault after its first accepted key.

## Validation scope [D/O]

Focused host checks: 82 UI/keyboard/status cases; 25 ownership/transport cases;
84 native menu/UI host cases. Focused SHARC checks: 28 CANOPY and 28 packet
cases, including mixed ownership, panel cancellation, independent release,
external pedal/bend isolation, and rejected panel controllers/channels.
Native builds, firmware preflight and compiled running-firmware UI/audio
acceptance pass. The final run includes Status freshness, local-key cleanup,
independent level sweeps, held-note tuning, native held-SRC state and presets.
Physical verification and the wider pixel/modifier matrix remain open; see the
[implementation handover](../handovers/HANDOVER-2026-11-06-canopy-native-ui.md).
The complete CANOPY DSP/instrument component run passed 271 cases with one
skipped; eight focused cases additionally cover PANEL arp bend isolation and
native retuning. The keyboard-exit/glyph host revision passed all ten bridge tests.
These observations have not received an independent image-byte audit, so none
is marked [V]. Local audition is not proof of polyphonic pattern recording.

## USER exit and directional octave spread (2026-11-06) [D]

USER fixes the learned voicing, not the chord selector. SRC B can now leave USER
in either direction while retaining the learned shape for later recall. The
read-only guard and feedback apply only to VOICING. The compiled acceptance
learns a shape, enters ROOT, and turns native encoder B through 10/9/10/11.

ARP C now displays -3 through +3 as an octave spread, not transposition. Zero
keeps the played octave, positive values add octaves above, and negative values
add octaves below. The pad is unchanged. Four notes plus three extra octaves
require at most sixteen candidates; extensions outside MIDI 0..127 are omitted.
As Played retains the original notes first, then extends in the selected
direction. Live changes take effect at the next onset.

CPST v2 retains the 64-byte preset size and stores the signed spread with a +3
bias. V1's total upward octave counts 1/2/3 migrate to spreads 0/+1/+2. Dirty
comparison canonicalizes saved records so migration alone is not an edit.
CPS8 carries the same biased field in both the audio and presentation halves;
CPS6/7 keep their prior interpretation. Full 32-word copy and validation are
required for CPS8; a focused test caught an initial ten-word copy error.

## Physical screenshots and PLAY level investigation (2026-11-06) [D/O]

The physical screen RPC was working. Windows RtMidi delivered the 1194-byte
SysEx reply as 1024- and 170-byte fragments, which `dkscreen` discarded as
separate incomplete messages. It now buffers through F7 before decoding.
Nine tool tests cover complete and fragmented replies; ordinary RtMidi screen
capture subsequently worked on the installed `6c03036ecf861957` image.

Screen readback confirmed CANOPY in PLAY, but EXTERNAL MIDI was OFF. The first
USB note recordings therefore exercised the stock path and are not CANOPY
level evidence. It was temporarily enabled for the later comparison. The sound
had ARP MIX 76/127 (60%, not the initially misread 50%), gate 25%, 1/16 rate,
drift 23, and active delay/reverb/chorus sends of 43/52/19.

Equal-velocity MIDI 60..71 recordings used the same 0.35..0.95-second window
after each note-on. With effects sends off and source level verified at 57,
pad-only RMS varied 1.056 dB, gradually with pitch. Restoring mix 76 while
keeping sends off produced 5.166 dB with non-monotonic changes. These are
whole-device USB measurements through the existing stock filter/AMP chain,
not isolated oscillator levels. Captures had no overflows. The automatic
transient-hit summary is unsuitable for these sustained notes; use the
event-aligned measurements instead. Intermediate captures with an unverified
mix or source level are excluded. Original sends, source level, mix and
external-input setting were restored and read back on screen.

An isolated assembled voice-kernel test over MIDI 48..84 at equal velocity
measured only 0.0160 dB (Velvet) and 0.0236 dB (Reed) sustained RMS variation.
The held pad and retriggered arp instead used independent reset phases. A new
four-pitch test failed on the original code in both direct and protected
execution: a same-pitch arp could oppose its held pad at intermediate MIX.

The correction copies the held owner's phase at same-pitch arp onset, matching
identity and tuned oscillator coefficients. A 128-byte temporary snapshot is
taken before pad rendering because the arp renders afterwards. An onset
deferred until after its own render uses the pad's end phase for the next
block. The instance remains 800 bytes; the snapshot is per synchronous call,
not retained ownership. Octave extensions do not copy the base phase. Existing
independent drift, envelopes, release tails, note gain and mixer law remain.
Phase, full-tail-pool and octave-boundary checks pass; physical post-update
gain qualification is recorded separately in the follow-up handover.

The final package passed all 312 CANOPY V2 DSP tests and compiled native UI/audio
acceptance, then was installed on the DTII as image `366a87b86ab545d2`.
Physical knob screenshots confirm both USER exits and spread -3/0/+3.
USB/DIN input was enabled and saved at the user's request; the preserved sound
and original effects sends were restored after testing.

Post-update USB measurements reduced the note-to-note variation of the
mixed-minus-pad level difference from 4.236 dB to 0.170 dB. However, the common
whole-device response still rises across MIDI 60..71: 6.332 dB mixed and
6.361 dB pad-only. The earlier pad-only recording varied 1.056 dB. Matching
visible controls and correct measured pitches do not resolve that discrepancy;
the remaining stock-processing/carrier-state contribution is still open.
These results support the cancellation fix, not a claim of uniformly loud notes.
