#if defined(__m68k__) && defined(__GNUC__)
/* No implicit memcpy/memset calls: this object links without libc. */
#pragma GCC optimize("no-tree-loop-distribute-patterns")
#endif
#include "sound-record.h"
/* Field widths cover every value cp_valid accepts: both source banks, the
 * remembered FM bank, the family, keyboard mode and the effective shape. */
static const uint8_t control_bits[CP_CONTROLS] = {4, 2,  7, 7,  9, 7, 4, 3, 3, 7, 7, 2, 0, 0, 7, 7,
                                                  3, 10, 7, 10, 2, 2, 1, 8, 7, 7, 8, 8, 7, 4, 8, 8};
static const uint8_t fm_bits[13] = {7, 2, 7, 4, 4, 7, 7, 7, 7, 7, 8, 8, 8};
enum { DATA_AT = 1, CRC_AT = 38, DATA_BITS = (CRC_AT - DATA_AT) * 8 };
static uint16_t crc(const uint8_t *p)
{
    uint16_t c = 0xffff;
    for (uint32_t i = 0; i < CRC_AT; ++i) {
        c ^= (uint16_t)p[i] << 8;
        for (uint32_t j = 0; j < 8; ++j)
            c = (uint16_t)((c << 1) ^ ((c & 0x8000) ? 0x1021 : 0));
    }
    return c;
}
static void put(uint8_t *b, uint32_t *at, uint32_t value, uint32_t width)
{
    for (uint32_t i = width; i > 0; --i) {
        uint32_t bit = (*at)++;
        b[DATA_AT + bit / 8] |= (uint8_t)(((value >> (i - 1)) & 1u) << (7 - bit % 8));
    }
}
static uint32_t get(const uint8_t *b, uint32_t *at, uint32_t width)
{
    uint32_t value = 0;
    for (uint32_t i = 0; i < width; ++i) {
        uint32_t bit = (*at)++;
        value = (value << 1) | ((b[DATA_AT + bit / 8] >> (7 - bit % 8)) & 1u);
    }
    return value;
}
int sr_encode(const CpPreset *p, uint8_t out[SR_BYTES])
{
    if (!p || !out || !cp_valid(p) || p->clock != CP_ARP_CLOCK || p->bpm != CP_ARP_BPM_RESERVED)
        return -1;
    uint8_t b[SR_BYTES];
    for (uint32_t i = 0; i < SR_BYTES; ++i)
        b[i] = 0;
    b[0] = SR_FORMAT;
    uint32_t at = 0;
    for (uint32_t i = 0; i < CP_CONTROLS; ++i)
        put(b, &at, p->control[i], control_bits[i]);
    for (uint32_t i = 0; i < 13; ++i)
        put(b, &at, p->fm[i], fm_bits[i]);
    put(b, &at, p->family, 2);
    put(b, &at, p->mode, 1);
    put(b, &at, p->shape.count, 3);
    for (uint32_t i = 0; i < 4; ++i)
        put(b, &at, p->shape.intervals[i], 7);
    if (at > DATA_BITS)
        return -1;
    uint16_t sum = crc(b);
    b[CRC_AT] = (uint8_t)(sum >> 8);
    b[CRC_AT + 1] = (uint8_t)sum;
    for (uint32_t i = 0; i < SR_BYTES; ++i)
        out[i] = b[i];
    return 0;
}
int sr_decode(CpPreset *out, const uint8_t in[SR_BYTES])
{
    if (!out || !in || in[0] != SR_FORMAT)
        return -1;
    uint16_t sum = crc(in);
    if (in[CRC_AT] != (uint8_t)(sum >> 8) || in[CRC_AT + 1] != (uint8_t)sum)
        return -1;
    CpPreset p;
    for (uint32_t i = 0; i < sizeof(p); ++i)
        ((volatile uint8_t *)&p)[i] = 0;
    uint32_t at = 0;
    for (uint32_t i = 0; i < CP_CONTROLS; ++i)
        p.control[i] = (uint16_t)get(in, &at, control_bits[i]);
    for (uint32_t i = 0; i < 13; ++i)
        p.fm[i] = (uint16_t)get(in, &at, fm_bits[i]);
    p.family = (uint8_t)get(in, &at, 2);
    p.mode = (uint8_t)get(in, &at, 1);
    p.shape.count = (uint8_t)get(in, &at, 3);
    for (uint32_t i = 0; i < 4; ++i)
        p.shape.intervals[i] = (uint8_t)get(in, &at, 7);
    while (at < DATA_BITS)
        if (get(in, &at, 1))
            return -1;
    p.clock = CP_ARP_CLOCK;
    p.bpm = CP_ARP_BPM_RESERVED;
    if (!cp_valid(&p))
        return -1;
    for (uint32_t i = 0; i < sizeof(p); ++i)
        ((volatile uint8_t *)out)[i] = ((const volatile uint8_t *)&p)[i];
    return 0;
}
int sr_present(const uint8_t in[SR_BYTES])
{
    return in && (((in[0] & 0xf0) == 0xc0 && in[0] != 0xc0) ||
                  (in[0] == 0xd1 && in[1] == 'R' && in[2] == 'L' && in[3] == 'Y'));
}
uint32_t sr_offset(uint32_t i)
{
    uint32_t position = i < 7 ? 4 * (i + 1) : 77 + (i - 7);
    return 0x1c + 2 * position;
}
void sr_gather(const uint8_t *record, uint8_t out[SR_BYTES])
{
    for (uint32_t i = 0; i < SR_SLOTS; ++i) {
        uint32_t at = sr_offset(i);
        out[2 * i] = record[at];
        out[2 * i + 1] = record[at + 1];
    }
}
void sr_scatter(uint8_t *record, const uint8_t in[SR_BYTES])
{
    for (uint32_t i = 0; i < SR_SLOTS; ++i) {
        uint32_t at = sr_offset(i);
        record[at] = in[2 * i];
        record[at + 1] = in[2 * i + 1];
    }
}
