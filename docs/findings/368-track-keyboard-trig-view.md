# Shared keyboard on the DTII TRIG view

Digitakt II OS 1.17. **[D]** records a static read, build, or host test. **[O]**
records unverified playback or integration.

## Native view seams [D]

The controller builder at `0x40066584` passes key code 1 to the TRIG
constructor through its shared-object wrapper. It passes audio page IDs
`{1, 2}` from `0x401f787c` and MIDI page IDs `{23, 24}` from `0x401f782c`.
The constructor at `0x40073032` installs primary vtable `0x401f94e0` after
constructing its ParameterPageView base. It inherits the base's page vector at
+`0x7c`, +`0x80`, and +`0x84` and page index at +`0x90`. The stock key
handler advances or wraps against the vector count. The show-values byte at
+`0x190` is changed by that same handler.

| Method | Stock target | Slot in this view |
| --- | --- | --- |
| Key | Tail to the shared ParameterPageView event handler | Primary +`0x08` |
| Draw | `0x40073264` | Primary +`0x10` |
| Turn | `0x4007375a` | Primary +`0x44` |
| Page title | `0x4006965a`, COW string result through A0 | Primary +`0x4c` |
| Resolve parameter | `0x40069628` | Primary +`0xbc` |
| Turn through the secondary interface | `0x4007385c`, adjusts this by four bytes | Primary +`0x100` |

All six slot anchors resolve from one unique 260-byte window in the 1.17 MAIN
image. The five called stock entries are separately resolved. The adapter
allocates a four-entry vector with the same allocator as the stock
constructor. The stock view retains ownership and its destructor still frees
the vector. If the requested track has no available backend, the adapter
returns to the two-entry list.

The extra views reuse a stock page ID for the frame and level column. On MIDI
tracks they reuse the second ID. The first MIDI page has a note visualization.
The custom resolver hides A–H from the stock parameter editor while ARP
or KEYS is visible. All other views keep their own untouched vtables.
Custom controls use the native four-column grid from `0x4006b0b0`. The
caption grid starts at x = 25 + 26 * column. Native widget rows start at
y = 34 or 7 in firmware coordinates. The physical OLED comparison measured
the stock switch, RATE, and continuous widgets one pixel to the left of the
caption grid, so those painters start at x = 24 + 26 * column.
The base view still draws its header, track rail, and page dots. The native
title slot supplies `Arpeggiator (3/4)` and `Keyboard (4/4)` to the stock
transient heading. The adapter uses the native focus tracker at view +`0x94`
and the same show-values byte as the stock pages.

The audio retrig page has RTRG `0x104` and RATE `0x107`. The stock descriptor
gives RTRG raw values `0x000` and `0x100`. RATE's formatter table at
`0x40213fb8` matches ARP indices 0 through 16 in order. Its raw value is the index
shifted left eight. ARP's two additional quarter-note rates use text. GATE,
SWNG and STRM reuse the native continuous control with projected values.
The adapter supplies values directly to the native control painter without
writing any stock parameter. It confines custom lock inversion to the body so
right-edge navigation marks remain visible.

One held step resolves to its own lock through the shared editor's held-step
mask. A held encoder click clears the corresponding track keyboard lock.
The stock edited marker protects the step from the stock short-tap toggle.
Multiple held steps and live recording refuse custom writes until those edits
can be committed as a single operation. A turn consumes the press gesture so
releasing an encoder after turning it cannot open a menu.

## Evidence [D]

- The host TRIG harness checks audio and MIDI navigation, allocation, track
  change, A–H versus LEVEL routing, click versus turn, single-step set and
  clear, and safe refusal during multiple held steps and live recording.
- `core/track-keyboard` builds with the ColdFire toolchain and its code
  passes the Patchkit ISA scan. The expansion binds stock `memcpy` and
  `memset`. GCC-generated switch jump tables are disabled in the two sources
  that use switches so the tables are kept outside executable sections.
- An emulator-only image applied successfully with `core/track-keyboard`,
  `core/instrument-play`, and `core/digikit-menu` as roots.
- The coupled panel regression booted that image. On audio track 1 it
  used the stock parameter key to open ARP, turned the enable control,
  and pressed a keyboard key with the sequencer stopped. The shared
  keyboard claimed the press. The stock audio audition counter rose to
  one on the generated note and went back to zero on release. The
  firmware did not report a fault.
- The panel learned three notes as offsets `0, +4, +7`. After applying
  USER, the KEYS page showed the same chord choice. The OLED regression
  asserts enough pixels are drawn below the header to catch an empty
  control area. The first attempt exposed that the formatted text routine
  expects a font wrapper, while this anchor points to a raw font
  descriptor. The adapter now uses the firmware's text-box routine.
- The host registration tests cover the MIDI vector with its own preserved
  page IDs. MIDI note delivery to the native worker, including note-off
  after a machine type change, is covered in [finding 369](369-track-keyboard-native-output.md).

The updated TRIG fixture checks the widget codes, raw value projections,
focus and held captions, lock inversion bounds, and stock RTRG override
message. It passed the x86 host build with undefined-behavior checks. The
new title slot and stock routine anchors match the stock 1.17 image. The
manual panel scenario has matching-value widget comparisons and captures
both track types in normal, focused, and held views.

## Open [O]

The first runtime binds track controls to a scratch pattern. Durable project
storage and native sequencer seed dispatch require their own integration.
The stock sample backend currently addresses one native voice per track.
Track reservations and summing the helpers through the owner's mixer
remain separate playback work.
