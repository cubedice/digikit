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
