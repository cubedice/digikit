# DTII DSP reset during active SPI

[D] Measured once on physical Digitakt II, OS 1.17, 2026-10-31. Firmware
image `7eed7aa5661f7981`, runtime 0.5.2, implementation commit `76a65ce8`.
The user authorized diagnostic firmware installation, DSP reset, and restoration.

## Artifacts and method

The preflighted firmware `out/patch/overloop-reset-proof-dt2.syx` has SHA-256
`f70100ae71a65697289cac04460307739e4a6f04802b7c0ba66fcaa8d9661920`.
The emulator-tested diagnostic `out/patch/dsp-reset-probe-dt2.dkm` has SHA-256
`2d76fd89b5976c36cae77a1a72c4a97f3ed3f4650c0c1d501ec18cf0973a82b1`.
The host verified firmware identity and installed reset-hook bytes before loading
Overloop plus the explicit diagnostic. The diagnostic never asserts reset on boot.

`tools/device/dsp_reset_probe.py --assert-reset` saved the successful measurement
to `out/reset-hardware-result.json`. Read-only readiness is in
`out/reset-readiness.json`. No firmware bytes or raw captures are committed.

## Observations

| Measurement | Before / entry | After reset |
| --- | --- | --- |
| DSPI2 MCR | `0x80010000` | `0x80010001` (HALT) |
| DSPI2 SR at quiescence entry | `0xc202a751` (TXRXS set) | `0x8202d963` at quiescence exit (TXRXS clear) |
| RX DMA CSR | `0x0048` (ACTIVE set) | `0x0008` |
| TX DMA CSR | `0x0008` | `0x0008` |
| Reset GPIO, board selector 1 | `0x03` | `0x01` (reset bit 1 low) |
| TX source after reset | `0x80002e98` | Stable at all three later queries |
| RX destination after reset | `0x80001946` | Stable at all three later queries |

Quiescence returned state 1 after one poll. Later snapshots, each separated by
200 ms, kept HALT set, TXRXS and DMA START/ACTIVE clear, and reset asserted.
Link state became BUSY/error 102 with revoked=1. Source pointer and serial 19
were retained. The lease fields were zero at entry; this run does not prove
interruption of a live V2 lease. It proves physical active-transfer shutdown of
an already-used link with Overloop loaded. Component tests separately cover
retention of pending ownership and timeout suppression of reset release.

## Recovery and scope

After the successful measurement, SELECT and then INFO and RESTART timed out.
The DSP is intentionally held in reset, but continued USB control after the
measurement is not established. The user power-cycled, restoring USB responses. The host saved Overloop-only
selection. The next soft restart entered safe mode (the boot counter was 20);
the safe boot cleared that counter. After removal of the unloaded diagnostic
and another software restart, INFO reported safe=false and crash_found=false,
with only `digikit/overloop` loaded and zero faults. Only `modules/overloop.dkm`
remained in the module directory. Its bytes exactly matched the pre-test backup:
SHA-256 `e388ebf2a4f0efb5298ec1a6bd40b3336357c8a1a3545dc184c2dd36442e5081`.

Recovery evidence: `out/reset-restored.json` and
`out/reset-recovery-safe-log.json`. The reset-protected firmware remains installed.
The original module was not overwritten. A physical power cycle, rather than
remote restart, is the demonstrated recovery route after DSP assertion.

This closes the physical DMA/SPI quiescence measurement for the hooked stock
reset helper at `0x400cd01c`. It does not establish coverage of other reset
sources, power faults, automatic DSP reload, or a playable CANOPY V2 release.

Documentation validation: focused pathcheck and staged whitespace checks pass.
Changed-file preflight passes layering and selects no host tests; its repository
pathcheck still fails on 256 existing stale references in 78 files. Firmware,
processor, and candidate boot checks are recorded in the implementation notes.
