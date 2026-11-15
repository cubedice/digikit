# CANOPY pattern-tempo arp and onset continuity

**[D] DTII OS 1.17.** This replaces the user-facing LOCAL / GRID / PHASE choice with
one pattern-tempo arp. ARP has RATE, ORDR, OCTS, GATE, SWNG and RST; its last
two encoder positions are empty. Speed comes from the normal DTII tempo.

## Clock and stopped playing

**[C] Transport-independent BPM follow-up (2026-11-14).** The earlier
phase-following scheduler described below has been replaced for current
tempo-capable providers. CANOPY now advances once per 32-sample audio callback
using the selected stock tempo, whether transport is stopped or playing. It
retains the most recently validated BPM through absent or rejected position
updates, including external phase expiry. A new valid tempo applies to the
next callback without resetting phase or producing overdue-note bursts.
This deliberately continues at the last received BPM if external clock stops;
it does not claim external phase lock during that interval.

Tempo is cached per instance even with no held keys or MIX=0. There is no
invented default before the first valid selected BPM. The existing TRIG phrase
and PATTERN boundary reset semantics remain; CONT keeps its phase. The status
display follows the accepted BPM scheduler instead of deriving CLK LOST from
transport flags. Legacy phase-only inputs retain their earlier strict behavior.

The instrument adapter now preserves authenticated tempo after a skipped
transmit frame while withholding grid, phase and stopped capabilities. Missing
or rejected wire records still cannot publish new tempo. This changes no
legacy position semantics for Overloop or the wavetable scan clock.

The focused direct/protected SHARC and adapter suite passes 109 cases
(`out/canopy-bpm-clock-pypy.log`): fractional BPM, all transport flag states,
TRIG/CONT phrase behavior, phase/sequence/stamp churn, long provider absence,
tempo updates, exact gate/onset timing, idle tempo acquisition, independent
instances, and legacy phase-only behavior. Six additional V5/V6/V7 guard cases
pass in `out/canopy-bpm-legacy-guard.log`: advancing stale phase cannot schedule
notes without its advertised capability or a previously accepted BPM. Ten
initial CPython checks also passed; they overlap the main suite. Scoped Python
lint and regression TypeScript checking pass. Core instrument-play objects
and the CANOPY package rebuild successfully. No physical DTII installation
is part of this verification.

The firmware acceptance fixture clears factory trigs through the native panel
before measuring a held MIDI chord. Selected-tempo writes alone do not change
the project setting restored by stock STOP. Writing the runtime master-tempo
cache at `0x402a19cc` as well did not prevent that reload. One initial STOP trace
exactly matched three blocks at 180 BPM followed by 1,213 blocks at the original
102 BPM, with no missing musical time; a second trace confirmed the cached
BPM changed to 102. The corrected fixture restores the original configured
tempo before measuring the STOP interval, measures PLAY at 180 BPM, and
separately measures 120/180 BPM in both transport states. Every interval still
requires exact cadence and note order, with cached BPM checked at both ends.

The full DTII 1.17 firmware acceptance passes in 162.46 seconds
(`out/canopy-bpm-device-final.log`, evidence
`out/canopy-pattern-arp/digi-session-uLPqe5/probe.json`). It measures 120/180 BPM
while stopped and playing, PLAY/STOP transitions in TRIG and CONT, native SRC
level edits in both transport states, and note release. All cadence intervals
meet the two-audio-block tolerance with correct held-chord order; the firmware
and protected-call guard report no faults. The acceptance uses a disposable
emulator card and does not install or test the physical device.

The following original design and hardware-gap measurements are historical;
their two-callback hold limit does not apply to the current BPM scheduler.

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

## Fine-clock frame gaps and DTII investigation

On the physical DTII, the PRISM coexistence image `6d96fd4bcbec36e2`
reproduced the reported `CLK LOST`/waiting behavior with CANOPY selected.
A temporary, image-specific diagnostic CANOPY published counters without
changing the scheduler. Over 8.44 seconds it recorded 285 receiver rejects,
277 fine-clock re-observations and 566 lost callbacks. Of 40 UI status samples,
38 were WAIT, one RUNNING and one LOST. The receiver reject and re-observation
counts tracked closely. The most recently accepted sequence delta was one in
all samples. Counters were sampled about every 0.21 seconds and differenced
modulo 1024. The diagnostic and original CANOPY are not the clock-gap fix.
The device's original four modules and saved settings were restored and
verified after the trace.

An otherwise equivalent temporary package with the bounded hold was later
tested on the same selected track. In 8.39 seconds the receiver recorded 362
rejects, the scheduler 14 re-observations and 21 lost callbacks; 37 of 40
status samples were RUNNING, two WAIT and one LOST. Compared with the first
trace, lost-callback rate fell from about 67/s to 2.5/s and re-observations
from about 33/s to 1.7/s. These are two short observations, not a controlled
measurement of every musical onset. Clock loss still occurs and the receiver
continues rejecting frames; this is a substantial improvement, not a complete
explanation or elimination of the hardware fault.

Earlier read-only ColdFire snapshots showed valid wire checksums while the
DSP reported WAIT/LOST. These snapshots and aggregate counters cannot reveal
why an individual DSP frame was rejected: a stale or duplicate sequence and
a transient bad checksum remain possible, among other causes. Sampling is too
slow to determine the lengths of individual runs of missing callbacks. The
hardware trace therefore does not establish that a two-callback tolerance
will cure the user's device, or explain the original ARP speed report.

One later gap-length diagnostic was inadvertently captured with the sequencer
stopped: in 8.406 seconds it counted 466 receiver rejects and 904 lost
callbacks. Its gap-length counter was not armed in stopped mode, so that
capture does not establish how long missing runs were, either stopped or
playing. The original modules and saved settings were restored and verified
after the hardware work.

CANOPY now preserves an **observed or acquired** running fine clock across at
most two consecutive callbacks with completely absent provider flags. It holds
its last phase and does not invent time or schedule notes during the gap; on a
valid return it resumes using the real phase, subject to the existing
discontinuity checks. Observation still waits for a real fresh boundary before
scheduling a first note.

The same bounded hold applies after CANOPY has received a valid stopped
transport and stock tempo. On the next valid stopped frame, CANOPY requires the
frame stamp to advance by exactly the number of missed callbacks plus one; it
then catches up phase using the returned tempo. Tempo changes during the
unobserved interval cannot be reconstructed, so the returned tempo is used for
at most two missing callbacks. A gate change or note onset inside the gap is
applied on the returning frame. Two 32-sample callbacks are about 1.33 ms at
48 kHz.

A third absent callback, an explicitly expired provider, invalid tempo, stale
or jumped stamp, and a missing provider before the first observation fail
closed. In stopped mode CANOPY retains the last accepted frame stamp after a
loss, including loss caused by invalid or expired provider flags; repeated
stale frames cannot restart the arp. A fresh stopped frame can recover it.
These changes improve scheduler resilience; they do not explain or eliminate
the underlying receiver rejections.

The focused SHARC direct and protected tests passed 28 cases, including three
two-callback gaps during initial observation followed by acquisition on a real
boundary, short gaps after acquisition, absence before observation, expiry,
STOP, prolonged loss, discontinuity, and existing recovery behavior. A
temporary package combining the fix with telemetry also passed 16 component
cases. An isolated copy of the original scheduler
failed the same realistic gap comparison: after recovery its phase was
4,800,000 while the uninterrupted reference was 12,000,000. This is a host
regression proof, not a physical validation of the fix. Run the focused suite
with:

```sh
uv run pytest -q tests/patchkit/test_canopy_v2.py -k 'fine_master_short_provider_gap or fine_master_short_gaps_during_initial_observation or fine_master_provider_gap_remains_bounded or fine_master_missing_provider_before_acquisition or fine_master_backward_correction_loss_and_recovery or pattern_arp_stop_preserves_phase_and_start_rejoins_grid or pattern_arp_requires_explicit_stop_and_valid_stock_tempo' --tb=short
```

After adding the stopped-clock hold, the expanded direct and protected SHARC
suite passed 52 tests. After adding stale-frame protection and explicit
gate/onset boundary checks, a narrower suite passed 24 tests. An isolated
pre-fix copy of the scheduler failed all eight targeted stopped-gap and
stale-frame cases, as expected. These are host-level regression results; the
stopped-mode fix has not been measured on the DTII. The final focused suite is:

```sh
uv run pytest -q tests/patchkit/test_canopy_v2.py -k 'pattern_arp_stopped_short_provider_gaps_preserve_tempo_and_sequence or pattern_arp_stopped_provider_gap_fails_closed or pattern_arp_stopped_bad_provider_cannot_restart_stale_frame or pattern_arp_stopped_repeated_frame or pattern_arp_stopped_frame_wrap' --tb=short
```
