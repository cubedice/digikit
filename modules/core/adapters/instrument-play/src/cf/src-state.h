/* Pure SRC control model. Input routing supplies an ownership token captured
 * for the view; stale events cannot mutate a new page, track or instance. */
#ifndef DK_CANOPY_SRC_STATE_H
#define DK_CANOPY_SRC_STATE_H
#include "preset.h"
enum { CS_TUNE=16, CS_LEVEL=17, CS_NONE=255, CS_FOCUS_MS=750 };
enum { CS_STALE=-1, CS_READONLY=0, CS_EDIT=1, CS_NATIVE=2 };
typedef struct {
    uint32_t generation, track, token, revision, pending, focus_at, metadata;
    uint16_t value[CP_CONTROLS];
    uint8_t page[16], visible, mode, clock, learned, modal, focus, focus_id;
} CsState;
/* Caller owns serial access. No retained view pointer or I/O. */
void cs_init(CsState *);
/* Import setup/preset/MIDI-selector changes into the same requested bank. */
void cs_sync(CsState *,const uint16_t values[CP_CONTROLS]);
void cs_metadata(CsState *,uint32_t metadata);
uint32_t cs_context(CsState *,uint32_t track,uint32_t generation,int visible,
    uint32_t page,uint32_t mode,uint32_t clock,int learned,int modal);
uint32_t cs_control(uint32_t page,uint32_t encoder);
int cs_edit(CsState *,uint32_t token,uint32_t encoder,int32_t detents,uint32_t now_ms,uint32_t *id);
int cs_focused(const CsState *,uint32_t now_ms);
/* A complete effective bank from the DSP, never a transport receipt. Values
 * must match before a pending marker is cleared; delayed ACKs are harmless. */
void cs_applied(CsState *,uint32_t generation,uint32_t revision,const uint32_t values[32]);
#endif
