/* Experimental persistent instruments. Runtime packing requires the optional
 * DT2 1.17 single-instance V2 playback adapter. DK_PARAM/DK_PAGE are V1 records and
 * cannot be attached to this declaration yet. */
#ifndef DK_INSTRUMENT_H
#define DK_INSTRUMENT_H
#include <dk/gen/instrument.h>
#include <dk/page.h>

/* Optional input extension used by the single-instance playback adapter.
 * Descriptor/reset ABI remains V2. A process callback accepts version=3 and
 * size_bytes=80 only if it implements this extension; legacy version=2/72
 * inputs remain valid. The first 72 bytes retain their V2 meanings, including
 * event word 5 as a sample offset (currently zero).
 * movement: committed mapper phrase (low 31 bits, nonzero), generated chord
 * flag (bit 31), or zero for an unmapped/legacy event batch. It remains stable
 * on idle blocks and is replaced atomically with the associated event batch.
 * kind=5 is a metadata-only commit: owner/token/pitch/value/offset are zero.
 * pattern: opaque committed pattern identity. Use only when timing flags
 * report valid; stopped frames can retain the last committed identity.
 * These fields are borrowed, read-only values, not addresses to dereference. */
#define DK_INSTRUMENT_INPUT_V3 3u
#define DK_INSTRUMENT_INPUT_V3_BYTES 80u
typedef struct dk_instrument_in_v3 {
    dk_instrument_in base;
    uint32_t movement;
    uint32_t pattern;
} dk_instrument_in_v3;

/* V4 is present only with a valid version-1 transport phase capability.
 * Phase and the committed boundary deadline share a wrapping u32 domain of
 * 21600000 ticks per quarter note. Neither is an audio sample timestamp.
 * Transport generation changes invalidate prior phase comparisons. */
#define DK_INSTRUMENT_INPUT_V4 4u
#define DK_INSTRUMENT_INPUT_V4_BYTES 88u
typedef struct dk_instrument_in_v4 {
    dk_instrument_in_v3 base;
    uint32_t phase;
    uint32_t boundary_phase;
} dk_instrument_in_v4;

/* V5 is sent only to a descriptor opting into STATUS_OUTPUT. V2..V4 remain
 * valid inputs. It retains V4's layout, with zero timing fields when absent.
 * V5 timing flags retain valid/running in bits0/1 and add provider information:
 * bit8 coherent grid provider, bit9 phase provider, bit10 explicitly stopped.
 * These high bits never authorize scheduling: use the low valid/running bits
 * and the required provider capability. They are absent from V2..V4 inputs.
 * status is an adapter-owned, aligned L2 buffer of exactly 13 words: ready,
 * then twelve normalized UI payload words. The adapter clears ready before
 * every process call. A successful callback may fill all payload words then
 * publish ready=1 last. No pointer may escape the call. Negative/invalid process
 * results suppress publication. Neither ready nor payload acknowledges MIDI.
 * The adapter must provide disjoint input/output/event/control/state storage. */
#define DK_INSTRUMENT_STATUS_OUTPUT 1u
#define DK_INSTRUMENT_INPUT_V5 5u
#define DK_INSTRUMENT_INPUT_V5_BYTES 96u
#define DK_INSTRUMENT_STATUS_WORDS 13u
typedef struct dk_instrument_in_v5 {
    dk_instrument_in_v4 base;
    uint32_t *status;
    uint32_t status_words;
} dk_instrument_in_v5;

/* V6 is sent only to descriptors with STATUS_OUTPUT | NATIVE_AMP.
 * V2..V5 remain valid fallback inputs. A valid native AMP snapshot lets an
 * instrument apply the track's envelope per note; the adapter then bypasses
 * the shared carrier envelope for that successfully rendered block only.
 * All appended fields are scalars, with no firmware addresses or lifetime.
 * mode: 0 unavailable (use legacy/shared AMP), 1 AHD, 2 ADSR.
 * Attack is a positive per-sample increment. Decay/release are multipliers
 * in [0,1]; hold is a sample count or UINT32_MAX for gate-controlled hold.
 * AHD snapshots have sustain=0 and release_multiplier=decay_multiplier.
 * options bit0 reflects native amplitude restart; other bits/reserved are0.
 * A callback must validate the snapshot before using it. Native coefficients
 * may change between blocks, including during a sounding note. */
#define DK_INSTRUMENT_NATIVE_AMP 2u
#define DK_INSTRUMENT_INPUT_V6 6u
#define DK_INSTRUMENT_INPUT_V6_BYTES 128u
typedef struct dk_instrument_in_v6 {
    dk_instrument_in_v5 base;
    uint32_t amp_mode;
    float amp_attack_increment;
    uint32_t amp_hold_samples;
    float amp_decay_multiplier;
    float amp_sustain;
    float amp_release_multiplier;
    uint32_t amp_options;
    uint32_t reserved;
} dk_instrument_in_v6;

/* V7 is sent only to descriptors with STATUS_OUTPUT | NATIVE_AMP | POST_AMP.
 * V2..V6 remain valid fallback inputs. post_amp is an adapter-owned, aligned
 * L2 output buffer of exactly 32 per-sample common gains in [0,1]. The ordinary
 * mono output retains its per-note envelopes; these gains are not audio. The
 * adapter normalizes output/gain (zero gain produces zero), then applies the
 * gain in place of shared AMP at the original stock AMP position for the owned
 * stereo pair. Normalized audio must be finite with magnitude at most 8.
 * The gain buffer is disjoint from the input, ordinary output, status, events,
 * controls and instance state.
 * The adapter initializes every gain to NaN before each callback. The callback
 * must write all 32 finite gains, even when returning QUIET. The adapter uses
 * them only after a successful process result and complete validation. With an
 * unavailable native snapshot (mode=0), return legacy PCM and unity gains; the
 * adapter retains the shared AMP path without normalization or post-AMP gain.
 * The pointer is borrowed for this call only.
 * POST_AMP requires both STATUS_OUTPUT and NATIVE_AMP; supported descriptor
 * flags are exactly 0, 1, 3 and 7. */
#define DK_INSTRUMENT_POST_AMP 4u
#define DK_INSTRUMENT_INPUT_V7 7u
#define DK_INSTRUMENT_INPUT_V7_BYTES 136u
#define DK_INSTRUMENT_POST_AMP_SAMPLES 32u

typedef struct dk_instrument_in_v7 {
    dk_instrument_in_v6 base;
    float *post_amp;
    uint32_t post_amp_samples;
} dk_instrument_in_v7;

/* ID is a C identifier, unique among all instruments, machines and effects.
 * Required initializers: .name, .short_name, .reset, .process and .desc.
 * Callbacks and descriptor name SHARC+ symbols in this module, not CF pointers. */
#define DK_INSTRUMENT(ID, ...)                                                   \
    DK_DESC_OVERRIDE_BEGIN                                                      \
    static const dk_desc_instrument dk_desc_instrument_##ID DK_DESC = {           \
        .kind = DK_DESC_INSTRUMENT, .size = sizeof(dk_desc_instrument), .id = #ID,  \
        __VA_ARGS__};                                                           \
    DK_DESC_OVERRIDE_END
#endif
