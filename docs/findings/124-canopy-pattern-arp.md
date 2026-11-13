# CANOPY pattern-tempo arp and onset continuity

**[D] DTII OS 1.17.** This replaces the user-facing LOCAL / GRID / PHASE choice with
one pattern-tempo arp. ARP has RATE, ORDR, OCTS, GATE, SWNG and RST; its last
two encoder positions are empty. Speed comes from the normal DTII tempo.

## Clock and stopped playing

The existing fine clock uses 21,600,000 ticks per quarter note. The stock
selected-tempo word at `0x402b67fc` is in units of 1/120 BPM; twice this value
is the musical advance per 32-sample audio block at 48 kHz. Earlier static
and emulator evidence is recorded in
[finding 78](78-canopy-carrier.md#fine-clock-integration-and-pulse-freshness).

The timing provider now transports that tempo in the same checked frame as
the pattern clock. The public position record remains ABI 2 for existing
clients, including Overloop. Instruments receive an explicit tempo capability
alongside the existing running, stopped and phase capabilities. Neither the
tempo capability nor STOP changes the valid/running bits for other clients.

While running, CANOPY uses the committed pattern phase. While explicitly
stopped, it advances its arp by twice the selected tempo per audio block,
retaining fractional BPM, triplet remainder, swing and gate phase. A first
held chord starts immediately without pressing PLAY. STOP retains an active
arp's phase; PLAY observes a fresh committed boundary before resynchronizing.
Loss of a running external clock does not masquerade as STOP or enable a
fallback clock. Invalid tempo, missing frames and absent timing capabilities
suppress new arp onsets while leaving held pad ownership intact.

Old presets keep their binary layout. Runtime imports ignore their previous
local BPM and clock choice, and this normalization alone does not mark the
preset as edited. Old packed CLOCK/BPM locks remain decodable but no longer
override timing. Other packed control IDs and native modulation are unchanged.

## Zero-attack onset

An arp retrigger inherits its held pad's oscillator phase to prevent
pitch-dependent cancellation at intermediate PAD/ARP mixes. That phase can
be near a waveform peak. A native AMP attack that rises in one sample then
produces an abrupt edge.

The arp now has a minimum 64-sample onset ramp (about 1.33 ms at 48 kHz).
Slower selected attacks retain their timing. The change applies to additive,
wavetable, FM and wavetable/FM arp voices, including the remaining attack of
an arp voice copied into a release tail. Pad attacks and selected release
coefficients are unchanged; there is no overall level reduction.

## Validation

Focused tests cover stopped fractional-tempo playback, swing and gate timing,
tempo changes, release of held notes, STOP/PLAY, missing frames and invalid
clock inputs. Native AMP tests cover AHD/ADSR onset audio across the four sound
families, both native restart settings, slower attacks and release continuity.
The focused checks passed:

- 63 CF/DSP provider tests (`out/canopy-arp-provider-full.log`), including
  tempo while stopped, malformed frames and legacy position consumers.
- 24 stopped-clock recovery checks (`out/canopy-arp-recovery.log`), plus the
  earlier fractional-tempo/rate cases in `out/canopy-arp-stopped.log`.
- 20 V5/V6/V7 clock-capability and pattern-restart checks
  (`out/canopy-arp-clock-review.log`). Pattern restart now masks capability
  and tempo metadata before checking the running/valid bits. Arp and
  wavetable scan both require phase capability on all V5+ input versions.
- 212 native-envelope/post-AMP checks
  (`out/canopy-arp-envelope-regressions.log`) and 28 onset waveform checks
  (`out/canopy-arp-attack.log`). The latter include additive, wavetable, FM
  and wavetable/FM, AHD/ADSR, restart and non-restart. The protected waveform
  matches an explicit 1/64 attack; sampled peak amplitude is below 0.081 in
  these fixtures. This fixes onset discontinuities, not arbitrary downstream
  gain clipping.
- 163 control/preset/display host cases across the initial focused host run
  and the corrected 16-case native-display rerun. Blank G/H pixels are checked
  in normal and show-values modes. TypeScript checking passes.
- 186 instrument transport/ownership/control tests: 185 passed in
  `out/canopy-arp-instrument-play.log`; the one skipped CF instruction-ceiling
  test passed with the explicit runner path in
  `out/canopy-arp-keyboard-ceiling.log`.

The affected merge gate was attempted with `DIGI_GATE_BASE=HEAD` and the
configured WSL temporary directory (`out/canopy-arp-gate-final.log`). It is not
green: existing formatting failures in `test_layercheck.py`,
`test_ready_inputs.py`, and `test_instrument_mailbox_lifecycle.py`, 43 existing
module-boundary violations, and five Nx selection subprocess timeouts stop it.
The regression host suite and type checking pass in that gate. The initial
gate attempt used `/tmp`, whose insufficient free space caused additional host
fixture failures; those did not recur with the configured temporary directory.

Core objects must be rebuilt before packaging this firmware: `patchkit patch`
reads committed core objects, while content modules rebuild automatically.
An initial emulator run correctly rejected the old G/H controls when stale
core objects were used. After rebuilding `core/instrument-play` and
`core/pattern-position`, source status reports current for both and CANOPY;
independent object-byte inspection confirms the ARP mapping ends in `ff ff`
and the retired labels are absent.

The matching firmware `out/patch/canopy-arp-final.syx` passes preflight
(`out/canopy-arp-preflight.log`), SHA-256
`266fc6e8270bf4d9de715018c9df6467b24d34e02cb4f9000605976d666f3c2e`.
Its CANOPY pack `out/patch/canopy-arp-final.dkm` is 241,188 bytes, SHA-256
`0f2714aa8fdbad5475e04d0b5e9b0ddbf273411da63744067acea5609b563994`.

The compiled DTII acceptance passed (`out/canopy-arp-device-final.log`,
`out/canopy-pattern-arp/probe.json`). It boots that exact image and module with
a disposable card, enables keyboard input through the real menu, selects the
machine through stock panel events, and plays held USB notes. G/H leave both
the configuration bank and revision unchanged. At 1/16, six stopped arp
intervals measure exactly 1,125 audio blocks at 120 BPM and 750 at 180 BPM
after removing the in-progress phase remainder. Changing tempo does not edit
the Sound. Held notes generate further onsets through PLAY and STOP; releasing
all keys stops new onsets. Runtime guard, CF fault and DSP fault checks pass.
The stock tempo and MIDI receive settings are test inputs; provider frames and
instrument DSP state are observed, never fabricated by this test.

Run the owner with `pnpm nx:pilot run canopy-arp-tests:test-device`; optional
`CANOPY_ARP_IMAGE`/`CANOPY_ARP_PACKAGE` select a matching already-built pair.
Future runs place evidence under `out/canopy-pattern-arp/<session-name>/` to
keep concurrent runs separate. Physical DTII installation and measurement are
separate; neither was performed for this change.
