# digikit SDK

The SDK is the stable interface between content modules and the digikit runtime.
It does not depend on the device. Device differences live in the `core/*` adapters
and the device profiles (`devices/*.toml`).

## Layout

| Path | What | Edit |
| --- | --- | --- |
| `sdk.toml` | The contract spec: the runtime version, the imports with their `service`, `since` and `context`, machine and effect layouts, log kinds, protocol numbers, limits, settings. | yes |
| `include/dk/core/` | The module contract: `<dk/core/module.h>` (`dk_module_def`, `dk_module_ctx` and the prototypes of every import), `<dk/core/log.h>`, `<dk/core/mem.h>`, `<dk/core/time.h>` (the runtime services), `<dk/core/trace.h>` (trace marks), `<dk/core/bytes.h>` (big-endian 32-bit access, header only), `<dk/core/text.h>` (bounded text: length, copy, equal, append, concatenate, number text, truncation, size and time text), `<dk/core/path.h>` (join, parent, leaf) and `<dk/core/status.h>` (the `DK_E_*` status codes; generated, do not edit). No device access. | `module.h`, `log.h`, `mem.h`, `time.h`, `trace.h`, `bytes.h`, `text.h`, `path.h` yes; `status.h` no |
| `include/dk/platform/` | What the device provides, one header per service group: `ui.h` (menus, lists and dialogs on the screen), `param.h` (read and write a machine parameter from a `dk_param_on_click` callback), `settings.h`, `files.h` (file listing and writing), `io.h`, `job.h` (background jobs), `hash32.h`, `usb_bulk.h`, `intro.h`, `data.h`. | yes |
| `include/dk/page/` | Pages: `page.h` (the frame and event of a canvas page, the page flags) and `widgets.h` (small page widgets). | yes |
| `include/dk/chooser/` | The slot list chooser (`chooser.h`) and its pure functions of row sort order and pool row text (`logic.h`). | yes |
| `include/dk/sample/` | Sample components: `pool.h` (per-module stereo sample pool), `view.h` (waveform drawing for native pages) and `picker.h` (on-device sample picker for a 16-entry pool). | yes |
| `include/dk/gen/` | C layouts and constants generated from `sdk.toml` (including `imports.h`, the prototypes of the service imports, and `sample.h`, the word layout of a sample pool entry). Included by the headers above, not by modules. | no |
| `src/` | Compiled SDK sources, in the folders of `include/dk/`: `chooser/chooser.c`, `chooser/logic.c`, `core/text.c`, `core/path.c`, `sample/picker.c`. The packer builds them into one SDK library and links the functions a module calls (`--gc-sections` drops the rest); a module does not list them in its `module.toml`. | yes |
| `dsp/` | SHARC+ code by concept (`osc/`, `filter/`, `dynamics/`, `delay/`, `util/`, `sample/`). See [dsp/README.md](dsp/README.md). `dsp/contract/` is on the SHARC+ include path and holds the call convention (`call.inc`), the includes generated from `sdk.toml` (`dk_fx.inc`, `dk_machine.inc`, `dk_sample_pool.inc`) and the hand-written `dk_trace.inc` (see Tracing). | yes |
| `templates/` | Module templates for `mise run module:new`: `machine/`, `fx/`, `tool/`. | yes |
| `tests/` | Host C tests, in the folders of `include/dk/` (`sdk:test-host`). | yes |
| `../modules/core/runtime/vectors/` | Fit and settings expected results shared by the Rust, Python and web tests. They belong to the runtime, not to the SDK. | no |

### Where a new thing goes

Include direction: `core` <- `platform` <- components (`page`, `machine`, `fx`, `chooser`, `sample`). A component may include
another component, without cycles.

- `core/` is the module contract: entry point, status, log, small helpers. No device access.
- `platform/` is the only layer that declares imports. A component never declares an import: it calls `platform/`.
- A new concept is a new component folder in `include/dk/`, with `src/<concept>/` for compiled code, `tests/<concept>/` for its host
  test and `dsp/<concept>/` for SHARC+ code. If it needs the device, its imports go in `platform/<name>.h`.
- Generated files go in `include/dk/gen/`, `dsp/contract/` and `include/dk/core/status.h` only.
- Names: functions `dk_<area>_<verb>`, types `dk_<thing>`, constants `DK_<AREA>_<NAME>`.

`patchkit gen` writes every generated file. `patchkit gen --check` fails when one is stale
(`mise run gen:modules -- --check`).

`pnpm nx:pilot run sdk:test-host` compiles the machine and effect fixture
declarations with the host `cc` compiler and checks their public-header records:
machine parameter units, effect parameter values, DSP symbols and routing.
It needs no firmware or DSP toolchain. Each shipped module still owns its device
conformance checks; this fast SDK check does not replace them.

## Entry point, services and builder calls

A module defines one constant, `dk_module` (`<dk/core/module.h>`), and reaches the device through named imports. The prototypes of the imports,
including the builder calls that declare machines and effects, are generated into `include/dk/gen/imports.h` from the `[[import]]` entries of
`sdk.toml`. Each import has a `service`, a `since` (the runtime version that added it) and a `context`; the generated header repeats the last two
in the comment of each prototype.

- [docs/MODULES.md](../docs/MODULES.md) says what a module is and why: the entry point, services and optional providers, the builder calls,
  what `module.toml` fixes, the load path and the versions.
- [docs/MODULE-AUTHORING.md](../docs/MODULE-AUTHORING.md) has the steps and a code sample for a machine, an effect and a tool.
- The naming rules (`dk_<area>_<verb>`, `ctx` and `arg`, `_fn` callback types) are in [docs/plans/sdk.md](../docs/plans/sdk.md) section 10.

The headers of the services are `core/log.h`, `core/mem.h`, `core/time.h`, `platform/host.h`, `settings.h`, `files.h`, `io.h`, `hash32.h`,
`intro.h`, `param.h`, `samples.h`, `ui.h`, `input.h`, `usb_bulk.h`, `dsp_link.h` and `debug.h`, and `page/page.h` for canvas pages.

## Include paths

The build passes these search paths, in this order:

- ColdFire C and assembly: the module's `build/gen/`, the module's `src/`, `modules/core/fw/inc`, then `sdk/include`.
- SHARC+ assembly: the module's `build/gen/`, the module's `src/`, `sdk/dsp/contract`, `sdk` (for `#include "dsp/<concept>/<name>.inc"`), then the directories of the module's sources.

A header in the module's `src/` is shared by C and SHARC+ sources. Use only decimal `#define`s and `//` comments on their own line: the SHARC+ assembler keeps a trailing comment in the macro text.

## Experimental phrase effects

`dsp/contract/dk_phrase_imports.inc` declares the public DSP imports for one captured-phrase
effect: the coherent control mailbox, status mailbox and two-bank audio lease.
It includes `dsp/contract/dk_phrase.inc`, whose byte offsets and constants are also shared
with C through `<dk/phrase.h>`. These are adapter data exports, not ColdFire
function imports in `sdk.toml`.

Importing `_phrase_control_info` requests exclusive phrase routing. The runtime
requires exactly one FX and no voice records, the control and routing providers,
and no existing phrase owner. It reserves that ownership before publishing the
FX row and revokes it before unloading the row. Missing capabilities reject the
load. The first supported port is DTII 1.17; these experimental headers do not
imply availability on other devices or firmware.

The engine receives selected stereo input before FADE and returns wet audio.
The adapter applies FADE, preserves unselected sources, and owns the `PH_S_FADE`
and `PH_S_ROUTE_FAULT` status words. The engine must leave both intact and publish
coherent, generation-tagged status. The storage descriptor supplies two distinct
stereo banks; validate its bounds and capacity, initialize frames before exposing
them, and never reuse a held bank while a grain still reads it. Neither bank may
alias the legacy loop lease. `_phrase_route_info` is the runtime's internal
provider-presence marker; content engines use the public control capability.

## Declaring an effect

An effect declares itself, its parameters and its page in a C file of the module, with `DK_FX`, `DK_FX_PARAM` and `DK_PAGE`
from `<dk/fx.h>`. This is `modules/fx/freqshift/src/module.c`:

```c
#include <dk/fx.h>
#include "freqshift.h"

DK_FX(fshift, .process = "_fs_process", .desc = "_fs_desc",
      .mix = 127, .route = DK_ROUTE_SEND, .routes = DK_ROUTE_SEND | DK_ROUTE_MASTER);
DK_FX_PARAM(fshift, FSHIFT_SHIFT_WORD, "Freq Shift", "SHIFT", DK_BIPOLAR, -64, 63, 0);
DK_PAGE(fshift, "FSH", "Freq Shifter", DK_KNOB_AT(FSHIFT_SHIFT_WORD, 0));
```

List the file as an `[[object]]` in `module.toml`. `patchkit pack` reads the records; the device never sees them.
`patchkit desc MODULE_ID` prints them. The fields and rules are in
[docs/MODULE-AUTHORING.md](../docs/MODULE-AUTHORING.md), section 5.4.

## Declaring a machine

A machine is declared the same way, with `DK_MACHINE`, `DK_PARAM` and `DK_PAGE` from `<dk/machine.h>`.
This is `modules/machines/pluck/src/module.c`:

```c
#include <dk/machine.h>

DK_MACHINE(pluck, .name = "Pluck", .short_name = "PLK", .render = "_pk_render",
           .note_on = "_pk_note_on", .desc = "_pk_desc");
DK_PARAM(pluck, 0, pluck_decay, "Decay", "DECAY", 0, 127, 64);
DK_PARAM(pluck, 1, pluck_pick, "Pick position", "PICK", 0, 127, 32);
DK_PARAM(pluck, 2, pluck_bright, "Brightness", "BRIGHT", 0, 127, 80);
DK_PARAM(pluck, 3, pluck_color, "Excitation colour", "COLOR", 0, 127, 40);
DK_PAGE(pluck, "PLUCK", "Pluck", DK_KNOB(0), DK_KNOB(1), DK_KNOB(2), DK_KNOB(3));
```

Parameter values are knob units 0 to 127. The stored word is the value times 256. The DSP symbols are required: a machine has no data-only form.
A machine has at most 4 parameters, in word order, and one page with `DK_KNOB` only. The adapter places word w on a fixed knob.
The rules are in [docs/MODULE-AUTHORING.md](../docs/MODULE-AUTHORING.md), sections 5.2 and 5.3.

`DK_PARAM_GLYPH(machine, word, GLYPH)` (in `<dk/page.h>`) sets the knob look of a parameter. Without it the glyph is `DK_GLYPH_ENCODER`:

| Glyph | Range |
| --- | --- |
| `DK_GLYPH_ENCODER` (default) | 0 .. 127 |
| `DK_GLYPH_FADER` | 0 .. 127 |
| `DK_GLYPH_PAN` | 0 .. 127 (centre 64, shows L/R) |
| `DK_GLYPH_ONOFF` | 0 .. 1 |
| `DK_GLYPH_TUNE` | 4 .. 124 (centre 64, one unit = one semitone) |
| `DK_GLYPH_PLAY` | 0 .. 3 (REV, REV.L, FWD.L, FWD; the Digitone II shows 0 .. 3) |

## Tracing

Trace marks tell you where a module was when it faulted or hung. They are opt-in and cost nothing when off.

| Side | Include | Macro | What it does when on |
| --- | --- | --- | --- |
| ColdFire C | `<dk/core/trace.h>` | `DK_TRACE_LOG("msg", value);` | Calls `dk_log(DK_LOG_TRACE, msg, value)`: the message (24 bytes kept) and a word go to the runtime's log ring (`dk:log`). |
| SHARC+ | `#include "dk_trace.inc"` | `DK_STEP(3, R0);` | Loads the step number 3 into the scratch register `R0` and stores it in the word `_dk_step`. Pick a register that is free at that point: SHARC+ cannot store an immediate to memory. |

The C macro is `DK_TRACE_LOG` because the build define is `DK_TRACE` and a function-like macro of that name would clash with it.

Build with `--trace` to switch them on: `mise run module:pack -- ID --device dt2 --trace`, `mise run module:check -- ID --trace`
(size lines), `mise run dk:dev -- ID --store ID --trace`. `--trace` defines `DK_TRACE` for the C sources (`-DDK_TRACE`) and the SHARC+
sources (`#ifdef DK_TRACE`), and sets bit 31 of the header word at 0x30 of the `.dkm` (`patchkit dkm-info` prints `trace yes`; a runtime
that does not know the bit ignores it). Traced and plain objects are cached apart (`out/cache/dko/<id>+DK_TRACE/`).

What you see:

- DIGIKIT, ABOUT: the module's result row ends in ` TRACE` (for example `- OK TRACE`).
- The fault record: the core clears `_dk_step` before each call into a module, the block tail copies it into the block's reply record, and the fault record prints the step of the last block that ended, for example `FX SEND STEP 3`. A step stored in the call that stops the DSP is lost with the core: put the mark before the call that you suspect on an earlier block, or make it a state that holds from block to block.
  A traced module imports `_dk_step` from `core/dyndsp`; a runtime without that export refuses it with `DSP IMPORT`.
- The log: the `DK_TRACE_LOG` messages.

Off means off: without `--trace` both macros expand to nothing (the arguments of `DK_TRACE_LOG` are not evaluated), the objects have no extra
instruction, data or import, and the `.dkm` is byte-identical to one built from sources without the marks
(`packages/patchkit/tests/trace.rs`). A `DK_STEP` costs two instructions when on, so keep it out of the innermost loop.
## Experimental persistent instruments

The CANOPY V2 work adds a separate `[instrument]` layout in `sdk.toml`, generated
as `include/dk/gen/instrument.h`, `include/dk/gen/instrument_consts.h` and
`asm/dk_instrument.inc`. It describes persistent track state, bounded normalized
MIDI batches and reset/process callbacks. `<dk/instrument.h>` provides
`DK_INSTRUMENT`. Experimental runtime packing accepts one DT2 1.17 instrument
whose manifest requires `core/instrument-play`, without V1 declarations or FX.
It emits a V2 DSP section and CF machine-list metadata, never a V1 voice callback.
Static firmware lowering still refuses these declarations. Default firmware
advertises machine contract 1.0 and cannot admit the package; the optional
playback adapter supplies a bounded policy. Integrated acceptance and hardware
release remain separate from declaration/packing support.

```c
#include <dk/instrument.h>
DK_INSTRUMENT(canopy, .name = "CANOPY", .short_name = "CANOPY",
              .reset = "_canopy_reset", .process = "_canopy_process",
              .desc = "_canopy_desc");
```

The three symbols name DSP code/data, not ColdFire function pointers. Instrument
IDs share a namespace with machine/effect IDs. `patchkit desc` reports these under
`instruments`, separately from V1 `machines` and `voices`. V1 `DK_PARAM`/`DK_PAGE`
records cannot yet target an instrument; V2 controls/pages require their own work.
See the
[CANOPY interface plan](../docs/design/canopy.md) for the remaining integration.

## Writing modules

See [docs/MODULE-AUTHORING.md](../docs/MODULE-AUTHORING.md). The plan for the SDK API is [docs/plans/sdk-api.md](../docs/plans/sdk-api.md).
