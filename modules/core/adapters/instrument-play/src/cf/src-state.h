/* Pure SRC control model. Input routing supplies an ownership token captured
 * for the view; stale events cannot mutate a new page, track or instance. */
#ifndef DK_CANOPY_SRC_STATE_H
#define DK_CANOPY_SRC_STATE_H
#include "preset.h"
enum { CS_TUNE=32, CS_LEVEL=33, CS_WAVE=34, CS_CLOCK=35, CS_BPM=36, CS_NONE=255, CS_FOCUS_MS=750, CS_LIST_TICKS=4 };
enum { CS_STALE=-1, CS_READONLY=0, CS_EDIT=1, CS_NATIVE=2 };
typedef struct {
    uint32_t generation, track, token, revision, pending, focus_at, metadata;
    uint16_t value[CP_CONTROLS];
    uint8_t page[16], visible, mode, clock, learned, modal, focus, focus_id;
    uint32_t native_tune, native_tune_pending;
    /* Signed panel movement, not accelerated parameter increments. Keep each
     * encoder's partial list step separate from other knobs and contexts. */
    int8_t list_delta[8];
    uint8_t list_id[8];
    uint16_t legacy_color, wavetable[12], fm[13];
} CsState;
/* Caller owns serial access. No retained view pointer or I/O. */
void cs_init(CsState *);
/* Import setup/preset/MIDI-selector changes into the same requested bank. */
void cs_sync(CsState *,const uint16_t values[CP_CONTROLS]);
void cs_import(CsState *,const CpPreset *);
void cs_metadata(CsState *,uint32_t metadata);
void cs_touch(CsState *);
void cs_native_tune(CsState *,uint32_t raw);
uint32_t cs_context(CsState *,uint32_t track,uint32_t generation,int visible,
    uint32_t page,uint32_t mode,uint32_t clock,int learned,int modal);
uint32_t cs_control(uint32_t page,uint32_t encoder);
uint32_t cs_pages(const CsState *);
uint32_t cs_mapped_control(const CsState *,uint32_t encoder);
/* View-only choices. PLAY is followed by the twelve persisted chord types;
 * Velvet/Reed are followed by the five persisted wavetable identifiers. */
uint32_t cs_choice_value(const CsState *,uint32_t control);
uint32_t cs_choice_max(const CsState *,uint32_t control);
int cs_edit(CsState *,uint32_t token,uint32_t encoder,int32_t detents,uint32_t now_ms,uint32_t *id);
int cs_focused(const CsState *,uint32_t now_ms);
/* A complete effective bank from the DSP, never a transport receipt. Values
 * must match before a pending marker is cleared; delayed ACKs are harmless. */
void cs_applied(CsState *,uint32_t generation,uint32_t revision,const uint32_t values[32],uint32_t tune);
#endif
