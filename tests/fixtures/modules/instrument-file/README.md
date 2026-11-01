# Normal V2 file loader diagnostic

Private DTII OS 1.17 firmware policy for `tests/regression/runtime/instrument-file.test.ts`.
It admits only the small diagnostic callback; it is not a CANOPY release budget.
The test uploads a checksummed DKM through the normal card protocol, then checks
post-CREATE publication and post-DETACH reclamation after a failing CF initializer.

Prepare from the repository root (never on the device):

```sh
uv run python -m tools.patchkit.patchkit build core/runtime test/instrument-file test/svc-init-fail
uv run python -m tools.patchkit.patchkit pack test/svc-init-fail --device dt2 -o out/regression/modules/dt2/svc-init-fail.dkm
uv run python -m tools.patchkit.patchkit patch --firmware firmware/Digitakt_II_OS1.17.syx --modules test/instrument-file -o out/patch/canopy-normal-file.syx
pnpm --dir tests/regression exec vitest run --project instrument-file runtime/instrument-file.test.ts
```

The regression requires the prepared DT2 sample card and a preflighted
`DIGI_EMUD_DT2` daemon. A blocked result retains memory and reports the coordinator
failure plus session-guard diagnostics; it must not be accepted as a successful
load merely because the module briefly appeared in the module list.
