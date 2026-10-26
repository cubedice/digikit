# Eight-second loop proof

`michael/bar-loop` 0.1.0 is an experimental Digitakt II OS 1.17 master effect. It captures **eight seconds of stereo audio** at 48 kHz: four bars of 4/4 at 120 BPM. Length is fixed in seconds; it does not follow tempo or quantize recording to a beat.

## Controls

The BLP page appears after the stock effects in FUNC + FX.

| Control | Behavior |
| --- | --- |
| A / REC | Raise from below 64 to 64 or above to capture once, for exactly 384,000 stereo frames. |
| B / START | Select frame 0–383,999; changes take effect at the next complete loop boundary. |
| C / FADE | 0 selects the live mix; 127 selects the frozen loop. Full-range changes ramp over 256 frames. |
| Framework MIX | Keep at **127** for the intended crossfade. |

Output remains live until all audio has been recorded. Capture reads the effect's input before its own crossfade; place it last to include preceding custom master effects. Playback continues at FADE=0. Short seam fades preserve the exact loop period but can produce an amplitude notch.

Audio storage is independent of kit data; its controls are kit-local. Prepare the destination kit's START, FADE and MIX to preserve the intended transition. REC does not overwrite a frozen loop. Reset, disable/re-enable or an FX guard fault invalidates capture; recording must finish again before playback. Audio is not saved with projects or across restarts.

## Explicit storage ownership

This effect requires the patch-time provider `michael/loop-storage` and its `_loop_buffer_info` DSP import. The provider reserves **8 MiB** by reducing the DT2 1.17 stock sample allocator from **400 MiB to 392 MiB**. The effect itself allocates only 64 bytes of metadata in the shared DSP module area. Its audio occupies **3,072,000 bytes** of the external reservation.

There is one reservation for **one consumer**. Do not load another effect that writes the same buffer. There is no guessed-address fallback: an absent provider prevents loading. Every call validates the provider's magic, version, alignment, capacity and address bounds before using its memory.

The 32 bytes immediately after audio contain eight `0x5a17b00f` canary words. Activation initializes only those eight words; later calls check them before touching audio. Corruption returns an FX fault, plays dry and clears capture validity. The provider must therefore supply at least **3,072,032 bytes**. Metadata words 7, 8 and 9 report the audio address, frame count and canary-initialized flag. Audio is never cleared synchronously and remains unreadable until a complete capture overwrites it.

The canary detects corruption at this boundary; it is not general memory protection. Full sample loading, deletion, compaction, project reload, recording, preview and hardware operation still need integration checks. The reservation design and allocator proof are documented in `modules/tools/loop-storage/README.md`.

## Build and validation

From the repository root:

```sh
mise run module:build -- modules/tools/loop-storage
mise run module:build -- modules/fx/bar-loop
mise run module:check -- michael/bar-loop --device dt2
mise run module:pack -- michael/bar-loop --device dt2 -o out/patch/bar-loop-dt2.dkm
```

The provider is patched into a private firmware image; copying the effect's `.dkm` alone does not reserve memory. The bounded runner `tools/patchkit/pk_short_loop_proof.py` selects the provider automatically with `--module michael/bar-loop --timeout 1800`. It requires the prepared Linux/WSL binaries and ready inputs described in the [short-loop README](../short-loop/README.md), and exercises kit changes with transport stopped.

`tests/patchkit/test_bar_loop.py` covers complete capture, playback, provider rejection, reset and canary faults. Standard FX conformance uses an explicitly registered test provider with a read-only descriptor and a narrowly allowed audio/canary range; this fixture does not prove stock allocator ownership. The separate `tools/patchkit/pk_loop_storage_proof.py` checks allocator boundaries. Record run results in `docs/findings/54-module-cookbook.md`; running-sequencer transitions, hardware deadlines and audible performance remain unverified.

The OS 1.17 firmware emulator proof passes **45 checks**, including complete eight-second capture, three kit changes, START adjustment, unchanged capture/canary hashes, exact cursor advancement and fading back to a silent destination. Four recordings each cover a complete loop. A separate boot without the provider rejects the effect cleanly with `E_DSP_IMPORT (-32)`. These runs use accelerated DSP execution with transport stopped; details and the broader repository-check limitations are in `docs/findings/54-module-cookbook.md`.

Built with **digikit by Em D — https://github.com/ElekLabs/digikit**. Distributed under the repository's AGPL-3.0-or-later license and NOTICE terms. This module is unofficial and is not endorsed by Elektron.
