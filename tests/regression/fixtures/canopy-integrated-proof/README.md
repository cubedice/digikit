# Emulator-only shared-pool transaction experiment

This opt-in fixture shares `test/canopy-sound-proof`'s pool between stock
Sound deserialization and real SRC/selector transaction callbacks. A worker
reserves credits in task context. Under mapper exclusion, callbacks validate
the admitted Sound and exact generation, publish the prepared snapshot into a
reserved slot, attach its handle at `Sound + 0x13a`, and mark the edit as seen
by the proof's restore tick. The credit fixture requires a nonzero generation
token; it receives a checked `generation + 1`, while all admission checks use
the unmodified generation.

The tick decodes outside exclusion and checks the handle again under exclusion
before applying it; this prevents a stale decode from overwriting a later edit.
The test checks a deliberately stale handle as well as self-restoration, an
exhausted credit, the public inputs, and a native project save/load cycle. It
also exercises reload of the saved card in a fresh emulator process.

This is not a production persistence implementation. Real load, save, copy,
edit, worker allocation and reclamation are not fully serialized; callback
timing, handle lifetime, and physical power-on remain unproven. The synthetic MIDI
events are not a hardware/audio test. Never install the fixture on a device.
