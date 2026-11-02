/* Normalized instrument UI messages. The link adapter must authenticate and
 * demultiplex a message before calling this receiver; these are not MIDI ACKs.
 * All storage is owned here. No module, view or DMA pointer survives a call. */
#ifndef DK_INSTRUMENT_STATUS_H
#define DK_INSTRUMENT_STATUS_H
#include <stdint.h>
enum { IS_WORDS=12, IS_DETAIL_WORDS=64, IS_FRAGMENTS=11, IS_TIMEOUT_MS=150 };
enum { IS_FAST=0, IS_DETAIL=1 };
typedef struct {
    uint32_t generation, track, mode, session;
    uint32_t fast[IS_WORDS], detail[IS_DETAIL_WORDS], assembly[IS_DETAIL_WORDS];
    uint32_t fast_at, detail_at, started_at, retry_at;
    uint32_t assembly_revision, assembly_control, assembly_block, fragments;
    uint32_t detail_revision, detail_control, capture_revision;
    uint8_t fast_valid, detail_valid, assembling, retried;
} IsStatus;
void is_select(IsStatus *,uint32_t track,uint32_t generation,uint32_t mode,uint32_t session);
/* Return 1 only on publication; 0 for accepted partials; -1 for rejected input.
 * Rejections never make old data fresh or partially replace published state. */
int is_receive(IsStatus *,const uint32_t words[IS_WORDS],uint32_t now_ms);
int is_fresh(const IsStatus *,uint32_t now_ms);
int is_detail_confirmed(const IsStatus *,uint32_t now_ms);
/* Once per 150 ms while a new matching detail is needed. Caller sends a normal
 * control-stream request; this function performs no I/O and never ACKs audio. */
int is_retry(IsStatus *,uint32_t now_ms);
#endif
