#include "track-canopy.h"

#include "track-midi.h"
#include "track-sequence.h"

#include "../../instrument-play/src/cf/src-control.h"

extern uint32_t dkrt_instrument_type(void);
extern int instrument_track_owned(uint32_t track);

bool tk_canopy_is_owner(uint8_t track, int type)
{
    return type > 0 && (uint32_t)type == dkrt_instrument_type() && instrument_track_owned(track);
}

static bool legacy(uint8_t track, TkTrackConfig *out)
{
    CpPreset saved;
    if (!instrument_track_keyboard_legacy(track, &saved) || !cp_valid(&saved))
        return false;
    static const uint8_t order[] = {TK_ORDER_UP, TK_ORDER_DOWN, TK_ORDER_UPDN, TK_ORDER_PLAY,
                                    TK_ORDER_RANDOM};
    static const uint8_t rate[] = {3, 6, 8, 9, TK_RATE_5Q, 11, TK_RATE_7Q, 12};
    const uint16_t *p = saved.control;
    TkTrackConfig config = tk_track_defaults();
    config.canopy_mix = (uint8_t)p[CP_ARP_MIX];
    config.params.poly = config.canopy_mix < 127;
    config.params.arp = config.canopy_mix != 0;
    config.params.order = order[p[CP_ORDER]];
    config.params.rate = rate[p[CP_RATE] - 1];
    config.params.gate = (uint8_t)p[CP_GATE];
    config.params.octaves = (int8_t)p[CP_OCTAVES] - 3;
    config.params.legacy_swing = (uint8_t)p[CP_SWING];
    unsigned swing = 50u + p[CP_SWING] / 2u;
    config.params.swing = (uint8_t)(swing > 75 ? 75 : swing);
    config.params.reset = (uint8_t)p[CP_RESTART];
    config.params.strum_ms = p[CP_STRUM];
    if (saved.mode == KB_ROOT) {
        config.params.chord = (uint8_t)p[CP_CHORD] + 1u;
        config.params.voicing = (uint8_t)p[CP_VOICING];
        if (config.params.chord == TK_CHORD_USER) {
            config.user.count = saved.shape.count;
            for (uint8_t i = 0; i < saved.shape.count; ++i)
                config.user.notes[i].interval = (int8_t)saved.shape.intervals[i];
        }
    }
    *out = config;
    return true;
}

static uint16_t routed;
volatile uint16_t tk_canopy_fallback;

static bool available(uint8_t track)
{
    return (routed & (1u << track)) != 0;
}

static bool send(void *unused, uint8_t track, const TkNoteEvent *event)
{
    (void)unused;
    return instrument_track_keyboard_note(track, event->token, event->pitch, event->velocity,
                                          event->on, event->lane) != 0;
}

void tk_canopy_backend(TkTrackBackend *out)
{
    *out = (TkTrackBackend){
        .output = send, .voices = 4, .blend = true, .inherit = legacy, .available = available};
}

void tk_canopy_sync(uint8_t track, bool active, uint32_t pattern)
{
    const TkTrackConfig *config = active ? tk_service_active_config(track) : NULL;
    unsigned lanes = 0;
    bool fallback = config && config->override && tk_sequence_canopy_legacy_locks(pattern, track);
    uint16_t bit = (uint16_t)(1u << track);
    if (fallback)
        tk_canopy_fallback |= bit;
    else
        tk_canopy_fallback &= (uint16_t)~bit;
    if (config && config->override && !fallback) {
        lanes = config->params.poly | (unsigned)config->params.arp << 1;
        if (!lanes)
            lanes = 1;
    }
    if (!lanes && (routed & bit)) {
        tk_midi_cancel_track(track);
        tk_service_cancel_track(track);
    }
    if (lanes)
        routed |= bit;
    else
        routed &= (uint16_t)~bit;
    instrument_src_track_keyboard(track, active);
    instrument_track_keyboard_lanes(track, lanes);
}
