"""Bounded JSON driver for the phrase control transport on the concrete SHARC rig.

The colocated Vitest test (modules/core/adapters/phrase-control/tests/transport.test.ts)
supplies states, operations and expectations. This tool only assembles the
production receiver and reply code, executes it and returns observations.
"""

from __future__ import annotations

import json
import sys

from tests.support import fx_rig as fx

CONTROL = fx.ROOT / "modules/core/adapters/phrase-control"
SOURCES = (
    fx.EFFECTS,
    CONTROL / "src/dsp/control.s",
    CONTROL / "tests/transport-fixture.s",
)


def probe(request: dict) -> dict:
    rig = fx.FxRig([], sources=SOURCES)
    built = rig.built
    symbols = {**built.symbols, **built.data}
    session = rig.sess

    def address(value):
        return symbols[value] if isinstance(value, str) else value

    observed = {}
    for operation in request["ops"]:
        op = operation["op"]
        if op in ("poke", "fill"):
            at = address(operation["at"]) + operation.get("offset", 0)
            word = address(operation["word"])
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
        raise ValueError("expected at most 128 named bounded probe requests")
    json.dump({name: probe(request) for name, request in requests.items()}, sys.stdout)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
