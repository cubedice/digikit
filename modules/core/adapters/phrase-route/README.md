# Selective phrase routing

Experimental DTII OS1.17 adapter for the exclusive phrase-effect capability.
The runtime assigns its owner an internal FX route of 2. Normal master and
send callbacks skip that row; this adapter invokes it once per block before
the native sends driver.

The original 16 stereo track buses and two raw external-input channels feed
the selected capture mix. Capture observes native main-exclusion bits for
internal tracks, and is independent of external direct-monitor level. Gain
and selection come from the coherent `core/phrase-control` frame. One selected
external channel is captured as centered mono; stereo keeps L/R separate.

The callback writes wet-only output through the existing guarded FX contract.
Its heartbeat and owner generation must advance/match, and a held recording
must exist before source attenuation is allowed. Invalid control, unload,
disabled owner, stale status, empty recording or bad wet output fails open to
the live sources. The adapter publishes actual FADE, independently of engine
health. Intentional RATE OFF remains a valid held phrase and may decay to
silence at full FADE.

A callback fault, stale heartbeat or invalid status latches LIVE. A later
successful callback cannot silently attenuate the sources again: a new command,
new owner, or FADE moved to zero and then re-engaged clears that latch.
The router publishes the current generation's latch in `PH_S_ROUTE_FAULT`,
so the UI can reset its visible FADE without treating reply latency as a fault.

Native sends and the native master are redirected to private copies of the
tracks and raw input. Equal-power FADE and the held/old source-mask handoff
scale those copies, with 32-sample gain ramps. Both source feeds to native
effects and direct monitoring therefore follow together. Existing native FX
tails and unselected sources are untouched. Native track-to-compressor routing
still runs normally; the wet return follows native FX-return routing.

Original track/input buffers, user levels, mute state and native send settings
are never written. Per-track USB and unrelated custom sends continue reading
the originals. The master hook adds the already-scaled phrase wet block after
native effects finish and before their return is mixed.

Firmware sites are hash-relative declarations in `devices/fw/phrase-route.toml`.
Concrete SHARC tests in `tests/route.test.ts` exercise the real adapter, real FX
guard, and the original native monitor/send/master and USB packing instructions
through a bounded JSON analysis driver. The USB cases seed the original native
source table and compare both packed output regions at LIVE/full FADE.
Full coupled-engine proof of external input, native send tails, USB delivery
to the host and hardware timing is required
before qualifying this experimental routing on a physical device.
