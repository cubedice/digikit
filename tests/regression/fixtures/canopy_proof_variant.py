"""Prepare an isolated, explicitly selected CANOPY persistence proof adapter.

The generated manifest is ignored build output. Production instrument-play's
manifest and compiled object are never changed by this helper.
"""

import re
from pathlib import Path


def main() -> None:
    root = Path(__file__).resolve().parents[3]
    adapter = root / "modules/core/adapters/instrument-play"
    target = root / "out/canopy-sound-proof-modules/instrument-play"
    target.mkdir(parents=True, exist_ok=True)
    wrapper = target / "control-proof.c"
    wrapper.write_text(
        '#define DK_CANOPY_SOUND_PROOF 1\n#include "'
        + (adapter / "src/cf/control.c").as_posix()
        + '"\n',
        encoding="utf-8",
    )
    keyboard_wrapper = target / "keyboard-proof.c"
    keyboard_wrapper.write_text(
        '#define DK_CANOPY_SOUND_PROOF 1\n#include "'
        + (root / "tests/regression/fixtures/canopy_keyboard_proof.c").as_posix()
        + '"\n',
        encoding="utf-8",
    )
    sound_wrapper = target / "sound-proof.c"
    sound_wrapper.write_text(
        '#define DK_CANOPY_SOUND_PROOF 1\n#include "'
        + (adapter / "src/cf/sound.c").as_posix()
        + '"\n',
        encoding="utf-8",
    )
    text = (adapter / "module.toml").read_text(encoding="utf-8")
    text = text.replace('id = "core/instrument-play"', 'id = "test/instrument-play-proof"', 1)
    text = text.replace('category = "core"', 'category = "test"', 1)
    text = text.replace('conflicts = [', 'conflicts = ["core/instrument-play", ', 1)

    def source(match: re.Match[str]) -> str:
        relative = match.group(1)
        path = (
            wrapper if relative == "src/cf/control.c"
            else keyboard_wrapper if relative == "src/cf/keyboard.c"
            else sound_wrapper if relative == "src/cf/sound.c"
            else adapter / relative
        )
        return '"' + path.as_posix() + '"'

    text = re.sub(r'"(src/[^"\n]+)"', source, text)
    (target / "module.toml").write_text(text, encoding="utf-8")
    print(target.parent)


if __name__ == "__main__":
    main()
