"""Pack CANOPY, a `[[machine]]` with `instrument = true`, and inspect its V2 DSP section."""

from canopy_paths import MODULE

from tools.patchkit import patchkit


def test_canopy_package_keeps_v2_callbacks_separate_from_v1(tmp_path):
    patchkit.build_module(MODULE)
    output = tmp_path / "canopy.dkm"
    result = patchkit.run(
        ["pack", "digikit/canopy", "--device", "dt2", "-o", str(output)], capture=True
    )
    assert result.returncode == 0, result.stderr
    file = output.read_bytes()

    def word(at):
        return int.from_bytes(file[at : at + 4], "big")

    at = word(0x98)  # H_DSP_OFF
    assert word(at + 4) == 8  # DSP section version 8: instruments with text ranges
    assert word(at + 0x30) == 0  # no V1 voices
    assert word(at + 0x44) == 1  # one V2 instrument
    assert word(word(at + 0x40)) != 0  # its machine type is the key of `canopy`
    assert word(0x94) > 0  # the keys table names the machine that init declares
