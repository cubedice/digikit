"""CANOPY under the host's load pressure (finding 377), on the SHARC core.

The adapter puts a pressure of 0 to 7 in bits 12..14 of the timing flags of a V5+ input. At 0 the instance renders what
it rendered before the budget existed (the pinned takes below, recorded at the commit before it); above 0 it sheds
voices in a fixed order through the eased 64-sample ramp, and takes them back when the pressure falls.

`CANOPY_BUDGET_REPORT=1 uv run pytest -q -s modules/machine/digikit/canopy/tests/test_canopy_budget.py -k report`
prints the takes' hashes and the instruction counts of a block (the comparable number of the cost work)."""

import hashlib
import math
import os
import struct

import pytest
from test_canopy_v2 import (
    FINE,
    fm_controls,
    make_canopy,
    native_amp,
    note,
    presentation_controls,
    spread_controls,
    wavetable_controls,
    wavetable_fm_controls,
    wt_float,
)
from test_canopy_v2 import canopy as canopy
from test_canopy_v2 import canopy_object as canopy_object

BLOCKS = 420
# the pressure is on from ON to OFF; the chord changes at STEAL (two more notes) and RELEASE (three notes off)
ON, OFF = 150, 300
STEAL, RELEASE = (60, 90), 200
# 120 BPM in 1/120 BPM with every capability of a running transport (valid, running, grid, phase, tempo)
PLAYING = 0xB03 | (14400 << 16)
AMP = native_amp(attack=1 / 256, decay=0.999, sustain=0.7, release=0.9991)


def family_controls(family):
    """A pad and arp sound of the family at ARP MIX 64, the arp's own clock at its fastest: a step every 37.5 blocks."""
    values = {
        "velvet": lambda: spread_controls(),
        "wavetable": lambda: wavetable_controls(mix=64),
        "fm": lambda: fm_controls(mix=64),
        # the bank the UI sends for a wavetable sound (CPSB): WT FM at depth 0, and at depth 60
        "wavetable-b": lambda: wavetable_fm_controls(mix=64),
        "wavetable-fm": lambda: wavetable_fm_controls(fm_depth=60, mix=64),
        "wavetable-settled": lambda: wavetable_fm_controls(mix=64),
    }[family]()
    values[6] = 64
    values[21] = (values[21] & 0xFFFF0000) | 64
    values[7] = 300
    values[8] = 8
    values[22] = (values[22] & 0xFFFF0000) | 8
    return values


def take(canopy, family, pressure=lambda block: 0, blocks=BLOCKS):
    """Four notes held from block 0, two more at STEAL (each takes a voice), three off at RELEASE, the arp running.
    Gives per block the output words, the instance state and the instruction count."""
    controls = family_controls(family)
    out, states, cost = [], [], []
    for block in range(blocks):
        # An instance that has played its wavetable family for a third of a second has its source mix (state word 232)
        # settled on the wavetable; a take from a new instance reaches that only at its end. "settled" starts there.
        settled = (
            {232: 0x3F800000} if block == 0 and family.endswith("settled") else None
        )
        events = []
        if block == 0:
            events = [note(1, 48), note(2, 55), note(3, 60), note(4, 64)]
        elif block in STEAL:
            events = [note(5 + STEAL.index(block), 67 + 5 * STEAL.index(block))]
        elif block == RELEASE:
            events = [note(t, 0, off=True) for t in (3, 5, 6)]
        r = canopy(
            events,
            controls=controls,
            native=AMP,
            post_amp=True,
            state_edits=settled,
            edits={
                4: block,
                12: PLAYING | (pressure(block) << 12),
                13: 1,
                16: 16,
            },
        )
        assert r["regs"]["R0"] in (0, 1), f"block {block}: the batch was refused"
        out.append(r["out"])
        states.append(r["state"])
        cost.append(r["instructions"])
    return out, states, cost


def digest(out):
    return hashlib.sha256(
        b"".join(struct.pack("<32I", *block) for block in out)
    ).hexdigest()


def samples(out):
    return [wt_float(w) for block in out for w in block]


def sounding(state):
    """The voices that sound: pad slots and tails 0..5, the arp group 6 (a token and a level above zero)."""
    slots = [i for i in range(6) if state[8 * i] and state[8 * i + 7]]
    return slots + ([6] if state[136] and state[143] else [])


def held_owners(state):
    return {
        state[8 * i] for i in range(4) if state[8 * i] and state[8 * i + 1] & 3 == 1
    }


# The takes at pressure 0, recorded at the commit before the budget (ee79d7018; the CPSB takes at the budget's commit,
# whose output the three others show to be the same): the budget and the bit-identical cost work must keep them.
# The three wavetable takes of a new instance changed once, deliberately, with the skipped pad oscillator: from block
# 414, where the source mix stops 0.000001 short of the wavetable and the pad oscillator's share (-140 dB) goes; the
# largest difference of a sample is 1.5e-8 at a peak of 0.147 (85a1a7f2... and 932238042f... before). The settled take
# is the same before and after.
PINNED = {
    "velvet": "efa291cb3ede4d588eeb98be404c966c3bcbd54c5772c054991576d4d8e20e9d",
    "wavetable": "ae8b040407c158012aa5a3c3aba9cb2e18f3c85d7debd917dc7c72b27d997843",
    "fm": "62ec0a005c7db37e272af65ac81cc482305365b448e559e2abf37d297b8c41c6",
    "wavetable-b": "ae8b040407c158012aa5a3c3aba9cb2e18f3c85d7debd917dc7c72b27d997843",
    "wavetable-fm": "c79bac7239c9fd29f5cc52076b5088fb5ea9d536290659e6659c8cb5fe1e7799",
    "wavetable-settled": "939559d42de4ae0738ba2eb6746b94880b600f1e662fd23afadc3cb5a823b0be",
}

DIRECT = pytest.mark.parametrize(
    "canopy_object", [False], indirect=True, ids=["direct"]
)
FAMILIES = pytest.mark.parametrize("family", ["velvet", "wavetable", "fm"])


@pytest.fixture(scope="module")
def takes(canopy_object):
    """Each take once per assembled object: (family, level) -> (out, states, cost)."""
    cache = {}

    def get(family, level):
        if (family, level) not in cache:
            cache[family, level] = take(
                make_canopy(canopy_object),
                family,
                lambda block: level if ON <= block < OFF else 0,
            )
        return cache[family, level]

    return get


@DIRECT
@pytest.mark.parametrize("family", list(PINNED))
def test_report(takes, family):
    if not os.environ.get("CANOPY_BUDGET_REPORT"):
        pytest.skip("a report, on request")
    out, states, cost = takes(family, 0)
    print(f"\n{family}: sha256 {digest(out)}")
    if os.environ.get("CANOPY_BUDGET_DUMP"):
        # the take's samples, to compare two builds numerically
        with open(f"{os.environ['CANOPY_BUDGET_DUMP']}-{family}.f32", "wb") as file:
            file.write(b"".join(struct.pack("<32I", *block) for block in out))
    for level in range(8) if family in ("velvet", "wavetable", "fm") else (0,):
        _, states, cost = takes(family, level)
        window = cost[ON + 10 : OFF]
        print(
            f"{family} pressure {level}: instructions a block mean {sum(window) / len(window):.0f} "
            f"max {max(window)} (whole take: mean {sum(cost) / len(cost):.0f} max {max(cost)}); "
            f"sounding at block {OFF - 1}: {sounding(states[OFF - 1])}"
        )


@pytest.mark.parametrize("family", list(PINNED))
def test_pressure_zero_renders_the_pinned_take(canopy, family):
    out, _, _ = take(canopy, family)
    assert digest(out) == PINNED[family]


@DIRECT
@FAMILIES
@pytest.mark.parametrize("level", range(1, 8))
def test_pressure_sheds_in_order_without_a_click(takes, family, level):
    base_out, base_states, base_cost = takes(family, 0)
    out, states, cost = takes(family, level)
    # until the pressure comes the take is the pinned one
    assert out[:ON] == base_out[:ON]
    # the scenario has what there is to shed: at pressure 0 both tails are in use when the pressure would come
    assert any(s[32] or s[40] for s in base_states[ON : ON + 40])
    settled = range(ON + 3, OFF)
    # 1+: no tail sounds once the 64-sample ramp is through, and none is taken
    assert all(not s[32] and not s[40] for s in (states[b] for b in settled))
    if level >= 2:
        # 2+: a released pad voice is gone three blocks after its release
        for block in range(RELEASE + 3, OFF):
            state = states[block]
            assert not any(
                state[8 * i] and state[8 * i + 1] & 3 == 0 for i in range(4)
            ), block
    cap = max(6 - level, 0)
    for block in settled:
        state = states[block]
        pads = [i for i in sounding(state) if i < 4 and state[8 * i + 1] & 3 == 1]
        if level >= 3:
            # 3..6: at most 6 - level held pads sound, the oldest ones
            assert len(pads) <= cap, (block, pads)
            owners = sorted(held_owners(state))
            assert {state[8 * i] for i in pads} <= set(owners[:cap]), block
        # the chord's owners stay whatever sounds: the same held tokens as without pressure
        assert held_owners(state) == held_owners(base_states[block]), block
    if level == 7:
        # 7: nothing sounds, and the arp keeps its clock (the same onset serial as without pressure)
        assert all(not any(out[b]) for b in settled)
        assert states[OFF - 1][149] == base_states[OFF - 1][149]
    # every level sheds at least what the level below shed (voices over the window: an arp note may start a block or
    # two earlier when its group has no ramp to finish)
    lower = takes(family, level - 1)
    assert sum(len(sounding(states[b])) for b in settled) <= sum(
        len(sounding(lower[1][b])) for b in settled
    )
    window = slice(ON + 10, OFF)
    assert sum(cost[window]) <= sum(lower[2][window])
    # no click: the largest second difference where the pressure comes and goes against the rest of the take
    x = samples(out)
    assert all(math.isfinite(v) and abs(v) <= 1 for v in x)
    d = [abs(x[n + 1] - 2 * x[n] + x[n - 1]) for n in range(1, len(x) - 1)]
    near = set()
    for block in (ON, OFF):
        near.update(range(32 * block - 64, 32 * block + 32 * 12))
    steady = max(v for n, v in enumerate(d) if n + 1 not in near and n > 3200)
    edge = max(d[n - 1] for n in near)
    assert edge < 2.0 * steady, (edge, steady)
    # and no step between samples larger than the take without pressure has
    base_x = samples(base_out)
    step = max(abs(b - a) for a, b in zip(x, x[1:], strict=False))
    base_step = max(abs(b - a) for a, b in zip(base_x, base_x[1:], strict=False))
    assert step <= 1.05 * base_step, (step, base_step)


@DIRECT
@FAMILIES
@pytest.mark.parametrize("level", [1, 3, 7])
def test_voices_return_when_the_pressure_falls(takes, family, level):
    base_out, base_states, _ = takes(family, 0)
    out, states, _ = takes(family, level)
    # the held pads sound again, with the owners they had
    last = states[-1]
    assert held_owners(last) == held_owners(base_states[-1]) != set()
    assert all(last[8 * i + 7] for i in range(4) if last[8 * i + 1] & 3 == 1)
    # the arp takes tails again
    assert any(s[32] or s[40] for s in states[OFF:])
    # and the level of the last 60 blocks is near the one of the take without pressure

    def rms(blocks):
        x = samples(blocks)
        return math.sqrt(sum(v * v for v in x) / len(x))

    # (a pad that was masked plays its attack and decay again, so it is not the same level yet)
    assert 0.5 < rms(out[-60:]) / rms(base_out[-60:]) < 2
    assert rms(out[-60:]) > 0.001


@DIRECT
def test_pressure_alone_is_no_clock_provider(canopy_object):
    """The arp's fine clock reads the timing flags for a provider: pressure bits without one are a missing frame."""
    controls = presentation_controls(base=FINE)
    results = []
    for flags in (0, 5 << 12):
        canopy = make_canopy(canopy_object)
        canopy(
            [note(1, 60), note(2, 64)],
            controls=controls,
            edits={0: 5, 1: 96, 12: PLAYING, 13: 1},
        )
        for block in range(1, 6):
            r = canopy(controls=controls, edits={0: 5, 1: 96, 4: block, 12: flags})
        results.append(r["state"][144:184])
    assert results[0] == results[1]


@DIRECT
def test_pressure_is_ignored_below_v5(canopy_object):
    """Bits 12..14 mean pressure only in a V5+ input: a V4 input with them set renders as without."""
    controls = family_controls("velvet")
    results = []
    for flags in (3, 3 | 7 << 12):
        canopy = make_canopy(canopy_object)
        canopy(
            [note(1, 60), note(2, 64)], controls=controls, edits={0: 4, 1: 88, 12: 3}
        )
        r = canopy(
            controls=controls, edits={0: 4, 1: 88, 12: flags}, blocks=8, capture=True
        )
        results.append(r["audio"])
    assert results[0] == results[1] and any(results[0])
