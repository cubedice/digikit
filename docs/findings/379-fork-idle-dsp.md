# 379. Lower DSP cost in the five Digitakt II mods

Changes based on `b0bf3bc4a`, for Digitakt II OS 1.17. Keep the audio, control and recording behavior while removing work on silent blocks and simple signal paths.

## Cost on the SHARC rig [D]

Counts are instructions per 32-frame block. Each comparison runs the same inputs before and after. The rows cover different hooks, so their counts should be read individually.

| Mod | Path | Before | After |
| --- | --- | ---: | ---: |
| PRISM | Resting | 1,334 | 1,271 |
| PRISM | Settled LOOP with Overloop | 9,023 | 8,188 |
| PRISM | Heaviest tested block | 21,038 | 21,038 |
| Overloop | Stopped and silent | 1,431 | 1,335 |
| Overloop | Live rolling record | 1,486 | 1,390 |
| Overloop | Held loop with transport stopped | 1,610 | 1,514 |
| Overloop | Heaviest tested block | 5,511 | 5,483 |
| CANOPY | Settled Velvet idle, native AMP and protected callback | 13,467 | 13,051 |
| CANOPY | One FM note, native AMP and protected callback | 22,202 | 22,078 |
| CANOPY | One FM pad voice plus arp, native AMP and protected callback | 30,984 | 30,795 |
| RELAY | Before the first command | 120 | 6 |
| RELAY | Initialized with an empty tape | 1,203 | 998 |
| RELAY | Parked playback with dry input | 3,033 | 1,643 |
| RELAY | First capture | 8,122 | 7,874 |
| RELAY | Unity playback | 5,231 | 4,992 |
| Track Keyboard | Quiet scan, highest-cost block of the reporting round | 646 | 392 |

Overloop and PRISM measure the effect hook, including its adapter. CANOPY measures its protected callback. Its process test ceiling remains 100,000 instructions per block. The largest reported FM source-handoff case used 77,920, and the FM reset used 1,131 against its 1,600-instruction ceiling. RELAY measures its capture wrapper, and Track Keyboard measures the quiet-bus and routing hook.

## Changes and checks

- **PRISM** uses a shorter mix loop after the input crossfade reaches LOOP. The send times zero, tap trim, and sum preserve the sample results. Pipelined peak checks shorten the idle scan and the active tape write. Five active output, tape and wet pins matched. The 19 rest and transition tests include wake, handover and mailbox routing.
- **Overloop** copies dry audio to the output and rolling record in one pass. It computes the tap's reciprocal gain only on blocks that mix a loop. The wet mailbox still rejects a nonfinite or oversized sample through the last word of the right channel. Audio, tap and status hashes matched in the three dry cases above. The 50 rig tests cover capture, transport, crossfades, waveform and buffer behavior.
- **CANOPY** skips the settled PAD/ARP blend once both renderers produce silence. Moving MIX still advances sample by sample. A hardware loop snapshots pad phases, and FM reuses its second operator's increment across the oversampled pair. Nine paired cases on both direct and protected paths matched PCM, instance state, post-AMP gain and status. All 171 targeted tests passed. The DTII package uses 104,936 of 106,496 staging bytes.
- **RELAY** skips the capture wrapper's full context save before its first command. Initialized empty tracks take a shorter status path. Stationary playback without writes or fades uses a planar dry pass, and the moving tape path loads block-constant gains once. Paired PCM, row and status words matched. All 13 rig tests passed, including the first capture and stationary dry gain after a fade. The DTII emulator also passed the recording and waveform page tests and stock MOD routing.
- **Track Keyboard** pipelines the next stereo-bus load and takes one maximum across the track. It compares that maximum to the same threshold after the last word. The reply still visits two tracks a block and rejects audible tails, active and pending cursors, infinity and NaN. All six fold and quiet-report tests passed.

The combined release set passed DTII firmware preflight. PRISM, Overloop, CANOPY and RELAY packed successfully, and the Track Keyboard and RELAY adapter objects passed the freshness check. Hardware timing is open [O].

## Coupled idle comparison [D]

A paired `coupled_ab` run used both firmware images' DSP changes over the same stock-ready snapshot. It exercised the track-bus hook, including Track Keyboard's quiet scan and RELAY's capture entry before the first command. The one-second PCM hashes matched. Across the whole DSP, interpreted instructions fell from 17,833,731 to 17,684,602 and fast-tier instructions fell from 28,590,796 to 28,187,589. The combined reduction was 552,336 guest instructions. AOT instruction counts matched. This pair establishes the idle instruction difference on the emulator. Hardware cycle measurements and active module timing are open [O].
