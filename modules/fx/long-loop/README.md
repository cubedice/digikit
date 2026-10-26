# One-second loop proof

`michael/long-loop` 0.1.0 is an experimental Digitakt II OS 1.17 master effect. It captures **one second of stereo audio** at 48 kHz, freezes it, and crossfades against the live mix. The capture contains audio, not sequencer or mixer settings.

## Controls

The LLP page appears after the stock effects in FUNC + FX.

| Control | Behavior |
| --- | --- |
| A / REC | Raise from below 64 to 64 or above to capture once, for exactly 48,000 stereo frames. |
| B / START | Select frame 0–47,999. A change takes effect at the next complete loop boundary. |
| C / FADE | 0 selects live audio; 127 selects the frozen loop. Changes ramp linearly, reaching the opposite endpoint in 256 frames. |
| Framework MIX | Keep at **127**; lower settings add a second dry/wet blend. |

Until capture finishes, output follows the live input regardless of FADE. Capture reads the effect's input before its crossfade. Playback continues at FADE=0. Both channels share a cursor; brief fades at the phrase and capture seams preserve the exact loop length. Put this effect last when the capture should include preceding custom master effects.

REC cannot replace a frozen capture. Activation, disable/re-enable, or an FX guard fault resets validity; a new complete capture is required. There is no project or power-cycle persistence. Controls remain kit-local, so prepare the destination kit with matching START, FADE and MIX before changing patterns.

## Storage

The runtime reserves **384,000 bytes of module-owned BSS** for audio and **64 bytes of descriptor state** for metadata. Only metadata is cleared synchronously. Audio may contain old or uninitialized bytes; the effect cannot read it until capture has overwritten both channels completely. Code and storage share DigiKit's 512 KiB DSP module area with other loaded modules.

This is a deliberate exception to the [FX authoring guidance](../../../.agents/skills/digikit-module/references/contract-fx.md) against extra mutable statics. The relocated BSS belongs to this module and serves exactly one FX row. Do not add another row sharing it. The implementation keeps the existing FX binary contract; it does not allocate stock sample memory. Metadata words 7 and 8 expose the relocated audio address and frame count for inspection.

## Build and validation

From the repository root:

```sh
mise run module:build -- modules/fx/long-loop
mise run module:check -- michael/long-loop --device dt2
mise run module:pack -- michael/long-loop --device dt2 -o out/patch/long-loop-dt2.dkm
```

`tests/patchkit/test_long_loop.py` compares assembled DSP execution with `tools/patchkit/long_loop_ref.py`, including complete capture, poisoned storage, START, reset and fault behavior. The firmware runner `tools/patchkit/pk_short_loop_proof.py` accepts `--module michael/long-loop`; it requires prepared Linux/WSL tools and ready inputs as described in the [short-loop README](../short-loop/README.md).

Length is fixed and recording starts manually on an audio-block boundary. There is no tempo following, beat quantization or automatic pattern transition. Running-sequencer transitions, hardware timing, heavy playback and audible transition quality remain experimental; test results belong in `docs/findings/54-module-cookbook.md`.

Built with **digikit by Em D — https://github.com/ElekLabs/digikit**. Distributed under the repository's AGPL-3.0-or-later license and NOTICE terms. This module is unofficial and is not endorsed by Elektron.
