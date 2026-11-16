/* CANOPY Sound associations held outside Sound memory (DTII 1.17).
 *
 * Each live kit-track Sound (129 kits of 16) and each Sound-pool entry (128)
 * has one fixed slot holding its CANOPY record extension, if any. The
 * association follows the stock operations that move whole Sounds: memcpy and
 * memset over complete Sounds, Sound loads and Sound initialization. Copies to
 * other memory (clipboard objects, undo buffers, stack temporaries) keep a
 * bounded set of temporary entries, each sealed with a hash of the Sound's
 * first SA_SEAL_BYTES as copied (everything but the slice table and sample
 * reference); a temporary is used only while those bytes still match, so a
 * reused or rewritten buffer never yields a stale extension. Eviction or a
 * mismatch loses the extension; it never changes Sound memory.
 *
 * Long copies and fills that touch neither the fixed slots nor a page holding
 * a temporary return after a few compares: stock moves large buffers often.
 *
 * Every slot write advances a sequence number, so the owner of a live Sound
 * can tell its own publication from a stock replacement. The caller
 * serializes all calls (interrupts masked). Nothing here allocates, blocks or
 * fails, and only the memory given to sa_init is written. */
#ifndef DK_CANOPY_SOUND_ASSOC_H
#define DK_CANOPY_SOUND_ASSOC_H
#include <stdint.h>
#include "sound-record.h"
enum {
    SA_SOUND=0x450, SA_SEAL_BYTES=0x140, SA_KIT=0x5574, SA_KIT_FIRST=0x34,
    SA_KITS=129, SA_TRACKS=16, SA_POOL=128, SA_SLOTS=SA_KITS*SA_TRACKS+SA_POOL
};
typedef struct {
    uint32_t seq;
    uint8_t present, pad[3];
    uint8_t payload[SR_BYTES];
} SaSlot;
typedef struct {
    uintptr_t addr;   /* 0: dead, removed at the end of the operation */
    uint32_t seal, stamp;
    uint8_t payload[SR_BYTES];
} SaTemp;
enum {
    SA_STAT_COPIES, SA_STAT_CLEARS, SA_STAT_LOADS, SA_STAT_INITS,
    SA_STAT_TEMP_HITS, SA_STAT_TEMP_STALE, SA_STAT_EVICTIONS, SA_STAT_PUTS,
    SA_STAT_PASSED,   /* long copies and fills that touched no Sound */
    SA_STATS
};
/* Temporaries are marked by 64 KiB page within a 128 MiB window around the
 * kit table; any outside it disables the page filter (far). */
enum { SA_WINDOW=0x8000000, SA_PAGE_SHIFT=16, SA_PAGE_WORDS=(SA_WINDOW>>SA_PAGE_SHIFT)/32 };
typedef struct {
    uintptr_t kits, pool, window;
    SaSlot *slot;
    SaTemp *temp;
    uint32_t temps, used, holes, far, clock;
    uint32_t pages[SA_PAGE_WORDS];
    uint32_t stats[SA_STATS];
} SaStore;
/* Bytes needed for the fixed slots; the remainder of the memory given to
 * sa_init becomes temporary entries. */
#define SA_SLOT_BYTES ((uint32_t)(SA_SLOTS*sizeof(SaSlot)))
/* Returns the number of temporary entries (0 if memory is too small). */
uint32_t sa_init(SaStore *s,uintptr_t kits,uintptr_t pool,void *memory,uint32_t bytes);
/* The fixed slot of the Sound starting at sound, or -1. */
int sa_slot(const SaStore *s,uintptr_t sound);
/* [dst, dst+len) was just copied from [src, src+len) (no overlap). */
void sa_copied(SaStore *s,uintptr_t dst,uintptr_t src,uint32_t len);
/* [dst, dst+len) was just filled with a constant. */
void sa_cleared(SaStore *s,uintptr_t dst,uint32_t len);
/* The Sound at sound was just loaded (payload: its record extension, or 0)
 * or initialized (0). */
void sa_replaced(SaStore *s,uintptr_t sound,const uint8_t *payload);
/* 1 and the payload if the Sound has an extension, else 0. *seq (optional)
 * receives a fixed slot's sequence number, 0 for other memory. */
int sa_get(SaStore *s,uintptr_t sound,uint8_t out[SR_BYTES],uint32_t *seq);
/* The owner of a fixed slot publishes its state; returns the new sequence
 * number, or 0 if sound has no fixed slot. */
uint32_t sa_put(SaStore *s,uintptr_t sound,const uint8_t payload[SR_BYTES]);
uint32_t sa_seq(const SaStore *s,uintptr_t sound);
uint32_t sa_seal(uintptr_t sound);
#endif
