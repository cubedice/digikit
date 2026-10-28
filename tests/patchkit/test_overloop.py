"""Armed, step-counted capture on the real SHARC effect adapter.
Synthetic position records isolate the consumer; firmware event alignment is a
separate integration proof. No arbitrary scheduler-to-audio offset is applied.
"""

import pytest
from support.fx_rig import NFAULT, P0, f2w, nw
from support.overloop_provider import BASE, OverloopRig, external_word

from tools.patchkit import overloop_ref as R

LIVE = (0.625,) * 32 + (-0.375,) * 32


class Player:
    def __init__(self, *, start=4, length=0):
        self.rig = OverloopRig(record_stores=True)
        self.tx, self.seq, self.step = 0, 1, 3
        self.generation, self.pattern, self.pattern_length = 1, 0x47100000, 16
        self.params = [0.0, float(start), 0.0, float(length)]
        self.input = []

    def block(self, *, step=None, flags=3, samples=LIVE, hostile=False, tx_delta=1):
        if step is not None and step != self.step:
            self.step = step
            self.seq += 1
        self.tx += tx_delta
        words = [
            0x50504F53,
            2,
            self.tx,
            self.seq,
            self.generation,
            flags,
            self.pattern,
            self.step,
            self.pattern_length,
            0,
            self.tx - 1,
            0,
            self.tx,
            self.step,
            1,
            0,
            self.tx,
            0,
            self.tx,
            tx_delta,
        ]
        at = nw(self.rig.built.data["_pattern_position_info"])
        for k, word in enumerate(words):
            self.rig.sess.poke(at + k, word)
        for k, value in enumerate(self.params):
            self.rig.set_row_word(0, P0 + k, f2w(value))
        result = self.rig.block(samples, ctx=None if hostile else "friendly")
        assert result.returned, result.halt
        assert not result.ctx_bad and not result.regs_bad
        assert self.rig.row_word(0, NFAULT) == 0
        assert not self.rig.foreign_stores(0)
        return result

    def state(self, k):
        return self.rig.state_word(0, k)

    def arm(self):
        self.block()
        self.params[0] = 1.0
        self.block()
        assert self.state(0) == 3 and self.state(1) == 0

    def start(self):
        self.arm()
        self.block(step=4)
        assert self.state(0) == 1 and self.state(1) == 32


@pytest.mark.parametrize(
    "length,expected", [(0, 16), (1, 1), (5, 5), (20, 20), (127, 127)]
)
def test_records_exact_step_count_and_stops_once(length, expected):
    p = Player(length=length)
    p.start()
    first = p.state(18)
    for elapsed in range(expected):
        p.block()  # second audio block of this step
        p.block(step=(4 + elapsed + 1) % 16)
    assert p.state(0) == 2
    assert p.state(8) == expected * 64
    assert p.state(19) - first == expected * 2
    at = external_word(BASE)
    assert [p.rig.sess.peek(at + k) for k in range(expected * 64)] == [f2w(LIVE[0])] * (
        expected * 64
    )
    assert [p.rig.sess.peek(at + R.FRAMES + k) for k in range(expected * 64)] == [
        f2w(LIVE[32])
    ] * (expected * 64)
    end = p.state(19)
    for step in range(16):
        p.block(step=step, samples=(0.0,) * 64)
    assert p.state(0) == 2 and p.state(19) == end  # held REC cannot retrigger
    assert not p.rig.audio_events()


def test_rec_on_start_waits_for_next_occurrence_and_latches_both_knobs():
    p = Player(length=3)
    p.block()
    p.params[0] = 1.0
    p.block(step=4)
    assert p.state(0) == 3
    p.params[1], p.params[3] = 9.0, 1.0
    for step in list(range(5, 16)) + list(range(5)):
        p.block(step=step)
    assert p.state(0) == 1 and p.state(21) == 3
    for step in (5, 6, 7):
        p.block(step=step)
    assert p.state(0) == 2 and p.state(8) == 96


@pytest.mark.parametrize(
    "failure", ["stop", "generation", "pattern", "length", "gap", "step_skip"]
)
def test_incomplete_recording_is_never_played(failure):
    p = Player()
    p.start()
    args = {}
    if failure == "stop":
        args["flags"] = 0
    if failure == "generation":
        p.generation += 1
    if failure == "pattern":
        p.pattern += 16
    if failure == "length":
        p.pattern_length = 32
    if failure == "gap":
        args["tx_delta"] = 2
    if failure == "step_skip":
        args["step"] = 6
    p.params[2] = 1.0
    p.block(**args)
    assert p.state(0) == 0 and p.state(8) == 0
    assert tuple(p.rig.scratch()) == LIVE
    assert not p.rig.audio_events()


def test_capacity_overrun_aborts_but_exact_capacity_boundary_closes():
    for closes in (False, True):
        p = Player(length=1)
        p.start()
        at = nw(p.rig.built.data[p.rig.state_symbol(0)])
        p.rig.sess.poke(at + 1, R.FRAMES)
        p.block(step=5 if closes else None)
        assert p.state(0) == (2 if closes else 0)
        assert p.state(8) == (R.FRAMES if closes else 0)
        assert not p.rig.audio_events()


def test_stopped_arm_catches_first_play_boundary_and_hostile_context():
    p = Player(start=0, length=1)
    p.block(flags=0)
    p.params[0] = 1.0
    p.block(flags=0)
    p.generation += 1
    p.block(step=0, hostile=True)
    assert p.state(0) == 1 and p.state(1) == 32
    p.block(step=1, hostile=True)
    assert p.state(0) == 2 and p.state(8) == 32


def test_out_of_range_start_waits_and_rec_can_rearm_after_capture():
    p = Player(start=127, length=1)
    p.arm()
    for step in range(16):
        p.block(step=step)
    assert p.state(0) == 3 and p.state(1) == 0
    p.params = [0.0, 4.0, 0.0, 1.0]
    p.block()
    p.params[0] = 1.0
    p.block(step=3)
    p.block(step=4)
    p.block(step=5)
    assert p.state(0) == 2 and p.state(8) == 32
    p.params[0] = 0.0
    p.block()
    p.params[0] = 1.0
    p.block()
    assert p.state(0) == 3 and p.state(8) == 32
    assert p.rig.audio_events() == []  # previous loop survives while armed


def test_fade_outputs_frozen_audio_after_live_input_changes():
    p = Player(length=2)
    p.start()
    for _ in range(4):
        p.block()
    p.block(step=5)
    for _ in range(4):
        p.block()
    p.block(step=6)
    p.params[2] = 1.0
    for _ in range(12):
        p.block(samples=(0.0,) * 64)
    wet = p.rig.scratch()
    assert max(wet[:32]) == LIVE[0] and min(wet[32:]) == LIVE[32]
    assert not p.rig.audio_events()


def test_changed_tempo_counts_boundaries_instead_of_fixed_seconds():
    p = Player(length=3)
    p.start()
    blocks = 1
    for step, extra in ((5, 1), (6, 5), (7, 2)):
        for _ in range(extra):
            p.block()
            blocks += 1
        p.block(step=step)
        if step != 7:
            blocks += 1
    assert p.state(0) == 2 and p.state(8) == blocks * 32


@pytest.mark.parametrize("kind", ["magic", "capacity", "canary"])
def test_corrupt_storage_fails_before_audio_access(kind):
    p = Player()
    p.block()
    info = nw(p.rig.built.data["_loop_buffer_info"])
    if kind == "magic":
        p.rig.sess.poke(info, 0)
    if kind == "capacity":
        p.rig.sess.poke(info + 3, R.STORAGE_BYTES - 4)
    if kind == "canary":
        p.rig.sess.poke(external_word(BASE + R.AUDIO_BYTES), 0)
    result = p.rig.block(LIVE, ctx="friendly")
    assert result.returned and not result.ctx_bad and not result.regs_bad
    assert p.rig.row_word(0, NFAULT) == 1
    assert result.master == LIVE and not p.rig.audio_events()


def test_current_sdk_declares_four_knobs_without_building_unrelated_modules():
    from tools.patchkit.pk_conform import fx_records

    (fx,) = fx_records("digikit/overloop")
    assert fx["id"] == "loop" and fx["route"] == "master"
    assert [(k["short"], k["position"]) for k in fx["knob"]] == [
        ("REC", 0),
        ("START", 1),
        ("FADE", 2),
        ("LENGTH", 3),
    ]
    assert fx["knob"][3]["default"] == 0 and fx["knob"][3]["max"] == 127
