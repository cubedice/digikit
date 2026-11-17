# CANOPY compact SRC controls and native MOD

DTII OS 1.17, 2026-11-10. This revision removes keyboard/arp latch, consolidates
the SRC pages, and connects six fixed custom targets per sound type to the
stock MOD engine.

## Controls and persistence [D]

TUNE/LEV retain their native behavior on SOUND A/H only. SOUND D selects WAVE.
ARP G/H select clock and local BPM. Wavetable TONE contains position, scan
depth/rate/shape, envelope amount, attack, release and velocity amount. WT FM
uses A/B/C for depth/ratio/envelope amount. FM TONE uses Aâ€“F for
depth/algorithm/balance/feedback/ratios; FM ENV uses Aâ€“F for
attack/decay/sustain/release/envelope/velocity. Unused cells consume edits without
changing hidden native parameters. Dedicated LEVEL/DATA remains native on every
page. Wheel amounts and scan clock/restart share the MODULATION submenu.

Keyboard ownership now has physical keys and sustain pedal only. Chord learning
still captures pitches independently of their audio lifetime. Reserved control
12 always encodes zero. CPST v1â€“v5 decode accepts the former latch 0/1 field and
normalizes it to zero before validating the active bank; malformed values still
fail. Saving writes canonical CPST v5 without changing the other sound values.

## Native mapping and audio [D]

Native Sound mirrors 26/27/28/31/32/33 are neutral bipolar carriers, initialized
to `0x4000` on admission and retained-pattern refresh. An optional parameter-code
callback supplies six named descriptors for the admitted type and source family.
Ordinary machines use the existing mapping. Targets are:

| Source | Targets in slot order |
| --- | --- |
| Velvet / Reed | Color, drift, arp mix, strum, gate, swing |
| Wavetable | Position, scan depth, scan rate, timbre envelope, WT FM depth, WT FM envelope |
| FM | Depth, balance, feedback, envelope, attack, release |

The adapter reads the **previous completed** stock frame. MOD therefore has a
one-block (32 samples at 48 kHz, approximately 0.67 ms) delay. The active frame
type must match the admitted instrument. A retained same-type pattern switch
keeps the instance and can retain the previous block's modulation at its
boundary. The native frame is never read as an unverified RX-ring pointer.
Reset, quiet/lost admission and foreign frame types clear the priming flag;
the first matching block after each such boundary uses neutral base controls.

Mirror 28 requires a separate stock sampler conversion bypass. The first
compiled candidate exposed why: Sound held the neutral `0x4000`, but the
completed frame held zero after sample bank/slot conversion. The resulting
offset moved arp mix or scan rate even with MOD disabled. The corrected hooks
skip only this split/recombine path on track zero when the live instrument
generation, admitted type and actual SRAM track type agree. Stock tracks replay
their displaced instructions and flags. Sampler-only aliases 29/30 are hidden
for the instrument. Native MOD destination selectors use the high byte of the
Sound halfword (`mirror << 8`); depth is centered on `0x4000`.

Offsets are applied to a private 32-word scratch bank with unchanged CPSB
schema/revision. Bounds, duplicated scalar controls, synchronized scan-rate
limits and GRID's zero swing are preserved. Neutral modulation returns the base
pointer directly. The accepted base/preset bank is immutable. Detail telemetry
echoes the accepted base controls under their revision, while voice identity and
fast performance status remain live; moving LFO samples cannot leave a knob
permanently pending or mix different control values under one detail revision.

## Narrow firmware checks [V]

A second agent independently checked the stock 1.17 bytes and provenance.
At `0x400da3ac`, the parameter-mapper hook replaces the loads from
`0x10(sp)` and `0x14(sp)`. The callback's saved registers and pushed mirror make
its `0x24(sp)` access read the original type argument. The existing successful
return at `0x400da424` restores D2/A2 and returns. The stock DSP anchor at
`0x283858e2` loads I5 with completed-frame address `0x2558dc`. These checks verify
the specific hook ABI and address, not physical audio timing or hardware safety.
The sampler split hook at `0x4002e8e2` replaces six bytes; the recombine hook at
`0x4002e978` replaces ten. Their continuations and displaced register/flag
behavior were independently checked against the same image. The guarded type
check loads the verified instruction's address operand, then reads the current
SRAM type byte rather than treating the operand's ROM address as SRAM.

## Bounded adapter cost [D]

The existing CANOPY callback ceilings remain 100,000 process and 1,600 reset
instructions. The MOD adapter executes outside that protected callback. In the
focused instruction harness, its overlay takes 170 instructions when neutral
and 653 with all six offsets active; detail-status handling takes 15 instructions
without rewriting and 149 when rewriting. These counts include the small
harness entry and are not hardware cycle measurements or an end-to-end A/B.

## Qualification

Focused keyboard, preset and UI checks pass (53 keyboard, 100 preset, 42 SRC and
13 native UI cases). The focused Python suite for the hook, the machines
adapter and the per-block MOD adapter passes: 283 tests and one skip in
322.88 seconds (`out/canopy-controls-mod-pytests.log`), covering all 24
family/lane combinations reaching the real callback, unchanged base values,
reset priming, slot-28 preservation and bounded adapter costs.

The release pair is `out/patch/canopy-controls-mod-release.syx` (image
`b6ac5d85…381f`, preflight OK) and `out/patch/canopy-controls-mod-release.dkm`
(`19fafa65…c67f`), byte-identical to the earlier compiled candidate. The
compiled Stage 5 native page proof passes on it in 475.16 seconds
(`out/canopy-controls-mod-release-compiled.log`). The compiled Stage 6
coexistence proof passes in 466.09 seconds
(`out/canopy-controls-mod-release-stage6.log`): with the admitted wavetable
sound, the stock MOD DEST picker lists Tune, Sample Level and the six family
targets; selecting POSITION and raising DEP moves the effective position
through 568–700 while the base controls and held-note ownership stay
unchanged; selecting SCAN RATE moves the effective rate through 254–578 (the
full-width mirror 28 carrier); selecting None restores the base bank exactly.
The native MOD display still shows the unchanged base values. The remaining
Overloop, transfer, replacement and shared-tail checks pass as before.

Two harness facts were established while proving this **[D]**: the DEST popup
moves one item per UP/DOWN key press, while encoder ticks inside it are
accelerated (32 raw ticks moved zero or one item, 64 moved five); its
"Confirmed!" toast swallows panel input for several seconds; and every [MOD]
press advances the LFO page, also when it arrives from another page. The proof
walks the picker in its list order (None, Tune, Sample Level, then mirrors
26, 27, 28, 31, 32, 33) and counts [MOD] presses to return to LFO 1.

The affected repository gate was run once (`out/canopy-controls-mod-gate.log`).
Its failures are environmental on this WSL/drvfs host (emulator control
sockets, sparse card images, the wasm target, pytest temp files) or
pre-existing format and layer findings in untouched files; the pinned DT2
patched-pluck digest moved to `e4ddef1a…d38a` because the machines adapter's
parameter-code hook gained the optional callback (section 3 only), and the
shared generated reference was regenerated. This is not a hardware-qualified
release; physical installation is recorded in the handover.
