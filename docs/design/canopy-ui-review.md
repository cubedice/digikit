# CANOPY: native DTII interface proposal

## Review proposal: three groups, four SRC pages (2026-11-12)

**Status: implemented as a local firmware candidate; physical-device and full
pixel-comparison acceptance remain open.** The user approved
four pages arranged into three groups: **KEYS + ARP**, **SOUND**, and **ENV**.
This section supersedes the historical layout recommendations below. It keeps
all current synthesis controls, removes duplicate performance controls from the
main view, and gives otherwise unused cells a small diagram that explains the
sound. It does not change the synthesis, modulation, preset, or trig-lock
behavior.

The physical SRC cycle is **KEYS → ARP → SOUND → ENV → KEYS**, for every source.
KEYS and ARP are the playing group; SOUND chooses and shapes the oscillator;
ENV changes its timbre over a note. Keep the normal page dots and stock SRC
press/hold distinction. There is no new navigation gesture, encoder-push menu,
or alternate meaning for holding SRC. Input routing, learned-shape management,
wheel assignments, and preset management stay in their existing settings
groups. Chord selection and the ordinary synthesis controls are on SRC.

### Rendered review set

The review artifacts live in the ignored output directory so that no
firmware-derived bitmap or font data enters Git:

- [Main four-page board](../../out/design/canopy-src-groups/main-board.png).
- [Wavetable variants](../../out/design/canopy-src-groups/wavetable-board.png).
- [Native show-values examples](../../out/design/canopy-src-groups/values-board.png).
- [Pixel-size preview](../../out/design/canopy-src-groups/index.html).
- [Render manifest](../../out/design/canopy-src-groups/manifest.json).

Each display design is a **128 × 64, one-bit pixel layout**. Enlargements use
nearest-neighbor scaling. The review boards are design renderings, not
screenshots of the implementation. They do not substitute for the firmware
capture/comparison acceptance below.

The board uses a representative test-pattern header from the native captures;
actual pattern and sound text remains firmware-owned. The example note markers
are a fixed playing-state illustration, not a device recording. The rendered
TUNE, source LEV, track strip and header regions have zero differing pixels
against their local stock reference images in both normal and held-SRC views.
The native five-pixel parameter font also passes an exact TUNE-label comparison.

Implementation check: a DTII 1.17 coupled emulator captured all four pages
and the Velvet, wavetable and FM variants under `out/canopy-longboot/dt2/`.
At the captured default settings, the complete KEYS TUNE cell, source LEV
cell and track strip each matched the stock reference with zero pixel changes.
Those captures preceded the final wavetable rounding and voice-fault display
corrections, which do not touch the native controls. The later source passed
the focused host controls/renderer tests and rebuilt the ColdFire object.
Held-SRC and other values still require the full firmware pixel matrix below.

Visual references are the
[Digitakt II AMP page](https://www.elektron.se/wp-content/uploads/2025/07/Digitakt-2-User-Manual_ENG_OS1.15A_250708.pdf#page=56)
for connected envelope segments and the
[Digitone II FM Tone pages](https://www.elektron.se/wp-content/uploads/2026/10/Digitone-2-User-Manual_ENG_OS1.12_261008.pdf#page=89)
for operator routing and grouped timbre controls. These are visual references,
not claims that CANOPY uses the same synthesis architecture. Exact DTII pixels
come from the local OS1.17 captures, rather than the older DTII manual.

### Shared visual and interaction rules

- Preserve the native header, track identity, dedicated track-level strip,
  page dots, fonts, focus feedback, and control-cell anchors. Diagrams occupy
  only the explicitly allocated custom cells; none may overwrite protected
  native areas.
- Keep exact stock **TUNE at KEYS A** and **LEV at KEYS H**. The dedicated
  LEVEL/DATA knob edits native track level on all four pages. Source LEV and
  track level retain their distinct roles and exact stock focus strings;
  neither graphic is an audio meter.
- Use normal labels under graphics. Holding SRC retains the graphics and
  substitutes parameter values in the native label positions. Read-only
  graphics stay read-only; do not invent a value or encoder target for them.
- Discrete lists retain four raw knob ticks per choice and no acceleration.
  Custom turning feedback supplies the full parameter name and formatted
  value. Native TUNE/LEV feedback and modifiers remain unchanged.
- Holding a trig and turning a control edits its lock; pressing that encoder
  while holding a trig clears the lock. Graphic-only cells consume custom
  turns/presses without changing a sound or creating a lock. They are not
  shortcuts to hidden editors.
- Draw applied note/arp telemetry only when it is fresh and belongs to the
  selected instance. Show **NO NOTES** or **RELEASE** when appropriate; clear
  active-note markers and show **LINK LOST** for stale telemetry. Never leave
  a last-known note highlighted as though it were still playing. Parameter
  diagrams depict configured shapes, not fabricated signal measurements.

### Group 1: KEYS and ARP

KEYS makes the relationship between the keyboard, generated chord, and pad/arp
blend visible. There is no root-key encoder: the root is the note the player
supplies. CHRD is the single mode/shape selector; adding a separate keyboard
mode control would duplicate its current behavior.

| Encoder | KEYS | Behavior and picture |
| --- | --- | --- |
| A | TUNE | Exact native stock tuning widget, range, formatting, focus and modifiers. |
| B | CHRD | PLAY sounds the pressed notes. MAJ, MAJ6, SUS2, MIN6, MIN, SUS4, ADD9, DOM7, DIM, MIN7, USER and MAJ7 generate their selected shape from a played root. Turning to PLAY remains available from USER. |
| C | VOIC | CLS, OPEN, INV1 or INV2; unavailable in PLAY. USER retains its learned voicing and shows LRND. |
| D | STRM | OFF or 1–500 ms total spread from the first to last generated chord note; unavailable in PLAY. A staggered-note glyph explains the time offset. |
| E/F | Read-only keyboard and chord | One combined region shows held/generated pitch-class keys, root/chord identification, and note ownership from fresh telemetry. It is a compact playing aid, not a root-setting control or audio meter. |
| G | MIX | Native-style horizontal crossfader between PAD and ARP. Its thumb represents the configured blend, with PAD at 0 and ARP at 127; intermediate feedback uses the current percentage format. |
| H | LEV | Exact native stock source-level widget and Sample Level feedback. |

The keyboard picture explains what CHRD did to the input. In PLAY it represents
the actual admitted notes. In root modes it represents the generated voicing;
the root/chord caption distinguishes that from an arbitrary keyboard chord.
Show a literal note description when recognition is ambiguous. Do not invent a
chord name from an incomplete or stale set. The compact keys show pitch classes;
the existing detailed Status view remains the place for every absolute note and
ownership detail.

ARP holds the timing and traversal controls together. A compact rising/falling
note path explains order and octave direction; an active marker uses real arp
telemetry, while the outline can show the configured traversal when idle.

| Encoder | ARP | Behavior and picture |
| --- | --- | --- |
| A | RATE | Pattern-tempo divisions: 1/4, 1/8, 1/8T, 1/16, 5/Q, 1/16T, 7/Q and 1/32. Use pulse spacing, not an independent BPM readout. |
| B | ORDR | UP, DOWN, UPDN, PLAY (As Played) or RND; the path shape follows the selected ordering. Random mode must not depict a promised future sequence. |
| C | OCTS | −3…+3 octave spread. Zero is only the played octave; positive adds octaves above and negative below, never both directions at once. |
| D | GATE | 1–100%; a pulse-width glyph distinguishes note-on duration from step spacing. |
| E | SWNG | Existing swing behavior and display scale; straight timing displays 50%. Alternating interval spacing illustrates the offset. |
| F | RST | TRIG, PATT or CONT. A return marker explains whether the note path restarts for a phrase, a pattern, or continues. |
| G/H | Read-only arp preview | Combined note/order diagram, current arp note when valid, and clock/no-note state. No extra MIX or tempo control. |

The arp always uses pattern BPM and can play held notes while the sequencer is
stopped. A stopped transport therefore does not imply an inactive arp. Show
**ARP OFF** when MIX is at PAD; distinguish no held notes from missing clock
information. RATE and SWNG diagrams are explanatory settings pictures, not a
replacement clock source.

### Group 2: SOUND

WAVE consistently occupies A and remains one source/table selector: VELVT,
REED, ANLG, GLASS, VOCAL, ORGAN, METAL or FM. It preserves the remembered
settings of each source family. It has no relationship to keyboard mode.

| Encoder | Wavetable SOUND | Standalone FM SOUND | Velvet/Reed SOUND |
| --- | --- | --- | --- |
| A | WAVE | WAVE | WAVE |
| B | POS | DEP | COLR |
| C | DEP | ALG | DRFT |
| D | RATE, or HZ in free mode | BAL | Read-only waveform region |
| E | SHAP | RTA | Read-only waveform region |
| F | DRFT | RTB | Read-only waveform region |
| G | CLK | FDBK | Read-only waveform region |
| H | RST | DRFT | Read-only waveform region |

For wavetables, the eight-frame strip and cursor explain **POS 1.00–8.00**.
Above it, draw a normalized cycle of the selected table interpolated at the
configured base position, before scanning, envelope modulation or FM. It is a
source-shape illustration, not a scope. The GLASS rendering uses the actual
factory coefficients interpolated at position 3.25.
The centered scan span explains **DEP 0–100%**; full depth reaches 3.5 frames
on either side before the table-end clamp. Keep the configured base cursor
distinct from any optional applied-position indicator. A SHAP mini-wave shows
SINE, TRI or smooth RAND scanning. RATE is **0.01–10.00 Hz** under FREE, or
4BAR, 2BAR, 1BAR, 1/2, 1/4, 1/8 or 1/16 under SYNC. CLK directly exposes the
existing scan clock selector, and RST exposes FREE, TRIG or PATT scan restart.
These are the wavetable scan settings, not new arp clock settings. Retain the
current change-of-clock defaults: 0.10 Hz for FREE, 1BAR for SYNC.

For standalone FM, the ALG cell uses a small operator connection diagram
inspired by Elektron's FM pages. TWIN, STACK, SUM and CHAIN must draw the
implemented connections; feedback returns to modulator B. DEP means harmonic
modulation depth, not output volume. BAL illustrates **tone X ↔ tone Y**,
distinct from KEYS MIX's **PAD ↔ ARP**. RTA and RTB are frequency multipliers:
0.25, 0.50, 0.75, 1.00, 1.25, √2, 1.50, √3, 2, 2.50, 3, 4, 5, 6, 7 and 8.
Keep the existing compact numerical formatting and √2/√3 focus explanation.

Velvet/Reed use the spare region for a qualitative family waveform. COLR and
DRFT remain editable. Do not introduce position, FM, or scan controls for
sources that do not implement them. The waveform is a synthesis explanation,
not a live scope or a measured audio trace.

### Group 3: ENV

ENV is a **timbre envelope**, while the existing AMP page remains the loudness
envelope. Native AMP-style geometry and segment handles communicate time and
shape without implying that ENV controls the note's audible release. Keep the
existing numerical ranges; ATK/DEC/REL are 0–127 control values, not invented
millisecond readouts. Both engines' current timbre envelopes use straight
segments. Draw their attack, decay and release widths using the square root of
each actual stage duration, normalized to the available graph width. This
keeps long releases legible without pretending the horizontal axis is linear
time. The held segment has a fixed illustrative width; a dotted boundary marks
note-off. Sustain height remains proportional to its value. Snap endpoints to
the pixel grid; zero-time segments are vertical.

| Encoder | Standalone FM ENV | Wavetable ENV |
| --- | --- | --- |
| A | ATK, first segment of the shared ADSR graph | ATK, rising segment of the combined A/B AR graph |
| B | DEC, falling segment toward sustain | REL, falling segment of the combined A/B AR graph |
| C | SUS, 0–100% held level | FM, base wavetable-FM depth 0–100% |
| D | REL, release segment | RAT, wavetable-FM frequency ratio |
| E | ENV, signed envelope-to-FM-depth amount | PENV, signed envelope-to-position amount |
| F | VEL, signed velocity-to-FM-depth amount | FENV, signed envelope-to-wavetable-FM-depth amount |
| G | Read-only routing diagram | VEL, signed velocity-to-position amount |
| H | Read-only routing diagram | Read-only shared-envelope routing symbol |

FM draws one ADSR curve across A–D. A focus or turn highlights the affected
segment without moving its native parameter label/value anchor. The G/H diagram
contrasts **ENV → FM** with **AMP → VOL** so that brightness release and volume
release are visibly separate. ENV and VEL amounts retain their signed
−100%…+100% scale, with zero as neutral.

Wavetable draws an **AR envelope with a held plateau**, not an ADSR envelope.
It has no decay or sustain parameter. The same AR trajectory feeds PENV and
FENV: changing ATK or REL affects both enabled destinations. The routing symbol
shows this split to POS and FM; the two amount controls remain independent.
FM at C adds modulation to the selected wavetable, and RAT chooses its frequency
multiplier from the existing sixteen-entry ratio list. VEL still affects
position, not FM depth. All three signed amounts retain −100%…+100% with zero
neutral. Wheel amounts stay in MODULATION settings as previously requested.

Velvet/Reed retain the fourth page position but show a source-specific
**AMP SHAPES VOLUME** explanation in the custom body. No dummy envelope,
inactive numeric knobs, or new synthesis parameters are added. TUNE, source
LEV, and track level are not repeated as spare-cell fillers on SOUND or ENV.

### State, migration, and implementation boundaries

This is a page-map and drawing change. Keep stable parameter identities,
preset bytes, family-specific remembered banks, six native MOD destinations,
and packed trig-lock IDs. In particular, WAVE must continue choosing both
family and table through its existing selector identity. The new PENV/FENV
labels distinguish already separate amounts; they do not create new controls.
Moving CLK and wavetable RST onto SOUND exposes the existing settings and must
not produce a second copy of either value.

Page indices can keep their current roles: 0 becomes KEYS, 1 remains ARP,
2 becomes SOUND, and 3 becomes ENV. Legacy sources gain a descriptive page 3,
so navigation has a stable four-page cycle across source changes. Moving a
control between encoders must preserve held-trig lock editing and clearing,
live recording, native MOD lookup, preset restore, track ownership, focus
invalidation, and pending/applied revision handling. Refresh source-specific
drawings atomically when WAVE changes; never show a previous family's labels
against the new family's values.

Fresh data is required only for live note/arp markers. Native level/tuning
controls and static parameter diagrams remain responsive without telemetry.
Pending custom changes may use the existing bounded feedback marker, but never
add it to native TUNE/LEV text. Faults must remain legible within the custom
region and cannot be represented by a fake note, meter, or endlessly moving
cursor.

### Pixel and behavior acceptance for implementation

The layout contract uses inclusive pixel bounds, origin at the upper left:

| Region | Pixel placement |
| --- | --- |
| Native header | x0–127, y0–9 |
| Native track/track-level strip | x0–21, y10–63 |
| Custom body | x22–123, y10–63 |
| Native page indicators | x124–127 |
| Encoder column centers | x33, 59, 85, 111 |
| Top labels or held values | y32–36, native five-pixel font |
| Bottom labels or held values | y59–63, native five-pixel font |
| Protected KEYS TUNE cell | x23–45, y11–36 |
| Protected KEYS source LEV cell | x101–123, y38–63 |

Combined diagrams may span their allocated cells; labels/values still use the
individual encoder anchors. Keep all custom cell text within 23 pixels and
put longer explanations in native turning feedback. Keyboard/root, arp state,
and routing captions are read-only and do not turn into fake values under SRC.

1. Capture every page at 128 × 64 in normal and held-SRC states, for FM,
   wavetable and both legacy sources. Use the render manifest as the proposed
   layout reference; use unpatched OS1.17 captures as the native-pixel oracle.
2. Require zero differing pixels in protected native header/track/level/page
   regions after accounting for actual page-state content, plus the complete
   KEYS A/H cells at identical settings. Test neutral, fractional, negative,
   and endpoint TUNE and 0/intermediate/127 source and track levels. Preserve
   native focus strings, timers, modifier behavior and label/value baselines.
3. Check diagram bounds, native font glyphs, clipping, all four page dots,
   graphics-versus-values mode, pending feedback, and source changes. No
   antialiasing, stretched text, one-pixel spill, or graphics over H's label.
4. Exercise PLAY, each generated chord family, USER, unavailable VOIC/STRM,
   ambiguous notes, zero notes, release-only state, arp while stopped,
   octave extremes, random order, stale telemetry and clock loss. A diagram
   must not falsely imply a sounding voice or future random note.
5. Validate WT boundary positions/depth, both scan clocks and reset modes,
   all FM algorithms, ratio endpoints, signed modulation extrema and neutral
   amounts. WT must show one shared AR; standalone FM must show its actual
   ADSR. Neither can replace or falsely describe the stock AMP envelope.
6. Run preserved-value/preset, MOD, held-trig/live-lock, encoder-clear,
   track-switch, source-switch and SRC press/hold acceptance with the compiled
   firmware/module pair. Review final emulator captures and then the physical
   device before claiming an installed pixel match.

## Historical proposal and implementation record

The remainder records earlier layouts and their investigation. Its status and
placement statements apply to those revisions, not to the new proposal above.

Current ARP revision (2026-11-12): A–F are RATE, ORDR, OCTS, GATE, SWNG and RST.
G/H are blank. The arp always follows the current pattern tempo and is playable
from held keyboard notes while the track or sequencer is stopped. There is no
local BPM or clock selector. This supersedes the historical ARP mockup below;
the current control contract is in [the technical design](canopy.md#controls-and-display).

Status: **implemented; compiled UI/audio acceptance passed, 2026-11-06**.
The native renderer, level mapping, Status menu and local-panel bridge are
implemented. The acceptance evidence below distinguishes measured results from
remaining checks. The user identified the **dedicated LEVEL/DATA knob**, rather
than encoder H, as ineffective. Installed firmware has not yet been changed for
this revision. [Implementation results and remaining acceptance](../handovers/HANDOVER-2026-11-06-canopy-native-ui.md)
include zero-difference native control captures and independent audible level
sweeps; the full pixel/modifier matrix and physical verification remain open.

**Review revision: exact stock parity is required for TUNE, source LEV, track
LEV, and hold-SRC show-values mode.** Their earlier schematic drawings and the
proposed rename to "Source Level" are withdrawn. Stock OS1.17 is the visual and
interaction reference; a close-looking reconstruction is not acceptable.

## Recommendation

Use the standard DTII parameter-page layout: preserve the native header, track identity, left-hand track-level fader, and right-hand page dots. Put CANOPY's eight parameters into the usual two rows of four, with native text and parameter widgets. Keep TUNE at A and source LEV at H on the Sound page.

Remove the large central chord heading, note strip, L/P badges, and stretched custom lettering from the SRC pages. Put detailed performance information in a native **STATUS** view, opened from `DIGIKIT > MODULES > CANOPY > SETTINGS > STATUS`. This is an additional menu action, not a third SRC page. The tradeoff is deliberate: the main pages become familiar and readable; all four live note names are available in Status rather than permanently squeezed between the knobs.

The two SRC pages are **SOUND** and **ARP**. ARP replaces the longer MOVEMENT name. Keep the Sound controls in their current physical order. On ARP, replace the currently read-only SOURCE/KEYS tile at H with ARP MIX, linked to Sound G. Input routing and keyboard mode remain in Settings. The two MIX appearances edit one shared parameter, not separate gain stages.

## Why two levels exist

| Control | Position and appearance | Meaning |
| --- | --- | --- |
| Track level | Native narrow **LEV** fader at the far left, below track identity; dedicated **LEVEL/DATA** knob | Balances the selected track in the mix. It remains accessible on both SRC pages and other normal parameter pages. |
| Source level | Native **LEV** widget at bottom-right of SOUND; encoder **H** | Sets the source's level within its sound. Reuse the stock source-level parameter, including its exact native **Sample Level** focus text. Explain its purpose in CANOPY help without renaming the stock UI. |
| AMP volume | Existing **VOL** on the stock AMP page | Another existing sound parameter; this proposal adds no extra CANOPY volume. |

The two LEV graphics show parameter settings, **not audio meters**. Silence, held notes, and release tails must not animate their values. Retain stock LEV labels and exact stock focus strings: **Track Level** versus **Sample Level**, including capitalization, spacing and numeric formatting. Do not join the two controls or silently make H edit track level.

## Exact stock controls and show-values contract

The reference is the unpatched Digitakt II OS1.17 Oneshot SRC control page.
Local emulator captures were made from firmware SHA256
`26c22f6652625ac2cfd47f7ee970d388ed8b6427dae3563c0a6a2f2d334350d5`.
These captures establish reference appearance, not CANOPY implementation or
audio acceptance. They live under `out/design/canopy-stock-ui/` and stay out of
Git because the repository excludes firmware-derived material.

| Element | Required implementation |
| --- | --- |
| TUNE at A | Use the stock TUNE widget/draw path, native parameter code `0xca`, formatter and input handler. Preserve its graphical tuning indicator, label/value baseline, alignment, sign, decimal precision, focus behavior and modifiers. Do not draw a substitute number box or use the SDK helper with a different range. |
| Source LEV at H | Use the stock `0xd1` widget, level marks/fill, LEV label, value placement and native Sample Level focus feedback. Do not draw the prototype's horizontal-thumb fader. |
| Track LEV at left | Preserve the stock track column and native track-level code 10, including ticks, fill, label/value placement, Track Level focus feedback and LEVEL/DATA handling. This is independent of H and CANOPY's telemetry freshness. |
| Holding SRC | Use the native held-key state and native parameter-page values presentation. The captured control page **keeps the parameter graphics and puts values in their label positions**; it does not replace the graphics with centered numbers. The left track LEV also becomes its numeric value. Preserve native header behavior and per-control formatting. |
| Releasing SRC | Return through the stock release handler and redraw rules. Show-values is momentary; no click-to-latch switch, separate CANOPY timeout or extra page advance. Preserve the stock distinction between a short SRC press and a hold. |

Custom CANOPY cells must participate in this same show-values state using the
native font, value baseline, alignment and parameter feedback conventions.
Their musical values differ from stock sample controls; their presentation must
not introduce another values layout. Leave native A/H, track LEV and their
focus/header regions untouched by custom overpainting. Pending indicators may
appear only in custom-control feedback; do not add a star to stock TUNE/LEV or
change their native formatting.

Reference frames:

- [Normal stock SRC page](../../out/design/canopy-stock-ui/stock-src-main.png)
- [Stock SRC held](../../out/design/canopy-stock-ui/stock-src-main-held.png)
- [Stock TUNE feedback](../../out/design/canopy-stock-ui/stock-tune-focus.png)
- [Stock source-level feedback](../../out/design/canopy-stock-ui/stock-source-level-focus.png)
- [Stock track-level feedback](../../out/design/canopy-stock-ui/stock-track-level-focus.png)

The previous interactive schematic is replaced with these actual reference
frames. Full CANOPY screenshots must come from the implementation before they
can be labelled an exact-match preview.

Elektron's manual distinguishes active-track level, source/sample level, and AMP volume. It also documents USB **PRE/POST FADER** routing: a pre-fader USB feed can remain audible as track level changes. This is a diagnostic possibility, not a confirmed explanation for this device. See sections 6, 11.6, 14.6.7 and Appendix A in the [official manual](https://www.elektron.se/wp-content/uploads/2025/07/Digitakt-2-User-Manual_ENG_OS1.15A_250708.pdf). That manual describes OS1.15A; implementation and acceptance must use the repository's pinned OS1.17.

## Diagnosis before implementation

- **Confirmed visual problem:** the CANOPY painter clears and replaces x=0..123, y=10..63. This includes the stock left-hand track identity and track LEV. Its four equal-width columns consequently start too far left. The header and page dots survive, but the rest of the native layout does not.
- **Confirmed typography problem:** the current 3x5 glyphs are enlarged into 5x7 values by repeating strokes. They are not the native parameter font.
- **Confirmed label ambiguity:** encoder H reads stock source code `0xd1`, while the custom UI calls it LEVEL. Native focus can simultaneously say `Sample Level=...`; that is another presentation of H's parameter, not another independently implemented CANOPY level.
- **Confirmed input intent:** `ip_src_turn` forwards non-A–H turns to the stock handler. `ip_src_parameter` explicitly maps Sound A/H and delegates the remaining indexes. Thus there is no explicit decision to disable LEVEL/DATA in CANOPY.
- **Unresolved audible fault:** forwarding a handler does not prove the selected track's level is edited or applied to CANOPY audio. Existing native UI acceptance covers A/H edits, not a dedicated LEVEL/DATA sweep with isolated audio. The old statement that level remains downstream is not sufficient physical evidence. The cause could be mapping, audio routing, monitoring, or a combination; it is not established by this review.

Source references: [native bridge](../../modules/core/adapters/instrument-play/src/cf/src-native.c), [painter](../../modules/core/adapters/instrument-play/src/cf/src-display.c), [control mapping](../../modules/core/adapters/instrument-play/src/cf/src-state.c), [native UI tests](../../tests/regression/host/canopy-native.c), and [earlier SRC findings](../findings/137-canopy-src-pages.md). The shared velocity adapter substitutes only its velocity factor; do not replace the entire stock gain calculation with a new CANOPY master multiplier.

## Built-in DTII keyboard: original gap and required integration

The DTII's KEYBOARD mode plays the selected track from its TRIG keys. CANOPY's
current "KEYBOARD" setup instead enables an **external USB/DIN MIDI** route.
These are different input paths. The implementation has not connected the local
panel's note presses/releases to CANOPY's independent-note ownership engine.
Changing the DTII keyboard mode therefore does not supply CANOPY with those
notes. This is an implementation gap, not a requirement for another keyboard
setting or a limitation of CANOPY's oscillators.

Code evidence: `mi_channel_message` in
[the MIDI adapter](../../modules/core/adapters/midi-events/src/cf/adapter.c)
accepts USB source flags 2/4 and DIN flags 8/16, and falls through for other
sources. Route enable accepts only those two ports. The
[instrument controller](../../modules/core/adapters/instrument-play/src/cf/control.c)
enables only those routes and suspends admission if both are off. Stock trigger
activity does not itself create the note-on/note-off events required by the
persistent instrument. The currently fixed SOURCE value is not evidence of a
working local-panel or sequencer-note bridge; tempo synchronization is separate.

Add built-in keyboard support to the functional work accompanying this review:

1. Resolve an OS1.17 panel-note hook that retains individual key-down/key-up
   identity and the firmware's octave/scale-resolved pitch, before any stock
   single-voice collapse. Confirm its target track through the native resolver.
   A generic raw-button hook or the final shared audio trigger is insufficient.
2. Feed those events through the existing PLAY/ROOT/LEARN mapping and bounded
   note ownership, with a distinct **PANEL** source. Do not pretend that local
   keys arrived through DIN, or require a USB/DIN channel to play the panel.
   Maintain independent releases when panel and external MIDI hold the same pitch.
3. Preserve the pitch and owner captured at key-down until that key's release,
   including octave/scale changes while held. Define track/machine/mode changes
   so panel notes cannot stick, jump to another track, or release unrelated MIDI
   notes. Apply latch and generated-chord behavior through the same mapper.
4. Consume only the admitted CANOPY performance events needed to prevent a
   stock retrigger of the shared envelope. Preserve transport, navigation,
   step editing, stock machines and their native keyboard handling. Native live
   recording and sequencer playback remain separately qualified capabilities;
   local audition must not be advertised as proof of polyphonic pattern recording.
5. Verify first-note playback with USB and DIN disabled, independent release of
   several panel notes, PLAY/ROOT/LEARN, octave/scale changes, mixed panel/MIDI
   ownership, track switches and no stuck notes on keyboard-mode exit. Check both
   generated events and isolated final audio on the device.

In Settings/help, distinguish **NOTE INPUT** (panel, USB MIDI, DIN MIDI) from
**PLAY MODE** (PLAY / ROOT / learned shape). The panel route should work when
CANOPY is the active supported track without enabling external MIDI. Until the
bridge is implemented and verified, state **USB/DIN MIDI ONLY** in help and do
not show an apparently functional Panel option. Keep this work separate from
the visual renderer so a successful screenshot cannot close the input gap.

## Screen layout and style

```text
  Native pattern / title / transport / tempo header
  ┌─────┬────────┬────────┬────────┬────────┐
  │track│   A    │   B    │   C    │   D    │
  │type │  TUNE  │  CHRD  │  VOIC  │  COLR  │
  │     ├────────┼────────┼────────┼────────┤
  │ LEV │   E    │   F    │   G    │   H    │
  │fader│  DRFT  │  STRM  │  MIX   │  LEV   │
  └─────┴────────┴────────┴────────┴────────┘
                                       native page dots
```

This diagram explains ownership and placement; it does not propose cell borders or encoder letters on the OLED.

1. **Native chrome:** let firmware draw the header, SMP/track-number badge, track fader, and page dots. Preserve the current pattern title and transport content. Never replace the title with a permanent CANOPY banner. The reference captures retain their stock pattern title.
2. **Parameter area:** use the actual native cell origins, baselines and draw extents. The existing 99x53 canvas is useful for custom content, but its x=25 origin is not authority to crop or move a stock widget. Keep the entire native A/H cells, left track column, header and page dots under firmware ownership. Bound custom overlays to their verified cell extents.
3. **Text:** call the native font renderer. Use uppercase, short labels below widgets, normal native weight, and no scaled-up microfont. Full names and precise values appear in ordinary native-style turn feedback. Choose short values explicitly rather than shrinking or cutting text.
4. **Widgets:** TUNE and both LEV indicators use the actual stock draw paths specified above. Continuous CANOPY macros use native rotary presentation; discrete choices use compact native text values. Keep native anchors and baselines. No new persistent central banner.
5. **Focus:** preserve exact native TUNE, Sample Level and Track Level feedback. Custom full-name/value feedback uses the same native presentation area and timing. Requested custom values may show `*` until the matching DSP revision arrives. Never render both native and custom focus banners or overwrite native focus with CANOPY's 750 ms timer.
6. **All-values view:** hold SRC to replace parameter labels with values while retaining the native graphics, as measured above. Release restores labels through the native state machine. The custom controls must follow that same state; no persistent toggle or separate CANOPY hold threshold.
7. **Unavailable values:** use `--`, or `AUTO` where meaningful. Turning an unavailable control briefly explains why, without modifying it. Full descriptions must fit the native feedback area by measured abbreviation or a separate Status/Settings row, never by smaller text.

## SOUND: controls and explanation

The earlier Sound illustration is withdrawn because its TUNE and LEV widgets
were approximations. The control map below remains the proposal; the exact
stock controls contract above determines appearance.

| Knob | Label | What it does / what you see |
| --- | --- | --- |
| A | TUNE | Native transpose, including fine tuning; preserve its current native range, adjustment and Sound storage. |
| B | CHRD | Chord shape in ROOT mode; `AUTO` in PLAY because the keyboard supplies the notes; `USER` for a learned shape. Turning B always lets you leave USER for another chord in either direction, without deleting the learned shape. Full detected/generated chord appears in Status. |
| C | VOIC | Close/open/inversion choice in ROOT; `--` in PLAY; `LRND` for the saved learned voicing. Shorten `CLOSE` to `CLS`, with the full name in focus. |
| D | COLR | Existing 0..127 tone-color macro. Rotary widget; exact value while turning or holding SRC. |
| E | DRFT | Existing 0..127 drift macro. Rotary widget; no fake measured cents value. |
| F | STRM | Chord spread, OFF or milliseconds; unavailable in PLAY. For values too long for the cell, show the number and put units in full focus feedback. |
| G | MIX | Pad-to-arp balance: PAD at 0, ARP at 127, percentage between. Focus says `Arp Mix`; it is not output volume. Same-pitch arp onsets align with their held pad to avoid pitch-dependent cancellation; the arp retains its own gate and envelope. |
| H | LEV | Source level. Exact stock control and glyph; focus retains `Sample Level`. |

## ARP: controls and explanation

Use the same native layout and track LEV on ARP, with the controls below.

| Knob | Label | What it does / what you see |
| --- | --- | --- |
| A | RATE | Existing clock division, e.g. 1/16. Triplet and step-only representations must fit without implying unsupported timing. |
| B | ORDR | UP, DOWN, UPDN, PLAY, or RND. Full `As Played` in focus distinguishes arp order from keyboard PLAY mode. |
| C | OCTS | Signed octave spread: **−3 to +3**, in one direction at a time. **0** uses only the played octave; **+1…+3** add one to three octaves above it; **−1…−3** add one to three below it. The pad pitch stays unchanged. Values display as `-3`, `0`, `+3`, without the ambiguous `OCT` suffix. |
| D | GATE | Note-gate percentage; full value in focus. |
| E | SWNG | Existing swing percentage, straight at 50%; `--` when the active clock capability cannot support it. |
| F | RST | TRIG, PATT or CONT; full restart-policy name in focus. |
| G | LTCH | Keyboard latch OFF/ON setting. The Status view separately distinguishes enabled from actually holding notes. |
| H | MIX | Same parameter as Sound G; lets you bring in the arp without leaving this page. Replaces the fixed KEYS tile, not a second mixer. |

SRC and native UP/DOWN navigation switch SOUND/ARP. FUNC+SRC remains machine selection. Leaving SRC or changing tracks cancels temporary focus. Selecting CANOPY restores its last SRC page. Parameter IDs, preset contents, ranges, DSP algorithms, and MIDI routing do not change with this layout.

Holding SRC retains the graphics and substitutes values for labels on both
pages. The previous two show-values mockups are withdrawn: they removed the
graphics and used the wrong value positions.

## STATUS: live performance explanation

Use a native menu/modal reached through the CANOPY Settings entry. The following is a content sketch, not a third encoder page:

Implementation uses the stock scrolling text-page layout: the short title
`STATUS` occupies the left column, normally with six read-only rows on the right.
Long mode/chord pairs use an extra row; complete text takes priority over fitting
the first line. UP/DOWN reveals the final arp row. `CANOPY STATUS` remains the menu entry;
the shorter in-page title prevents overlap with the native row column.

[Actual implementation capture](../../out/design/canopy-native-ui/canopy-native-status.png)
shows the stock scrolling layout with a live C-major chord. Firmware-derived
captures stay local under `out/` and are not committed.

```text
CANOPY STATUS
PLAY       Cmaj7
C4 E4 G4 B4
LATCH OFF  PEDAL UP
ARP OFF
NO BACK
```

- Show mode, detected/generated chord, and the four admitted notes. Use actual notes when recognition is ambiguous. Handle accidental notes, negative octaves and long names using two note rows when needed; never clip a pitch to fit.
- Show latch enabled/holding and pedal up/down as words, replacing unexplained L/P badges. Individual note ownership can use the existing solid/dotted/short underline conventions, explained in Settings help.
- While arp is audible, show its actual pitch and clock state. At zero mix, say ARP OFF. At stale telemetry, clear stale highlights and show LINK LOST; distinguish NO NOTES from a failed connection.
- Keep PANIC and LEARN in their existing Settings paths. This status view does not capture notes or alter the sound. NO returns to Settings.
- Faults, failed saves and lost clock also get bounded native-style notices on the main pages. Faults outrank edit feedback; stale status must not be hidden by a permanently open focus banner.

## Implementation sequence

1. **Reproduce and isolate LEVEL/DATA first.** On the installed image, record the active machine/track, the parameter resolved for the dedicated encoder, and the native track-level value. Compare turns on SRC Sound, SRC Arp, AMP and the stock mixer. The dedicated encoder uses resolver index 8; native track level must resolve to code 10 on both SRC pages. Do not assume the index from A–H event numbering. If the value does not change, fix the narrow mapping/dispatch boundary for the admitted instrument. If it changes, trace the resulting gain and monitor tap. Retest on the rebuilt/rebased image separately.
2. **Prove audible level.** Use isolated CANOPY, Overloop at LIVE/bypassed, no other sounding tracks, and a documented dry monitoring path. Compare source H and track LEVEL/DATA independently at 0, an intermediate value and 127, including notes already held and their release tails. Measure headphones/main or post-fader USB; separately confirm expected pre-fader USB behavior. Preserve stock send/tail semantics. Never patch gain just to compensate for monitoring before the fader.
3. **Replace the drawing boundary.** Keep native rendering for A/H and track LEV, including focus and show-values mode. Supply a narrow page/parameter descriptor or adapter path that lets the firmware see those native parameters even though CANOPY has no legacy DK_PARAM table. Use bounded overlays only for custom cells. Preserve lifecycle/generation guards. CANOPY's persistent-instrument declaration cannot simply be assumed to support ordinary V2 callback pages unchanged. The upstream stock-overlay mechanism is a starting point, not proof that every held-key mode is already passed through.
4. **Integrate widgets and feedback.** Retain native A/H editing, names, formatters and draw functions; expose native show-values state to custom cells without duplicating the SRC key state machine. Use current stable IDs, change only ARP H's visual mapping to CP_ARP_MIX, and remove the SOURCE tile. Keep pending-versus-applied semantics. The hand-drawn SDK tune/level helpers are not acceptable substitutes for exact stock widgets.
5. **Add Status and update help.** Use the existing native Settings/modal infrastructure and authenticated status snapshot. Show complete notes, descriptive latch/pedal state, and action-relevant clock/error messages. Suppress hidden SRC repaint under the modal; restore focus/page state correctly afterward.
6. **Acceptance and packaging.** Extend existing owner tests rather than create ad-hoc device scripts. Validate the affected build, capture both real SRC pages, hold-values, min/max focus, Status and fault states, then repeat physical level measurements before packaging for installation.

The built-in keyboard bridge above is an additional functional work item with
its own event/audio acceptance, not a font or layout change.

## Acceptance criteria

- Native header, left track badge/fader and right page dots match an OS1.17 stock page in position, font and clipping. No leftover pixels, XOR artifacts, clipped H labels, or overwritten track identity.
- **Zero-pixel-difference gate:** capture unpatched Oneshot and CANOPY with identical native values and UI context. Compare complete protected TUNE/LEV cells, track-level column and native focus regions at 128x64 with no scaling, antialiasing or tolerance. Cover neutral/fractional/negative/end-range TUNE and 0/intermediate/127 levels. Match all native glyphs, labels, value positions and focus strings; exclude only genuinely different custom parameters and unrelated pattern/transport content.
- **Exact show-values gate:** compare normal, SRC-down/held, edits while held, and SRC-up states. Graphics remain visible, label/value positions and header behavior match stock, and track level participates. Cover both CANOPY pages, native modifier combinations and interruption by a modal/track change. A short press still follows native navigation; a hold must not cause an extra page advance. Verify custom cells through the same native text/layout mechanism. The old click-to-toggle preview is not a gesture reference.
- Dedicated LEVEL/DATA updates the native track-level setting on both SRC pages and affects isolated post-fader output, including held CANOPY notes. It does not change H's source value. The left fader remains responsive even when CANOPY telemetry is stale.
- H on SOUND independently changes the native source level and affects sound; it does not change track level. Zero tests distinguish dry signal from any already-running stock effect tails.
- ARP H and SOUND G show/edit the same MIX value. Neither changes track/source level, nor turns on arp merely because another arp setting was edited.
- Native A/H modifiers and hold-SRC behavior are preserved; unsupported custom press/modifier gestures remain consumed until specifically qualified. All six existing Sound custom controls keep their positions.
- Long values, four accidental/low-octave notes, PLAY/ROOT/learned states, pending changes, lost clock, stale telemetry and error notices fit without reduced-size fonts.
- Navigation, queued-event ownership, generation changes, Settings/LEARN modals, preset save/load, other tracks and stock machines remain correct. Full automatic Sound/project persistence for custom controls remains outside this UI change.

The review decision is the stock parameter-page layout with performance details moved into Status. The level-knob fault must be resolved and measured as part of that implementation, independently of approving the visual design.
