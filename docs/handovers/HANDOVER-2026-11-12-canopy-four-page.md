# CANOPY four-page SRC implementation

The approved [UI design](../design/canopy-ui-review.md) is implemented on the
current branch as a firmware candidate. SRC cycles KEYS, ARP, SOUND and ENV for
all source families. The new diagrams use bounded native-font drawing, the
factory waveform preview and envelope timing tables. TUNE, source LEV and the
track-level strip stay on the firmware's native path. The control map moves
existing IDs rather than changing presets, packed trig-lock IDs or MOD targets.
Changing wavetable CLK sets the same rate defaults as the existing settings
menu; a held sync-clock lock displays the same rate clamp used by the DSP.

Focused checks passed: 62 SRC and native-adapter host tests, 25 existing
CANOPY adapter integration tests, the generated preview-table check, scoped
Python lint and the changed TypeScript formatting check. The latest
`core/instrument-play:build-objects` rebuilt the ColdFire object successfully.

A coupled DTII 1.17 emulator loaded the selected CANOPY module after 80
seconds of emulated time (boot report: 1 of 1 OK). Normal-state captures of
the four pages and Velvet, wavetable and FM variants are under
`out/canopy-longboot/dt2/`. The captured default KEYS TUNE, source LEV and
track-level regions each matched the stock reference with zero differing
pixels. The capture predates the last waveform rounding and voice-fault
display corrections; neither touches the native regions. The host tests
exercise those corrections. The temporary 25 GB test card was removed.

Open acceptance: capture the final build in held-SRC mode and complete the
full native-value and pixel matrix in the design. Run the current device
acceptance suite after updating any historical scenarios that still assume
the retired SRC layout, and qualify on physical hardware after installation.
The broader `test:changed` gate did not pass: it reported pre-existing
workspace formatting and layer-check failures, and host runtime tests require
50 GB free under `/tmp`, which is an 8 GB tmpfs in this WSL environment.
No firmware was installed on the DTII.
