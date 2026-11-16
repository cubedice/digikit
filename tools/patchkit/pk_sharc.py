#!/usr/bin/env python3
"""SHARC+ test support: the Rust build (packages/patchkit/src/build/sharc_obj.rs) for rigs that place an object themselves, and the
runner of the concrete SHARC+ core.

`patchkit build` makes the SHARC+ objects of the modules: Selache as a library assembles, the encoding-bug guards and fix-ups apply, and
every instruction is re-decoded with the decoder of the runtime. This module calls the same code through `patchkit sharc` for tests that
link an object at a base of their own and run it on the core:

  1. ``assemble`` / ``compile_c``: the source (or the object of Selache's C compiler) as a ``SharcObject`` (section, symbols,
     relocations, instruction rows, data sections);
  2. ``link``: the object placed at a short-word (PC) address with externs, verified (every instruction re-decoded, every relocated
     field read back); a ``Linked`` with the code in the order of the boot stream;
  3. ``run_call`` / ``CallSession``: run the linked code on the concrete core (tools/sharc_dev/sharc_run.py).

Source text: ``expand_includes``, ``apply_conditionals`` and ``sharc_object`` (the `.dko.json` object of sources) are `patchkit sharc`
too.
"""

from __future__ import annotations

import hashlib
import json
import subprocess
import tempfile
import tomllib
from dataclasses import dataclass, field
from pathlib import Path
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from tools.sharc_dev.sharcldr import LoadedMemory

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent

from tools.patchkit import patchkit, pk_toolchains  # noqa: E402

PROCESSOR = "ADSP-21569"
SW_ALIAS_BASE = (
    0x28000000  # loader byte address of short-word 0 (tools/sharc_dev/sharcldr.py)
)


class PkSharcError(RuntimeError):
    pass


@dataclass
class SharcReloc:
    parcel: int  # offset of the instruction, in parcels
    rtype: int
    symbol: str
    addend: int


@dataclass
class SharcObject:
    source_sha256: str
    section: bytes  # Selache parcel order (big-endian per 16 bits)
    symbols: dict[
        str, tuple[int, bool]
    ]  # name -> (parcel offset, global); defined symbols only
    relocs: list[SharcReloc]
    rows: list[tuple[int, str]]  # instruction extents: (parcel offset, raw hex)
    toolchain: dict[str, str] = field(default_factory=dict)
    # Data sections (DM): name -> (bytes in memory order, alignment, is_bss); data symbols: name -> (section, byte offset, global)
    data: dict[str, tuple[bytes, int, bool]] = field(default_factory=dict)
    data_symbols: dict[str, tuple[str, int, bool]] = field(default_factory=dict)
    # what `link` hands to `patchkit sharc link`: ("text", source) or ("elf", object bytes, source)
    origin: tuple = ()


@dataclass
class Linked:
    base_sw: int
    code_selache: bytes
    code_boot: bytes  # little-endian per parcel, the order of the boot stream and of tools/sharc_dev/sharc_isa.py
    symbols: dict[str, int]  # short-word addresses
    checked: int = 0  # instructions re-decoded
    ureg_class: list[str] = field(
        default_factory=list
    )  # stores whose source register the instruction class excludes


def _sharc(args: list[str], stdin: str | None = None) -> str:
    """`patchkit sharc ARGS` (stdout); a failure is a PkSharcError with the binary's message."""
    r = subprocess.run(
        [str(patchkit.binary()), "--root", str(ROOT), "sharc", *args],
        cwd=ROOT,
        input=stdin,
        capture_output=True,
        text=True,
    )
    if r.returncode:
        raise PkSharcError((r.stderr or r.stdout).strip().removeprefix("patchkit: "))
    return r.stdout


def _input_args(origin: tuple, tmp: Path) -> list[str]:
    if origin[0] == "text":
        (tmp / "m.s").write_text(origin[1])
        return ["--text", str(tmp / "m.s")]
    _, elf, source = origin
    (tmp / "m.doj").write_bytes(elf)
    (tmp / "m.src").write_text(source)
    return ["--elf", str(tmp / "m.doj"), "--source", str(tmp / "m.src")]


def _object(origin: tuple) -> SharcObject:
    with tempfile.TemporaryDirectory(prefix="pk-sharc-") as d:
        j = json.loads(_sharc(["asm", *_input_args(origin, Path(d))]))
    return SharcObject(
        j["source_sha256"],
        bytes.fromhex(j["section"]),
        {n: (p, g) for n, (p, g) in j["symbols"].items()},
        [
            SharcReloc(r["parcel"], r["rtype"], r["symbol"], r["addend"])
            for r in j["relocs"]
        ],
        [(p, h) for p, h in j["rows"]],
        j["toolchain"],
        {n: (bytes.fromhex(h), a, b) for n, (h, a, b) in j["data"].items()},
        {n: (s, o, g) for n, (s, o, g) in j["data_symbols"].items()},
        origin,
    )


def assemble(source: str) -> SharcObject:
    """Selache assembles `source` (an expanded text); the instruction check of the build applies."""
    return _object(("text", source))


def compile_c(source: str) -> SharcObject:
    """Selache's C compiler (selcc); evaluated in docs/findings/20-patch-toolchains.md. Its object passes the same instruction check as
    an assembled one."""
    sel = pk_toolchains.selache_dir()
    if sel is None:
        raise PkSharcError(
            "selache is not built (uv run python -m tools.patchkit.pk_toolchains selache)"
        )
    selcc_path = sel / "target" / "release" / "selcc"
    if not selcc_path.exists():
        raise PkSharcError(
            f"{selcc_path} does not exist: selcc is not built by `pk_toolchains selache`"
        )
    selcc = str(selcc_path)
    with tempfile.TemporaryDirectory(prefix="pk-sharc-") as d:
        src, obj = Path(d) / "m.c", Path(d) / "m.doj"
        src.write_text(source)
        r = subprocess.run(
            [selcc, "-proc", PROCESSOR, "-c", "-o", str(obj), str(src)],
            capture_output=True,
            text=True,
        )
        if r.returncode:
            raise PkSharcError(f"selcc failed:\n{r.stdout}{r.stderr}")
        return _object(("elf", obj.read_bytes(), source))


def link(
    obj: SharcObject, base_sw: int, externs: dict[str, int] | None = None
) -> Linked:
    """Place `obj` at the short-word address `base_sw`, resolve its relocations against its own symbols and `externs`, and verify it."""
    with tempfile.TemporaryDirectory(prefix="pk-sharc-") as d:
        args = ["link", *_input_args(obj.origin, Path(d)), "--base", hex(base_sw)]
        for n, v in (externs or {}).items():
            args += ["--extern", f"{n}={v:#x}"]
        j = json.loads(_sharc(args))
    return Linked(
        j["base_sw"],
        bytes.fromhex(j["code_selache"]),
        bytes.fromhex(j["code_boot"]),
        j["symbols"],
        j["checked"],
        j["ureg_class"],
    )


def build(
    source: str, base_sw: int, externs: dict[str, int] | None = None
) -> tuple[SharcObject, Linked]:
    obj = assemble(source)
    return obj, link(obj, base_sw, externs)


def sharc_include_dirs(moddir: Path) -> list[Path]:
    """The include directories of a SHARC+ object of the module: its generated and own sources, then the shared ones listed in
    tools/patchkit/toolchain/pins.toml (the file `patchkit build` reads). The directories of the sources come after these."""
    pins = tomllib.loads((HERE / "toolchain" / "pins.toml").read_text())
    return [
        moddir / "build" / "gen",
        moddir / "src",
        *(ROOT / d for d in pins["include"]["sharc"]),
    ]


def expand_includes(text: str, dirs: list[Path], depth: int = 0) -> str:
    """Replace `#include "name"` lines by the file's text (searched in `dirs`); `patchkit sharc inc`."""
    args = [x for d in dirs for x in ("-I", str(d))]
    return _sharc(["inc", *args], stdin=text)


def apply_conditionals(text: str, defines: frozenset[str] = frozenset()) -> str:
    """Build options: `#ifdef NAME` / `#ifndef NAME` / `#else` / `#endif` lines; `patchkit sharc cond`."""
    args = [x for d in sorted(defines) for x in ("--define", d)]
    return _sharc(["cond", *args], stdin=text)


def sharc_object(
    sources: list[Path],
    include_dirs: list[Path] | None = None,
    defines: frozenset[str] = frozenset(),
) -> dict:
    """One or more `.s` files, concatenated into one translation unit, to the `.dko.json` object `patchkit build` writes."""
    args = [str(p) for p in sources]
    for d in include_dirs or []:
        args += ["-I", str(d)]
    for d in sorted(defines):
        args += ["--define", d]
    text = _sharc(["text", *args])
    return json.loads(_sharc(["object", "--text", "-"], stdin=text))


# ---------------------------------------------------------------- running on the concrete core


_DECODE_CACHES: dict[bytes, dict] = {}


def run_call(
    linked: Linked,
    entry: str,
    regs: dict[str, int] | None = None,
    dm_words: dict[int, int] | None = None,
    reads: dict[str, tuple[int, int]] | None = None,
    max_steps: int = 100000,
    return_sw: int = 0xB9F000,
    others: tuple[Linked, ...] = (),
    read_regs: tuple[str, ...] = (),
    extra_breaks: tuple[int, ...] = (),
    backend: str = "python",
) -> dict[str, object]:
    """Run ``entry`` (a symbol) to its RTS on the concrete core and return registers and memory reads.

    ``dm_words`` maps normal-word DM addresses (L2: 0x08000000 up) to values to poke before the run;
    ``reads`` maps a result name to (normal-word address, word count). Floats are passed as raw bits.
    ``backend``: "python" (the Python core), "native" (the native core, see NativeCallSession) or "check" (both,
    failing on any difference); ``session_backend()`` picks one. ``extra_breaks`` needs the Python core.
    """
    args = (
        linked,
        entry,
        regs,
        dm_words,
        reads,
        max_steps,
        return_sw,
        others,
        read_regs,
    )
    if backend == "python" or extra_breaks:
        return _run_call_python(*args, extra_breaks)
    if backend == "native":
        return _run_call_native(*args)
    if backend == "check":
        expected = _run_call_python(*args, extra_breaks)
        _check_same(f"run_call {entry}", expected, _run_call_native(*args))
        return expected
    raise PkSharcError(f"unknown backend {backend!r}")


def _run_call_python(
    linked: Linked,
    entry: str,
    regs: dict[str, int] | None = None,
    dm_words: dict[int, int] | None = None,
    reads: dict[str, tuple[int, int]] | None = None,
    max_steps: int = 100000,
    return_sw: int = 0xB9F000,
    others: tuple[Linked, ...] = (),
    read_regs: tuple[str, ...] = (),
    extra_breaks: tuple[int, ...] = (),
) -> dict[str, object]:
    from tools.sharc_dev import sharc_run
    from tools.sharc_dev import sharc_trace as st
    from tools.sharc_dev.sharcldr import LoadedMemory

    start = linked.symbols[entry]
    blocks, data = [], b""
    for i, obj in enumerate((linked, *others)):
        blocks.append(
            {
                "target_address": SW_ALIAS_BASE + 2 * obj.base_sw,
                "byte_count": len(obj.code_boot),
                "fill": False,
                "payload_offset": len(data),
                "payload_len": len(obj.code_boot),
                "index": i,
            }
        )
        data += obj.code_boot
    mem = LoadedMemory(data, blocks)
    # The decode of a pc depends only on the loaded code bytes and their placement, so calls that load the same code
    # (a rig calling a routine block after block) share one decode cache instead of decoding every instruction again.
    cache_key = hashlib.sha256(
        repr([(b["target_address"], b["byte_count"]) for b in blocks]).encode() + data
    ).digest()
    runner = sharc_run.Runner(
        mem,
        start,
        regs=regs or {},
        breakpoints=[return_sw, *extra_breaks],
    )
    runner._cache = _DECODE_CACHES.setdefault(cache_key, {})
    for addr, word in (dm_words or {}).items():
        if not st._dm_write(
            runner.state, addr, 4, st.Const(word & 0xFFFFFFFF), normal_word=True
        ):
            raise PkSharcError(f"cannot poke DM word {addr:#x}")
    runner.state.call_stack.append(return_sw)
    res = runner.run(max_steps)
    out: dict[str, object] = {
        "halt": res.halt.to_json(),
        "instructions": res.instructions,
        "final_pc_sw": res.final_pc_sw,
    }
    for name, (addr, count) in (reads or {}).items():
        vals = []
        for k in range(count):
            v = st._dm_read(runner.state, addr + k, 4, normal_word=True)
            vals.append(None if v is None else v.value & 0xFFFFFFFF)
        out[name] = vals
    out["regs"] = {
        n: getattr(runner.state.uregs.get(st.UREG_CODES[n]), "value", None)
        for n in (
            "R0",
            "R1",
            "R2",
            "R4",
            "R5",
            "R6",
            "R7",
            "R8",
            "R9",
            "R10",
            "R11",
            "R12",
            "R13",
            "I0",
            *read_regs,
        )
    }
    return out


class CallSession:
    """A concrete core that keeps its memory between calls (``run_call`` builds a new core for every call).

    ``call`` starts a routine at a symbol the way ``run_call`` does: the registers given, a return address that stops the
    run, and all registers it does not name as the last call left them. DM written by a call stays (a delay line, a
    routine's state); ``poke`` and ``peek`` read and write DM words in normal-word addresses. A rig that calls a routine
    block after block uses this instead of copying its state in and out of every call.

    ``base_memory`` optionally supplies an immutable original image. Linked code overlays it last; sessions with a base image
    retain the Runner-local decode cache so that different images cannot share decoded instructions.

    ``fresh_registers`` starts every call from the registers of a new core, as ``run_call`` does, while DM still carries over:
    a rig that used ``run_call`` and copied its memory in and out of every call gets the same register semantics without the
    copies. ``fork`` gives an independent session with a copy of this one's DM, so a shared image is poked once."""

    def __init__(
        self,
        linked: list[Linked],
        return_sw: int,
        breaks: tuple[int, ...] = (),
        base_memory: LoadedMemory | None = None,
        fresh_registers: bool = False,
    ) -> None:
        from tools.sharc_dev import sharc_run
        from tools.sharc_dev.sharcldr import LoadedMemory

        self._args = (
            list(linked),
            return_sw,
            tuple(breaks),
            base_memory,
            fresh_registers,
        )
        self.fresh_registers = fresh_registers
        self._st = sharc_run.st
        self._run = sharc_run
        self.return_sw = return_sw
        # Keep the original image's block metadata and payload offsets intact;
        # linked code is appended last, following LoadedMemory's overlay order.
        blocks = list(base_memory.blocks) if base_memory is not None else []
        data = base_memory.data if base_memory is not None else b""
        for obj in linked:
            blocks.append(
                {
                    "target_address": SW_ALIAS_BASE + 2 * obj.base_sw,
                    "byte_count": len(obj.code_boot),
                    "fill": False,
                    "payload_offset": len(data),
                    "payload_len": len(obj.code_boot),
                    "index": len(blocks),
                }
            )
            data += obj.code_boot
        mem = LoadedMemory(data, blocks)
        cache_key = hashlib.sha256(
            repr([(b["target_address"], b["byte_count"]) for b in blocks]).encode()
            + data
        ).digest()
        self.runner = sharc_run.Runner(
            mem,
            linked[0].base_sw,
            regs={},
            breakpoints=[return_sw, *breaks],
        )
        if base_memory is None:
            self.runner._cache = _DECODE_CACHES.setdefault(cache_key, {})
        # The new core's state: the register file a fresh_registers call starts from.
        self._initial = self.runner.state
        self.symbols: dict[str, int] = {}
        for obj in linked:
            self.symbols.update(obj.symbols)

    def fork(
        self, backend: str = "python"
    ) -> CallSession | NativeCallSession | CheckedSession:
        """An independent session on the same code with a copy of this one's DM (its registers are a new core's).
        BACKEND "native" runs its calls on the native core (NativeCallSession; fresh_registers sessions only),
        "check" on both (CheckedSession); ``session_backend()`` picks one."""
        if backend == "native":
            return NativeCallSession(self, _require_native())
        if backend == "check":
            return CheckedSession(
                self.fork("python"), NativeCallSession(self, _require_native())
            )
        if backend != "python":
            raise PkSharcError(f"unknown backend {backend!r}")
        other = CallSession(*self._args)
        other.runner.state.overlay.update(self.runner.state.overlay)
        return other

    def record_stores(self) -> None:
        """Log every DM store of the following calls (address, width, PC) in ``stores`` (canonical DM byte addresses). Pokes and
        peeks between calls are not logged. Uses the driver's write watchpoints (tools/sharc_dev/sharc_run.py), so a store
        has one semantics path."""
        wp = self._run.Watchpoint(0, 1 << 40, on_read=False, stop=False, label="store")
        self.runner.attach_watchpoints([wp])
        self.runner.set_watch_active(False)
        self._recording = True

    def record_reads(self, addr: int, count: int) -> None:
        """Log every DM read of the normal-word range [addr, addr + count) of the following calls in ``stores`` (the same
        watchpoint log as ``record_stores``; the two replace each other). A kernel test uses it to prove that a routine does
        not read past the buffers it is given."""
        wp = self._run.Watchpoint(
            addr,
            addr + count,
            on_read=True,
            on_write=False,
            stop=False,
            label="read",
            normal_word=True,
        )
        self.runner.attach_watchpoints([wp])
        self.runner.set_watch_active(False)
        self._recording = True

    stores: tuple = ()
    _recording = False

    def poke(self, addr: int, word: int) -> None:
        if not self._st._dm_write(
            self.runner.state,
            addr,
            4,
            self._st.Const(word & 0xFFFFFFFF),
            normal_word=True,
        ):
            raise PkSharcError(f"cannot poke DM word {addr:#x}")

    def peek(self, addr: int) -> int | None:
        v = self._st._dm_read(self.runner.state, addr, 4, normal_word=True)
        return None if v is None else v.value & 0xFFFFFFFF

    def call(
        self,
        entry: str,
        regs: dict[str, int],
        max_steps: int = 100000,
        read_regs: tuple[str, ...] = (),
    ) -> dict[str, object]:
        import dataclasses

        st, runner = self._st, self.runner
        old = runner.state
        # fresh_registers: every field but DM from the new core's state (registers, MMRs, accumulators, banks).
        base = self._initial if self.fresh_registers else old
        new_uregs = dict(base.uregs)
        for key, value in regs.items():
            new_uregs[st._seed_code(key)] = st._seed_value(value)
        state = dataclasses.replace(
            base,
            overlay=old.overlay,
            trace=[] if self.fresh_registers else old.trace,
            pc_sw=self.symbols[entry],
            uregs=new_uregs,
            call_stack=[self.return_sw],
            pc_stack=(
                [0x01000000 | (self.return_sw & 0xFFFFFF)] if base.stack_model else []
            ),
            pc_stack_pending=-1,
            pc_stack_requested=-1,
            loops=[],
            loop_depth=0,
            loop_slots=list(base.loop_slots),
            status_stack=[],
            mmrs=dict(base.mmrs),
            special=dict(base.special),
            bank_alt=dict(base.bank_alt),
            pending=None,
            stopped=None,
        )  # the overlay (DM written so far) is shared, not copied
        if state.stack_model:
            st._sync_pc_stack(state)
            st._sync_status_stack(state)
        runner.state = state
        runner.instructions = 0
        if self._recording:
            runner.clear_watch_log()
            runner.set_watch_active(True)
        try:
            res = runner.run(max_steps)
        finally:
            if self._recording:
                runner.set_watch_active(False)
                self.stores = runner.watch_log
        names = (
            "R0",
            "R1",
            "R2",
            "R4",
            "R5",
            "R6",
            "R7",
            "R8",
            "R9",
            "R10",
            "R11",
            "R12",
            "R13",
            "I0",
            *read_regs,
        )
        return {
            "halt": res.halt.to_json(),
            "instructions": res.instructions,
            "final_pc_sw": res.final_pc_sw,
            "regs": {
                n: getattr(state.uregs.get(st.UREG_CODES[n]), "value", None)
                for n in names
            },
        }


NATIVE_HINT = "mise run gen:native-lib (SHARC_NATIVE_LIB overrides)"
_NATIVE_LIBRARY: list[str | None] = []
_NATIVE_CORES: dict[bytes, object] = {}
REG_NAMES = (
    "R0",
    "R1",
    "R2",
    "R4",
    "R5",
    "R6",
    "R7",
    "R8",
    "R9",
    "R10",
    "R11",
    "R12",
    "R13",
    "I0",
)


def native_library() -> str | None:
    """The native SHARC+ library to run calls on, or None when it is not built or is stale for this tree."""
    if not _NATIVE_LIBRARY:
        from tools.sharc_dev import sharc_transpile_run as nr

        library: str | None = nr.DEFAULT_LIB
        try:
            if not library or not Path(library).is_file():
                raise nr.NativeLibraryUnusable(library or "")
            nr.NativeCore(nr.pack_image(None), library)  # refuses a stale library
        except (nr.NativeLibraryUnusable, OSError):
            library = None
        _NATIVE_LIBRARY.append(library)
    return _NATIVE_LIBRARY[0]


def session_backend() -> str:
    """The backend for run_call and CallSession.fork: PK_SHARC_BACKEND (python, native, check or auto; default
    auto). auto is native when a current native library exists, else python; check runs both and fails on any
    difference. The cores share their semantics (tools/sharc_dev/sharc_core); native is far faster on long calls."""
    import os

    wanted = os.environ.get("PK_SHARC_BACKEND", "auto")
    if wanted not in ("python", "native", "check", "auto"):
        raise PkSharcError(
            f"PK_SHARC_BACKEND={wanted!r}: python, native, check or auto"
        )
    if wanted == "auto":
        return "native" if native_library() is not None else "python"
    return wanted


def _require_native() -> str:
    library = native_library()
    if library is None:
        raise PkSharcError(f"no usable native SHARC library: {NATIVE_HINT}")
    return library


def _check_same(label: str, expected: object, actual: object) -> None:
    if expected == actual:
        return
    if isinstance(expected, dict) and isinstance(actual, dict):
        keys = sorted(
            str(k)
            for k in expected.keys() | actual.keys()
            if expected.get(k) != actual.get(k)
        )
        raise PkSharcError(f"{label}: the native and Python cores differ in {keys}")
    raise PkSharcError(
        f"{label}: the native and Python cores differ ({expected!r} != {actual!r})"
    )


def _native_byte(addr: int) -> int:
    """The DM byte address a normal-word address names (memory._dm_read/_dm_write's mapping)."""
    from tools.sharc_dev.sharc_core.addressing import normal_word_to_byte

    mapped = normal_word_to_byte(addr)
    return addr if mapped is None else mapped


def _native_run(core, data, template, return_sw: int, max_steps: int):
    """Run CORE's call until it reaches RETURN_SW (a breakpoint, as the Python core stops) or MAX_STEPS. An
    instruction the native core traps on runs on the Python core (state through TEMPLATE's configuration) and the
    state moves back. (halt, instructions, fallbacks by trap reason)."""
    from tools.sharc_dev import sharc_run
    from tools.sharc_dev import sharc_transpile_run as nr

    done, fallbacks = 0, {}
    while True:
        if done >= max_steps:
            return sharc_run.Halt("max-steps", core.pc()), done, fallbacks
        done += core.run(max_steps - done)
        if done >= max_steps:
            continue
        if core.pc() == return_sw:
            return sharc_run.Halt("breakpoint", return_sw), done, fallbacks
        reason = (core.halt_reason or "trap").split(" at ")[0]
        fallbacks[reason] = fallbacks.get(reason, 0) + 1
        runner = nr._runner_for(nr.to_python(core, template), data)
        try:
            runner.step()
        except sharc_run.Halt as stop:
            nr.to_native(core, runner.state)
            core.set_option(nr.OPT_RUNTIME_DECODE, True)
            return stop, done, fallbacks
        done += 1
        nr.to_native(core, runner.state)
        core.set_option(nr.OPT_RUNTIME_DECODE, True)


def _native_regs(core, names: tuple[str, ...]) -> dict[str, int | None]:
    from tools.sharc_dev import sharc_trace as st

    return {n: getattr(core.get_reg(st.UREG_CODES[n]), "value", None) for n in names}


def _run_call_native(
    linked: Linked,
    entry: str,
    regs: dict[str, int] | None,
    dm_words: dict[int, int] | None,
    reads: dict[str, tuple[int, int]] | None,
    max_steps: int,
    return_sw: int,
    others: tuple[Linked, ...],
    read_regs: tuple[str, ...],
) -> dict[str, object]:
    """run_call on the native core: a new core's state (registers and an empty DM), the pokes, the call."""
    from tools.sharc_dev import sharc_run
    from tools.sharc_dev import sharc_transpile_run as nr
    from tools.sharc_dev.sharcldr import LoadedMemory

    library = _require_native()
    blocks, data = [], b""
    for i, obj in enumerate((linked, *others)):
        blocks.append(
            {
                "target_address": SW_ALIAS_BASE + 2 * obj.base_sw,
                "byte_count": len(obj.code_boot),
                "fill": False,
                "payload_offset": len(data),
                "payload_len": len(obj.code_boot),
                "index": i,
            }
        )
        data += obj.code_boot
    mem = LoadedMemory(data, blocks)
    key = hashlib.sha256(
        repr([(b["target_address"], b["byte_count"]) for b in blocks]).encode() + data
    ).digest()
    core = _NATIVE_CORES.get(key)
    if core is None:
        core = _NATIVE_CORES[key] = nr.NativeCore(nr.pack_image(mem), library)
    # The Python core's start state for this call; loading it resets the native core, DM included.
    runner = sharc_run.Runner(
        mem, linked.symbols[entry], regs=regs or {}, breakpoints=[return_sw]
    )
    runner.state.call_stack.append(return_sw)
    nr.to_native(core, runner.state)
    core.set_option(nr.OPT_RUNTIME_DECODE, True)
    for addr, word in (dm_words or {}).items():
        if (
            core.poke(_native_byte(addr), (word & 0xFFFFFFFF).to_bytes(4, "little"), 4)
            != 1
        ):
            raise PkSharcError(f"cannot poke DM word {addr:#x}")
    halt, done, _ = _native_run(core, mem, runner.state, return_sw, max_steps)
    out: dict[str, object] = {
        "halt": halt.to_json(),
        "instructions": done,
        "final_pc_sw": core.pc(),
    }
    for name, (addr, count) in (reads or {}).items():
        out[name] = [core.peek(_native_byte(addr + k), 4) for k in range(count)]
    out["regs"] = _native_regs(core, (*REG_NAMES, *read_regs))
    return out


class NativeCallSession:
    """A fresh_registers CallSession whose calls run on the native SHARC+ core.

    The native core (packages/emulator/sharc) is generated from the same semantics as the Python core
    (tools/sharc_dev/sharc_core). DM moves into it once, from the session it forks, and stays there; ``poke`` and
    ``peek`` reach it in normal-word addresses as CallSession's do. Every call starts from a new core's registers.
    An instruction the native core traps on (a form it does not run natively) runs on the Python core, and the
    state moves back. Watchpoint recording and ``fork`` stay on CallSession."""

    def __init__(self, source: CallSession, library: str) -> None:
        from tools.sharc_dev import sharc_transpile_run as nr

        if not source.fresh_registers:
            raise PkSharcError("a native session needs a fresh_registers source")
        self._nr = nr
        self._st = source._st
        self.return_sw = source.return_sw
        self.symbols = dict(source.symbols)
        self._data = source.runner.data
        self._initial = source._initial
        self._template = source.runner.state
        self.core = nr.NativeCore(nr.pack_image(self._data), library)
        nr.to_native(self.core, self._template)
        self.core.set_option(nr.OPT_RUNTIME_DECODE, True)
        self._codes = sorted(set(self._st.UREG_CODES.values()))
        self._special = sum(
            1 << i
            for i, name in enumerate(nr.SPECIAL_SLOTS)
            if name in self._initial.special
        )
        # Instructions the Python core ran for this session, by trap reason.
        self.fallbacks: dict[str, int] = {}

    def poke(self, addr: int, word: int) -> None:
        if (
            self.core.poke(
                _native_byte(addr), (word & 0xFFFFFFFF).to_bytes(4, "little"), 4
            )
            != 1
        ):
            raise PkSharcError(f"cannot poke DM word {addr:#x}")

    def peek(self, addr: int) -> int | None:
        return self.core.peek(_native_byte(addr), 4)

    def call(
        self,
        entry: str,
        regs: dict[str, int],
        max_steps: int = 100000,
        read_regs: tuple[str, ...] = (),
    ) -> dict[str, object]:
        st, core = self._st, self.core
        seeded = {st._seed_code(k): st._seed_value(v) for k, v in regs.items()}
        for code in self._codes:
            core.set_reg(
                code,
                seeded.get(code, self._initial.uregs.get(code, st.Unknown("reset"))),
            )
        core.set_option(self._nr.OPT_SPECIAL_PRESENT, self._special)
        core.fresh_call(self.symbols[entry], self.return_sw)
        halt, done, fallbacks = _native_run(
            core, self._data, self._template, self.return_sw, max_steps
        )
        for reason, count in fallbacks.items():
            self.fallbacks[reason] = self.fallbacks.get(reason, 0) + count
        return {
            "halt": halt.to_json(),
            "instructions": done,
            "final_pc_sw": core.pc(),
            "regs": _native_regs(core, (*REG_NAMES, *read_regs)),
        }


class CheckedSession:
    """Two sessions forked from one, Python and native, driven together: every call's result and every peek must
    agree (PK_SHARC_BACKEND=check). The Python session's values are returned."""

    def __init__(self, python: CallSession, native: NativeCallSession) -> None:
        self.python, self.native = python, native
        self.symbols = python.symbols

    def poke(self, addr: int, word: int) -> None:
        self.python.poke(addr, word)
        self.native.poke(addr, word)

    def peek(self, addr: int) -> int | None:
        value = self.python.peek(addr)
        _check_same(f"peek {addr:#x}", value, self.native.peek(addr))
        return value

    def call(self, entry: str, regs: dict[str, int], **kwargs) -> dict[str, object]:
        result = self.python.call(entry, regs, **kwargs)
        _check_same(f"call {entry}", result, self.native.call(entry, regs, **kwargs))
        return result
