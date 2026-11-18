/* Host test of the CANOPY working copy (instrument-play work-copy.h): the record, the change detection and the rule
 * that a boot copy is applied once. */
#include <assert.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/work-copy.h"

int main(void)
{
    CwStore w;
    uint8_t a[SR_BYTES], b[SR_BYTES], out[SR_BYTES], record[CW_BYTES], again[CW_BYTES];
    for (uint32_t i = 0; i < SR_BYTES; ++i) {
        a[i] = (uint8_t)(0x10 + i);
        b[i] = (uint8_t)(0x80 + i);
    }
    /* Nothing stored: an empty store, nothing pending, and a wrong record is refused whole. */
    assert(cw_decode(&w, 0, 0) < 0 && !w.dirty);
    memset(record, 0x5a, sizeof(record));
    assert(cw_decode(&w, record, sizeof(record)) < 0);
    for (uint32_t t = 0; t < CW_TRACKS; ++t)
        assert(!w.present[t] && !cw_boot(&w, t, out));

    /* A change makes the copy dirty; the same state again does not; an invalid track is ignored. */
    assert(cw_set(&w, 3, a) == 1 && w.dirty);
    w.dirty = 0;
    assert(cw_set(&w, 3, a) == 0 && !w.dirty);
    assert(cw_set(&w, 10, b) == 1 && cw_set(&w, CW_TRACKS, b) == 0);
    /* A state set in this session is not a boot copy. */
    assert(!cw_boot(&w, 3, out));

    /* The record round-trips; after a decode the stored tracks, and only they, have a boot copy. */
    cw_encode(&w, record);
    assert(record[0] == 'C' && record[1] == 'W' && record[2] == 1 && record[3] == CW_TRACKS);
    CwStore r;
    assert(!cw_decode(&r, record, sizeof(record)) && !r.dirty);
    cw_encode(&r, again);
    assert(!memcmp(record, again, sizeof(record)));
    assert(cw_boot(&r, 3, out) && !memcmp(out, a, SR_BYTES));
    assert(cw_boot(&r, 10, out) && !memcmp(out, b, SR_BYTES));
    assert(!cw_boot(&r, 0, out) && !cw_boot(&r, CW_TRACKS, out));

    /* A boot copy stays until it is taken, then it is gone; the other track keeps its own until the window closes. */
    assert(cw_boot(&r, 3, out));
    cw_taken(&r, 3);
    assert(!cw_boot(&r, 3, out) && cw_boot(&r, 10, out));
    cw_expire(&r);
    assert(!cw_boot(&r, 10, out));
    /* Taking or expiring changes nothing that is stored. */
    cw_encode(&r, again);
    assert(!memcmp(record, again, sizeof(record)) && !r.dirty);

    /* A truncated record is refused. */
    assert(cw_decode(&r, record, sizeof(record) - 1) < 0 && !r.present[3]);
    return 0;
}
