"""Compare CANOPY's note AMP against the pinned stock DSP instructions."""

import hashlib
import struct

import pytest
from support.paths import REPO_ROOT
from test_canopy_v2 import canopy_object as canopy_object

from tools.patchkit import pk_sharc
from tools.sharc_dev import canopy_carrier_probe as stock
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
@pytest.mark.parametrize("ownership", ["native", "v5", "unowned", "stale"])
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
    stock.put(hooked, symbols["_mi_native_owner_valid"], ownership != "v5")
    stock.put(
        hooked, symbols["_mi_native_owner_stamp"], 40 if ownership == "stale" else 41
    )
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
        if ownership == "native":
            assert output == incoming
            assert stock.record(hooked) == before
        else:
            assert output == [
                stock.get(reference, stock.BUFFER + i * 4) for i in range(32)
            ]
            assert stock.record(hooked) == stock.record(reference)


@pytest.mark.parametrize(
    "mode,hold,release,flag,off_block",
    [
        (1, 2, 9, 0, 1),  # Finite AHD ignores early note-off, then decays.
        (1, 127, 9, 0, 5),  # Infinite HOLD turns note-off into decay.
        (2, 2, 9, 0, 7),  # ADSR uses its independent release parameter.
        (2, 2, 127, 1, 2),  # Native restart shape and infinite release.
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
        attack=3 / 127,
        hold=hold / 127,
        decay=7 / 127,
        sustain=0.375,
        release=release / 127,
        flag=flag,
    )
    configured = stock.record(reference)
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
