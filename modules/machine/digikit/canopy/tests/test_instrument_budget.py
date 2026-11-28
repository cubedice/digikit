"""The adapter's DSP load budget (core/instrument-play `_iv_budget`, finding 377) on the SHARC core: the pressure it
derives from the cost of the last block, and CANOPY shedding and returning under it through the admitted callback."""

from test_instrument_play import playback as playback
from test_instrument_play import playback_object as playback_object


def rotated(cost):
    """The block handler leaves its cycle count rotated by 16 bits in word 0 of its reply record (finding 125)."""
    return ((cost << 16) | (cost >> 16)) & 0xFFFFFFFF


def started(playback, flags):
    call, put, get, reset, configure, events, symbols = playback
    assert reset() == 1
    call(0)
    assert configure(values=(3, 0, 64, 0, 5, 15)) == 1
    put("_ir_process_flags", [flags])
    put("_iv_budget_calm", [4])

    def block(cost):
        put("_test_reply_record", [rotated(cost)])
        call(0)
        assert get("_iv_result") == [0] or get("_iv_result") == [1]
        return get("_iv_pressure")[0]

    return block


def test_pressure_follows_the_last_block_cost_with_hysteresis(playback):
    call, put, get, _, _, _, symbols = playback
    block = started(playback, 1)
    limit, margin = get("_iv_budget_limit")[0], get("_iv_budget_margin")[0]
    assert (limit, margin, get("_iv_budget_calm")[0]) == (570000, 40000, 4)
    # without a reply record (core/dsp-link has built none) nothing is measured
    assert [block(2 * limit) for _ in range(3)] == [0, 0, 0]
    put("_dv_rec", [symbols["_test_reply_record"], 0, 0])
    # the limit itself is not over it; every block over it raises the pressure by one, up to 7
    assert block(limit) == 0
    assert [block(limit + 1) for _ in range(9)] == [1, 2, 3, 4, 5, 6, 7, 7, 7]
    assert get("_iv_budget_overs") == [9] and get("_iv_budget_cost") == [limit + 1]
    # a block under the limit but within the margin holds it, however many come
    assert [block(limit - margin + 1) for _ in range(12)] == [7] * 12
    # four calm blocks in a row lower it by one; a block within the margin starts the count again
    assert [block(limit - margin) for _ in range(4)] == [7, 7, 7, 6]
    assert [block(limit - margin) for _ in range(3)] == [6, 6, 6]
    assert block(limit - 1) == 6
    assert [block(0) for _ in range(4)] == [6, 6, 6, 5]
    # one block over the limit takes it up again at once
    assert block(limit + 1) == 6
    assert [block(0) for _ in range(26)][-7:] == [1, 1, 1, 1, 0, 0, 0]
    assert get("_iv_budget_overs") == [10]


def test_pressure_reaches_a_v5_input_in_bits_12_to_14_only(playback):
    call, put, get, _, _, _, symbols = playback
    block = started(playback, 1)
    put("_dv_rec", [symbols["_test_reply_record"], 0, 0])
    block(0)
    assert get("_iv_input", 13)[:2] == [5, 96]
    calm = get("_iv_input", 13)[12]
    assert calm & 0x7000 == 0
    for level in range(1, 8):
        assert block(10**6) == level
        assert get("_iv_input", 13)[12] == calm | (level << 12)
    # an instrument without the V5 status flag gets the V4 input: no bit of the pressure
    put("_ir_process_flags", [0])
    assert block(10**6) == 7
    assert get("_iv_input", 13)[:2] != [5, 96]
    assert get("_iv_input", 13)[12] & 0x7000 == 0


def test_canopy_goes_silent_under_full_pressure_and_returns(playback):
    call, put, get, _, _, events, symbols = playback
    block = started(playback, 1)
    put("_dv_rec", [symbols["_test_reply_record"], 0, 0])
    assert events([(1, 1, 60, 127, 0, 0), (2, 1, 64, 127, 0, 0)]) == 1
    for _ in range(40):
        assert block(0) == 0
    assert any(get("_iv_output", 32))

    def pads():
        """token -> (gate, level) of the four foreground slots"""
        state = get("_test_state", 32)
        return {
            state[i]: (state[i + 1], state[i + 7]) for i in range(0, 32, 8) if state[i]
        }

    assert set(pads()) == {1, 2} and all(level for _, level in pads().values())
    # full pressure: the pad's voices ramp out (64 samples) and stay owners
    levels = [block(10**6) for _ in range(12)]
    assert levels[:7] == [1, 2, 3, 4, 5, 6, 7]
    assert not any(get("_iv_output", 32))
    assert pads() == {1: (1, 0), 2: (1, 0)}
    assert get("_iv_active") == [1]  # still an active instrument: its owners are held
    # calm again: the pressure falls a level every four blocks and the pad fades back in
    while block(0):
        pass
    for _ in range(8):
        block(0)
    assert any(get("_iv_output", 32))
    assert set(pads()) == {1, 2} and all(
        gate == 1 and level for gate, level in pads().values()
    )
