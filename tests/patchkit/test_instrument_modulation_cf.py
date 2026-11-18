"""Execute the native parameter-code hook, including ABI and default isolation."""

import importlib.util
import struct

import pytest
from support.paths import REPO_ROOT
from support.words import be32

from tools.patchkit import pk_cfunit


def _machines_fw():
    """The firmware fixtures of core/machines' own test (it moved into the module), loaded by path."""
    path = REPO_ROOT / "modules/core/adapters/machines/tests/test_pk_machines_fw.py"
    spec = importlib.util.spec_from_file_location("machines_fw_fixtures", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_fw = _machines_fw()
COUNT, FIRST, ROWS, SRC, STOCK_LIST = (
    _fw.COUNT,
    _fw.FIRST,
    _fw.ROWS,
    _fw.SRC,
    _fw.STOCK_LIST,
)
STUBS, SYX, TYPE_FN, tables = _fw.STUBS, _fw.SYX, _fw.TYPE_FN, _fw.tables
# `mf_typearg` is a routine of the machines service file; it reads the image tables `MF_TAB` and `MF_EXT` (here: the fixtures, with
# `instrument_mod_param` of src-mod.c in the unit)
HOOKS, TAB, EXT, NATIVE_CALLS = _fw.HOOKS, _fw.TAB, _fw.EXT, _fw.NATIVE_CALLS
pytestmark = _fw.pytestmark
src_allocator = _fw.src_allocator

CONTROL = 0x47ED1000
SOUND = 0x47ED2000
NAMES = (
    "color",
    "drift",
    "mix",
    "strum",
    "gate",
    "swing",
    "position",
    "scan_depth",
    "scan_rate",
    "timbre_env",
    "wtfm_depth",
    "wtfm_env",
    "fm_depth",
    "balance",
    "feedback",
    "fm_env",
    "fm_attack",
    "fm_release",
)
MIRRORS = (26, 27, 28, 31, 32, 33)


@pytest.fixture(scope="module")
def mapped(tmp_path_factory, src_allocator):
    stub = tmp_path_factory.mktemp("mod-hook") / "owner.c"
    stub.write_text(f"""
#include <stdint.h>
uint32_t dkrt_instrument_type(void) {{ return *(volatile uint32_t *){CONTROL:#x}; }}
uint32_t dkrt_instrument_generation(void) {{ return *(volatile uint32_t *){CONTROL + 4:#x}; }}
/* sound.c stand-in: one admitted instance (state [0], type [5]) on track ip_track. */
int instrument_sound_admitted(uint32_t track) {{
    const volatile uint32_t *a = (const volatile uint32_t *){CONTROL + 8:#x};
    return track == *(volatile uint32_t *){CONTROL + 48:#x} && a[0] && a[5] == dkrt_instrument_type();
}}
""")
    return pk_cfunit.CfUnit(
        SYX,
        [
            SRC,
            HOOKS,
            TAB,
            EXT,
            SRC.with_name("src-pages.c"),
            REPO_ROOT / "tests/fixtures/native-page-absent.s",
            src_allocator,
            stub,
            REPO_ROOT / "modules/core/adapters/instrument-play/src/cf/src-mod.c",
            REPO_ROOT / "modules/core/adapters/instrument-play/src/cf/src-mod.s",
        ],
        [
            *STUBS,
            "ip_mod_split_next",
            "ip_mod_split_skip",
            "ip_mod_recombine_next",
            "ip_mod_recombine_skip",
        ],
        {
            "_mf_first": FIRST,
            "_mf_count": COUNT,
            "_mf_entries": COUNT,
            "_mf_rows": ROWS,
            "_mf_list_stock": STOCK_LIST,
            "_mf_spt": TYPE_FN,
            "mf_spt": TYPE_FN,
            **NATIVE_CALLS,
            "ip_mod_track_type": CONTROL + 40,
            **{f"ip_mod_{name}": 0x300 + index for index, name in enumerate(NAMES)},
        },
        [
            "mf_typearg",
            "instrument_mod_publish",
            "instrument_mod_defaults",
            "ip_mod_split",
            "ip_mod_recombine",
        ],
    )


@pytest.mark.parametrize("family", range(4))
def test_mod_picker_and_refresh_map_same_slots_without_clobbering_live_registers(
    mapped, family
):
    memory = {**tables(), CONTROL: be32(20) + be32(9)}
    cases = [{"routine": "instrument_mod_publish", "stack": [mapped.rts, family]}]
    for mirror in MIRRORS:
        cases.append(
            {
                "routine": "mf_typearg",
                "stack": [0] * 4 + [20, 0xBBBB],
                "d": {1: 0x77, 2: 0x88},
                "a": {0: mirror, 1: 0x1234, 2: 0x99},
            }
        )
    results = mapped.run_cases(memory, cases)[1:]
    start = 0 if family < 2 else 6 * (family - 1)
    for index, result in enumerate(results):
        assert result.stub == "mf_x_typearg"
        assert result.d[0] == 0x300 + start + index
        assert result.d[1:3] == [0x77, 0x88]
        assert result.a[:3] == [MIRRORS[index], 0x1234, 0x99]
        assert result.stack[4:6] == [20, 0xBBBB]


@pytest.mark.parametrize(
    "type_no,generation,mirror",
    [(0, 9, 26), (21, 9, 26), (20, 0, 26), (20, 9, 25), (20, 9, 34), (20, 9, 70)],
)
def test_nonowned_parameters_and_missing_generation_keep_stock_mapping(
    mapped, type_no, generation, mirror
):
    memory = {**tables(), CONTROL: be32(20) + be32(generation)}
    result = mapped.run_case(
        "mf_typearg", mem=memory, a={0: mirror}, stack=[0] * 4 + [type_no, 0xBBBB]
    )
    assert result.stub == "dk_tail_09514a38"
    assert result.stack[4] == type_no


def test_carrier_defaults_touch_only_unused_src_words(mapped):
    original = bytes(range(256))
    result = mapped.run_case(
        "instrument_mod_defaults",
        mem={SOUND: original},
        stack=[mapped.rts, SOUND],
        reads=[(SOUND, len(original))],
    )
    expected = bytearray(original)
    for mirror in MIRRORS:
        struct.pack_into(">H", expected, 0x14 + 2 * mirror, 0x4000)
    assert result.reads == [bytes(expected)]


@pytest.mark.parametrize("mirror", [29, 30])
def test_sampler_bank_aliases_are_not_exposed_for_canopy(mapped, mirror):
    result = mapped.run_case(
        "mf_typearg",
        mem={**tables(), CONTROL: be32(20) + be32(9)},
        stack=[0] * 4 + [20],
        a={0: mirror},
    )
    assert result.stub == "mf_x_typearg" and result.d[0] == 0


@pytest.mark.parametrize("site,track_register", [("split", 7), ("recombine", 6)])
@pytest.mark.parametrize(
    "track,owner,generation,admitted,source_type,frame_type,bypass",
    [
        (0, 0, 9, 1, 20, 20, True),
        (11, 11, 9, 1, 20, 20, True),
        (1, 0, 9, 1, 20, 20, False),
        (0, 11, 9, 1, 20, 20, False),
        (11, 0xFFFFFFFF, 9, 1, 20, 20, False),
        (0, 0, 0, 1, 20, 20, False),
        (0, 0, 9, 0, 20, 20, False),
        (0, 0, 9, 1, 21, 20, False),
        (0, 0, 9, 1, 20, 0, False),
        (11, 11, 9, 1, 20, 0, False),
    ],
)
def test_sampler_conversion_is_bypassed_only_for_current_admitted_track(
    mapped,
    site,
    track_register,
    track,
    owner,
    generation,
    admitted,
    source_type,
    frame_type,
    bypass,
):
    admission = [admitted, SOUND, 0, 0, 0, source_type, 0, 0]
    source = bytearray(128)
    struct.pack_into(">H", source, 0x5A, 0x6500)
    # The type-row literal names track 0's SRAM row; rows are 0x9a bytes apart.
    control = bytearray(64 + 0x9A * 16)
    control[:52] = (
        be32(20)
        + be32(generation)
        + b"".join(be32(v) for v in admission)
        + be32(CONTROL + 64)
        + be32(0)
        + be32(owner)
    )
    control[64 + 0x9A * track] = frame_type
    memory = {CONTROL: bytes(control), SOUND: bytes(source)}
    result = mapped.run_case(
        "ip_mod_" + site,
        mem=memory,
        d={0: 0xABCD1234, 1: 0x88776655, track_register: track},
        a={0: 0x4321, 1: 0x6789, 4: SOUND},
        stack=[0x12345678],
        reads=[(SOUND, 128)],
    )
    assert result.stub == "ip_mod_" + site + ("_skip" if bypass else "_next")
    assert result.a[0:2] == [0x4321, 0x6789]
    assert result.a[4] == SOUND and result.d[track_register] == track
    assert result.stack[0] == 0x12345678
    assert result.reads == [bytes(source)]
    if bypass:
        assert result.d[:2] == [0xABCD1234, 0x88776655]
    elif site == "split":
        assert result.d[:2] == [0xABCD6500, 0xABCD6500]
    else:
        assert result.d[:2] == [0x6500, 0x88776655]
        assert (result.n, result.z, result.v, result.c) == (0, 0, 0, 0)
