/* Unlinked host experiment: never install on hardware. */
#include "raw-store.h"

static uint32_t record_offset(uint32_t i)
{
    return 0x1cu + 2u * (i < 8 ? 4u * (i + 1u) : 77u + i - 8u);
}
static void gather(const uint8_t *record, uint8_t bytes[SC_BYTES])
{
    for (uint32_t i = 0; i < 21; ++i) {
        uint32_t at = record_offset(i);
        bytes[2 * i] = record[at];
        bytes[2 * i + 1] = record[at + 1];
    }
}
CrsKind crs_classify(const uint8_t bytes[SC_BYTES])
{
    uint8_t any = 0;
    for (uint32_t i = 0; i < SC_BYTES; ++i)
        any |= bytes[i];
    if (!any)
        return CRS_EMPTY;
    if (bytes[0] != 'C' || bytes[1] != 'S')
        return CRS_FOREIGN;
    if (bytes[2] != 1)
        return CRS_UNKNOWN_VERSION;
    uint16_t crc = 0xffff;
    for (uint32_t i = 0; i < 40; ++i) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (uint32_t j = 0; j < 8; ++j)
            crc = (uint16_t)((crc << 1) ^ ((crc & 0x8000) ? 0x1021 : 0));
    }
    return bytes[40] == (uint8_t)(crc >> 8) && bytes[41] == (uint8_t)crc ? CRS_CURRENT
                                                                         : CRS_CORRUPT;
}
int crs_begin(CrsBatch *batch, SpPool *pool, uint32_t max_records)
{
    uint16_t first;
    if (!batch || !pool || batch->pool || !max_records || max_records > CRS_PROJECT_LIMIT)
        return -1;
    if (sp_reserve_batch_task(pool, max_records, &first))
        return -1;
    batch->pool = pool;
    batch->next = first;
    batch->end = (uint32_t)first + max_records;
    return 0;
}
int crs_attach(CrsBatch *batch, const uint8_t *record, uint32_t length, uint16_t *handle,
               CrsKind *kind)
{
    if (!batch || !batch->pool || !record || length < CRS_RECORD_BYTES || !handle || !kind)
        return -1;
    uint8_t bytes[SC_BYTES];
    gather(record, bytes);
    CrsKind found = crs_classify(bytes);
    uint16_t selected = 0;
    if (found != CRS_EMPTY) {
        if (batch->next >= batch->end)
            return -1;
        selected = (uint16_t)batch->next;
        if (sp_publish(batch->pool, selected, bytes))
            return -1;
        ++batch->next;
    }
    *handle = selected;
    *kind = found;
    return 0;
}
int crs_end(CrsBatch *batch)
{
    if (!batch || !batch->pool)
        return -1;
    if (batch->next < batch->end &&
        sp_cancel_reserved_tail(batch->pool, (uint16_t)batch->next, batch->end - batch->next))
        return -1;
    batch->pool = 0;
    batch->next = 0;
    batch->end = 0;
    return 0;
}
int crs_write(const SpPool *pool, uint16_t handle, uint8_t *record, uint32_t length)
{
    if (!pool || !record || length < CRS_RECORD_BYTES)
        return -1;
    if (!handle)
        return 0;
    uint8_t bytes[SC_BYTES];
    if (sp_read(pool, handle, bytes))
        return -1;
    for (uint32_t i = 0; i < 21; ++i) {
        uint32_t at = record_offset(i);
        record[at] = bytes[2 * i];
        record[at + 1] = bytes[2 * i + 1];
    }
    return 0;
}
