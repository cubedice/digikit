# 353. The block interrupt on small task stacks: a stack overflow as the cause of a day-long freeze

A Digitakt II with the fork's image of 2026-11-14 (CANOPY BPM, Overloop, PRISM, remote; runtime 0.5.4) froze after about a
day powered on. The device keeps one fault record: `CPU CRASH | IN FIRMWARE | PC 40000458`. This finding names the place of the
crash, the likely cause and the change that removes it. Everything is from DT2 1.17; marks as in `docs/FINDINGS.md`. Nothing here
is verified on hardware.

## The crash record

- **[D]** The device had no fault record at the install of 2026-11-14 (the settings record read before and after the install holds
  no `dk/fault` key; `crash_found` false). It had one on 2026-11-17. The crash happened on that image.
- **[D]** `0x40000458` is the `rte` that ends the firmware's task switch (`0x40000410`..`0x40000458`). The routine saves
  `d0-d7/a0-a7` into the running task's record at `+0x0c` (`0x47db3f64` holds the record), takes the next record from the ready ring
  (`0x40964914`), restores its registers and returns into it. `0x4000129c` stores the routine in vectors 205 (`0x40000334`) and 32
  (`0x40000080`): the time-slice interrupt and `trap #0` (finding 132).
- A crash on that `rte` means the record or the stack of the task being resumed was damaged: the restored `a7` or the frame at it
  is not valid. The same signature was seen in the emulator after a stack was eaten by a recursive helper (finding 79).
- **[D]** The record of a task lies directly below its stack: `task_create(record, entry, priority, stack, size)` is called with
  `stack = record + 0x54` for the two smallest tasks (below). A stack overflow therefore writes into the task's own record first
  (`+0x50` down to the ring link at `+0x00`) and then into what lies below it.

## The tasks and their stacks

**[D]** `uv run python -m tools.coldfire.task_stacks --image dt2-1.17` (from the Ghidra dump of this image):

| task | record | stack | size |
| --- | --- | --- | --- |
| deferred calls, priority 9 (`FUN_40000ea0`: takes a call from the queue `0x409648f0` and makes it) | `0x4096369c` | `0x409636f0`..`0x409646f0` | 4 KiB |
| timer task, priority 10 (entry `0x40002a46`; the PIT2 handler `FUN_40002a18` on vector 207 posts its semaphore) | `0x40964dbc` | `0x40964e10`..`0x40965610` | 2 KiB |
| UI task, priority 6 | `0x40966ee8` | `0x4096775c`..`0x4098f75c` | 160 KiB |
| ten others | | | 16 or 32 KiB |

The runtime adds its own I/O task (16 KiB) and helper task (4 KiB) in `cf.expansion`; they were not measured here **[O]**.

## What runs in the block interrupt

**[D]** The block interrupt handler `FUN_4002dd0c` (1500 per second) runs on the stack of the task it interrupts. The fork's hooks
in it: `cf.link.hook.block_isr` (core/health), `cf.link.hook.block_isr.frame_call` (`dl_tick` of core/dsp-link, which calls
`pp_write_frame`, `instrument_write_frame`, `instrument_write_lock_frame` and `phrase_write_frame`), `cf.pattern.hook.step_due`,
`cf.pattern.hook.start_due`, the two `instrument_lock_due` hooks, `instrument_mod_split`, `instrument_mod_recombine` and
`cf.machine.hook.clone.6`.

## Measurement

`DIGI_STACK_WATCH` on the thirteen firmware task stacks during `modules/fx/prism/tests/coexistence.device.test.ts` (CANOPY,
Overloop, PRISM and remote on the `canopy-overloop-prism` set, about two minutes of emulated use; the test passes in every run):

| | before the change | after |
| --- | --- | --- |
| timer task (2048 bytes): deepest store | 1860 bytes, 188 free | 888 bytes, 1160 free |
| intro task (16 KiB) | 2076 | 1040 |
| deferred calls (4 KiB) | 48 | 48 |
| UI task | 28512 | 28512 |

**[D]** The path to the deepest store on the timer task before the change, all in one block interrupt: `health` block hook (284) →
`dl_tick` (360) → `pp_write_frame_impl` (424) → `instrument_write_frame` (520) → `mt_reply_valid` (584) → `mt_prepare_mapped`
(1828) → `mi_peek` (1860). `mt_prepare_mapped` (core/midi-events, `src/cf/transport.c`) held `MiEvent first, input[8],
output[8]` as locals: 17 events of 72 bytes, 1224 bytes. The timer task was about 50 bytes deep when the interrupt came (its own
deepest store without an interrupt was 128), so the interrupt path took about 1800 bytes and an overflow needs the timer task to
be about 240 bytes deep at that moment. The image of 2026-11-14 has the same locals (`lane/prism`, `transport.c:64`).

## Conclusion and marks

- **[D]** The block interrupt of the fork's images can overflow the 2 KiB stack of the firmware's timer task. An overflow damages
  that task's record and then the scheduler's data below it; the next task switch crashes on its `rte`, which is the recorded PC.
- **[O]** No overflow was reproduced. The timer task's own worst depth is not known (its callbacks are reached through pointers),
  so the rate cannot be given; a coincidence of a deep timer callback and a block interrupt about once a day is consistent with
  1500 interrupts per second. The measurement is from the port of 2026-11-17, not from the image of 2026-11-14.
- **[O]** Whether the freeze the owner saw is this crash: the record has no time, and after a CPU crash the runtime resets the
  device instead of halting (finding 46). A damaged scheduler can also stop tasks without an exception.

## The change

`mt_prepare_mapped` keeps `locks`, `source`, `first`, `input` and `output` in static storage. Its one caller outside tests is
`instrument_write_frame`, called only by `dl_tick` in the block interrupt, which does not nest. Checks: `cargo test -p dkmidi-host`
(27 cases), the coexistence test, and the table above.

## Open

- **[O]** The remaining interrupt path is 888 bytes on the timer task (`phrase_write_frame` and a 64-bit division, `__udivdi3`).
  A stack of the fork's own for its block-interrupt work would make the use on the interrupted task constant; not built.
- **[O]** No test guards the number. The harness already writes the watch summary of every effect session; a limit on the
  interrupt-time use of the timer task's stack could be checked there.
- **[O]** The memcpy, memset and bzero hooks of CANOPY Sound persistence (finding 351) run C code in every task and interrupt for
  lengths from `0x450`; their stack use on the small tasks was not measured.
- **[O]** A soak of hours on hardware after the install.
