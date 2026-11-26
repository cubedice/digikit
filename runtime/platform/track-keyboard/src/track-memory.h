#ifndef DK_TRACK_MEMORY_H
#define DK_TRACK_MEMORY_H

#include <stddef.h>
#include <stdbool.h>

#if defined(__m68k__)
/* CF expansion objects bind these routines to the stock firmware. */
void *memset(void *destination, int value, size_t size);
void *memcpy(void *destination, const void *source, size_t size);
#else
#include <string.h>
#endif

static inline bool tk_bytes_equal(const void *left, const void *right, size_t size)
{
    const unsigned char *a = left, *b = right;
    for (size_t i = 0; i < size; ++i)
        if (a[i] != b[i])
            return false;
    return true;
}

#endif
