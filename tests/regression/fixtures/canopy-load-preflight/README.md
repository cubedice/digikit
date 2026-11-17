# Disposable emulator experiment

This fixture must never be installed on hardware. Its sole hook guards the
DTII 1.17 card-project wrapper's call to its loader. It reserves room for 2,192
Sound snapshots before that call and consumes those handles while the loader
runs. The injected refusal test fails a slab allocation after one succeeds,
checks that the new slab is freed, and verifies two edited working Sounds,
not the entire project. The separate host test covers allocation failure, rollback,
capacity, nested transactions, and allocation-free publication.

This is not a complete safe load protocol. It does not cover the separate
memory-project loader or other Sound loads, validate every possible stock
project record count, prevent concurrent pool access, guarantee that all
callers handle failure, or reclaim published handles. At completion it can
return a still-unpublished tail if no intervening allocation has moved that
tail; otherwise the unused credits remain consumed. Repeated distinct loads
can still exhaust the pool. The fixture has not been timed or tested for
firmware heap fragmentation and uses only the observed card wrapper.
