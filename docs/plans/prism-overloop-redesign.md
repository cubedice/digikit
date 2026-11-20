# PRISM and Overloop: redesign proposal

Status: steps 1 to 4 of section 7 are built; what remains is the install (step 5, the owner's). Step 1, the engine, is
measured on the SHARC-only rig ([finding 358](../findings/358-prism-repeater-engine.md)); step 2, PRISM as a send effect
with the one page and without `core/phrase-route` ([finding 359](../findings/359-prism-send-effect-and-page.md)); step 3,
Overloop as an always-listening looper without `core/loop-controls` ([finding 360](../findings/360-overloop-automatic-loop.md));
step 4, the loop as PRISM's input with INPT, XFAD and ARM on PRISM's page, and PRISM's output carried past Overloop's
crossfader ([finding 361](../findings/361-prism-loop-input-and-return.md)). Steps 2 to 4 are proven on the emulated
Digitakt II; the findings list where the pages differ from the drawings below. The owner's decision of 2026-11-19 is built
too: the per-track PRS send amounts of section 2.3 are replaced by tracks chosen on or off with the TRIG keys while G (INPT)
is held on PRISM's page, at full send, and the send page has no PRS cell
([finding 362](../findings/362-prism-track-select.md)); it is proven on the emulated Digitakt II and has not run on
hardware. After the owner's first use on the device (image `d0e4825dd04d8646`): the position stream, Overloop's run and its
status tolerate frames of the link that come twice or late, ARM is on Overloop's encoder E under its label, and PRISM's INPT
is a two-position switch turned by G at any time ([finding 363](../findings/363-loop-link-jitter.md)); SYNC follows the
Mimeophon, ZONE an octave of the synced time and RATE a ratio inside it, in place of section 2.1's note lengths
([finding 364](../findings/364-prism-sync-zones.md)). After the owner's use of image `8278188469b2dc9f`: a queued pattern
takes the loop where the new pattern is heard (the position record carries the playing pattern), and the loop is silent
while the transport is stopped, START begins it at its first frame and CONTINUE where it stood, which replaces "STOP:
the loop plays on" ([finding 365](../findings/365-loop-transport-and-playing-pattern.md)). Digitakt II 1.17 only, as today.

The screen drawings are schematic: they show what each cell holds and where, not pixels. The real screen is 128 x 64 and
uses the native knob and text primitives that `core/phrase-ui` draws with today.

## 1. Summary

| | Today | Proposed |
|---|---|---|
| PRISM | Five pages (DELAY, LOOP, GRAIN, MOVE, SPACE) and an input overlay; its own routing with four firmware hooks; takes phrases from selected tracks and ducks them | One page. A send effect: each track has a PRISM send amount, PRISM's output goes to the FX return. A repeater modelled on the Mimeophon: ZONE, RATE, REPEATS, COLOR, HALO, SKEW, FLIP, HOLD |
| Input selection | 18-bit source mask, a gain per track, EXT L/R, an overlay on the TRIG keys | Removed. The per-track send amount decides what PRISM hears |
| Overloop | Four knobs (REC, START, FADE, LENGTH); you arm, it waits for START, records LENGTH steps, and a helper copies knob values into the next pattern's kit | Always listening. One action: ARM. At the next pattern change it keeps the last full cycle of the pattern that was playing. No START, no LENGTH |
| Crossfade | Overloop's FADE knob on Overloop's page | On the PRISM page: the crossfader, a switch that feeds PRISM from the live tracks or from the loop, and all PRISM controls together |
| Image adapters of the fork | `phrase-route`, `phrase-control`, `phrase-ui`, `loop-controls`, `loop-display`, `loop-storage`, `pattern-position` | `phrase-route` and `loop-controls` go away; `phrase-ui` and `phrase-control` shrink to one page and about a dozen control words |

## 2. PRISM

### 2.1 The page

One page, eight encoders, the LEVEL/DATA knob, and five encoder presses.

```
┌──────────────────────────────────────────────────────────┐
│ A02 │ PRISM                               ECHO   375 MS  │
├─────┬────────────┬────────────┬────────────┬─────────────┤
│ MIX │ ZONE       │ RATE       │ RPTS       │ COLR        │
│  █  │ ····■···   │ 1/8.       │ 62         │ DARK 20     │
│  █  │ ECHO       │ SYNC       │            │             │
│  █  ├────────────┼────────────┼────────────┼─────────────┤
│  ░  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  ░  │ 35         │ L 12       │ LIVE       │ --          │
│ 80  │            │            │            │ NO LOOP     │
└─────┴────────────┴────────────┴────────────┴─────────────┘
  LEVEL    A / E        B / F        C / G        D / H
```

| Control | Encoder | What it does | Mimeophon counterpart |
|---|---|---|---|
| ZONE | A | Picks one of eight time ranges. A change of zone crossfades to the new time; the pitch of what is in the buffer does not bend | Zone |
| RATE | B | Moves the time inside the zone. Free: continuous, and the pitch bends while it moves, like tape. SYNC: steps through note lengths of the pattern tempo, each step crossfaded | Rate, and tempo sync |
| RPTS | C | Repeats: 0 is one echo, about three quarters is endless, the last quarter grows until the limiter holds it | Repeats |
| COLR | D | Colour of the repeats. Left of centre each repeat is darker and softer, centre is clean, right of centre each repeat is thinner and brighter | Color |
| HALO | E | Smears each repeat a little more than the last; high settings turn the repeats into a wash | Halo |
| SKEW | F | Makes the left side shorter and the right side longer (or the reverse). 0 is even | Skew |
| INPT | G | PRISM's input, a switch with two positions. LIVE: the tracks, by their send amounts. LOOP: Overloop's loop, and the track sends are closed. Without a loop it stays at LIVE (section 4) | - |
| XFAD | H | Overloop's crossfader between the live pattern and the loop (section 4) | - |
| MIX | LEVEL | PRISM's level on the FX return | Mix |

| Press | Action | Shown as |
|---|---|---|
| A (ZONE) | FLIP: the repeats play backwards | `FLIP` under the zone name |
| B (RATE) | SYNC on or off | `SYNC` or `FREE` under the rate |
| C (RPTS) | HOLD: the input is closed and what is in the buffer repeats without loss. ZONE and RATE then pick a part of the held sound and do not change it | `HOLD` under the repeats |
| G (INPT) | Switch the input, the same as turning G | `LIVE` or `LOOP` in the INPT cell |
| H (XFAD) | ARM Overloop (section 3) | `ARMD` in the XFAD cell |
| FUNC + NO | Drop the loop | `NO LOOP` |

FUNC + RATE turns the rate finely and always bends the pitch (the Mimeophon's µRate has a modulation input; there is
none here).

With HOLD and FLIP on, and RATE free:

```
┌──────────────────────────────────────────────────────────┐
│ A02 │ PRISM                               SLAP    92 MS  │
├─────┬────────────┬────────────┬────────────┬─────────────┤
│ MIX │ ZONE       │ RATE       │ RPTS       │ COLR        │
│  █  │ ···■····   │ 92 MS      │ INF        │ BRIT 44     │
│  █  │ SLAP  FLIP │ FREE       │ HOLD       │             │
│  █  ├────────────┼────────────┼────────────┼─────────────┤
│  █  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  ░  │ 70         │ R 30       │ LIVE       │ --          │
│ 104 │            │            │            │ NO LOOP     │
└─────┴────────────┴────────────┴────────────┴─────────────┘
```

### 2.2 Zones

Proposed ranges. They overlap so that RATE can cross a boundary in either zone. The names are placeholders; the ranges
have to be set by ear against the Mimeophon and its manual, which I have not checked for exact figures.

| Zone | Name | Free range | Character |
|---|---|---|---|
| 1 | KARP | 1 to 10 ms | pitched string, comb |
| 2 | FLNG | 5 to 40 ms | flange, metallic |
| 3 | CHOR | 20 to 120 ms | chorus, doubling |
| 4 | SLAP | 60 to 350 ms | slapback |
| 5 | ECHO | 150 ms to 1 s | echo |
| 6 | LONG | 0.5 to 2.7 s | long echo |
| 7 | PHRS | 1.3 to 5.4 s | phrase |
| 8 | LOOP | 2.7 to 10.7 s | looping |

The longest time today is 10.75 s (half of one 8 MiB bank). Using the whole bank would give 21.5 s.

### 2.3 Where the sound comes from and goes

PRISM becomes an ordinary send effect of the effect framework that is already in the image:

- Each track gets a PRISM send amount on the second page of the track's FX key. That page exists today for send effects.

```
┌──────────────────────────────────────────────────────────┐
│ A02 │ TRK 3  FX SENDS                                     │
├─────┬────────────┬────────────┬────────────┬─────────────┤
│     │ PRS        │            │            │             │
│     │ 64         │            │            │             │
└─────┴────────────┴────────────┴────────────┴─────────────┘
```

- PRISM's input is one of two things, chosen by INPT: the sum of the tracks times their send amounts (LIVE), or
  Overloop's loop (LOOP). A change of INPT is crossfaded over a few milliseconds, and what is already in PRISM's
  buffer keeps repeating.
- PRISM's output times MIX is added to the FX return. The dry tracks are not touched.

What goes away with this:

- The source mask, the per-track capture gains, the EXT L/R sources and the TRIG overlay.
- The ducking of the selected tracks (today's FADE law). A send effect adds; it does not take the dry sound away.
- `core/phrase-route` with its four hooks in the stock send and master code, its private copies of 18 buses, and the
  copy of the effect guard that the upstream merge of 2026-11-19 forced it to carry.
- PRISM's own phrase capture (the LOOP page). Capturing a pattern is Overloop's job.

### 2.4 What changes in the engine

| Block today | Proposed |
|---|---|
| Tape with one read head; TIME glides at half a frame per sample | Two read heads. Free RATE glides (pitch bends); a zone change, a SYNC step, FLIP and HOLD crossfade between the heads. This also removes the kink at a mode change and the 10 ms dip per reverse segment that finding 354 lists as open |
| Feedback: fixed low pass near 3 kHz, gain 0 to 1.1, soft limit | COLR: a tilt filter in the feedback path, dark to bright, with saturation on the dark side. RPTS keeps the 0 to 1.1 range on a new curve |
| SPACE: four-line diffusion network after the delay, with its own page | HALO: the same network inside the repeat path, so every pass smears more |
| SKEW lengthens one side by up to 50 % | Both sides move: one shorter, one longer |
| WOW, DRIV, BAL | Removed as controls. A little drift stays in the dark half of COLR |
| Eight grains, MOVE (scan, position), TUNE | Removed |
| Phrase capture, two banks that swap | Removed from PRISM (one bank is the tape) |

Load: today's heaviest block is 42,928 of the 48,000 instructions PRISM reserves, and the grains are computed even when
they are silent. Without grains and capture the load should fall clearly; I have not measured by how much.

## 3. Overloop

### 3.1 Behaviour

Overloop records the master mix all the time into a rolling buffer and notes where each pattern cycle began.

1. ARM. One action. It can be given at any point in the pattern, also in its last step.
2. At the next pattern change Overloop keeps the last complete cycle of the pattern that was playing. The length is the
   pattern's length; nothing is set by hand.
3. From that boundary the loop plays in time, the crossfader stands at LOOP, and the new pattern runs underneath,
   unheard. What you hear does not change at the boundary.
4. At the same boundary PRISM's input switches to LOOP by itself. This happens at every pattern change that was
   armed.
5. Turning the crossfader to LIVE brings the new pattern in, dry.

Details:

- The loop holds what was heard: the tracks, the stock effects, and PRISM's output at that time.
- A loop stays until the next armed change replaces it or FUNC + NO drops it. With the crossfader at LIVE it is silent.
  When a loop is dropped, PRISM's input goes back to LIVE.
- ARM is one-shot. Arming again while a loop plays is allowed: the next change records what is heard then, the new
  pattern and whatever of the old loop is still mixed in.
- The buffer holds 21.8 s. A pattern cycle longer than that (128 steps below about 88 BPM) is cut to the last 21.8 s,
  and the page says so.
- The crossfader, PRISM's input switch and the armed state are not stored in the kit. They do not change when the pattern changes,
  so the helper that copies knob values into the next kit (`core/loop-controls`) is no longer needed.

### 3.2 Overloop's own page

Overloop stays usable without PRISM. Its page keeps the timeline drawing it has today and two knobs instead of four.

```
┌──────────────────────────────────────────────────────────┐
│ A02 │ OVERLOOP                                    ARMED  │
├─────┬────────────────────────────────────────────────────┤
│     │ NEXT CHANGE KEEPS  A02  64 ST  7.74 S              │
│     │ ├──────────────────────────▲───────────────┤       │
│     │ LIVE ●━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ LOOP       │
│     ├────────────┬───────────────────────────────────────┤
│     │ ARM        │ XFAD                                  │
│     │ ON         │ LIVE                                  │
└─────┴────────────┴───────────────────────────────────────┘
```

ARM is a knob here because a plain effect page has no keys: turn it up to arm, as REC works today. XFAD is the same
value the PRISM page shows.

## 4. Performing a transition

PRISM hears either the live tracks or the loop, never both. An armed pattern change switches it to the loop. The loop
reaches PRISM after the crossfader, so its feed fades with it.

```
 tracks ──┬────────────────────────────► master mix = LIVE ──┐
          │ per-track PRISM send                             ├─ XFAD ──► out
          ▼                                                  │    ▲
        LIVE ─┐                    LOOP ──► (loop side) ─────┘    │
              ├─ INPT ─► PRISM ─── MIX ───► FX return ────────────┘
        LOOP ─┘
          ▲
          └── the loop, at the level the crossfader gives it
```

Step by step, all on the PRISM page.

**1. Pattern A02 plays, PRISM works on the live tracks. Press H to arm.**

```
│ A02 │ PRISM                               ECHO   375 MS  │
│ ...                                                      │
│  ░  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  ░  │ 35         │ L 12       │ LIVE       │ ARMD        │
│ 80  │            │            │            │ KEEPS A02   │
```

**2. Choose A03 the usual way. At the change the loop takes over, the crossfader stands at LOOP and INPT switches to
LOOP by itself. The sound continues as A02.**

```
│ A03 │ PRISM                    LOOP A02  64 ST  7.74 S   │
│ ...                                                      │
│  ░  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  ░  │ 35         │ L 12       │ LOOP       │ LOOP        │
│ 80  │            │            │            │ ────────●   │
```

**3. Play PRISM on the old pattern: ZONE, RATE, RPTS, COLR, HALO, MIX; press C to hold. A03 is not touched by any of
it.**

```
│ A03 │ PRISM                    LOOP A02  64 ST  7.74 S   │
│ MIX │ ZONE       │ RATE       │ RPTS       │ COLR        │
│  █  │ ·····■··   │ 1/2        │ INF        │ DARK 35     │
│  █  │ LONG       │ SYNC       │ HOLD       │             │
│  █  ├────────────┼────────────┼────────────┼─────────────┤
│  █  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  █  │ 80         │ L 12       │ LOOP       │ LOOP        │
│ 110 │            │            │            │ ────────●   │
```

**4. Turn H towards LIVE. A03 comes in dry: its sends do not reach PRISM while INPT is LOOP. The loop and its feed into
PRISM fade together; what PRISM already holds rings on as RPTS allows.**

```
│  █  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  █  │ 80         │ L 12       │ LOOP       │ 38          │
│ 110 │            │            │            │ ───●─────   │
```

**5. H at LIVE: A03 alone and dry, with the last echoes of A02 dying away. Set PRISM up for the new pattern while it
hears nothing new.**

**6. Press or turn G: INPT goes to LIVE and PRISM works on A03 by its track sends.**

```
│ A03 │ PRISM                               ECHO   375 MS  │
│ ...                                                      │
│  ░  │ HALO       │ SKEW       │ INPT       │ XFAD        │
│  ░  │ 35         │ L 12       │ LIVE       │ LIVE        │
│ 80  │            │            │            │ ●────────   │
```

The loop stays ready until the next armed change or FUNC + NO. Switching INPT back to LOOP while the crossfader is at
LIVE feeds PRISM nothing, because the loop's feed follows the crossfader.

## 5. Decisions for the owner

| # | Question | Recommendation |
|---|---|---|
| 1 | Are PRISM's settings global or stored in each kit? | Global, as today. Stored per kit they would jump at every pattern change, in the middle of a transition |
| 2 | Does the loop reach PRISM before or after the crossfader? | **Decided (owner, 2026-11-18): after.** Crossfading to LIVE then also closes PRISM's feed, and the new pattern arrives dry with the old echoes dying away. Before the crossfader, the old pattern would keep feeding PRISM at full level until INPT is switched |
| 2a | Does INPT go back to LIVE by itself when the crossfader reaches LIVE? | No. It is the owner's move, so PRISM can be set up for the new pattern first |
| 3 | Does the loop hold what was heard, or the tracks without PRISM? | **Decided (owner, 2026-11-18): the mix without PRISM.** PRISM goes on sounding over the loop at its full level, so a loop that held PRISM's echoes would play them twice at the change and keep them where no PRISM control reaches them. What is heard still goes on unchanged at the change: the loop plus PRISM, which rings on |
| 4 | External inputs into PRISM | Not in the first version. A send effect sums track buses, and the external input is not one. Today's EXT L/R source is lost |
| 5 | Keep Overloop usable without PRISM, with its own page? | Yes |
| 6 | When the crossfader reaches LIVE, keep the loop or drop it? | Keep it |
| 7 | The granular engine (GRAIN, MOVE) | Remove it from PRISM. It could come back as a separate effect; that is not part of this proposal |
| 8 | Zone names, ranges, and the longest time (10.7 s or 21.5 s) | Set by ear in step 1 below |
| 9 | How is PRISM heard past the crossfader, when Overloop is a master insert behind the FX return? | **Decided (owner, 2026-11-18): option B.** While Overloop runs, PRISM's output does not go to the FX return: Overloop adds it after its crossfader and after its record, at the master stage's gain (its +10 dB and the volume ramp). Accepted with it: with Overloop loaded, PRISM's echoes skip the master compressor, the clip of the master stage and the master overdrive, also without a loop; they follow the master volume. Without Overloop PRISM goes to the FX return as before |
| 10 | At what level does the loop feed PRISM? | **Decided (owner, 2026-11-18):** INPT stays a plain switch; the loop's feed gets a fixed trim, -6 dB on the whole mix at full send (`PRISM_LOOP_TRIM` in `modules/fx/prism/src/prism.h`), set by ear after the install. No control on the page |

## 6. What is lost

- The granular engine and the scan through a held phrase.
- Phrase capture from selected tracks: PRISM no longer takes a phrase of some tracks and replaces them. Overloop
  captures the whole mix.
- Ducking of the sources, and external inputs as a source.
- Overloop's START and LENGTH: a loop is always the last full cycle.
- WOW, DRIV and BAL as controls.

## 7. Steps, effort and confidence

| Step | Work | Proof | Effort (agent days) |
|---|---|---|---|
| 1 | PRISM engine on the rig: two heads, zones, COLR, HALO in the loop, SKEW, FLIP, HOLD; grains and capture out | Engine tests with numeric reports (step sizes, pitch during a RATE move, decay per repeat); WAVs for the owner | 3 to 4 (built) |
| 2 | PRISM as a send effect with the one page; `phrase-route` removed | Device tests: per-track send, FX return, page and presses; coexistence with CANOPY | 2 to 3 (built) |
| 3 | Overloop: rolling record, loop of the last cycle at an armed change, crossfader and send outside the kit; `loop-controls` removed | Rig tests of the seam (loop length against the pattern length, level across the boundary); device test of an armed change | 2 to 3 (built) |
| 4 | Loop into PRISM; INPT, XFAD and ARM on the PRISM page; INPT switches to LOOP at an armed change | Device test of the whole transition: PRISM hears only the loop after the change, the new pattern is dry at LIVE, and PRISM hears the tracks after INPT is switched | 1 to 2 (built) |
| 5 | Install on the Digitakt and listen | The owner | - |

Total 8 to 12 agent days.

Payoff: both effects become things to play, the crossfade becomes one gesture, and the fork loses its two most invasive
adapters (the routing hooks and the kit helper), which also makes the next upstream merges cheaper.

Confidence:

- High that the page, the send route and the simpler Overloop can be built: each uses mechanisms that exist in the tree.
- Medium for the seam of the automatic loop. The pattern pointer can run ahead of what is heard (finding 133), swing and
  micro timing are not in the step grid, and a pattern change that does not wait for the pattern end has to be defined.
- "Sounds very similar to the Mimeophon" is not something the tests can show. It needs the owner's ears, ideally against
  recordings of the module with the same input.

Open technical points:

- (Settled in step 2, finding 359.) The native page was tied to the exclusive phrase route only in the fx service: the
  page's callbacks sit in the FX view's own table and ask which page is shown, so a send effect has the painted page
  with encoder presses without a new hook.
- Overloop runs after the send effects in a block, so PRISM hears the loop one block (0.7 ms) late, and at an armed change
  it hears the new pattern for that one block (finding 361).
- An effect page of the plain framework has seven knobs and no keys. That is why PRISM keeps a native page.
- Upstream's reload of modules (E5) and its folder regroup (E8) are still open; the new adapters should follow
  upstream's service layout where it is settled.
