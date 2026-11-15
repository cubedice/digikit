#ifndef DK_INSTRUMENT_SRC_CONTROL_H
#define DK_INSTRUMENT_SRC_CONTROL_H
#include "src-display.h"
#include "src-lock-event.h"
/* UI-task bridge. The admitted carrier is the track in ip_track. Context
 * must be refreshed on selection/mode/page boundaries before dispatching input.
 * Neither this bridge nor the model retains borrowed firmware view pointers. */
uint32_t instrument_src_context(uint32_t track,uint32_t page,int visible);
uint32_t instrument_src_pages(void);
uint32_t instrument_src_tune_page(uint32_t page);
int instrument_src_encoder(uint32_t token,uint32_t encoder,int32_t detents,uint32_t now_ms);
void instrument_src_display(CvDisplay *,uint32_t now_ms,const char *tune,const char *level);
void instrument_src_feedback(char out[32],uint32_t encoder);
/* Packed trig locks. The ID and base of the control under `encoder`
 * in the UI instance: CS_EDIT when it can be locked, CS_NATIVE for TUNE/LEV
 * (stock locks), CS_STALE before initialization, else CS_READONLY. */
int instrument_src_lock_target(uint32_t encoder,const IlpSnapshot *snapshot,uint32_t *control,uint32_t *id,uint32_t *base);
/* cs_lock_step on the UI instance. */
int instrument_src_lock_step(uint32_t token,uint32_t encoder,int32_t detents,uint32_t now_ms,
    const IlpSnapshot *snapshot,uint32_t current,uint32_t *next);
void instrument_src_lock_feedback(char out[32],uint32_t encoder,const IlpSnapshot *snapshot,uint32_t control,uint32_t value);
/* instrument_src_display showing the held trig's absolute packed locks. */
void instrument_src_display_locked(CvDisplay *,uint32_t now_ms,const char *tune,const char *level,
    const IlpSnapshot *snapshot);
void instrument_src_preview_step(CvDisplay *,const IlpSnapshot *,int32_t note);
/* Drain telemetry on the UI task independently of the paint cadence. */
void instrument_src_poll(uint32_t now_ms);
/* Reserved for the authenticated status demultiplexer, not registry ACKs. */
int instrument_src_receive(const uint32_t words[IS_WORDS],uint32_t now_ms);
#endif
