# Loop storage reservation

This experimental, patch-time DT2 1.17 module reserves 8 MiB for one stereo loop. It reduces the stock sample allocator from 400 MiB to 392 MiB. At 48 kHz, float32 stereo, the reservation holds 21.845 seconds; four bars of 4/4 at 60 BPM need 16 seconds. The effect still chooses its own loop length.

The firmware is selected by the existing port SHA-256. Six writes use the `cf.main` anchor, which verifies the complete original MAIN image before patching. This is a firmware reservation, not a claim that unused sample memory is free. No firmware bytes are included in this module.

| DT2 1.17 operand | Purpose | New value |
| --- | --- | --- |
| `0x40153ae2` | End used by nonempty free-space getter | `0x18800400` |
| `0x40153af2` | Empty free-space getter | `0x18800000` |
| `0x40153afc` | Total capacity getter | `0x18800000` |
| `0x4015470c` | Tail-allocation end bound | `0x18800400` |
| `0x40154764` | Empty-pool preflight size bound | `0x18800000` |
| `0x40154aec` | Empty-pool allocation size bound | `0x18800000` |

The coordinate systems differ by the ColdFire sample offset bias. The sender at `0x40153a10` subtracts `0x400` from the destination before dividing by 4096. The DSP page copier adds `page * 4096` to `0x8422b9c8`. Thus CF end offset `0x18800400` maps to DSP `0x9ca2b9c8`, and stock preview starts at DSP `0x9d22b9c8`. Adding the bias a second time would overwrite 1024 preview bytes.

The two preview destination operands at `0x401541fc` and `0x40154228`, the stereo preview addresses, and the unrelated numeric-table value at `0x401e9c74` are untouched. The existing LP0 bound remains `0x19200`, because preview occupies the last 2 MiB above the original 400 MiB sample pool. Reducing that bound globally would break preview. This module does not provide general memory write protection.

`_loop_buffer_info` is a four-word DSP descriptor: magic `0x4c425546` (LBUF), version 1, byte address, byte length. DigiKit's DT2 import table exposes its address when this provider is present; a consumer must fail to load when the provider is absent. The provider has one buffer for one consumer. Consumers keep capture-valid metadata separately, invalidate it on activation and fault, and never read uninitialized audio. The 8 MiB buffer is neither transferred nor synchronously cleared.

The stock allocator's observed valid-file paths preserve the reservation: the first allocation uses the patched size bound, later allocations either consume an existing free gap or pass the patched tail bound. Reset empties the allocation vector. Static inspection alone does not establish every possible writer. Full sample load, delete, compaction, project reload, recording, preview, restart, and hardware timing need integration checks with buffer canaries. This reservation is experimental until those checks pass.

Build with `python -m tools.patchkit.pk_build build modules/core/adapters/loop-storage`. Patch a private stock image with `michael/loop-storage` selected. The bounded allocator proof, `python -m tools.patchkit.pk_loop_storage_proof --help`, compares real stock and patched ColdFire helper execution using synthetic allocation records. It does not load 400 MiB of audio or claim a full sample-loading test. Generated scripts, patched firmware, logs and reports belong under `out/`.
