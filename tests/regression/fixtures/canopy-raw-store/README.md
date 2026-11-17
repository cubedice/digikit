# Unlinked raw-record experiment

The raw path stores all nonzero extension bytes without calling the CANOPY
module. Recognized, corrupt, unknown-version, and unrelated nonzero bytes all
round-trip unchanged; classification is advisory and does not establish a
Sound's owner or validate control values. The module can decode only after
admission. A zero extension maps to no handle.

The caller must reserve the entire load's capacity and serialize the pool,
Sound handles, edits, and copies before entering stock loader code. The
experiment has no hooks or checked stock callers and cannot enforce that
contract. Invalid handles and reservation failures fail without touching the
given output, but stock firmware may ignore a failure. It also has no safe
reclamation or edit, copy, startup, and real-device protocol. Do not install.

`compact-pool.c` is a separate unlinked storage candidate. Its immutable entry
is 43 bytes including a publication flag; a slab has 95 entries, and no hash
table or per-entry link is needed. For a 32-bit target its 690 slab pointers,
count and three allocator/context pointers require 2,776 bytes; a fresh
2,192-entry reservation needs 24 slabs (at most 98,304 buddy-block bytes).
It deliberately does not deduplicate, reclaim published entries, solve
concurrent access, or connect to the raw serializer above. Its allocation and
alignment have not been measured on the device.
