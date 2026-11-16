"""Ready-input cache keys must work with large, sparse card images."""

import hashlib
import json
from pathlib import Path

from tools.emulator import ready


def test_card_cache_key_streams_content_without_reading_whole_image(
    monkeypatch, tmp_path, capsys
):
    card = tmp_path / "card.img"
    content = b"card-header" + bytes(600_000) + b"card-footer"
    card.write_bytes(content)
    expected_key = hashlib.sha256(content).hexdigest()[:12]
    syx = tmp_path / "firmware.syx"
    syx.write_bytes(b"firmware")
    output = tmp_path / "ready"
    cached = output / "by-card" / expected_key
    cached.mkdir(parents=True)
    for path in (
        output / "digi-audio-dn2-image.bin",
        output / "cold.state",
        cached / "digi-audio-m5.snap",
        cached / "digi-audio-m5.snap.dsp",
        cached / "digi-audio-dn2-image.bin",
    ):
        path.touch()
    (output / "cold-idle.dsp").write_bytes(bytes(24))
    read_bytes = Path.read_bytes

    def bounded_read_bytes(path):
        assert path != card, "card images must not be materialized in memory"
        return read_bytes(path)

    monkeypatch.setattr(Path, "read_bytes", bounded_read_bytes)
    monkeypatch.setattr(
        ready,
        "build_examples",
        lambda: (_ for _ in ()).throw(
            AssertionError("a matching cached card must not rebuild")
        ),
    )
    assert (
        ready.main(
            ["dt2", "--syx", str(syx), "--out", str(output), "--card-image", str(card)]
        )
        == 0
    )
    assert f"ready_dir={cached}" in capsys.readouterr().out
    source = json.loads((cached / "SOURCE.json").read_text())
    assert source["card_image"] == str(card)
    assert source["syx_sha256"] == hashlib.sha256(b"firmware").hexdigest()
