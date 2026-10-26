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

**Kit recall is still a limitation.** DigiKit recalls REC, START, FADE and framework MIX with each kit. The buffer lives outside the kit, but the controls are not global. A destination kit with FADE=0 immediately requests a fade to live; MIX=0 bypasses the loop. Prepared kits retained the required settings in the firmware emulator with transport stopped. Switching during sequencer playback and transient-free control delivery still need validation.

## Scope and evidence

- Fixed 250 ms, float32 stereo at 48 kHz; **96,064 bytes of state**, plus module code/data. Allocation counts against the shared 512 KiB DSP module area.
- One capture per activation; no overdub, background recapture, normalization or sample-RAM access.
- Recording is manual and audio-block aligned. There is no beat/bar quantization, tempo following, time stretching, or automatic pattern switching.
- START rotates a fixed-length loop. The proof does not yet expose length, a separate musical capture-start point, or a waveform editor.
- Restart, disable/re-enable, or an FX guard fault clears capture state. This is not project or power-cycle persistence.
- The unit tests execute assembled SHARC code behind DigiKit's FX adapter and compare it with a float32 reference model.
- A full OS 1.17 firmware emulator run with transport stopped passed 35 behavioral checks across three actual kit switches. Both captured buffers remained unchanged and the cursor advanced by the exact audio-block count. START selected frame 3,117; prepared kits recalled it with FADE and MIX. Four complete 500 ms PCM captures confirmed audible loop playback over a silent destination, then exact silence after fading back to live.
- That run used `SHARC_FAST_AUTO=256,64`. Its saved trace passed revalidation after correcting an idle-range endpoint check: the rejected endpoint was the idle loop's closing jump. The original failed verdict is retained alongside `out/runs/short-loop-firmware/report.rechecked.json`; details are in `docs/findings/54-module-cookbook.md`.
- Running-sequencer transitions and hardware playback remain unverified. Analog/USB output, monitored inputs, FX tails, transition transients and heavy playback also need validation.

Source: `src/dsp/short_loop.s`. Reference: `tools/patchkit/short_loop_ref.py`. Tests: `tests/patchkit/test_short_loop.py` and the standard FX conformance suite. The test rig now reserves each runtime effect's declared state size, rather than a fixed 4 KiB.

The bounded firmware runner is `tools/patchkit/pk_short_loop_proof.py`. Run it from Linux/WSL with prepared binaries, your stock OS image, and the existing `DIGI_EMUD_DT2` and `DIGI_PK_READY_DT2_117` environment overrides:

```sh
python -m tools.patchkit.pk_short_loop_proof \
  --work out/runs/short-loop-firmware \
  --patchkit /path/to/patchkit --plusdrive /path/to/plusdrive \
  --cf-script /path/to/cf_script --timeout 900
```

It drives real encoder and pattern-button events with transport stopped, writes a JSON verdict plus PCM captures and screenshots, and checks both audio buffers and exact cursor advancement across kit changes. `report.json` is the latest verdict; each run also gets a unique report. On WSL mounts that cannot create sparse card images, add `--card-directory /tmp/digikit-short-loop-proof`. The runner records the emulator configuration; an accelerated run is not automatically evidence of interpreter parity. Generated firmware, card images and audio stay out of version control.

## Longer loops

Increasing the buffer constant is not sufficient. The FX adapter clears all descriptor-owned state synchronously when an effect activates or faults. A test using identical passthrough code measured 25,114 instructions to activate with 250 ms equivalent storage, versus 49,114 with 500 ms. The current generic effect-plus-adapter conformance ceiling is 31,832 instructions. Subsequent blocks cost 691 instructions at either size. This measures initialization in the SHARC test rig, not a hardware deadline miss.

Float32 stereo at 48 kHz needs 384,000 bytes per second:

| Duration | Audio storage | Musical example |
| --- | ---: | --- |
| 250 ms | 96,000 bytes | Current proof |
| 1 second | 384,000 bytes | Half a bar at 120 BPM |
| 2 seconds | 768,000 bytes | One bar of 4/4 at 120 BPM |
| 8 seconds | 3,072,000 bytes | Four bars of 4/4 at 120 BPM |

The shared DSP module region is 524,288 bytes, including every loaded module's code and state. Multi-bar recording therefore needs a separately reserved audio buffer. DigiKit does not currently expose that reservation; its `Api.alloc` allocates ColdFire memory, not DSP sample memory.

The next storage proof should introduce bounded metadata initialization, then validate a longer capture within the existing region. Audio must remain unreadable until capture has overwritten the entire buffer, including after a fault or re-enable. An owned external buffer comes after that, with explicit allocation failure, reset, sample-loading and release tests. See `tests/patchkit/test_short_loop_storage_budget.py` and `docs/findings/54-module-cookbook.md` for evidence and open questions.

Built with **digikit by Em D — https://github.com/ElekLabs/digikit**. Distributed under the repository's AGPL-3.0-or-later license and NOTICE terms. This module is unofficial and is not endorsed by Elektron.
