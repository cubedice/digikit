# CANOPY: native DTII interface proposal

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

Source references: [native bridge](../../modules/core/adapters/instrument-play/src/cf/src-native.c), [painter](../../modules/core/adapters/instrument-play/src/cf/src-display.c), [control mapping](../../modules/core/adapters/instrument-play/src/cf/src-state.c), [native UI tests](../../tests/regression/host/canopy-native.c), and [earlier SRC findings](../findings/80-canopy-src-pages.md). The shared velocity adapter substitutes only its velocity factor; do not replace the entire stock gain calculation with a new CANOPY master multiplier.

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
