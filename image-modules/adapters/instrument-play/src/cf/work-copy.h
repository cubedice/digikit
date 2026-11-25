/* CANOPY working copy: the last settings of each carrier track, kept in the settings store so that they come back after
 * a power cycle without a project save.
 *
 * The Sound extension (sound-record.h) travels with a saved project only: nothing writes an edit into what the firmware
 * loads at boot. The working copy holds one extension per track; control.c updates it whenever a track's bound state
 * changes, writes it a moment after the last change, and applies it to the first bind of that track after boot, while
 * the boot window is open. After the window (or once taken) a track follows its Sound as before, so a project loaded
 * later brings its own settings.
 *
 * Record: 'C' 'W', version 1, the number of tracks, then per track one byte (1 present) and the extension. */
#ifndef DK_CANOPY_WORK_COPY_H
#define DK_CANOPY_WORK_COPY_H
#include <stdint.h>
#include "sound-record.h"
enum {
    CW_TRACKS = 16,
    CW_HEADER = 4,
    CW_ENTRY = 1 + SR_BYTES,
    CW_BYTES = CW_HEADER + CW_TRACKS * CW_ENTRY
};
typedef struct {
    uint8_t present[CW_TRACKS];
    /* Set by cw_decode for every present track: the boot copy is still to be applied. */
    uint8_t pending[CW_TRACKS];
    uint8_t payload[CW_TRACKS][SR_BYTES];
    /* Changed since the last cw_encode that was written. */
    uint8_t dirty;
} CwStore;
/* Byte loops through volatile: this code links without libc and must not turn into a memcpy call. */
static inline void cw_copy(uint8_t *to, const uint8_t *from, uint32_t n)
{
    for (uint32_t i = 0; i < n; ++i)
        ((volatile uint8_t *)to)[i] = from[i];
}
static inline void cw_clear(CwStore *w)
{
    for (uint32_t i = 0; i < sizeof(*w); ++i)
        ((volatile uint8_t *)w)[i] = 0;
}
/* 0 on success; anything else leaves an empty store. */
static inline int cw_decode(CwStore *w, const uint8_t *in, uint32_t n)
{
    cw_clear(w);
    if (!in || n != CW_BYTES || in[0] != 'C' || in[1] != 'W' || in[2] != 1 || in[3] != CW_TRACKS)
        return -1;
    for (uint32_t t = 0; t < CW_TRACKS; ++t) {
        const uint8_t *e = in + CW_HEADER + t * CW_ENTRY;
        if (e[0] != 1)
            continue;
        w->present[t] = w->pending[t] = 1;
        cw_copy(w->payload[t], e + 1, SR_BYTES);
    }
    return 0;
}
static inline void cw_encode(const CwStore *w, uint8_t out[CW_BYTES])
{
    out[0] = 'C';
    out[1] = 'W';
    out[2] = 1;
    out[3] = CW_TRACKS;
    for (uint32_t t = 0; t < CW_TRACKS; ++t) {
        uint8_t *e = out + CW_HEADER + t * CW_ENTRY;
        e[0] = w->present[t] ? 1 : 0;
        for (uint32_t i = 0; i < SR_BYTES; ++i)
            ((volatile uint8_t *)e)[1 + i] = w->present[t] ? w->payload[t][i] : 0;
    }
}
/* The state of TRACK is PAYLOAD now. 1 when that changed the copy (then it is dirty). */
static inline int cw_set(CwStore *w, uint32_t track, const uint8_t payload[SR_BYTES])
{
    if (track >= CW_TRACKS)
        return 0;
    int same = w->present[track];
    for (uint32_t i = 0; same && i < SR_BYTES; ++i)
        same = w->payload[track][i] == payload[i];
    if (same)
        return 0;
    cw_copy(w->payload[track], payload, SR_BYTES);
    w->present[track] = 1;
    w->dirty = 1;
    return 1;
}
/* The boot copy of TRACK, if it is still to be applied: 1 and OUT filled. It stays pending until cw_taken. */
static inline int cw_boot(const CwStore *w, uint32_t track, uint8_t out[SR_BYTES])
{
    if (track >= CW_TRACKS || !w->pending[track])
        return 0;
    cw_copy(out, w->payload[track], SR_BYTES);
    return 1;
}
static inline void cw_taken(CwStore *w, uint32_t track)
{
    if (track < CW_TRACKS)
        w->pending[track] = 0;
}
/* The boot window is over: no track takes its boot copy any more. */
static inline void cw_expire(CwStore *w)
{
    for (uint32_t t = 0; t < CW_TRACKS; ++t)
        w->pending[t] = 0;
}
#endif
