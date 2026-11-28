# CANOPY pages and menus: stock parity

The owner's review of 2026-11-27 compared CANOPY's three SRC pages (VOICE, FM ENV, DETAIL), the track keyboard's ARP and KEYS
pages, and CANOPY's menus against the stock pages: the SRC, AMP and TRIG pages of a ONESHOT sample track and the stock
SETTINGS lists. The comparison ran on the DT2 1.17 emulator with the hardware set `canopy-overloop-prism-relay` and the
owner's settings, done the same way as the [PRISM, Overloop and RELAY review](fx-relay-ui-parity.md).
The owner chose items 1 to 9 and 11 to 15, with option b for item 2 (the track keyboard's toast rule) and option a for
item 7 (placeholders). Item 10, PRISM's and Overloop's crossfader picture for MIX, was not chosen: MIX keeps its own
picture.

## What the stock pages do (measured)

The [PRISM, Overloop and RELAY review](fx-relay-ui-parity.md) measured the header, the cells and the waveform page. This
review adds:

- **Title bubble.** On arrival a SRC page shows the machine's name with no page count (`Oneshot`), as does AMP
  (`Amplitude`). TRIG pages show a count: `Trig Parameters (1/4)`, `Retrig Parameters (2/4)`.
- **SRC held.** The bubble shows the machine's short name, its colon, and then the sample's name six pixels after the
  colon: `ONE: 1-sine220`, colon at x 34, name from x 40.
- **A turn.** The turned cell's caption shows the value while the focus countdown runs. The header does not change and no
  toast appears (TUNE, LEV and the track level alike).
- **Values shown (SRC or TRIG held).** Knobs, faders and pictorial icons show their values in their captions (TUNE
  `0.00`, PLAY `FWD`, VEL `100`, TRIG 2's VFAD and PTIM). A cell whose graphic is already the value in words or in a
  box, or a switch, keeps its caption: NOTE, PROB, SAMP, MODE, TRIG 2's LEN and RATE boxes, and the switches RSET, LFO.T,
  FLT.T, FILL, COND, RTRG, PORT. A turned or locked RATE box keeps its caption too.
- **FUNC held** changes nothing on SRC, AMP or TRIG.
- **Text graphics.** A value written as a cell's graphic (PROB's `100%`) sits on rows 19 to 23 in the top row, centred on
  the graphic rows 13 to 29.
- **Empty cell.** A dotted square 17 pixels a side, centred on the cell (x − 8 to x + 8). Its rows are 13 to 29 at the
  top and 40 to 56 at the bottom: dots on every second pixel of the top and bottom edges, and on every second row of the
  sides.
- **Cell centres.** x 33, 59, 85 and 111 on every page, TRIG included. The widgets' origin is x 24 + 26 a column.
- **Lists.** The title column holds a short word in a 4-pixel-a-character font, up to 9 characters before the selection
  bar at x 40: `SETTINGS`, `DIGIKIT`, `MIDI` for MIDI CONFIG, `SYNC`. A row holds about 17 characters of the same width,
  with its value ending at the right (`CLOCK RECEIVE` and its box, a value row's `000`). Rows are actions and settings
  only.

## What changed

| | Before | Now |
| --- | --- | --- |
| 1 SRC held | `Canopy:` and nothing | `Canopy: GLASS`: the sound's source where a sample's name would stand (`VELVET`, `REED`, the table, `FM` and its algorithm), or a fault's heading |
| 2 Turn feedback | a toast on every turn: `Color=65*`, `ALGORITHM=STACK*` | the caption only, as stock. A changed named choice also names itself, as the track keyboard's ORDR does: `Glass`, `Stack`, `Triangle`, `Tempo sync`, `Restart on trig`, `Square root of 2` |
| 3 Empty cells | blank | the stock dotted placeholder: VOICE C and D (Velvet, Reed), DETAIL B to F, FM ENV H (FM) |
| 4 Values shown | text cells repeat their graphic (`VELVT` over `VELVT`); ARP/KEYS switches and text say `OFF`, `UP`, `1/16` twice | text cells and switches keep their captions; knobs and pictures show values |
| 5 Text graphics | rows 18 to 22 (45 to 49) | rows 19 to 23 (46 to 50), on CANOPY's pages and on ARP/KEYS |
| 6 ARP/KEYS text | captions and text centred 1 pixel right (34 + 26 a column) | 33 + 26 a column; the widgets and lock boxes stay where they were |
| 7 FM ENV, Velvet and Reed | three lines of explanation | eight stock placeholders; the page cycle stays VOICE, FM ENV, DETAIL for every source |
| 8 Velvet/Reed waveform | its own `VELVET`/`REED` on the caption row | the waveform alone; WAVE names the source |
| 9 Pictures | FM's operators to row 59; the wavetable picture on rows 12 to 30 | within the stock graphic rows: operators as 8 × 7 boxes on rows 40 to 56; the wavetable's span on row 13, cycle on 15 to 25, rail on 28 |
| 11 Menu titles | `Canopy SETUP`, `Canopy MODULATION`, `Canopy PRESETS` overran the rows | `Canopy` (as opened from the DIGIKIT row), `MOD`, `PRESETS` |
| 12 PRESETS | `SAVE CURRENT SOUND` clipped, `LOAD SAVED SOUND` | `SAVE SOUND`, `LOAD SOUND` |
| 13 MODULATION | rows of a value only (`FREE HZ`, `FREE`); `WHEEL TO WT FM` ran into its value | `SCAN CLOCK   FREE`, `SCAN RESET   FREE` (the popups keep `FREE HZ`, `TEMPO SYNC`); `WHEEL POS`, `WHEEL FM`, `WHEEL WT FM` |
| 14 Information rows | SETUP: `READY`, `MIDI: TRACK CHANNEL`; module SETTINGS: three help rows, one of them stale (`SOUND H`) | none: the readiness line is STATUS's last row; the READMEs carry the help |
| 15 RECOVER KEYBOARD | `Release ALL keyboard keys and pedals. Ready?`, clipped on both sides | `All keys and pedals up?` |

The header text of item 1 is drawn after the help view, like RELAY's tape. `core/fxview`'s `fxv_help_draw` calls
`ip_help_draw`, a new entry of the instrument service's seam vector, so the seam version goes from 3 to 4: the image and
`services/instrument.dks` must be installed together. The text uses the bubble's font (`HelpBubbleView` +0x9c) at its
slide height (+0xa4), with `cf.text.fn.draw`. Its place is not fixed. The short name before the colon is the module's
(`Canopy`), so the draw finds the colon as the last dark pixel of the bar's text rows, once the bubble is all in, and
writes six pixels after it.

Items 2 and 7 change two lines of the [accepted CANOPY UI contract](canopy-ui-review.md): custom turns no longer toast
the parameter's full name and value, and Velvet/Reed's FM ENV no longer explains itself in the body.

## Checks

- Host: `tests/regression/host/canopy-native.c` draws every page and checks:
  - the placeholders' pixels (DETAIL B to F, all of Velvet's FM ENV, FM's FM ENV H);
  - the text values on their new rows, and text captions kept with values shown;
  - the wavetable and FM pictures within rows 13 to 29 and 40 to 56;
  - no toast for a number, and the choice's name for a changed choice;
  - the `header` scenario: the colon found in the bar, the source six pixels after it, the bubble's font and height,
    once a frame, nothing while the bubble slides in before it is all in, and nothing on another track.
- Device: `track-keyboard/tests/panel.device.test.ts` now checks the stock rule on ARP and KEYS: a turned RATE box keeps
  its caption, GATE's focus shows its value, and with TRIG held only the knobs' captions change (GATE and SWNG, STRM). The
  longest chord at the right edge is checked on the text graphic's rows.
- `canopy-src.c` checks `cv_feedback`'s words. `canopy-preset.test.ts` and `canopy-live-ui-transaction.c` check the menu
  rows. `track-keyboard/tests/trig-view.c` checks the ARP/KEYS centres, rows and kept captions.
- Emulator: captures of every page and menu before and after, against the stock pages in the same run (`ui-capture` tests,
  not committed).
