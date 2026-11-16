"""Compare CANOPY's note AMP against the pinned stock DSP instructions."""

import hashlib
import math
import struct

import pytest
from support.paths import REPO_ROOT
from test_canopy_v2 import canopy_object as canopy_object

from modules.machines.canopy.tools import carrier_probe as stock
from tools.patchkit import pk_sharc
from tools.sharc_dev.sharc_run import Runner
from tools.sharc_dev.sharcldr import LoadedMemory


def _float(word):
    return struct.unpack("<f", word.to_bytes(4, "little"))[0]


@pytest.fixture(scope="module")
def stock_amp_image():
    path = REPO_ROOT / "out/sections/dt2-1.17/section_7_BLOB.bin"
    if not path.exists():
        pytest.skip("requires the local pinned DT2 1.17 DSP extraction")
    blob = path.read_bytes()
    assert hashlib.sha256(blob).hexdigest() == stock.BLOB_SHA
    return LoadedMemory.from_stream(blob)


@pytest.fixture(scope="module")
def hooked_amp_image(stock_amp_image):
    _, memory, initial, symbols = stock.carrier_policy(stock_amp_image.data)
    return memory, initial, symbols


@pytest.mark.parametrize("mode", [1, 2])
@pytest.mark.parametrize("ownership", ["native", "post", "v5", "unowned", "stale"])
def test_shared_amp_hook_changes_only_current_native_output(
    stock_amp_image, hooked_amp_image, mode, ownership
):
    memory, initial, symbols = hooked_amp_image
    reference = Runner(stock_amp_image, stock.ENVELOPE, breakpoints=[stock.END])
    hooked = Runner(memory, stock.ENVELOPE, breakpoints=[stock.END])
    for address, value in initial.items():
        stock.put(hooked, address, value)
    for current in (reference, hooked):
        for base, count in ((stock.STACK - 256, 128), (stock.RECORD, 11)):
            for i in range(count):
                stock.put(current, base + 4 * i, 0)
    hooked = stock.bind_workspace(hooked, stock.RECORD - 0xD604)
    config = dict(
        mode=mode,
        attack=3 / 127,
        hold=2 / 127,
        decay=7 / 127,
        sustain=0.375,
        release=9 / 127,
    )
    reference = stock.configure_from_dispatcher(reference, **config)
    hooked = stock.configure_from_dispatcher(hooked, **config)
    # Per-track owner tables, track 0: the native owner's stamp and its gains.
    gains_at = symbols["_probe_post_gains"]
    stock.put(
        hooked,
        symbols["_mi_native_owner"],
        0 if ownership == "v5" else 40 if ownership == "stale" else 41,
    )
    stock.put(hooked, symbols["_mi_post_ptr"], 0 if ownership == "native" else gains_at)
    for i in range(32):
        stock.put(hooked, gains_at + 4 * i, stock.bits(0.5))
    stock.put(hooked, symbols["_mi_carrier_stamp"], 41)
    pairs = symbols["_mi_carrier_pairs"]
    stock.put(hooked, pairs, 41)
    stock.put(hooked, pairs + 8, ownership != "unowned")
    incoming = [stock.bits(0.25 + i / 1024) for i in range(32)]
    for block in range(12):
        trigger, release = block == 0, block == 5
        stock.put(hooked, pairs + 4, int(trigger) | (int(release) << 1))
        for current in (reference, hooked):
            for i, value in enumerate(incoming):
                stock.put(current, stock.BUFFER + i * 4, value)
        before = stock.record(hooked)
        reference = stock.call(
            reference, stock.ENVELOPE, trigger=trigger, release=release
        )
        hooked = stock.call(hooked, stock.ENVELOPE, trigger=trigger, release=release)
        output = [stock.get(hooked, stock.BUFFER + i * 4) for i in range(32)]
        if ownership in ("native", "post"):
            assert output == (
                incoming
                if ownership == "native"
                else [stock.bits(_float(word) * 0.5) for word in incoming]
            )
            assert stock.record(hooked) == before
        else:
            assert output == [
                stock.get(reference, stock.BUFFER + i * 4) for i in range(32)
            ]
            assert stock.record(hooked) == stock.record(reference)


def _bit_reduce(runner, raw):
    """Call the pinned stock BR stage with its separate one-word record."""
    br_record = stock.RECORD + 256
    stock.put(runner, br_record, stock.bits(raw / 127))
    stock.put(runner, stock.STACK - 4, stock.END - 1)
    stock.put(runner, stock.STACK, stock.STACK + 256)
    stock.put(runner, stock.STACK + 4, 32)
    runner = runner.fresh_call(
        0x1CDD8B,
        regs=dict(
            R4=br_record,
            R8=stock.BUFFER,
            R12=stock.BUFFER,
            I6=stock.STACK,
            I7=stock.STACK,
            M5=0,
            M6=1,
            M7=0xFFFFFFFF,
            M13=0,
            M14=1,
            M15=0xFFFFFFFF,
            MODE1=0x1011800,
            **{f"L{i}": 0 for i in range(16)},
        ),
        return_address=stock.END,
    )
    result = runner.run(2000)
    assert result.halt.reason == "breakpoint" and result.final_pc_sw == stock.END
    return runner


@pytest.mark.parametrize("raw_br", [4.85, 64])
def test_post_amp_keeps_late_release_exponential_after_actual_stock_bit_reduction(
    hooked_amp_image, raw_br
):
    memory, initial, symbols = hooked_amp_image
    curves = {}
    for post in (False, True):
        runner = Runner(memory, stock.ENVELOPE, breakpoints=[stock.END])
        for address, value in initial.items():
            stock.put(runner, address, value)
        for base, count in ((stock.STACK - 256, 128), (stock.RECORD, 11)):
            for i in range(count):
                stock.put(runner, base + 4 * i, 0)
        runner = stock.bind_workspace(runner, stock.RECORD - 0xD604)
        runner = stock.configure_from_dispatcher(
            runner, mode=2, attack=0, hold=0, decay=0, sustain=1, release=80 / 127
        )
        record = stock.record(runner)
        q = -_float(stock.get(runner, 0x26AF30 + 4 * record[4]))
        assert 0 < q < 1
        gains_at = symbols["_probe_post_gains"]
        for name, value in (
            ("_mi_native_owner", 41),
            ("_mi_post_ptr", gains_at if post else 0),
            ("_mi_carrier_stamp", 41),
        ):
            stock.put(runner, symbols[name], value)
        pairs = symbols["_mi_carrier_pairs"]
        stock.put(runner, pairs, 41)
        stock.put(runner, pairs + 8, 1)
        peaks = []
        for seconds in (14, 18):
            gains = [q ** (48000 * seconds + i) for i in range(32)]
            for i, gain in enumerate(gains):
                source = 0.25 if i % 2 else -0.25
                stock.put(
                    runner,
                    stock.BUFFER + 4 * i,
                    stock.bits(source if post else source * gain),
                )
                stock.put(runner, gains_at + 4 * i, stock.bits(gain))
            runner = _bit_reduce(runner, raw_br)
            reduced = [
                _float(stock.get(runner, stock.BUFFER + 4 * i)) for i in range(32)
            ]
            runner = stock.call(runner, stock.ENVELOPE)
            output = [
                _float(stock.get(runner, stock.BUFFER + 4 * i)) for i in range(32)
            ]
            assert stock.record(runner) == record
            if post:
                assert output == pytest.approx(
                    [
                        sample * gain
                        for sample, gain in zip(reduced, gains, strict=True)
                    ],
                    rel=2e-7,
                    abs=1e-12,
                )
            else:
                assert output == reduced
            peaks.append(math.sqrt(sum(sample * sample for sample in output) / 32))
        curves[post] = peaks
        if post:
            assert peaks[1] / peaks[0] == pytest.approx(q ** (48000 * 4), rel=2e-6)
            for i in range(32):
                stock.put(runner, gains_at + 4 * i, 0)
            runner = stock.call(runner, stock.ENVELOPE)
            assert [stock.get(runner, stock.BUFFER + 4 * i) for i in range(32)] == [
                0
            ] * 32
    assert curves[False][0] > 0
    assert curves[False][1] == curves[False][0]  # The old pre-BR tail plateaus.
    assert curves[True][1] < curves[True][0] * 0.1


# CANOPY keeps every onset at least 256 samples long (voice.s `_cp_amp_begin`; test_canopy_v2
# pins that ramp), so it matches the stock instructions sample for sample only for attacks of
# 256 samples or more. Raw attack 13 is the fastest: 272 samples, about 8.5 blocks.
ATTACK = 13


@pytest.mark.parametrize(
    "mode,hold,release,flag,off_block",
    [
        (1, 2, 9, 0, 1),  # Finite AHD ignores early note-off, then decays.
        (1, 127, 9, 0, 14),  # Infinite HOLD turns note-off into decay.
        (2, 2, 9, 0, 16),  # ADSR uses its independent release parameter.
        (2, 2, 127, 1, 11),  # Native restart shape and infinite release.
    ],
)
def test_native_note_curve_matches_stock_instructions(
    canopy_object, stock_amp_image, mode, hold, release, flag, off_block
):
    linked, _, initial, _, symbols, _ = canopy_object
    session = pk_sharc.CallSession([linked], 0xBCF100)
    for address, value in initial.items():
        session.poke(address, value)
    state, input_base = 0x20050000, 0x20011000
    for i in range(744):
        session.poke(state // 4 + i, 0)
    session.poke(state // 4, 1)
    session.poke(state // 4 + 1, 1)
    for name, value in (
        ("_cp_amp_state", state),
        ("_cp_amp_input", input_base),
        ("_cp_amp_mode", mode),
    ):
        session.poke(symbols[name] // 4, value)

    reference = Runner(stock_amp_image, stock.ENVELOPE, breakpoints=[stock.END])
    for base, count in ((stock.STACK - 256, 128), (stock.RECORD, 11)):
        for i in range(count):
            stock.put(reference, base + 4 * i, 0)
    reference = stock.configure_from_dispatcher(
        reference,
        mode=mode,
        attack=ATTACK / 127,
        hold=hold / 127,
        decay=7 / 127,
        sustain=0.375,
        release=release / 127,
        flag=flag,
    )
    configured = stock.record(reference)
    assert -_float(stock.get(reference, 0x26AD30 + 4 * configured[0])) <= 1 / 256
    native = [
        configured[6],
        stock.bits(-_float(stock.get(reference, 0x26AD30 + 4 * configured[0]))),
        0xFFFFFFFF
        if configured[1] == 127
        else stock.get(reference, 0x26B130 + 4 * configured[1]),
        stock.bits(-_float(stock.get(reference, 0x26AF30 + 4 * configured[2]))),
        configured[3],
        stock.bits(-_float(stock.get(reference, 0x26AF30 + 4 * configured[4]))),
        int(configured[5] != 0),
        0,
    ]
    for i, value in enumerate(native, 24):
        session.poke(input_base // 4 + i, value)

    native_curve, stock_curve = [], []
    for block in range(32):
        if block == off_block:
            session.poke(state // 4 + 1, 0)
        for i in range(32):
            stock.put(reference, stock.BUFFER + 4 * i, stock.bits(1.0))
        reference = stock.call(
            reference, stock.ENVELOPE, trigger=block == 0, release=block == off_block
        )
        result = session.call(
            "_cp_amp_begin",
            {
                "I0": state,
                "M5": 0,
                "M6": 1,
                "L0": 0,
                "L1": 0,
                "L2": 0,
                "MODE1": 0x1011800,
            },
            max_steps=2000,
        )
        assert result["halt"]["reason"] == "breakpoint", result
        native_curve += [
            _float(session.peek(symbols["_cp_amp_samples"] // 4 + i)) for i in range(32)
        ]
        stock_curve += [
            _float(stock.get(reference, stock.BUFFER + 4 * i)) for i in range(32)
        ]
        session.poke(state // 4 + 7, session.peek(symbols["_cp_amp_end"] // 4))
    assert native_curve == pytest.approx(stock_curve, rel=2e-6, abs=2e-7)
    assert max(native_curve) > 0.5
    if mode == 2 and release == 127:
        assert native_curve[-1] == pytest.approx(native_curve[off_block * 32], abs=2e-7)
