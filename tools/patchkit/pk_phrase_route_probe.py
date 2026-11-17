"""Bounded JSON driver for the phrase router's concrete SHARC analysis rig.

The colocated Vitest test supplies states, operations and expectations. This
tool only assembles production code, executes it and returns observations.
Native calls use the hash-checked local original DTII1.17 DSP image. Firmware
bytes never leave the local process or enter a test fixture.
"""

from __future__ import annotations

import hashlib
import json
import sys
from functools import lru_cache

from tests.support import fx_rig as fx
from tools.patchkit import pk_sharc
from tools.sharc_dev.sharcldr import LoadedMemory

ROOT = fx.ROOT
MODULE = ROOT / "modules/core/adapters/phrase-route"
SOURCES = (fx.EFFECTS, MODULE / "src/dsp/route.s", MODULE / "tests/route-fixture.s")
CONTROL = ROOT / "modules/core/adapters/phrase-control"
TRANSPORT_SOURCES = (
    fx.EFFECTS,
    CONTROL / "src/dsp/control.s",
    CONTROL / "tests/transport-fixture.s",
)


@lru_cache(maxsize=1)
def original_dsp():
    data = (ROOT / "out/sections/dt2-1.17/section_7_BLOB.bin").read_bytes()
    if hashlib.sha256(data).hexdigest() != (
        "0f514a12a2255f5c081e292c47f1f29462003177658da4bbae0a22fd737fffa2"
    ):
        raise ValueError("native phrase probes require original DTII1.17 DSP")
    return LoadedMemory.from_stream(data)


def probe(request: dict) -> dict:
    native = request.get("native", False)
    transport = request.get("transport", False)
    sources = TRANSPORT_SOURCES if transport else SOURCES
    rig = (
        None
        if native
        else fx.FxRig(
            [] if transport else [fx.Row("_test_phrase", "_te_state_a", route=2)],
            sources=sources,
        )
    )
    built = fx.build(sources) if native else rig.built
    symbols = {
        **built.symbols,
        **built.data,
        "_fx_table": fx.TABLE,
        "_fxs_trackbus": fx.TRACKBUS,
        "_fxs_return": fx.RETURN,
    }

    def address(value):
        return symbols[value] if isinstance(value, str) else value

    if native:
        externs = {**symbols, **request.get("externs", {})}
        obj = fx._assemble(MODULE / "src/dsp/route.s")
        base = built.symbols["_phr_pre"] - obj.symbols["_phr_pre"][0]
        production = pk_sharc.link(obj, base, externs)
        hooks = [
            pk_sharc.build(
                ".SECTION/PM seg_pmco;\n.GLOBAL _site;\n_site: JUMP " + target + ";\n",
                at,
                {target: symbols[target]},
            )[1]
            for at, target in request.get("hooks", [])
        ]
        session = pk_sharc.CallSession(
            [*built.linked, production, *hooks],
            fx.dv.RETURN_SW,
            breaks=tuple(request["breaks"]),
            base_memory=original_dsp(),
        )
        session.runner.state.assume_nw32 = True
    else:
        session = rig.sess

    observed = {}
    for operation in request["ops"]:
        op = operation["op"]
        if op in ("poke", "fill"):
            at = address(operation["at"]) + operation.get("offset", 0)
            value = operation.get("float")
            word = fx.f2w(value) if value is not None else address(operation["word"])
            for index in range(operation.get("count", 1)):
                session.poke(fx.nw(at) + index, word)
        elif op == "read":
            at = address(operation["at"]) + operation.get("offset", 0)
            observed[operation["tag"]] = [
                session.peek(fx.nw(at) + index) or 0
                for index in range(operation.get("count", 1))
            ]
        elif op == "call":
            regs = {
                key: address(value) for key, value in operation.get("regs", {}).items()
            }
            if native:
                session.symbols["_probe_entry"] = address(operation["entry"])
                result = session.call(
                    "_probe_entry",
                    {
                        **fx.MARKS,
                        **fx.dv.FRIENDLY,
                        "I6": fx.dv.STACK,
                        "I7": fx.dv.STACK,
                        **regs,
                    },
                    max_steps=operation.get("max_steps", 30_000),
                )
                observed[operation["tag"]] = {
                    "halt": result["halt"],
                    "pc": result["final_pc_sw"],
                    "regs": result["regs"],
                }
            else:
                block = rig.block(
                    [0.0] * 64,
                    entry=operation["entry"],
                    regs=regs,
                    ctx=operation.get("ctx"),
                )
                observed[operation["tag"]] = {
                    "returned": block.returned,
                    "instructions": block.instructions,
                    "ctx_bad": block.ctx_bad,
                    "regs_bad": block.regs_bad,
                    "regs": block.raw["regs"],
                    "halt": block.halt,
                }
        else:
            raise ValueError(f"unknown probe operation {op!r}")
    return {"observed": observed, "symbols": symbols}


def main() -> None:
    requests = json.load(sys.stdin)
    if not isinstance(requests, dict) or len(requests) > 128:
        raise ValueError("expected at most128 named bounded probe requests")
    json.dump({name: probe(request) for name, request in requests.items()}, sys.stdout)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
