"""CANOPY's arp with RST TRIG while the transport plays (finding 365): a trig of the track restarts the arp phrase, and a trig
that plays a chord takes voices from the notes before it. The click measure of finding 354 (the largest second difference
around an event against a steady stretch of the same take) at each trig, on the SHARC core.

`CANOPY_CLICK_WAV=DIR` also writes the takes of the chord trigs as WAV files for listening (never committed)."""

import math
import os
import struct
from pathlib import Path

import pytest
from test_canopy_v2 import FINE, make_canopy, note, presentation_controls, wt_float
from test_canopy_v2 import canopy as canopy
from test_canopy_v2 import canopy_object as canopy_object

# 120 BPM in 1/120 BPM; the record of a running transport with every capability (valid, running, grid, phase, tempo)
TEMPO = 14400
PLAYING = 0xB03 | (TEMPO << 16)
# a step of 16ths at 120 BPM is 187.5 blocks
STEP_BLOCKS = 187.5
# the trigs: every 131 blocks from block 97 (not on the arp's grid)
FIRST, EVERY = 97, 131


def controls(restart=0, rate=8, arp=127):
    """FINE's arp at RATE `rate`, RST `restart` (0 TRIG, 1 PATTERN, 2 CONT), the arp's level `arp` (0 pad only)."""
    return presentation_controls(
        base=FINE[:6] + (arp,) + FINE[7:8] + (rate,) + FINE[9:14] + (restart, 0)
    )


def timing(block, phrase, step=0, boundary=0):
    return {
        0: 5,
        1: 96,
        4: block,
        12: PLAYING,
        13: 1,
        14: boundary,
        15: step,
        16: 16,
        18: 0x80000000 | phrase,
    }


def render(canopy, blocks, trig, *, restart=0, arp=127):
    """A chord held from the keys, then a trig of the track every EVERY blocks from FIRST: `phrase` a new phrase alone;
    `chord` the chord again from the trig (the notes before it off and new ones on, in one block); `chord-later` the new
    ones on and the old ones off a block later; `note` a note of the trig over the chord, released 40 blocks later.
    Gives the samples and the arp's onset blocks."""
    chord = [(1, 60), (2, 64), (3, 67)]
    out, onsets, serial, phrase, token, later, offs = [], [], 0, 1, 10, [], {}
    for block in range(blocks):
        events = [note(t, p) for t, p in chord] if block == 0 else []
        events += later
        later = []
        if block in offs:
            events.append(note(offs.pop(block), 72, off=True))
        if block >= FIRST and (block - FIRST) % EVERY == 0:
            phrase += 1
            if trig in ("chord", "chord-later"):
                old = [note(t, p, off=True) for t, p in chord]
                chord = [(token + i, p) for i, (_, p) in enumerate(chord)]
                token += len(chord)
                events += (old if trig == "chord" else []) + [
                    note(t, p) for t, p in chord
                ]
                if trig == "chord-later":
                    later = old
            elif trig == "note":
                events.append(note(token, 72))
                offs[block + 40] = token
                token += 1
            else:
                events.append([5, 0, 0, 0, 0, 0])
        step = int(block / STEP_BLOCKS)
        r = canopy(
            events,
            controls=controls(restart, arp=arp),
            edits=timing(block, phrase, step % 16, step),
        )
        assert r["regs"]["R0"] in (0, 1), f"block {block}: the batch was refused"
        out.extend(wt_float(w) for w in r["out"])
        if r["state"][149] != serial:
            serial = r["state"][149]
            onsets.append(block)
    return out, onsets


def clicks(out, blocks):
    """The click measure at each trig: the largest second difference from 2 blocks before it to 16 after, against the
    largest one of the take away from the trigs (the arp's own onsets included)."""
    d = [abs(out[n + 1] - 2 * out[n] + out[n - 1]) for n in range(1, len(out) - 1)]
    trigs = range(FIRST, blocks, EVERY)
    near = {n for t in trigs for n in range(32 * t - 400, 32 * t + 800)}
    steady = max(v for n, v in enumerate(d) if n + 1 not in near and n > 3200)
    return [max(d[32 * t - 65 : 32 * t + 511]) / steady for t in trigs]


def write_wav(path, samples):
    data = b"".join(struct.pack("<f", v) for v in samples)
    head = struct.pack(
        "<4sI4s4sIHHIIHH4sI",
        b"RIFF",
        36 + len(data),
        b"WAVE",
        b"fmt ",
        16,
        3,
        1,
        48000,
        192000,
        4,
        32,
        b"data",
        len(data),
    )
    path.write_bytes(head + data)


@pytest.mark.parametrize("canopy_object", [False], indirect=True, ids=["direct"])
@pytest.mark.parametrize("arp", [127, 64, 0], ids=["arp", "arp-and-pad", "pad"])
@pytest.mark.parametrize("trig", ["phrase", "chord", "chord-later", "note"])
def test_a_trig_restarts_the_arp_without_a_click(canopy, trig, arp, record_property):
    blocks = 900
    out, onsets = render(canopy, blocks, trig, arp=arp)
    assert all(math.isfinite(v) for v in out)
    measure = clicks(out, blocks)
    record_property("clicks", measure)
    print(
        f"{trig} arp={arp}: click at each trig",
        " ".join(f"{c:.2f}" for c in measure),
        "x steady",
    )
    # finding 354's bound for a crossfaded event; before the eased reset ramp a chord trig measured 4.3 to 6.5
    assert max(measure) < 2.0
    if arp:
        # the arp restarts at every trig: its onsets keep their grid between trigs (one every 94 blocks at RATE 8)
        assert onsets[:3] == [0, 94, FIRST]
        assert onsets[3] - onsets[2] == 94


@pytest.mark.skipif(
    not os.environ.get("CANOPY_CLICK_WAV"),
    reason="CANOPY_CLICK_WAV names where the WAV files go",
)
@pytest.mark.parametrize("canopy_object", [False], indirect=True, ids=["direct"])
def test_write_chord_trig_takes(canopy_object):
    folder = Path(os.environ["CANOPY_CLICK_WAV"])
    folder.mkdir(parents=True, exist_ok=True)
    for arp, name in [(127, "arp"), (64, "arp-and-pad"), (0, "pad")]:
        out, _ = render(make_canopy(canopy_object), 4500, "chord", arp=arp)
        # the same gain for every take (+14 dB), so takes before and after a change compare
        write_wav(folder / f"chord-trigs-{name}.wav", [5.0 * v for v in out])
