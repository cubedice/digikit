# PRISM, Overloop and RELAY pages: stock parity

The owner's review of 2026-11-27: the pages of PRISM and Overloop in the FX view and RELAY's SRC pages against the stock send
FX pages (Delay, Reverb, Chorus) and the stock SRC pages of a sample track, on the DT2 1.17 emulator with the hardware set
`canopy-overloop-prism-relay` and the owner's settings. The owner chose items 1 to 8 and R1 to R5 below; item 9 (stock knob
and switch widgets for PRISM's controls) was declined: PRISM's graphics stay its own.

## What the stock pages do (measured)

- **Header.** The global header: the pattern (`A01`), the kit's name and the tempo. On arriving at a page the help bubble
  shows its title for about six seconds (`Delay (1/5)`, the page and the count of the FX view's pages). With SRC held (FUNC + FX
  held on the FX view) the bubble names the machine and the sample (`ONE: 1-sine220`). The bubble is the help view, which
  draws after every view.
- **Left column.** The effect's tab (`DEL`, `REV`, `CHO`; on the SRC page the track) above the level strip (`VOL` on the FX
  pages, `LEV` on the SRC page).
- **Cells.** Two rows of four, centred at x 33, 59, 85 and 111; a graphic above a caption (top captions on rows 32 to 36,
  bottom ones from row 59); an unused cell is a dotted square, 17 pixels a side.
- **Values.** A turned cell's caption shows its value until the view's focus countdown ends (about two to four seconds); FUNC
  + FX held (SRC held on the SRC page) shows every value, the level strip's too.
- **Waveform page (SRC page 2).** Small widgets in the top row (rows 13 to 21), the waveform without a frame between dotted end
  markers at x 25 and 121 with a two-pixel flag on their second row, rows 26 to 54, a centre line on row 39; captions only on
  the bottom row.

## What changed

| | Before | Now |
| --- | --- | --- |
| 1 Header (PRISM, Overloop) | the page's own status line over rows 0 to 9, also over the help bubble | the stock header and title bubble (`PRISM (4/5)`, `Overloop (5/5)`) |
| 2 Tab | cleared | the stock tab (`PRS`, `OVL`): the pages paint x 16 to 123 below the header |
| 3 Level strip (PRISM) | `MIX` | `VOL` (PRISM's MIX is the level of its wet return); the strip was already the stock one |
| 4 Status words | in the header | in the cells: `FLIP`, `HOLD`, `ARMD` inverted in ZONE's, RPTS's and XFAD's captions; `NO TRK`, `SILENT`, `TRIGS` in INPT's; `NO LINK` in RATE's graphic; Overloop's line under its waveform |
| 5 Alignment (PRISM) | cells 1 to 1.5 pixels left of the stock ones (x 32 + 26 a cell) | x 33 + 26 a cell |
| 6 Turn feedback | none (PRISM), values always on a second line (Overloop) | the stock focus countdown: the pages touch it when they take a turn (`fxv_focus_touch`) and show the value while it runs (`fxv_focus_active`) |
| 7 FUNC + FX held (Overloop) | nothing | both captions show their values |
| 8 Overloop's cells | label over value, the slider under F and G | ARM's ring in E, the stock empty cell in F and G, the crossfader in H, captions below |
| R1 SRC held (RELAY) | `RLY:` and nothing | `RLY: T13 4.2 S` (the tape's track and length, `EMPTY` without one), in the bubble's own font |
| R2 C's caption (RELAY, page 1) | none | `STOP`, `PLAY` or `REC` |
| R3 INPT (RELAY, page 1) | `EXT LR` across the cell's edge | stacked, `EXT` over `LR`, as the stock pages stack `PRE` over `FLTR` |
| R4 Top row (RELAY, page 2) | text | small graphics: the input in a small frame, RATE's rail and knob, the transport mark, FDBK's filled frame |
| R5 Waveforms (RELAY page 2, Overloop) | a framed box | the stock waveform style and place |

The view state comes from the firmware: `FxPageView` inherits `ParameterPageView`, whose focus tracker (`+0x94`, the anchors
`cf.page.site.instrument_focus_touch` and `..._active` that CANOPY and RELAY already use on the SRC view) and show-values flag
(`+0x190`) every stock page uses; `core/fxview` gives them to its page adapters. RELAY's header text is written after the help
view's draw (`rtm_help_draw`, a new entry of RELAY's seam vector, version 2), in the bubble's own font (`HelpBubbleView` keeps
it at `+0x9c` and its height at `+0xa4`) with the same text routine (`cf.text.fn.draw`).

## Checks

- Host: `core/phrase-ui:test-host`, `core/loop-display:test-host` and `core/relay-track:test-host` draw every page through
  canvases that refuse any pixel in the header, under the tab (above row 38) or in the page indicator's columns, and check the
  captions, the values after a turn and with the values shown, the cells' places and the waveforms' pixels.
- Emulator: captures of every page at rest, after a turn (400 ms and 4.4 s) and with FUNC + FX or SRC held, against the stock
  pages in the same run (`ui-capture` tests, not committed).
