# Persistent instrument playback adapter

Development-only DTII OS 1.17 adapter for one admitted V2 instance on track 1
(the zero-based DSP track 0). It is not in a default patch set. There is no
production CF admission policy, route UI or public CANOPY package yet.

The early-frame transport callbacks copy normalized events into an owned input
buffer. The admitted process callback runs once per frame; the raw stock-carrier
entry copies its output without advancing state. A separate optional voice table
keeps legacy registrations first and never shadows a stock type. The adapter
retains a silent dispatch row after a fault rather than falling into a stock
sample renderer for that type.

Replacement reset calls the admitted reset entry through a protected wrapper.
CANOPY retains oscillator phase while releasing its voices over at most 64 samples;
configuration waits for that tail before accepting new ownership. CREATE, RECOVER
and UNLOAD still clear CANOPY state. Inactive tracks acknowledge transport resets.

Instruction-level checks are in `tests/patchkit/test_instrument_play.py`, with
stock dispatch coverage in `tests/patchkit/test_dsp_voice.py`. These do not prove
final stereo output, live Sound selection, fault feedback to CF, public route
readiness, or a device CPU budget. Those remain integration requirements before
this adapter can be enabled for users.
