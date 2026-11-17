# Phrase FX performance UI

`core/phrase-ui` owns the native FX surface for the loaded phrase capability.
It does not identify a content module by name or effect ID. `core/phrase-control`
publishes the single active native FX page and handles settings, capture actions,
timing and coherent DSP telemetry.

UP/DOWN cycle LOOP, GRAIN, MOVE and SPACE inside this one global effect entry.
FUNC+FX advances the global effect once; the local page is recalled on return.
H opens input selection. TRIG releases toggle sources; holding TRIG while
turning LEVEL edits that source's capture gain and consumes the toggle.
FUNC+TRIG selects only that internal source. A chooses external OFF/L/R/ST,
B sets linked external gain, YES captures/cancels and NO/H closes the overlay.
Capture locks input selection and gains while keeping FADE available.
FUNC+NO clears the phrase from either a control page or IN without leaving
that view. The gesture is scoped to the active visible FX owner and consumes
its repeats and release; normal NO still closes IN or passes to the stock view.

The UI model owns complete key gestures, including their releases after an
overlay closes. Saved track levels, native mute state, transport and recording
remain independent. All parameters are supplied through the phrase contract;
the native kit's eight generic effect words are not reused as hidden storage.
The renderer uses native text and knob primitives through a bounded canvas.
IN scopes the native LED setter to the sixteen TRIG indicators: selected
effective sources are bright and excluded sources dim. Other LEDs retain
their native handling, and the next native LED frame restores the TRIG grid
when the overlay closes or loses its view. Held sources remain visible on
screen, including the L/R external tiles.

`tests/ui.test.ts` compiles the same model and renderer used by firmware. It
checks local/global navigation, page recall, release ownership, gain-edit
selection, capture locking, discrete detents and all render-state bounds.
It also checks the deliberate CLEAR gesture and routing-fault status display.
Firmware integration and LED restoration require device acceptance in addition
to these host tests.

The DTII 1.17 binding is declared in `devices/fw/phrase-ui.toml`. It replaces
the native FX view's draw/key/encoder slots and its encoder-interface thunk,
retaining the original functions for every other effect. The view's page
getter is vtable slot 34; its native held-values flag is at `+0x190`. The
secondary encoder receiver is `view+4`, so that thunk adjusts the receiver
before entering the shared handler. The LED hook wraps the native setter
through a generated tail and leaves its per-frame priority reset intact.
Eligibility uses the firmware's resolved drawable suffix, which removes FX
under an opaque modal while permitting a passive transparent view above it.
Local navigation and encoder ownership begin when native dispatch calls the
FX view. IN's TRIG gestures are intercepted at the key-manager boundary,
before native grid/track handlers can also audition, select or edit a trig.
Only eligible IN gestures and outstanding owned releases take this path;
every other key still enters the stock dispatcher. Losing eligibility closes
IN and cancels its held gestures. A post-HelpBubble paint wrapper restores
only the ten-pixel PRISM header while that FX view remains eligible, preserving
its local page, state and page count above the native global page label.
View and LED layout background is in findings 71 and 19; the declarations
and local 1.17 instruction windows are the version-specific binding evidence.
