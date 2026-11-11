"""Native SRC MOD mirrors through the production per-block CANOPY adapter."""

import pytest
from test_canopy_v2 import fm_controls, wavetable_fm_controls
from test_instrument_play import playback as playback
from test_instrument_play import playback_object as playback_object

MIRRORS = (0x6E, 0x6F, 0x70, 0x73, 0x74, 0x75)
TARGETS = (
    ((18, 0, 127), (19, 0, 127), (21, 0, 127), (20, 0, 500), (25, 0, 100), (26, 0, 75)),
    ((18, 0, 127), (19, 0, 127), (21, 0, 127), (20, 0, 500), (25, 0, 100), (26, 0, 75)),
    (
        (17, 16, 700),
        (18, 16, 100),
        (19, 16, 1000),
        (23, 16, 200),
        (28, 16, 100),
        (30, 16, 200),
    ),
    (
        (18, 0, 127),
        (17, 16, 100),
        (20, 16, 100),
        (25, 16, 200),
        (21, 16, 127),
        (24, 16, 127),
    ),
)


def bank(family):
    controls = fm_controls() if family == 3 else wavetable_fm_controls()
    controls[0] = (controls[0] & ~255) | 11
    controls[1] = family
    return controls


def frame(playback, lane=None, raw=0x4000, machine=20):
    put = playback[1]
    words = [0] * 256
    words[37] = machine
    for index, halfword in enumerate(MIRRORS):
        value = raw if lane is None or lane == index else 0x4000
        words[halfword // 2] |= value << (16 * (halfword & 1))
    put("_iv_mod_frame", words)


def effective(playback, controls):
    call, put, get, _, _, _, symbols = playback
    put("_test_values", controls)
    pointer = call(6, R4=symbols["_test_values"], R8=len(controls))
    values = (
        get("_test_values", 32)
        if pointer == symbols["_test_values"]
        else get("_iv_mod_controls", 32)
    )
    assert get("_test_values", 32) == controls, (
        "native modulation must never edit base/preset controls"
    )
    return pointer, values


@pytest.mark.parametrize("family", range(4))
@pytest.mark.parametrize("lane", range(6))
def test_every_family_lane_changes_its_real_control_and_stays_valid(
    playback, family, lane
):
    controls = bank(family)
    word, shift, maximum = TARGETS[family][lane]
    frame(playback, lane, 0x6000)
    _, values = effective(playback, controls)
    base = (controls[word] >> shift) & 65535
    low = 1 if (family < 2 and lane == 4) or (family == 2 and lane == 2) else 0
    assert (values[word] >> shift) & 65535 == min(maximum, base + (maximum - low) // 2)
    # The actual production validator accepts duplicate scalar/presentation
    # fields and the actual module renders the effective bank without a fault.
    call, _, get, reset, configure, events, _ = playback
    assert reset() == 1
    call(0)
    assert configure(values=controls) == 1
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    call(0)
    call(0)  # The first block after ownership reset intentionally stays neutral.
    assert get("_iv_result") == [0]
    assert get("_iv_input", 10)[9] == playback[-1]["_iv_mod_controls"]
    assert get("_iv_mod_controls", 32) == values
    assert get("_iv_controls", 32) == controls


@pytest.mark.parametrize("raw", [0, 0x7F00, 0xFFFF])
@pytest.mark.parametrize("family", range(4))
def test_extremes_clamp_invalid_frames_are_neutral_and_return_restores_base(
    playback, family, raw
):
    controls = bank(family)
    frame(playback, raw=raw)
    _, values = effective(playback, controls)
    if raw == 0xFFFF:
        assert values == controls
    for word, shift, maximum in TARGETS[family]:
        assert 0 <= ((values[word] >> shift) & 65535) <= maximum
    frame(playback)
    pointer, values = effective(playback, controls)
    assert pointer == playback[-1]["_test_values"] and values == controls


def test_clock_constraints_and_biased_envelope_are_preserved(playback):
    controls = bank(2)
    controls[22] |= 1 << 16
    controls[19] = (controls[19] & 65535) | (4 << 16)
    frame(playback, 2, 0x7F00)
    assert effective(playback, controls)[1][19] >> 16 == 7
    frame(playback, 2, 0)
    assert effective(playback, controls)[1][19] >> 16 == 1
    frame(playback, 3, 0x2000)
    assert effective(playback, controls)[1][23] >> 16 == 0
    controls = bank(0)
    controls[13] = 1  # MASTER GRID owns swing.
    frame(playback, 5, 0x7F00)
    values = effective(playback, controls)[1]
    assert values[12] == values[26] & 65535 == 0


def test_other_track_type_and_legacy_schema_cannot_borrow_modulation(playback):
    controls = bank(2)
    frame(playback, raw=0, machine=0)
    assert effective(playback, controls)[1] == controls
    frame(playback, raw=0)
    controls[0] = (controls[0] & ~255) | 9
    assert effective(playback, controls)[1] == controls


def test_previous_frame_is_neutral_after_reset_row_loss_and_type_changes(playback):
    call, put, get, reset, configure, _, symbols = playback
    controls = bank(2)
    frame(playback, 0, 0x6000)
    assert reset() == 1
    assert configure(values=controls) == 1

    def neutral_then_modulated():
        call(0)
        assert get("_iv_input", 10)[9] == symbols["_iv_controls"]
        call(0)
        assert get("_iv_input", 10)[9] == symbols["_iv_mod_controls"]

    neutral_then_modulated()
    # Inactive tracks' transport reset must not interrupt this owner.
    assert reset(track=1) == 1
    assert get("_iv_mod_ready") == [1]
    # The module's own reset invalidates even an unchanged generation.
    assert call(7) == 0
    assert get("_iv_mod_ready") == [0]
    neutral_then_modulated()
    frame(playback, 0, 0x6000, machine=0)
    call(0)
    assert get("_iv_mod_ready") == [0]
    assert get("_iv_input", 10)[9] == symbols["_iv_controls"]
    frame(playback, 0, 0x6000)
    neutral_then_modulated()
    # No callback/cache or warmed frame survives loss of the admitted session.
    put("_instrument_session", [0])
    call(0)
    assert get("_iv_output", 32) == [0] * 32
    assert get("_iv_mod_ready") == get("_iv_revision") == [0]
    put("_instrument_session", [1])
    assert reset(generation=2) == 1
    assert configure(generation=2, values=controls) == 1
    neutral_then_modulated()


def test_all_lanes_have_bounded_adapter_cost_and_neutral_path_avoids_copy(
    playback, record_property
):
    controls = bank(2)
    frame(playback)
    pointer, _ = effective(playback, controls)
    neutral = playback[0].last_instructions
    assert pointer == playback[-1]["_test_values"]
    frame(playback, raw=0x6000)
    pointer, _ = effective(playback, controls)
    active = playback[0].last_instructions
    assert pointer == playback[-1]["_iv_mod_controls"]
    assert neutral < 250 and active < 1200
    record_property("native_mod_neutral_instructions", neutral)
    record_property("native_mod_six_lanes_instructions", active)


def test_detail_ack_reports_base_values_while_native_lfo_moves_audio_controls(playback):
    call, put, get, reset, configure, _, _ = playback
    controls = bank(2)
    assert reset() == 1
    call(0)
    assert configure(values=controls) == 1
    put("_ir_process_flags", [1])
    frame(playback, raw=0x6000)
    fragments = {}
    for _ in range(88):
        call(0)
        status = get("_instrument_ui_status", 13)
        assert status[0] == 1
        if status[2] >> 30:
            assert status[6] == 1
            fragments[status[5] & 255] = status[7:]
    echoed = sum((fragments[i] for i in range(11)), [])
    expected = [value & 65535 for value in controls[16:]] + [
        value >> 16 for value in controls[16:]
    ]
    assert echoed[:32] == expected
    assert echoed[42] == controls[0] >> 16


def test_detail_rewrite_has_bounded_cost_and_neutral_fast_path(
    playback, record_property
):
    call, put, get, _, _, _, symbols = playback
    controls = bank(2)
    put("_test_values", controls)
    packet = [0] * 34
    packet[0], packet[9], packet[11], packet[22] = (
        5,
        symbols["_test_values"],
        1,
        symbols["_instrument_ui_status"],
    )
    put("_iv_input", packet)
    call(8, R4=symbols["_test_values"], R8=symbols["_iv_input"])
    neutral = call.last_instructions
    packet[9] = symbols["_iv_mod_controls"]
    put("_iv_input", packet)
    put("_instrument_ui_status", [1, 0, 1 << 30, 0, 0, 0x50B00, 1] + [999] * 6)
    call(8, R4=symbols["_test_values"], R8=symbols["_iv_input"])
    active = call.last_instructions
    assert get("_instrument_ui_status", 13)[7:] == [v & 65535 for v in controls[16:22]]
    assert neutral < 50 and active < 170
    record_property("native_mod_status_neutral_instructions", neutral)
    record_property("native_mod_status_detail_instructions", active)


@pytest.mark.parametrize("family", [0, 2, 3])
def test_native_modulation_changes_sounding_pcm_without_config_revision_or_note_retrigger(
    playback, family
):
    call, put, get, reset, configure, events, _ = playback
    controls = bank(family)
    assert reset() == 1
    call(0)
    assert configure(values=controls) == 1
    assert events([(1, 1, 60, 100, 0, 0)]) == 1
    call(0)
    # Rewind only the test instance between the paired next blocks: same note,
    # phase and envelope, changed stock mirror, no configuration submission.
    state = get("_test_state", 744)
    call(0)
    baseline = get("_iv_output", 32)
    put("_test_state", state)
    frame(playback, 0, 0x7000)
    call(0)
    changed = get("_iv_output", 32)
    assert baseline != changed
    assert get("_iv_result") == [0]
    assert get("_iv_controls", 32) == controls
    assert get("_iv_revision") == [1]
    before = [(state[i], state[i + 1]) for i in range(0, 48, 8)]
    after = get("_test_state", 48)
    assert [(after[i], after[i + 1]) for i in range(0, 48, 8)] == before
    assert any(token == 1 for token, _ in before)
    # No modulation state survives revocation of the admitted owner.
    put("_instrument_session", [0])
    call(0)
    assert get("_iv_output", 32) == [0] * 32
