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
