#ifndef DK_TRACK_CONFIG_H
#define DK_TRACK_CONFIG_H

#include "track-keyboard.h"

enum TkControl {
    TK_CTL_ARP,
    TK_CTL_POLY,
    TK_CTL_ORDER,
    TK_CTL_GATE,
    TK_CTL_RATE,
    TK_CTL_OCTAVES,
    TK_CTL_SWING,
    TK_CTL_RESET,
    TK_CTL_CHORD,
    TK_CTL_VOICING,
    TK_CTL_STRUM,
    TK_CTL_VOICES,
    TK_CONTROL_COUNT
};

#define TK_TRACKS 16
#define TK_STEPS 128
#define TK_RECIPE_OVERRIDE (1u << TK_CONTROL_COUNT)

typedef struct {
    TkParams params;
    TkRecipe user;
    uint16_t helpers;
    int8_t selector_octave, selector_channel;
    uint8_t canopy_mix;
    bool override;
} TkTrackConfig;

typedef struct {
    uint16_t mask;
    int16_t values[TK_CONTROL_COUNT];
    TkRecipe user;
} TkStepLocks;

TkTrackConfig tk_track_defaults(void);
bool tk_get_control(const TkParams *params, enum TkControl control, int *value);
bool tk_set_control(TkParams *params, enum TkControl control, int value);
bool tk_set_lock(TkStepLocks *locks, enum TkControl control, int value);
void tk_clear_lock(TkStepLocks *locks, enum TkControl control);
bool tk_resolve_step(const TkTrackConfig *config, const TkStepLocks *locks, TkParams *params,
                     TkRecipe *user);

/* The firmware supplies masks for audio tracks, empty tracks and existing claims. */
bool tk_assign_helpers(TkTrackConfig *config, uint8_t owner, uint16_t request, uint16_t audio,
                       uint16_t empty, uint16_t already_reserved);

/* Versioned fixed-width record. Pattern adapters own association and transactions. */
#define TK_CONFIG_V1_BYTES 64
#define TK_CONFIG_BYTES 68
bool tk_config_encode(const TkTrackConfig *config, uint8_t *data, size_t size);
bool tk_config_decode(TkTrackConfig *config, const uint8_t *data, size_t size);

#endif
