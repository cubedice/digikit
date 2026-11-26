# 373. PRISM's COLR after the Mimeophon's Color: one filter chain, uncoloured at three quarters

The owner's request of 2026-11-26: make PRISM's COLR match the Mimeophon's Color. Digitakt II OS 1.17, the engine on the
SHARC-only rig, the provider and the page on the host. Nothing here ran on hardware and nobody has listened. Marks as in
`docs/FINDINGS.md`; every result here is **[D]** (measured once in this work) unless it is marked **[O]**. It replaces
the colour of [finding 358](358-prism-repeater-engine.md) ("Colour", the COLR rows of its table and its COLR choice);
the rest of 358 stands.

## What the Mimeophon's manual says

Read from the manual's text (makenoise-manuals.com, `mimeophon-manual.pdf`); it gives no frequency and no level.

- The panel control is uncoloured ("Unity") at about 3 o'clock, not at noon.
- Its range is named after delays, from the lowest setting up: the darkest oil-can echo, a warm BBD, a mid-range crunch
  BBD, tape echo, a crisp clean delay, a thin sizzling early digital delay line.
- It is in the feedback network, so its effect adds up over the repeats, and it sets the timbre of Halo.
- It colours the first echo too (the manual's patch with Repeats at the minimum uses it as a filter).

PRISM had the last two already (COLR sits between the heads and HALO, inside the loop). It differed in the first two:
flat at the centre, one low pass (800 Hz) below it and one high pass (1 kHz) above it.

## The design

One chain at every position, in `src/dsp/loop.s`: a one-pole high pass, a one-pole low pass, a cubic saturation. Three
numbers set it (the two coefficients and the saturation); they come from a table of nine rows over COLR -1..1 with
straight lines between the rows, and they move through a block in 32 equal steps. The frequencies are this work's choice
**[O]**: the manual names characters, and these numbers are to be set by ear.

| COLR | Page value | After the manual | Low pass | High pass | Saturation |
| ---: | ---: | --- | --- | ---: | ---: |
| -1 | 0 | oil can | 600 Hz | 150 Hz | 0.6 |
| -0.75 | 16 | | 1.1 kHz | 80 Hz | 0.5 |
| -0.5 | 32 | warm BBD | 2 kHz | 40 Hz | 0.5 |
| -0.25 | 48 | mid-range crunch BBD | 2.8 kHz | 350 Hz | 1 |
| 0 | 64 | | 4.2 kHz | 180 Hz | 0.7 |
| 0.25 | 80 | tape | 8 kHz | 50 Hz | 0.3 |
| 0.5 | 96 | clean | none | 2 Hz | 0 |
| 0.75 | 111.5 | | none, the top lifted (k 1.12) | 300 Hz | 0 |
| 1 | 127 | thin early digital | none, the top lifted (k 1.25) | 900 Hz | 0 |

- **High pass**: `y = (1 - k / 2) (x - lp)`, then `lp += k (x - lp)`. It is exactly 1 at the top of the band and less
  below, so a repeat at unity never grows through it. At the uncoloured row it is the DC blocker (2 Hz). A first form,
  `x - lp` after the update, lost 0.002 dB a pass at every frequency: unity repeats fell to 0.9977 in nine turns.
- **Low pass**: `lp += k (y - lp)`. `k = 1` passes the signal as it is; above 1 it lifts the top (k 1.25: +1.15 dB at
  10 kHz, nothing at 4 kHz), the "sizzle" of the last rows. With RPTS at unity that top grows from repeat to repeat until
  the feedback limiter holds it.
- **Saturation**: `y - w t^3 / 3`, `t` = `y` limited to +-1. With `w = 0` it is exact, also above 1 (the old dark side
  clipped there; the uncoloured path must not).
- **Drift**: by the square of the distance below the uncoloured point (1 at COLR -1), as before at most 30 frames and 2 %
  of the time. None from the uncoloured point up.
- **Page and provider**: the raw value stays 0..127. 96 (`PH_COLR_CLEAN`) is the uncoloured point and the default;
  0..96 is -1..0.5, so a row falls on every 16th step, and 96..127 is 0.5..1. The cell's value is the distance from 96
  and its state names the colour (`OIL`, `BBD`, `CRUN`, `TAPE`, nothing, `DIGI`). The icon is filled below 96 and an
  outline from 96 up. Stored settings have format 5; a record of format 3 or 4 (uncoloured at 64) loads at the same
  share of the way to either end (64 becomes 96).

## Measured

`pnpm nx:pilot run digikit/prism:test-dsp`, 59 of 59. dB from one repeat to the next at unity repeats, three bursts at
once (0.04 each); in brackets what the table's numbers give for one pass.

| COLR | 200 Hz | 4 kHz | 10 kHz |
| ---: | ---: | ---: | ---: |
| -1 | -2.39 (-2.40) | -16.71 (-16.48) | -25.05 (-23.82) |
| -0.75 | -0.79 (-0.79) | -11.65 (-11.43) | -19.76 (-18.60) |
| -0.625, between rows | -0.45 (-0.45) | -9.02 (-8.81) | -16.90 (-15.74) |
| -0.5 | -0.22 (-0.21) | -7.11 (-6.89) | -14.77 (-13.52) |
| -0.25 | -6.10 (-6.11) | -4.99 (-4.76) | -12.16 (-10.76) |
| 0 | -2.59 (-2.59) | -2.99 (-2.71) | -9.36 (-7.62) |
| 0.25 | -0.27 (-0.27) | -1.17 (-0.88) | -5.41 (-3.49) |
| 0.5 | -0.000 | -0.000 | -0.000 |
| 0.75 | -5.12 (-5.12) | 0.09 (0.09) | 0.66 (0.66) |
| 1 | -13.26 (-13.26) | -0.01 (-0.01) | 1.15 (1.15) |

Below the uncoloured point the top falls 1.2 to 1.9 dB more than the filter takes: the heads drift there and read
between frames. The test allows that (up to 2.5 dB at 10 kHz); from the uncoloured point up the three bands are the
table's to 0.01 dB.

| Claim | Number |
| --- | --- |
| Saturation, a 1 kHz tone of 0.8, one echo: third harmonic | -160 dB uncoloured, -31.9 dB at COLR -0.25, -51.5 dB at -1 |
| Drift of the pitch | +-0.150 % at COLR -1, +-0.038 % at -0.25, none uncoloured |
| Clicks, COLR in steps over its whole range | 1.00 x steady |
| Clicks, COLR at once to -1, 1, 0 | 0.49, 0.67, 0.45 x steady |
| A block at rest | 6127 instructions (5296 before: the flat centre had its own short loop) |
| The heaviest block of the suite | 22113 instructions (22186 before), the `dsp_cost_fx` of `module.toml` |

## Open

- **[O]** Whether any row sounds like the delay it is named after. `tests/render-audio.ts` writes one file a row
  (`colr-0-oil-can.wav` to `colr-8-digital.wav`).
- **[O]** The manual says Color "controls the timbre of the Halo". Here HALO hears the coloured signal, and its late
  reverb has its own fixed damping.
- **[O]** A device that has PRISM installed keeps its stored COLR by the conversion above; that has not run on one.
