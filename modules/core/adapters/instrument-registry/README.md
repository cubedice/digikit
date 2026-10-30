# Persistent instrument registration mailbox

Experimental, optional DTII 1.17 adapter for the V2 instrument load protocol.
It answers capability queries only. It does not publish instruments, call reset
callbacks, dispatch audio, or make CANOPY installable. V2 remains disabled.

The provider exports `_dl_irows`, a 64-byte mailbox for DSP-link window 3, and
`_dl_iwords`, the address of its 16-word capacity constant. The protected MIDI
block wrapper calls `_instrument_registry_block` before processing MIDI input.
The provider uses no firmware anchors of its own.

## Command protocol version 1

Words are logical 32-bit DSP values, transported by the existing DSP-link encoder.
There is one outstanding command globally. The sender must clear word 0 and await
its transfer ACK, write words 1..15 and await their transfer ACK, then commit word
0 last. Do not begin the next command until the matching registry reply arrives.
The transport ACK alone does not acknowledge initialization or detachment.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49524731` (`IRG1`) |
| 1 | Protocol version, 1 |
| 2 | Nonzero, strictly increasing unsigned sequence; never wraps within a boot |
| 3 | Module owner, 0..31 |
| 4 | Nonzero generation |
| 5 | Operation: 1 QUERY, 2 INITIALIZE, 3 DETACH |
| 6 | Registry row, 0..7 |
| 7..14 | Reserved, must be zero |
| 15 | XOR of words 0..14, including the committed magic |

Invalid, incomplete, duplicate or older commands retain the previous reply and
do not advance the sequence. A valid command advances the sequence even when its
operation is unsupported. Retrying a committed command with the same sequence
therefore preserves its original reply. After sequence `0xffffffff`, a reboot
is required before further commands. This protocol is bootstrap-only; lifecycle
payloads and capability bits must be specified before enabling V2 loading.

## Reply

`_instrument_registry_reply` holds eight words, copied into words 4..11 of the
existing `_instrument_reply` by the MIDI reply builder. The outer checksum covers
these words and its commit magic is published last. Existing DSP-link reply rows
carry the result; there is no additional reply allocation.

| Word | Meaning |
| --- | --- |
| 0 | Commit magic `0x49525231` (`IRR1`), zero before the first accepted command |
| 1..5 | Echoed sequence, owner, generation, operation, row |
| 6 | Status: 0 query accepted, 1 unsupported lifecycle, 2 unknown operation |
| 7 | Capabilities, currently zero |

INITIALIZE and DETACH always return status 1. No successful reset or detach is
implied. Callers must validate the outer checksum, reply magic and all echoed
identifiers before interpreting status. A stale reply never authorizes freeing
memory or publishing a row.

Processor-component checks live in `tests/patchkit/test_instrument_registry.py`.
The composed-frame tests in `tests/patchkit/test_midi_events.py` exercise the
provider alongside the carrier and verify register preservation. These are
emulator checks; no hardware acceptance is claimed.
