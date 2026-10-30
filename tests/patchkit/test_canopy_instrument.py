"""Production packet-to-CANOPY callback integration on the SHARC core."""

import struct
from pathlib import Path

import pytest

from tools.patchkit import pk_sharc

ROOT = Path(__file__).resolve().parents[2]
MIDI = ROOT / "modules/core/adapters/midi-events/src/dsp"
CANOPY = ROOT / "modules/machines/canopy/src/dsp"
PACKET, OUTPUT = 0x20010000, 0x20080000


@pytest.fixture(scope="module")
def instrument():
    source = "\n".join(
        (MIDI / (name + ".s")).read_text()
        for name in ("packet", "receiver", "config", "consumer")
    )
    source += "\n" + "\n".join(
        (CANOPY / (name + ".s")).read_text()
        for name in ("voice", "tuning", "instrument")
    )
    obj = pk_sharc.assemble(source)
    sections, initial, address = {}, {}, 0x20020000
    for name, (blob, align, _) in obj.data.items():
        address = (address + align - 1) // align * align
        sections[name] = address
        initial.update(
            {
                address // 4 + i: int.from_bytes(blob[i * 4 : i * 4 + 4], "little")
                for i in range(len(blob) // 4)
            }
        )
        address += len(blob) + 64
    symbols = {
        name: sections[sec] + off for name, (sec, off, _) in obj.data_symbols.items()
    }
    linked = pk_sharc.link(obj, 0xB90000, symbols)

    def session():
        dm = dict(initial)
        dm.update({OUTPUT // 4 + i: 0 for i in range(32)})
        sequence = 1

        def call(
            kind,
            data=(),
            track=0,
            generation=1,
            revision=1,
            schema=0x43505331,
            port=0,
            channel=0,
            epoch=1,
        ):
            nonlocal sequence
            if kind == "render":
                entry, r4, r8 = "_cp_render_track", track, OUTPUT
            else:
                words = [0x494E0101, kind, epoch, sequence, 0, 0, 42, 0] + [0] * 24
                if kind == 2:
                    words[8:10] = [track, generation]
                elif kind in (3, 4):
                    words[5] = revision
                    words[8:16] = [
                        track,
                        generation,
                        revision,
                        int(kind == 4),
                        4,
                        schema,
                        0,
                        int(kind == 3),
                    ]
                    if kind == 3:
                        words[16] = 1
                else:
                    words[1] = 1 | (len(data) << 8)
                    words[4:6] = [1, revision]
                    for i, (token, event, note, velocity) in enumerate(data):
                        words[8 + 3 * i : 11 + 3 * i] = [
                            token,
                            generation,
                            event
                            | (port << 4)
                            | (channel << 6)
                            | (track << 10)
                            | (note << 14)
                            | (velocity << 21),
                        ]
                a = b = 0
                for i, word in enumerate(words):
                    if i != 7:
                        a = (a + word) & 0xFFFFFFFF
                        b = (b + a) & 0xFFFFFFFF
                words[7] = a ^ b
                dm.update({PACKET // 4 + i: word for i, word in enumerate(words)})
                entry, r4, r8 = "_mi_receive", PACKET, 0
            reads = {
                name: (sections[name] // 4, len(blob) // 4)
                for name, (blob, _, _) in obj.data.items()
            }
            result = pk_sharc.run_call(
                linked,
                entry,
                regs={
                    "R4": r4,
                    "R8": r8,
                    "M5": 0,
                    "M6": 1,
                    "L0": 0,
                    "L1": 0,
                    "L2": 0,
                    "MODE1": 0x1011800,
                },
                dm_words=dm,
                reads={**reads, "output": (OUTPUT // 4, 32)},
                max_steps=12000,
            )
            assert result["halt"]["reason"] == "breakpoint", result
            for name in reads:
                dm.update(
                    {
                        sections[name] // 4 + i: word
                        for i, word in enumerate(result[name])
                    }
                )
            status = result["regs"]["R0"]
            if kind != "render" and status == 1:
                sequence += 1
            at = symbols["_cp_banks"] // 4
            banks = [[dm[at + t * 64 + i] for i in range(64)] for t in range(16)]
            pcm = [
                struct.unpack("<f", struct.pack("<I", word))[0]
                for word in result["output"]
            ]
            return status, banks, pcm

        return call

    return session


def test_packet_notes_render_and_reset_without_cross_track_damage(instrument):
    call = instrument()
    assert call(2)[0] == 1
    assert call(3)[0] == call(4)[0] == 1
    assert call(1, [(101, 1, 60, 20), (102, 1, 67, 127)])[0] == 1
    status, banks, pcm = call("render")
    assert status == 1 and any(abs(x) > 0.00001 for x in pcm)
    before = banks[0]
    assert call(2, track=15)[1][0] == before
    assert call(1, [(101, 2, 60, 0)])[0] == 1
    _, banks, _ = call("render")
    slots = {banks[0][i]: banks[0][i : i + 8] for i in range(0, 48, 8) if banks[0][i]}
    assert slots[101][1] == 0 and slots[102][1] == 1
    assert call(2, generation=2)[0] == 1
    # Bounded reset tails survive ACK, but finish within two output blocks.
    call("render")
    assert call("render")[0] == 0
    assert call("render")[2] == [0.0] * 32
    assert call(1, [(103, 1, 60, 100)])[0] == 0  # stale generation


def test_batch_failure_rolls_back_all_notes_and_schema_is_checked(instrument):
    call = instrument()
    call(2)
    call(3, schema=123)
    assert call(4, schema=123)[0] == 0
    assert call(3)[0] == call(4)[0] == 1
    call(1, [(1, 1, 60, 100), (2, 1, 64, 100), (3, 1, 67, 100)])
    _, before, _ = call("render")
    # A valid onset followed by an invalid duplicate must not partly publish.
    status, after, _ = call(1, [(4, 1, 71, 100), (2, 1, 74, 100)])
    assert status == 0 and after == before
    # Unsupported controller after a release must not partly alter the bank.
    status, after, _ = call(1, [(1, 2, 60, 0), (0, 3, 66, 127)])
    assert status == 0 and after == before
    assert call(1, [(4, 1, 71, 100)])[0] == 1


def test_eight_onset_burst_commits_newest_four_without_permanent_retry(instrument):
    call = instrument()
    call(2)
    call(3)
    call(4)
    call(1, [(t, 1, 60 + t, 127) for t in range(1, 5)])
    call("render")
    status, banks, _ = call(1, [(t, 1, 60 + t, 127) for t in range(5, 13)])
    assert status == 1
    assert {banks[0][i] for i in range(0, 32, 8)} == {3, 4, 9, 10}
    assert {banks[0][i] for i in range(32, 48, 8)} == {1, 2}
    assert {banks[0][i] for i in range(48, 64, 4)} == {0, 11, 12}
    # Never-rendered intermediate notes need no audio tail or later release.
    before = banks[0]
    assert call(1, [(t, 2, 60 + t, 0) for t in range(1, 9)])[1][0] == before
    _, banks, _ = call("render")
    assert {banks[0][i] for i in range(0, 32, 8)} == {9, 10, 11, 12}
    assert all(banks[0][i] == 0 for i in range(32, 48, 8))
    assert all(banks[0][i] == 0 for i in range(48, 64, 4))


def gates(banks, track=0):
    return {
        banks[track][i]: banks[track][i + 1] for i in range(0, 32, 8) if banks[track][i]
    }


@pytest.mark.parametrize("port,channel", [(0, 0), (1, 15)])
def test_sustain_threshold_route_isolation_and_controller_reset(
    instrument, port, channel
):
    call = instrument()
    call(2)
    call(3)
    call(4)

    def events(data, **route):
        return call(1, data, port=port, channel=channel, **route)

    assert events([(101, 1, 60, 100), (102, 1, 67, 100)])[0] == 1
    call("render")
    assert events([(0, 3, 64, 64), (101, 2, 60, 0)])[0] == 1
    _, banks, pcm = call("render")
    assert gates(banks) == {101: 1, 102: 1}
    assert any(abs(x) > 1e-5 for x in pcm)
    # A different route's pedal-up cannot release this route's sustained C.
    assert call(1, [(0, 3, 64, 0)], port=1 - port, channel=channel)[0] == 1
    assert gates(call("render")[1])[101] == 1
    assert call(1, [(0, 3, 64, 0)], port=port, channel=(channel + 1) % 16)[0] == 1
    assert gates(call("render")[1])[101] == 1
    # Below 64 releases only C; physically held G remains held.
    _, banks, _ = events([(0, 3, 64, 63)])
    assert gates(banks) == {101: 0, 102: 1}
    events([(0, 3, 64, 127), (102, 2, 67, 0)])
    assert gates(call("render")[1])[102] == 1
    _, banks, _ = events([(0, 3, 121, 0)])
    assert gates(banks)[102] == 0


def test_pedal_and_deferred_release_rollback_with_invalid_batch(instrument):
    call = instrument()
    call(2)
    call(3)
    call(4)
    call(1, [(1, 1, 60, 100)])
    call("render")
    # Rejected pedal-down must not affect a later note-off.
    assert call(1, [(0, 3, 64, 127), (1, 1, 60, 100)])[0] == 0
    assert gates(call(1, [(1, 2, 60, 0)])[1])[1] == 0
    call(1, [(2, 1, 67, 100), (0, 3, 64, 127)])
    call("render")
    before = call(1, [(2, 2, 67, 0)])[1]
    assert gates(before)[2] == 1
    status, after, _ = call(1, [(0, 3, 64, 0), (2, 1, 67, 100)])
    assert status == 0 and before == after
    # Both the pedal bit and deferred token survived the rejected pedal-up.
    assert gates(call(1, [(0, 3, 64, 0)])[1])[2] == 0


def test_sustain_pending_steal_and_reset_do_not_resurrect_old_tokens(instrument):
    call = instrument()
    call(2)
    call(3)
    call(4)
    call(1, [(t, 1, 60 + t, 100) for t in range(1, 5)])
    call("render")
    call(1, [(0, 3, 64, 127)] + [(t, 2, 60 + t, 0) for t in range(1, 5)])
    # Force two tails and two pending replacements while every old key is pedal-held.
    assert call(1, [(t, 1, 60 + t, 100) for t in range(5, 9)])[0] == 1
    assert call(1, [(t, 2, 60 + t, 0) for t in range(5, 9)])[0] == 1
    call("render")
    _, banks, _ = call("render")
    slots = {banks[0][i]: banks[0][i + 1] for i in range(0, 32, 8)}
    assert slots == {t: 1 for t in range(5, 9)}
    _, banks, _ = call(1, [(0, 3, 64, 0)])
    assert all(banks[0][i + 1] == 0 for i in range(0, 32, 8))
    call(1, [(0, 3, 64, 127)])
    assert call(2, generation=2)[0] == 1
    call("render")
    assert call("render")[0] == 0
    call(3, generation=2)
    call(4, generation=2)
    call(1, [(20, 1, 60, 100)], generation=2)
    call("render")
    # Reset cleared the old down pedal; the fresh generation releases normally.
    _, banks, _ = call(1, [(20, 2, 60, 0)], generation=2)
    assert next(banks[0][i + 1] for i in range(0, 32, 8) if banks[0][i] == 20) == 0


def test_track_reset_isolates_pedal_and_stolen_release_keeps_fresh_key(instrument):
    call = instrument()
    for track in (0, 15):
        call(2, track=track)
        call(3, track=track)
        call(4, track=track)
        call(
            1,
            [(100 + track, 1, 60, 100), (0, 3, 64, 127), (100 + track, 2, 60, 0)],
            track=track,
        )
        call("render", track=track)
    call(2, generation=2)
    assert gates(call("render", track=15)[1], 15)[115] == 1
    # Fill all remaining slots, then steal the old sustained voice. Its stored
    # pedal release must not kill the fresh same-pitch replacement.
    call(1, [(t, 1, 60, 100) for t in (116, 117, 118, 119)], track=15)
    call("render", track=15)
    call("render", track=15)
    _, banks, _ = call(1, [(0, 3, 64, 0)], track=15)
    assert gates(banks, 15) == {t: 1 for t in (116, 117, 118, 119)}


def test_new_epoch_clears_pedals_on_every_track(instrument):
    call = instrument()
    for track in (0, 15):
        call(2, track=track)
        call(3, track=track)
        call(4, track=track)
        call(
            1,
            [(1 + track, 1, 60, 100), (0, 3, 64, 127), (1 + track, 2, 60, 0)],
            track=track,
        )
        call("render", track=track)
    assert call(2, epoch=2)[0] == 1
    for track in (0, 15):
        call("render", track=track)
        assert call("render", track=track)[0] == 0
        if track:
            assert call(2, track=track, epoch=2)[0] == 1
        assert call(3, track=track, epoch=2)[0] == 1
        assert call(4, track=track, epoch=2)[0] == 1
        assert call(1, [(100 + track, 1, 67, 100)], track=track, epoch=2)[0] == 1
        call("render", track=track)
        _, banks, _ = call(1, [(100 + track, 2, 67, 0)], track=track, epoch=2)
        assert gates(banks, track)[100 + track] == 0
