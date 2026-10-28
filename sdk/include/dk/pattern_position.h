/* Experimental optional DT2 1.17 DSP import from michael/pattern-position.
 * Version 2 publishes nominal master-grid boundaries committed by the stock
 * timed-event consumer, not each scheduling snapshot or microtimed note.
 * DSP words are little endian. The provider publishes before command dispatch.
 * A missing provider makes a required runtime import fail with E_DSP_IMPORT.
 */
#ifndef DK_PATTERN_POSITION_H
#define DK_PATTERN_POSITION_H
#include <stdint.h>

#define DK_POSITION_MAGIC 0x50504f53u
#define DK_POSITION_VERSION 2u
#define DK_POSITION_VALID 1u
#define DK_POSITION_RUNNING 2u

typedef struct dk_pattern_position {
    uint32_t magic;             /* zero on invalid/replayed/corrupt frame */
    uint32_t version;
    uint32_t transmit_sequence; /* CF audio-send frames; unsigned wrap */
    uint32_t boundary_sequence; /* committed boundaries; unsigned wrap */
    uint32_t start_generation;  /* generation frozen with the boundary */
    uint32_t flags;
    uint32_t pattern_identity;  /* opaque scheduling pointer, not a slot ID */
    uint32_t master_step;       /* committed native master-grid position */
    uint32_t master_length;     /* 0 means unknown/special */
    uint32_t reserved;          /* zero */
    uint32_t commit_block;      /* CF block preparing the boundary's frame */
    uint32_t scheduler_clock;   /* committed raw deadline, not samples */
    uint32_t transmit_block;    /* same CF counter, when sent */
    uint32_t native_step;       /* diagnostic current scheduling position */
    uint32_t raw_transport;     /* 0 reset, 1 running, 2 paused */
    uint32_t wire_check;
    uint32_t received_frames;
    uint32_t rejected_frames;
    uint32_t accepted_frames;
    uint32_t transmit_delta;    /* first accepted frame: 0 */
} dk_pattern_position;

#endif
