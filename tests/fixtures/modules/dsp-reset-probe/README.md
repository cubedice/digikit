# DTII DSP reset hardware diagnostic

This is an explicit diagnostic for DTII OS 1.17, not a musical module. It exposes
command `0x3f` and does nothing to reset at initialization. Do not load it alongside
another service using that command. The default empty request reads peripheral
state. `RSET` requires active SPI and the patched reset entry, asserts DSP reset,
and captures before/after state. `RLSE` is available for controlled release tests;
it does not reload DSP firmware. Whole-device restart is required after a test.

Prepare an image containing the normal Overloop framework modules plus
`core/midi-events`, and run `patchkit preflight` before installation. Pack this
module with `uv run python -m tools.patchkit.patchkit pack test/dsp-reset-probe
--device dt2`. Preserve the existing selected modules when selecting the probe.

After the prepared image and probe have been installed, use the native Windows
Python with `python-rtmidi`, from the repository root:

```powershell
python -m tools.device.dsp_reset_probe --port Digitakt --firmware out/patch/overloop-reset-proof-dt2.syx --output out/reset-readiness.json
python -m tools.device.dsp_reset_probe --port Digitakt --firmware out/patch/overloop-reset-proof-dt2.syx --output out/reset-hardware-result.json --assert-reset
```

The first command is read-only. The second stops audio and leaves DSP reset
asserted. Save current work before executing it. The runner verifies the installed
image ID and hook bytes and requires a previously used link; it does not install
firmware or change the selected modules. Afterwards restore the original module
selection and restart the whole device.

Each snapshot has eight big-endian words: DSPI2 MCR, SR, RX CSR, TX CSR, TX source
address, RX destination address, GPIO pin data, stock board selector byte. The
96-byte reply contains snapshots immediately before reset, immediately after it,
and at the current query. A passing report requires the quiescence callback itself
to have entered during active serial transfer, subsequent stable DMA addresses,
HALT with SPI and both DMA channels inactive, asserted reset GPIO, and retained
link ownership. It does not claim to cover unhooked reset sources or power faults.

Processor tests exercise timeout and release suppression. The `dsp-reset` Vitest
project boots the exact candidate and checks diagnostic loading and malformed
command rejection without claiming to model hardware reset.
