#ifndef DK_TRACK_RECORD_H
#define DK_TRACK_RECORD_H
#include "track-service.h"

/* DTII 1.17 v4 pattern record. The stock loader skips lock entries with
 * both header bytes set to 0xff. Only their unreferenced payloads are used. */
#define TK_NATIVE_RECORD_BYTES 0x15c00u
#define TK_NATIVE_LOCKS_OFFSET 0x10a04u
#define TK_NATIVE_LOCKS 80u
#define TK_NATIVE_LOCK_STRIDE 258u
#define TK_NATIVE_TAIL_OFFSET 0x15accu
#define TK_NATIVE_TAIL_PAYLOAD (TK_NATIVE_RECORD_BYTES - TK_NATIVE_TAIL_OFFSET - 6u)

bool tk_record_write(uint8_t *record, const uint8_t *data, size_t size);
size_t tk_record_read(const uint8_t *record, uint8_t *data, size_t capacity);

#endif
