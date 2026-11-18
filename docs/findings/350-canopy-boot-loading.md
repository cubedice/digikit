# CANOPY boot loading and transfer size

CANOPY's installed DT2 package contains a 226,260-byte DSP image and 5,800
bytes of BSS. At boot the V2 instrument loader transfers the image and clears
the BSS before publishing the instrument. Each transfer has its own identity,
acknowledgement and drain; the UI task advances the transfer on successive
passes. [D]

The original 256-byte cap therefore requires 884 image writes and 23 BSS
writes (including alignment), or 907 separate transfers, before metadata and
initialization. Enlarging the bounded source buffer and image chunks to 4,096
bytes requires 56 image writes and two BSS writes, or 58 transfers for the same
package. The underlying link still splits these into its normal frame-sized
chunks and checks and drains each transfer. Instance state is not included in
the zero fill. These are counts derived from the installed package and loader,
not measured hardware boot times. [D]

The coordinator host test transfers a 207,368-byte image and 2,240-byte BSS,
checks every byte and zero, and observes 52 image/zero writes and 18 mailbox
writes. Timeout, uncertain link, lost boot, late acknowledgements, ownership,
drain and partial-publication tests continue to apply. The larger source buffer
is retained for the entire in-flight transfer, as before. [D]

The DT2 1.17 firmware and CANOPY module built with the larger buffer. The
`canopy-arp-tests:test-device` emulator acceptance loaded CANOPY and passed
keyboard playback, tempo, transport and panel-encoder checks. [D]

With 16-bit factory wavetables (two samples to a word, `src/dsp/wavetable.s`
scales the crossfaded table output by 2^-14 once, exact for every stored
sample) the packed DT2 module is 147,352 bytes, with a 132,292-byte DSP image
and the same 5,800 bytes of BSS: 33 image writes and two BSS writes at 4,096
bytes, 35 transfers instead of 58. The DT2 staging buffer, which holds the
whole file while it loads, is 160 KiB (240 KiB with the float tables). The five
registered `canopy-arp-tests` files pass on the DT2 with this package. [D]

The time from power-on to a playable track depends on the rest of firmware
boot, UI task scheduling and the physical link. A paired physical measurement
has not been made, and the optimized firmware has not been installed. [O]

## The main loop during the load

Measured in the emulator on 2026-11-18 (DT2 1.17, the image of `modules/fx/prism/tests/coexistence.device.test.ts`:
CANOPY, Overloop, PRISM and the remote module selected): a restart of the runtime, with the daemon's status (program
counter, instruction count, main-loop hits) read about every 1.9 ms of emulated time through the boot.

- **[D]** The main loop does not beat for 1.04 s once per boot: 1,045 ms on the tree before the upstream merge of
  2026-11-19 (`64c9cc5d6`), 1,044 ms on the merged tree. Every other gap of the 22 s after the restart is 36 ms.
- **[D]** In that gap the program counter is in the runtime's preparation of the persistent instrument for about 38 % of
  the instructions (`Coordinator::admit` called from `instrument::pending::prepare`, and `instrument_section::word`), in
  the firmware for about 52 %, and in the frame writers of the adapters (`mt_reply_valid`, `instrument_write_frame`,
  `phrase_write_frame`) for about 7 %. The samples do not say which of the firmware's share is interrupt code.
- **[D]** The merge moved the start of the gap from 0.22 s to 0.59 s after the restart: the service files load first.
  The emulator's stall watchdog counts only while the firmware has reached its main panel. Before the merge part of the
  gap lay before that point and the coexistence test passed with a limit of 600 ms; since the merge the whole gap is
  counted and the default limit of 1,000 ms reports it. The test sets its limit to 1,500 ms for that reason.
- **[O]** Whether a main loop that stands for 1.04 s matters on the device was not examined. The devices installed since
  2026-11-14 boot with this load.
- **[O]** The preparation could be spread over several passes of the UI task. Not tried.
