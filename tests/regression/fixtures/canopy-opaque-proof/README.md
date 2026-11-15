# Emulator-only opaque Sound proof

This fixture patches pinned DTII 1.17 Sound load and save sites. Once armed,
the next successful stock load supplies a valid Sound record as a template.
Inside that callback it performs four synthetic stock Sound deserialize/save
calls with current-signature, unknown-version, corrupt and foreign 42-byte
records. CANOPY is not installed. The hooks apply the unlinked opaque store
only to the private synthetic Sound, storing its handle in the candidate
`Sound + 0x13a` field, and check both its extension bytes and
every other serialized byte against the stock writer's output.

This is not a project/card roundtrip, cold-boot proof, production integration,
concurrency or Sound-lifetime test, or hardware-safe implementation. The recursive stock calls
and allocation run inside the original deserializer callback. The fixture
does not protect real Sounds from allocation failure and must never run on a
device. The emulator test uses a disposable card and is explicitly opt-in.
