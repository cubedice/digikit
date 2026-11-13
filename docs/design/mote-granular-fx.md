# MOTE granular phrase effect proposal

Status: design for review, 2026-11-13. MOTE is a working name.

MOTE captures a phrase from chosen tracks or external audio, turns it into
grains, and lets the player crossfade those live sources into the captured
texture. The other tracks keep playing. Track selection uses a TRIG overlay
similar to mute mode; everyday playing needs no per-track FX-page edits or
DigiKit settings navigation.

The musical references are the rhythmic fragments and sustained textures of
[Microcosm](https://www.hologramelectronics.com/pages/microcosm) and the
always-listening capture of [MOOD MKII](https://www.chasebliss.com/mood-mkii).
MOTE exposes capture, grains, movement and space directly, instead of requiring
a different algorithm for each combination.

## Performance workflow

1. Press **FUNC + FX** to open the FX view, then repeat to select MOTE.
   **UP / DOWN** moves between MOTE's four pages. Press encoder **H, IN**.
2. Tap TRIGs to choose input tracks. Encoder **A, EXT** adds external L, R or
   stereo input. Hold a TRIG and turn LEVEL/DATA to balance that track's capture
   gain. Encoder B sets external capture gain.
3. Press H or NO to return to the same page. Set TAKE and CUE on LOOP.
4. Turn REC on, or press YES while in the input overlay. NEXT captures the
   upcoming phrase; PAST freezes the preceding phrase. Recording finishes
   automatically after one take and REC returns to OFF.
5. Turn **LEVEL/DATA, FADE** from **LIVE** toward **MOTE**. Only the sources in
   the held phrase fade out of the live mix. Adjust the grains, crop, movement
   and space while the phrase plays.
6. Choose different inputs for the next take if desired. They do not change
   the current crossfade until the new recording completes.

For example, capture tracks 3 and 7 while drums remain on tracks 1 and 2.
At full MOTE, the granular phrase replaces the live contributions of 3 and 7;
the drums retain their direct levels and sends. Choosing 3 and 9 for the next
take leaves that behavior intact until the new phrase is ready.

## Selective crossfade and routing

There is one performance routing model: selected-source replacement. This
supersedes the earlier separate SEND and MASTER modes and the proposed VOL
control. Selecting all 16 tracks covers all internal track sources; it does
not capture the final master, shared FX returns or compressor output.
External input always remains an explicit selection.

| FADE position | Sources in the held phrase | MOTE return | Other sources |
| --- | --- | --- | --- |
| LIVE, 0% | Normal live output | Silent | Normal output |
| Between endpoints | Gradually reduced | Gradually increased | Normal output |
| MOTE, 100% | Direct output removed | Full processed phrase | Normal output |

The recording taps are **before FADE**. A fully wet performance can still
capture another phrase at full input strength. Track capture follows the
track's normal audible level, pan and mute behavior, before shared send FX.
External capture has its own input gain, independent of its direct-monitor
fader. MOTE never writes the saved track LEVEL or External Mixer settings.

```text
Track audio / external capture tap
    +--> input selection + capture gain --> rolling / held phrase
    |                                        --> grains --> space --> wet gain --+
    +--> normal direct contribution --> selective dry gain ---------------------+--> mix
    +--> native send feeds --> selective send gain --> shared FX returns -------+
Other track contributions -----------------------------------------------------+
```

The held phrase stores its input membership alongside its audio. This
membership, rather than the next-take selection, controls the dry gain.
Capture gain balances the recorded mix; it is not the percentage of a track
that FADE replaces. A track recorded at a low input gain still has its whole
direct contribution replaced. A zero-gain input is excluded from the
committed membership and shown inactive. Turning a source off preserves its
last nonzero gain for reuse.

Use smoothed independent gains and an equal-power starting curve, with wet
headroom and grain-density compensation. This controls discontinuities and
density-related level jumps; it does not guarantee identical perceived
loudness for every texture. Other tracks' direct contributions and sends are
unchanged, although shared downstream compression may respond to the new mix.

The complete routing design also fades the selected sources' **new feeds to
native delay, reverb and chorus**. Existing shared tails finish naturally;
unselected sources keep feeding those effects. This needs an earlier routing
hook in addition to the selective direct gain. A direct-only prototype must
be identified as such: the existing send framework would otherwise keep
feeding native effects even at full MOTE. Existing mixed FX returns cannot be
separated back into tracks. Other custom effects keep their current routing.
The held membership controls both the direct and native-send gains; both
follow the same phrase handoff and fault recovery.

External L/R membership fades only those channels' direct monitoring inside
the device, preserving their saved monitor levels and pan. If direct
monitoring was already off, LIVE does not turn it on. Monitoring through a
separate audio interface is outside MOTE's routing.

## Navigation between effects and MOTE pages

MOTE occupies **one entry** alongside DEL, REV, CHO and the other loaded
effects. Its four control pages are nested inside that entry. The effect
order follows the FX framework's loaded-effect order; MOTE's internal pages
never add extra stops to the global effect list.

| Gesture | Result |
| --- | --- |
| FUNC + FX from outside the FX view | Open the FX view using its normal selected-effect behavior. |
| Repeat FUNC + FX in the FX view | Advance to the next effect, wrapping through DEL, REV, CHO and loaded effects. MOTE appears once. |
| DOWN while MOTE is selected | Next MOTE page: LOOP → GRAIN → MOVE → SPACE → LOOP. |
| UP while MOTE is selected | Previous MOTE page, wrapping from LOOP to SPACE. |
| H on a MOTE control page | Open its input overlay. |
| H or NO in the input overlay | Return to the MOTE page from which it was opened. |
| NO on a MOTE control page | Exit the FX view normally. |

The first visit to MOTE opens LOOP. Later visits restore its last control
page, including after visiting another effect or leaving the FX view. This
is UI state for the loaded instance, not an audio parameter or a per-pattern
setting; a fresh instance starts at LOOP. For example, leave GRAIN to adjust
delay, then return to MOTE and resume on GRAIN.

```text
FUNC + FX     DEL → REV → CHO → MOTE → other loaded effects → …
                                 |
UP / DOWN                 LOOP ↔ GRAIN ↔ MOVE ↔ SPACE
                          wraps within these four pages
```

The header identifies both levels, for example **MOTE GRAIN 2/4**, alongside
the pattern identity and capture status. The four right-edge page indicators
refer only to MOTE's pages. They do not count delay, reverb or chorus. Global
effect navigation remains available from every MOTE page.

While IN is open, UP/DOWN do not change the underlying control page or
escape to another effect. FUNC + FX closes IN, restores its key/LED ownership,
and advances exactly once to the next effect. H/NO only closes IN. Recording,
held playback, selected sources and FADE continue through every navigation
action; leaving the view is not bypassing the effect.
Consume that closing gesture and subsequent overlay-owned releases: the same
NO press must not also exit the FX view.

Implement this as a scoped navigation extension. The current FX framework
has a flat page list; only unmodified UP/DOWN events owned by MOTE's active
topmost control view, with no FUNC, TRIG or encoder held, become local
navigation. Stock effects retain their normal
behavior, and modified or held-TRIG gestures retain their existing owners.
Capture the complete navigation key gesture so it cannot also reach the
global page handler. Preserve native show-values handling without treating
its held or release events as additional effect advances. LEFT/RIGHT, the
physical PAGE key and LEVEL/DATA gain no new navigation role.
Show-values changes the display state, never the selected MOTE page.

## Four FX pages

Use **UP / DOWN** for **LOOP → GRAIN → MOVE → SPACE** within MOTE.
Use native fonts, focus feedback, control anchors and page indicators.
LEVEL/DATA has an intentionally explicit new role on these pages: the left
strip is labelled **FADE**, with MOTE at the top and LIVE at the bottom.
Turning it shows the exact percentage in normal focus feedback. It is a
control position, not an audio meter.

![Four MOTE page layouts](../../out/design/mote-fx/main-board.png)

| Encoder | LOOP | GRAIN | MOVE | SPACE |
| --- | --- | --- | --- | --- |
| A | REC | SIZE | POS | DIFF |
| B | FROM | RATE | SCAN | DEC |
| C | TAKE | TUNE | REVR | HPF |
| D | CUE | SHAP | WDTH | LPF |
| E | STRT | SPRY | Movement diagram | Space diagram |
| F | LEN | JITR | Movement diagram | Space diagram |
| G | Crop and duration | Grain overlap | Movement diagram | Space diagram |
| H press | IN | IN | IN | IN |

H occupies its entire bottom-right cell. Its small input-grid icon shows held
sources; an asterisk signals a different next selection. Turning H has no
effect. Diagram-only cells have no hidden parameter or action. There is no
second LEV or MIX knob competing with FADE.

Native show-values behavior substitutes formatted values under parameter
graphics. IN remains an action label and telemetry stays telemetry. Discrete
lists such as FROM and SHAP use deliberate detents, initially four raw ticks
per selection without acceleration. Numeric controls keep usable travel and
native focus formatting.

### LOOP captures and crops

| Control | Range or choice | Meaning |
| --- | --- | --- |
| REC | OFF / ON action | Arms one take; OFF cancels an incomplete take. Completion returns to OFF. |
| FROM | NEXT / PAST | NEXT starts at CUE; PAST ends at CUE. |
| TAKE | PAT or 1–128 master steps | PAT captures one complete master-pattern cycle. Values exceeding available storage are unavailable. |
| CUE | NOW or a one-based master step | The next occurrence of that boundary; no timing ambiguity between start and end. |
| STRT | Start through the latest valid sample | Non-destructive crop start, displayed as a percentage; always leaves room for the minimum safe LEN. |
| LEN | Minimum safe window through the remaining take | Crop length, shown as a proportion of the full take; constrained so start plus length stays within the recording. |

G shows the full take as a small timeline, brackets around the active crop,
and the **active loop duration**. In the example, TAKE 64 at 120 BPM and normal
1× pattern scale produced
8 seconds; STRT 25% and LEN 50% play a 4-second crop. Focus feedback reports
both time and captured-grid position where applicable. Increasing STRT past
the old end preserves length where possible, otherwise shortens it visibly.

STRT and LEN change playback live with smoothed read transitions. Grains wrap
inside the crop, including when SIZE exceeds LEN; they never read excluded
audio. Changing the crop does not alter the source membership or erase audio.

### GRAIN shapes the fragments

SIZE sets grain duration, initially about 5–500 ms. RATE sets their launch
interval using pattern BPM divisions and triplets, plus OFF. TUNE shifts grain
pitch independently of the scan head, initially ±24 semitones with fine
adjustment. SHAP selects SOFT, TRI, RISE or FALL windows, all with a minimum
anti-click edge. SPRY scatters grain origins around the head; JITR randomizes
launch timing. Both at zero give a predictable rhythmic result.

G visualizes overlapping windows and reports active grains against the actual
engine limit. The render's 3/16 is illustrative: the maximum must be profiled
with CANOPY and Overloop. Exhaustion shows LIMIT and skips a launch rather
than abruptly stealing a grain or silently changing SIZE or RATE. RATE OFF
stops new grains; existing grains and space tails finish. It does not erase
the phrase or move FADE, so full MOTE can intentionally decay to silence.

### MOVE traverses the phrase

POS sets the base position inside the crop. SCAN moves a separate head at
−2× to +2× recorded-time speed; zero stays in place while grains continue.
REVR sets the proportion of individual grains played backwards, independently
of scan direction. WDTH spreads grains in stereo while preserving the
captured stereo relationships.

The diagram uses end brackets for the crop, a triangle for stored POS, a
vertical cursor for the moving head, and small markers for actual grain
origins. The saved POS value does not drift with the head. Direction arrows
distinguish scan movement from reverse-playing grains.

### SPACE softens and filters

DIFF adds diffusion to the grains. DEC sets the resulting tail duration;
it is independent of TAKE and LEN. HPF and LPF filter the processed sound.
The diagram turns discrete strokes into a diminishing tail, with native-style
filter silhouettes above it. DIFF belongs to the wet sound and never changes
the live-to-MOTE balance. Destructive overdubbing and feedback into the
recording are outside the initial design.

## Input performance overlay

Press H from any MOTE page to open IN; press H or NO to close it and return to
that page. This is a live performance overlay, not a settings list. Source
edits take effect for the next recording as they are made; closing the overlay
does not cancel them. The ordinary mute-mode shortcut remains untouched.

![MOTE input selection layouts](../../out/design/mote-fx/inputs-board.png)

| Gesture | Action |
| --- | --- |
| Tap TRIG 1–16 | Toggle that track in the next recording. |
| FUNC + TRIG | Select only that internal track; external selection remains explicit. |
| Hold TRIG + turn LEVEL/DATA | Set that track's capture gain, 0–127. |
| Turn A, EXT | OFF / L / R / ST for the next recording. |
| Turn B, external GAIN | Set external capture gain, 0–127; stereo channels remain linked. |
| YES | Start or arm one take using FROM, TAKE and CUE; while armed/recording, cancel it. |
| H or NO | Close the overlay without changing transport or capture state. |
| FUNC + FX | Close the overlay and advance to the next effect; capture continues. |
| UP / DOWN | Stay in the overlay; preserve the underlying MOTE page. |
| LEVEL/DATA with no TRIG held | Continue controlling FADE. |

The screen retains the FADE strip and uses two rows of eight numbered tiles,
matching the TRIG layout. **A solid inner tile means the held phrase uses that
source; an outline means the next recording will use it.** A tile can have
both marks. A plain number belongs to neither. L/R tiles use the same code for
external input. The on-screen legend stays visible.

The second rendering shows held sources 3 and 7, with 3 and 9 selected next.
Track 7 remains under the crossfader until the new phrase commits; track 9
continues live meanwhile. The asterisk in the header marks the difference.
The armed example also includes external stereo in the next take.

In the overlay, TRIG LEDs show the next selection: bright selected, dim
excluded. The screen carries the held/next distinction without requiring
ambiguous blinking or extra LED colors. LED ownership and restoration need
device validation.

Holding a TRIG reveals its number/name and capture gain; the left strip
temporarily reads GAIN. Releasing the last held TRIG returns it to FADE. A tap toggles
on release, and a gain adjustment consumes that tap so release does not also
deselect the track. If several TRIGs are held, the most recently pressed one
still held owns gain editing. Closing the overlay restores FADE and normal
LED ownership immediately, while consuming later releases from its gestures.
The overlay owns complete key-down/key-up pairs so these
gestures cannot trigger notes, change track, edit locks or alter mutes.

Input selection and gain editing are locked during ARMED and RECORDING. The
header shows ARM or REC, the overlay shows LOCK, and YES CANCEL stays
available. FADE remains playable. This avoids a third hidden source set
between the held phrase and the take currently being captured.

## Capture state and phrase handoff

The header reports the actual capture state: EMPTY, FILLING, READY, ARMED,
RECORDING or HELD, using compact labels where required. A held phrase and a
refilling rolling history can coexist: the header prioritizes armed/recording
activity, while LOOP G continues showing the held crop. Focus/status feedback
shows rolling-history availability and the pending cue. Stale telemetry shows
LINK LOST instead of frozen animation that appears live.

Changing next sources or capture gains resets the rolling history. PAST is
available only after enough audio with that exact input mix exists; a stereo
recording cannot remove previously mixed sources afterward. FILLING shows
available/required duration. It never clears the held phrase.

Arming latches input membership, gains, the number of master-step intervals
and the cue. PAT resolves to the current pattern's cycle length at arm time.
Count musical intervals instead of freezing their duration in seconds: tempo
changes can lengthen or shorten the take. NOW retains its fractional step
phase so a complete cycle still has a complete cycle's duration. PAST uses
recorded timeline markers to locate the corresponding historical boundary.
These phase and history requirements belong to the capture clock contract.

A numbered CUE waits for running transport. NOW can capture while stopped
using a continuous grid derived from pattern BPM; there is no local tempo
control. A pattern change or timeline discontinuity that invalidates a pending
take cancels it and refills history, keeping the previous held phrase. Storage
checks apply to the actual samples: an overlong take, including one extended
by a tempo change, aborts safely with TOO LONG. Out-of-range cues show an inline
reason. Held playback continues while stopped, with RATE tied to pattern BPM.

An empty effective input selection shows NO INPUT and cannot arm. This covers
all sources off or at zero gain, while still allowing silence from a selected
nonzero source to be recorded. The held phrase and FADE remain unchanged.

On successful capture, audio, crop metadata and membership change together.
Keep the existing crop proportions where valid and clamp visibly for the new
take. REC returns to OFF; FADE keeps its current position. During a short,
smoothed handoff, old-only sources return to live, new-only sources take on
the current attenuation, and common sources retain it. Old/new grain outputs
transition together. Target about 20 ms initially, subject to listening and
CPU tests; both recordings remain readable until no old reader remains.
Show HANDOFF and reject another REC/YES capture until this bounded transition
finishes. Retire all old readers before the bank becomes writable rolling
storage again, then show FILLING as new history accumulates.

Cancel or failed capture preserves the previous phrase, membership and FADE.
Pattern changes outside capture do not automatically move the fader: the same
numbered live tracks remain under the held phrase's ownership. Completing the
first recording never automatically advances FADE.

Without a valid held phrase, FADE stays LIVE and the UI reports NO LOOP.
Clear, bypass, unload or an engine fault restores the direct sources and
mutes the processed return. Fault fallback belongs to the routing adapter,
so it works even when the grain engine stops running. Reset the visible FADE
to LIVE and require an explicit turn after recovery. The fallback must also
restore native-send feeds. Clear is a deliberate side action, not another
performance knob.

## Implementation scope

The first target is DTII OS1.17. DNII OS1.12 should share the control design,
but its long-storage and timing port requires separate validation.

| Work | Required result |
| --- | --- |
| Selective routing | Independent recording taps and smoothed direct-contribution gains for selected tracks and external monitoring. Never scale shared buses in place: per-track USB outputs must retain their normal signals. |
| Native send following | Fade new selected-source feeds to delay/reverb/chorus before their shared returns are formed. Keep existing tails and other sources intact. |
| Capture ownership | Atomic audio/membership commit, cancellation, valid-history tracking, and old-bank lifetime through a replacement transition. |
| Storage | Reserve MOTE's own rolling and held banks without borrowing Overloop's single-consumer buffer. |
| Grain DSP | Interpolated reads, safe window edges, live crop changes, bounded grains, density compensation and bounded diffusion. |
| FX framework | One global MOTE entry with four nested pages, scoped navigation and page recall, more parameter storage than the current eight-word contract, action controls, input/LED ownership and versioned telemetry. |
| Persistence | Save parameters and next-input choices. Capture audio is volatile initially; boot returns FADE to LIVE. Native trig locks and MOD destinations need a separate FX extension. |

The existing [send framework](../../modules/core/adapters/fx/README.md) builds
selected-track wet input but cannot attenuate the original direct
contributions. Its current [page framework](../../modules/core/adapters/fxpages/README.md)
also does not provide this four-page surface. These are explicit framework
changes, not module-only parameter declarations.

The [DTII loop reservation](../../modules/core/adapters/loop-storage/README.md)
currently provides one 8 MiB buffer for one consumer, about 21.84 seconds of
48 kHz float stereo. Proposed MOTE storage is two additional 8 MiB banks.
Keeping Overloop's 8 MiB would reserve 24 MiB total and leave 376 MiB of the
original 400 MiB sample pool, before any further reservation needs. Exact
usable durations and allocation safety require validation. Old held audio
must retire safely before its bank resumes rolling capture, with honest
FILLING feedback during refill.
Extend one coordinated allocator reservation/provider to expose these
separate regions while preserving Overloop's descriptor and buffer. Do not
stack independent sample-capacity patches over the current single provider.

Acceptance must cover selective tracks and external channels at both fader
ends; unchanged unselected contributions; preserved per-track USB; native
send feed following; held/next changes while fully wet; capture failure and
fault fallback; stopped transport; full-memory operation alongside Overloop;
bounded CPU with CANOPY; and click-free extreme crop/pitch/size transitions.
UI acceptance includes press/release ownership, LED restoration, normal and
show-values captures, boundary values and every capture state. Navigation
acceptance covers both local wrap directions, one global effect advance from
every MOTE page and IN, last-page recall after exit, stock-effect navigation,
modifier/held-TRIG ownership, and continued capture/playback/FADE through all
page changes. Hardware measurements follow emulator and preflight checks.

## Review layouts

The renderings use a 128 × 64 monochrome grid and the local DTII OS1.17 font.
They are proposed layouts, not device screenshots or proof of stock-identical
FX chrome. The main-page geometry reserves x0–21 for FADE, x22–123 for the
control cells, and x124–127 for page indicators. The input overlay expands
the content region through x127 and retains FADE.

- [Four-page board](../../out/design/mote-fx/main-board.png).
- [Input overlay states](../../out/design/mote-fx/inputs-board.png).
- [LIVE, midpoint and MOTE positions](../../out/design/mote-fx/fade-board.png).
- [Show-values board](../../out/design/mote-fx/values-board.png).
- [Combined local preview](../../out/design/mote-fx/index.html).
- [Render manifest](../../out/design/mote-fx/manifest.json).

Firmware-derived fonts and rendered images remain under ignored `out/`.
The local renderer is `out/design/render_mote_fx_v2.py`; the text, control
table and behavior above remain reviewable without generated assets.
