"""pytest plugin: run one shard of the collected tests.

    pytest -p tools.devtools.pytest_shard --shard I/N PATHS

keeps the tests whose position in the final collection order is I modulo N (0 <= I < N) and deselects the
rest, so N processes with I = 0 .. N-1 run every test exactly once. Adjacent cases of one parametrized test
land in different shards, which spreads a heavy test's cases. A shard the cut leaves empty passes.
tools/devtools/test_outcomes.py starts the shards of a file that declares `# python-tests shards: N` (tools/devtools/py_select.py).
"""

from __future__ import annotations

import pytest

# Whether this process's shard holds no test.
_EMPTY = pytest.StashKey[bool]()


def parse_shard(spec: str) -> tuple[int, int]:
    """(index, count) of I/N; ValueError unless 0 <= I < N."""
    index_text, sep, count_text = spec.partition("/")
    if not sep:
        raise ValueError(f"--shard {spec!r}: expected I/N")
    index, count = int(index_text), int(count_text)
    if not 0 <= index < count:
        raise ValueError(f"--shard {spec!r}: needs 0 <= I < N")
    return index, count


def pytest_addoption(parser: pytest.Parser) -> None:
    parser.addoption(
        "--shard",
        default=None,
        metavar="I/N",
        help="run only the tests at positions I, I+N, I+2N, ... of the collection (0 <= I < N)",
    )


# Last, so the shard is cut from the order every other plugin and conftest has settled.
@pytest.hookimpl(trylast=True)
def pytest_collection_modifyitems(
    config: pytest.Config, items: list[pytest.Item]
) -> None:
    spec = config.getoption("--shard")
    if spec is None:
        return
    try:
        index, count = parse_shard(spec)
    except ValueError as exc:
        raise pytest.UsageError(str(exc)) from None
    keep = [item for position, item in enumerate(items) if position % count == index]
    drop = [item for position, item in enumerate(items) if position % count != index]
    if drop:
        config.hook.pytest_deselected(items=drop)
        items[:] = keep
    config.stash[_EMPTY] = not keep


def pytest_sessionfinish(session: pytest.Session, exitstatus: int) -> None:
    if (
        session.config.stash.get(_EMPTY, False)
        and exitstatus == pytest.ExitCode.NO_TESTS_COLLECTED
    ):
        session.exitstatus = pytest.ExitCode.OK
