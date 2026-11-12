/* Up to IP_INSTANCES admitted instruments, one per kit track. Instance i is
 * DSP slot i and registry instance i; the CF binds it to a track by a reset
 * that carries the slot hint (mi_instance_bind). */
#ifndef DK_INSTRUMENT_INSTANCES_H
#define DK_INSTRUMENT_INSTANCES_H
#include <stdint.h>
#define IP_INSTANCES 4u
#define IP_NONE 0xffffffffu
/* sound.c: the track each instance is admitted for (IP_NONE when free). The
 * lowest tracks whose Sound carries the instrument type win; an instance keeps
 * its track while the track still qualifies. ip_track is the lowest of them. */
extern volatile uint32_t ip_tracks[IP_INSTANCES];
extern volatile uint32_t ip_track;
/* Per instance: state, source, refresh count, pattern, invalidations, type,
 * active in-place loads, observed loads (8 words each; [0..7] is instance 0). */
extern volatile uint32_t ip_admission[IP_INSTANCES * 8];
void instrument_sound_scan(void);
int instrument_sound_prepare(uint32_t instance, int allow_refresh);
uint32_t instrument_sound_tune(uint32_t instance);
/* 1 when an admitted instance currently owns this track's Sound. */
int instrument_sound_admitted(uint32_t track);
/* control.c */
void instrument_sound_suspend(uint32_t instance);
/* The UI's selected track (SRC page or KEYBOARD view). Menus, presets, panel
 * keys and the DSP status focus follow the instance on that track. */
void instrument_select_track(uint32_t track);
/* 1 when an instance is bound to this track and owns its UI and keys. */
int instrument_track_owned(uint32_t track);
#endif
