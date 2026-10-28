"""Resume partial compiler installs instead of mistaking wrappers for a toolchain."""

from tools.patchkit import pk_toolchains as T


def fake_install(tmp_path, monkeypatch, *, missing=(), library=False, tree=False):
    prefix = tmp_path / "m68k-elf"
    (prefix / "bin").mkdir(parents=True)
    for name in ("gcc", "as", "ld", "ar", "ranlib", "nm", "objcopy"):
        if name not in missing:
            (prefix / "bin" / f"m68k-elf-{name}").touch()
    if library:
        (prefix / "lib").mkdir()
        (prefix / "lib" / T.LIBGCC_CF_NAME).touch()
    makefile = tmp_path / "build/gcc/m68k-elf/libgcc/Makefile"
    if tree:
        makefile.parent.mkdir(parents=True)
        makefile.touch()
    monkeypatch.setattr(T, "TOOLS", tmp_path)
    monkeypatch.setattr(T, "M68K_PREFIX", prefix)
    monkeypatch.setattr(T, "fetch", lambda url: tmp_path / url.rsplit("/", 1)[-1])
    monkeypatch.setattr(T, "unpack", lambda path: path)
    monkeypatch.setattr(
        T,
        "m68k_dependency_prefixes",
        lambda: dict.fromkeys(("gmp", "mpfr", "libmpc", "isl", "zstd"), "/usr"),
    )
    configured = set()

    def run(command, cwd=None, env=None):
        if command[0].endswith("/configure") or command[0].endswith("\\configure"):
            configured.add(cwd.name)
            if cwd.name == "gcc":
                makefile.parent.mkdir(parents=True, exist_ok=True)
                makefile.touch()

    def build_library():
        assert makefile.exists(), "library build attempted without its GCC build tree"

    monkeypatch.setattr(T, "run", run)
    monkeypatch.setattr(T, "build_libgcc_cf", build_library)
    return configured


def test_proof_wrappers_do_not_skip_required_compiler_build(tmp_path, monkeypatch):
    configured = fake_install(tmp_path, monkeypatch, missing=("ar", "ranlib"))
    T.build_m68k()
    assert configured == {"binutils", "gcc"}


def test_complete_install_does_not_require_retained_build_tree(tmp_path, monkeypatch):
    configured = fake_install(tmp_path, monkeypatch, library=True)
    T.build_m68k()
    assert not configured


def test_existing_gcc_tree_can_finish_missing_coldfire_library(tmp_path, monkeypatch):
    configured = fake_install(tmp_path, monkeypatch, tree=True)
    T.build_m68k()
    assert not configured
