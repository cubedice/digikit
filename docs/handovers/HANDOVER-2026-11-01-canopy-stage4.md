# CANOPY Stage 4 movement policy checkpoint

Requested: complete Stage 4 after Stage 3 commit `ba23c3f0`.
**Stage 4 is incomplete.** This checkpoint adds the bounded C movement policy
and its component scenarios. It does not change the installed device, Stage 3
firmware, module manifest or packaged DSP. No new agents were delegated.

## Implemented policy

`modules/machines/canopy/src/movement.h` and `movement.c` expose a serialized,
allocation-free block scheduler. This code currently has no runtime caller.
One tick is 32 samples at 48 kHz. It consumes four admitted notes, not the
physical keyboard ledger. Each note retains velocity, port and channel.
Revision and explicit new-phrase identities are independent of the clock's
transport generation and committed pattern identity.

- UP, DOWN, endpoint-deduplicated UP/DOWN, AS PLAYED and seeded xorshift RANDOM.
- One to three octaves; unplayable octave extensions are omitted, never wrapped.
- Independent monotonic arp token domain, bounded gates and exhaustion handling.
- Generated-chord strum spreads onsets over at most 750 blocks / 500 ms. Every
  chord revision cancels pending old strums; already sounding common notes keep
  identity. PLAY onsets remain immediate.
- TRIG responds to new phrase, PATTERN to committed pattern identity, CONTINUE
  preserves ordinal/phase across membership changes. Transport generation
  always resets the clock. PANIC releases both lanes and cancels pending work.
- LOCAL has 30..300 BPM, 1/2/3/4/6/8 notes per quarter and 0..75 percent swing.
  Rational phase retains the remainder across triplets; alternating thresholds
  preserve pair duration. Live swing changes apply at a pair boundary. Tempo
  changes preserve phase. Clock/rate changes explicitly restart timing.
- MASTER accepts only committed boundaries and 1..16-step multiples, with no
  swing or inferred subdivisions. Missing/invalid frames pause scheduling;
  no local fallback occurs. Recovery waits for a new boundary, not the last
  cached step. STOP cancels pending strums and synchronized arp, retaining pads.
- Invalid configuration or input is rejected before mutation. The caller must
  fault/recover on skipped audio-block calls rather than trying to catch up.

The output bound is ten changes, with all releases before all onsets. A full
four-pad replacement plus arp transition exercises that bound. The PAD and ARP
token domains are explicitly separate; never feed arp tokens into the MIDI
physical ledger. The consumer must reserve the complete output batch before
advancing policy state.

## Evidence

`tests/regression/host/canopy-movement.test.ts` drives the actual C policy through
`canopy-movement.c`. Thirty cases cover ordering, octave clipping, route/velocity,
revisions, new phrases, pattern/transport reset, STOP/PANIC, live mix-disable,
strum, source switches, stale clock, invalid updates, counter wrap and exhaustion.
Triplet and swing comparisons run for 200000 blocks (133.33 seconds) against
absolute rational timing. Gate events cannot release the pad lane. Full-batch
canaries detect output overflow. Compilation enables undefined-behavior checks.

The policy also compiles with `m68k-elf-gcc -mcpu=54418 -O2 -ffreestanding
-fno-builtin -Wall -Wextra -Werror`, with no unresolved symbols. This is a CF
compilation check, not a CF instruction or DSP timing measurement.

Logs: `out/canopy-stage4-movement.log`, `out/canopy-stage4-types.log`.
The Stage 3 handover remains the source of current playable firmware/artifacts.

Required changed-file preflight passes all 30 selected host cases, lint,
formatting, types and layer checks. It exits with the existing global pathcheck
baseline: 256 stale references in 78 files. Scoped checks of all three changed
documents report zero stale references (`out/canopy-stage4-docs.log`). Full
preflight log: `out/canopy-stage4-changed.log`. No merge gate was run.

## Required next work

1. Integrate scheduling with the audio callback, preferably inside the DSP
   instrument so the ten internal changes do not overflow the eight-event
   external MIDI contract. The C policy supplies executable behavior for that
   implementation; its host tests alone are not Stage 4 acceptance. No UI-task
   clock or ordinary ACK-delayed MIDI queue can stand in for audio timing.
2. Carry the mapper's committed membership, generated/PLAY mode and explicit
   phrase identity coherently. Preserve immutable packet retries and common
   sounding voices. Do not infer arp membership from audio release tails.
3. Add the seventh (arp) group with shared release-tail allocation, smooth
   PAD/ARP audio blending, reset/fault handling and bounded per-block work.
   Merely adding a second independent full voice bank violates the pool design.
4. Expose movement controls through applied configuration revisions and the
   setup menu. Named SRC pages remain Stage 5.
5. Implement/measure the synchronized tempo/phase extension on internal and
   external clock. Pattern-position ABI 2 has no verified tempo, fine divisions,
   per-track swing or sample timestamp. The C MASTER first gate is provisionally
   one block until an observed interval exists, and its eight-second stale bound
   is unqualified. Fix these before offering musical synchronized playback.
6. Execute protected DSP ownership/guard/headroom/instruction proofs and actual
   running-firmware arp/strum/transport tests, including clock loss, recovery,
   tempo changes and existing Stage 1–3 keyboard/sound regressions. Do not mark
   Stage 4 complete until these pass. Hardware/coexistence remains Stage 6.
