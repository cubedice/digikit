# Emulator-only bank-load refusal experiment

The fixture patches the pinned DTII 1.17 130-Sound bank loader entry. Only a
private synthetic destination on DigiKit's worker is guarded. It forces an
actual slab allocation failure while reserving 130 entries from a private
fixture pool, before calling the original stock loader. The test checks
the failure result, allocator/guard counters, unchanged pool count, absence of
inner Sound loads, and a hash across the complete synthetic destination.

The separate test-only Sound-load callback registers a scheduler tick but does
not read or write any Sound field. This does not establish behavior of a real caller, external failure reporting,
safe reservation under concurrency, or hardware safety. No real project, card,
pattern, or kit is loaded by the synthetic command. The pattern-plus-kit path
mutates the pattern before entering the kit loader, so an inner kit or bank
guard cannot protect that earlier mutation. Never install this on hardware.
