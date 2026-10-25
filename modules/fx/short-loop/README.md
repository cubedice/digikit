# Short loop proof

An experimental Digitakt II OS 1.17 master effect that captures **250 ms of stereo audio**, freezes it, and crossfades against the live mix. The capture is audio, not a saved sequencer pattern. Module ID: `michael/short-loop`, version `0.1.0`.

This proof uses the existing DigiKit FX contract and its module-owned memory. It does not change the runtime, sequencer, sample allocator, or stored mixer levels. There is no firmware image in the package. Nothing here flashes a device.

## Controls

The SLP page appears after the stock effects in FUNC + FX.

| Control | Behavior |
| --- | --- |
| A / REC | Turn from below 64 to 64 or above to capture once. Recording starts when the control reaches the DSP and lasts exactly 12,000 stereo frames. |
| B / START | 0–127 selects playback start across the captured buffer, from frame 0 to frame 11,999. A change takes effect at the next complete loop boundary. |
| C / FADE | 0 is the live mix; 127 is the frozen loop. Intermediate positions make a linear crossfade. A full-range change ramps over 256 frames (5.33 ms). |
| Framework MIX | Keep at **127** for the intended behavior. Below 127 the framework adds another dry/wet blend outside this effect. |

Until a capture is complete, the effect passes live audio unchanged regardless of FADE. Capture reads the effect's input, before its own crossfade. Once frozen, neither incoming audio nor further REC turns can overwrite the buffer. Restart the emulator/runtime for another capture.

Playback keeps running when FADE is 0. Both channels share one cursor. Short fades at the physical capture seam and the phrase boundary reduce discontinuities without changing the 12,000-frame period; they can produce a small audible amplitude notch. Set this effect last in the master chain when the captured sound should include all preceding custom master effects.

## Emulator workflow

Use a DigiKit checkout with the DT2 FX runtime and your own stock OS 1.17 image. From the repository root:

```sh
mise run module:build -- modules/fx/short-loop
mise run module:check -- michael/short-loop --device dt2
mise run module:pack -- michael/short-loop --device dt2 -o out/patch/short-loop-dt2.dkm
mise run workbench -- firmware/Digitakt_II_OS1.17.syx
# In another shell, while the workbench is running:
mise run dk:run -- michael/short-loop --emu
```

For an A01 → A02 transition:

1. Prepare both patterns with framework MIX=127. Set A02's FADE=127, REC=0 and START to the desired loop start; save its kit settings. Return to A01 with FADE=0 and REC=0. Leave PERFORM KIT off so A02 can load its own kit.
2. Start A01, raise REC to 64 or above, and wait at least 250 ms after capture begins.
3. Raise FADE to 127 to hear the frozen audio alone.
4. Switch to A02. Its saved FADE=127 and MIX=127 are needed to keep hearing the loop.
5. Lower FADE toward 0 to bring in A02's complete live mix.

**Kit recall is still a limitation.** DigiKit recalls REC, START, FADE and framework MIX with each kit. The buffer lives outside the kit, but the controls are not global. A destination kit with FADE=0 immediately requests a fade to live; MIX=0 bypasses the loop. The preparation above is a workaround to test, not a fix for parameter persistence.

## Scope and evidence

- Fixed 250 ms, float32 stereo at 48 kHz; **96,064 bytes of state**, plus module code/data. Allocation counts against the shared 512 KiB DSP module area.
- One capture per activation; no overdub, background recapture, normalization or sample-RAM access.
- Recording is manual and audio-block aligned. There is no beat/bar quantization, tempo following, time stretching, or automatic pattern switching.
- START rotates a fixed-length loop. The proof does not yet expose length, a separate musical capture-start point, or a waveform editor.
- Restart, disable/re-enable, or an FX guard fault clears capture state. This is not project or power-cycle persistence.
- The tests execute assembled SHARC code behind DigiKit's FX adapter and compare it with a float32 reference model. Changing the live input while the loop plays is a **simulation of a source change**, not evidence of a real firmware pattern/kit switch.
- Full firmware emulator and hardware tests require separate validation. Check analog/USB output, monitored inputs, FX tails, actual kit recall and heavy playback before treating this as a performance tool.

Source: `src/dsp/short_loop.s`. Reference: `tools/patchkit/short_loop_ref.py`. Tests: `tests/patchkit/test_short_loop.py` and the standard FX conformance suite. The test rig now reserves each runtime effect's declared state size, rather than a fixed 4 KiB.

Built with **digikit by Em D — https://github.com/ElekLabs/digikit**. Distributed under the repository's AGPL-3.0-or-later license and NOTICE terms. This module is unofficial and is not endorsed by Elektron.
