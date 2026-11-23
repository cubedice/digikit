#include "track-record.h"

#include "track-memory.h"

static bool available(const uint8_t *lock)
{
    return lock[0] == 0xff && lock[1] == 0xff;
}

static size_t payload_capacity(const uint8_t *record)
{
    size_t size = TK_NATIVE_TAIL_PAYLOAD;
    const uint8_t *lock = record + TK_NATIVE_LOCKS_OFFSET;
    for (unsigned i = 0; i < TK_NATIVE_LOCKS; ++i, lock += TK_NATIVE_LOCK_STRIDE)
        if (available(lock))
            size += TK_NATIVE_LOCK_STRIDE - 2;
    return size;
}

bool tk_record_write(uint8_t *record, const uint8_t *data, size_t size)
{
    uint8_t *tail = record + TK_NATIVE_TAIL_OFFSET;
    bool ours = tk_bytes_equal(tail, "TKR1", 4);
    if (!size) {
        if (ours)
            memset(tail, 0, TK_NATIVE_RECORD_BYTES - TK_NATIVE_TAIL_OFFSET);
        return true;
    }
    if (!data || size > TK_PATTERN_BYTES || size > payload_capacity(record))
        return false;
    if (!ours) {
        for (unsigned i = 0; i < TK_NATIVE_RECORD_BYTES - TK_NATIVE_TAIL_OFFSET; ++i)
            if (tail[i])
                return false;
    }
    memset(tail, 0, TK_NATIVE_RECORD_BYTES - TK_NATIVE_TAIL_OFFSET);
    size_t head = size < TK_NATIVE_TAIL_PAYLOAD ? size : TK_NATIVE_TAIL_PAYLOAD;
    memcpy(tail + 6, data, head);
    size_t rest = size - head;
    const uint8_t *from = data + head;
    uint8_t *lock = record + TK_NATIVE_LOCKS_OFFSET;
    for (unsigned i = 0; i < TK_NATIVE_LOCKS && rest; ++i, lock += TK_NATIVE_LOCK_STRIDE) {
        if (!available(lock))
            continue;
        size_t n = rest < TK_NATIVE_LOCK_STRIDE - 2 ? rest : TK_NATIVE_LOCK_STRIDE - 2;
        memcpy(lock + 2, from, n);
        from += n;
        rest -= n;
    }
    tail[4] = (uint8_t)(size >> 8);
    tail[5] = (uint8_t)size;
    memcpy(tail, "TKR1", 4);
    return true;
}

size_t tk_record_read(const uint8_t *record, uint8_t *data, size_t capacity)
{
    const uint8_t *tail = record + TK_NATIVE_TAIL_OFFSET;
    if (!tk_bytes_equal(tail, "TKR1", 4))
        return 0;
    size_t size = (size_t)tail[4] << 8 | tail[5];
    if (size < TK_PATTERN_HEADER || size > capacity || size > payload_capacity(record))
        return 0;
    size_t head = size < TK_NATIVE_TAIL_PAYLOAD ? size : TK_NATIVE_TAIL_PAYLOAD;
    memcpy(data, tail + 6, head);
    size_t rest = size - head;
    uint8_t *to = data + head;
    const uint8_t *lock = record + TK_NATIVE_LOCKS_OFFSET;
    for (unsigned i = 0; i < TK_NATIVE_LOCKS && rest; ++i, lock += TK_NATIVE_LOCK_STRIDE) {
        if (!available(lock))
            continue;
        size_t n = rest < TK_NATIVE_LOCK_STRIDE - 2 ? rest : TK_NATIVE_LOCK_STRIDE - 2;
        memcpy(to, lock + 2, n);
        to += n;
        rest -= n;
    }
    return size;
}
