"""ISR anchors must resolve uniquely without embedding vendor instructions."""

import hashlib

import pytest

from tools.patchkit.port_resolve import ResolveError, resolve_cf_window_hash


def rule(window: bytes, **changes: object) -> dict:
    return {
        "name": "test.window",
        "kind": "cf_window_hash",
        "search_len": len(window),
        "sha256": hashlib.sha256(window).hexdigest(),
        "offset": 2,
        "len": 6,
        **changes,
    }


def test_unique_window_relocates_and_hashes_only_selected_site() -> None:
    window = bytes(range(12))  # synthetic; not firmware
    resolved = resolve_cf_window_hash(
        b"\xff" * 10 + window + b"\xfe" * 4, 0x1000, rule(window)
    )
    assert resolved == {
        "kind": "code",
        "addr": 0x100C,
        "len": 6,
        "sha256": hashlib.sha256(window[2:8]).hexdigest(),
    }


@pytest.mark.parametrize(
    "layout", ["missing", "duplicate", "odd", "mutated", "truncated"]
)
def test_refuses_ambiguous_or_changed_instruction_windows(layout: str) -> None:
    window = bytes(range(12))
    data = {
        "missing": b"\xff" * 30,
        "duplicate": window * 2,
        "odd": b"\xff" + window,
        "mutated": window[:-1] + b"\xff",
        "truncated": window[:-2],
    }[layout]
    with pytest.raises(ResolveError, match="matches"):
        resolve_cf_window_hash(data, 0x1000, rule(window))


@pytest.mark.parametrize(
    "changes",
    [
        {"search_len": 0},
        {"search_len": 5},
        {"search_len": 4098},
        {"offset": -2},
        {"offset": 1},
        {"offset": 8},
        {"len": 0},
        {"len": 3},
        {"sha256": "x" * 64},
    ],
)
def test_refuses_out_of_window_or_misaligned_rules(changes: dict) -> None:
    window = bytes(range(12))
    with pytest.raises(ResolveError, match="invalid"):
        resolve_cf_window_hash(window, 0x1000, rule(window, **changes))
