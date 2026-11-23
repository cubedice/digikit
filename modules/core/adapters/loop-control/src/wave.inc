/* Six 128-bin circular histories of the sound recorded, with 512 through 16384
 * frames per bin, doubling each level. A cell stores negative and positive peaks.
 * The block interrupt adds at most one completed interval per reply. A capture
 * copies just the selected 256-byte history, before any post-edge audio enters it.
 * The UI takes a bounded snapshot, restores interrupts and projects it to the LCD. */
enum { WAVE_BINS = 128, WAVE_LEVELS = 6, WAVE_FIRST_SHIFT = 9 };
typedef struct {
    u32 end, frames, valid, shift;
    u16 bins[WAVE_BINS];
} WaveView;
static u16 wave_ring[WAVE_LEVELS][WAVE_BINS];
static WaveView wave_loop;
static u32 wave_end, wave_valid, wave_count, wave_length;

static u16 wave_merge(u16 a, u16 b)
{
    unsigned down = (a & 255) > (b & 255) ? a & 255 : b & 255;
    return (u16)(((a > b ? a : b) & 0xff00) | down);
}

static u16 wave_decode(u16 peaks, u32 state)
{
    if (state & LS_WAVE_PRECISE)
        return peaks;
    /* Older engines send four nibbles. Combine channels before expanding 0..15 to 0..255. */
    unsigned down = peaks & 15, up = (peaks >> 4) & 15;
    if (((peaks >> 8) & 15u) > down)
        down = (peaks >> 8) & 15;
    if ((unsigned)(peaks >> 12) > up)
        up = peaks >> 12;
    return (u16)(((up << 8) | down) * 17);
}

static void wave_add(u32 end, u16 peak)
{
    int contiguous = end - wave_end == 512 && wave_valid;
    wave_valid = contiguous ? wave_valid + (wave_valid < (1u << 21) ? 512 : 0) : 512;
    wave_end = end;
    for (u32 i = 0, shift = WAVE_FIRST_SHIFT; i < WAVE_LEVELS; ++i, ++shift) {
        u16 *cell = &wave_ring[i][((end - 1) >> shift) & (WAVE_BINS - 1)];
        if (contiguous && ((end - 512) & ((1u << shift) - 1)))
            peak = wave_merge(peak, *cell);
        *cell = peak;
    }
}

static void wave_snapshot(WaveView *v, u32 end, u32 length, u32 valid)
{
    /* Leave one slot for unaligned endpoints. */
    u32 i = 0;
    while (i + 1 < WAVE_LEVELS && length > ((WAVE_BINS - 1u) << (WAVE_FIRST_SHIFT + i)))
        ++i;
    v->shift = WAVE_FIRST_SHIFT + i;
    v->end = end;
    v->frames = length;
    v->valid = valid < length ? valid : length;
    for (u32 j = 0; j < WAVE_BINS; ++j)
        v->bins[j] = wave_ring[i][j];
}

static void wave_accept(const u32 *w)
{
    u32 t = w[LS_WAVE_T], end = t & ~511u;
    u32 edge = w[LS_WAVE_EDGE], count = w[LS_STATE] >> LS_COUNT_SHIFT;
    int capture = (w[LS_STATE] & LS_LOOP) && (count != wave_count || !wave_length);
    /* A lease change or module restart begins a new rolling history. Natural
     * 32-bit wrapping advances by a small unsigned delta and keeps continuity. */
    if (t - wave_end > (1u << 21)) {
        wave_end = end;
        wave_valid = 0;
    }
    if (end != wave_end && (!capture || (int32_t)(end - edge) <= 0))
        wave_add(end, wave_decode((u16)w[LS_WAVE_PEAK], w[LS_STATE]));
    if (capture) {
        u32 span = w[LS_LOOP_FRAMES], silent = w[LS_WAVE_SILENT];
        u32 partial = edge & 511;
        u32 valid = edge - partial == wave_end ? wave_valid + partial : partial;
        u32 recorded = span > silent ? span - silent : 0;
        wave_snapshot(&wave_loop, edge, span, valid < recorded ? valid : recorded);
        if (partial) {
            u32 mask = (1u << wave_loop.shift) - 1;
            u16 *cell = &wave_loop.bins[((edge - 1) >> wave_loop.shift) & (WAVE_BINS - 1)];
            u16 p = wave_decode((u16)(w[LS_WAVE_PEAK] >> 16), w[LS_STATE]);
            *cell = valid > partial && ((edge - partial) & mask) ? wave_merge(*cell, p) : p;
        }
    }
    if (end != wave_end)
        wave_add(end, wave_decode((u16)w[LS_WAVE_PEAK], w[LS_STATE]));
    wave_count = count;
    wave_length = (w[LS_STATE] & LS_LOOP) ? w[LS_LOOP_FRAMES] : 0;
}

void dkrt_loop_wave_peaks(u16 out[LS_WAVE_COLUMNS])
{
    WaveView v;
    u32 sr = dkrt_irq_mask();
    if (wave_length) {
        v.end = wave_loop.end;
        v.frames = wave_loop.frames;
        v.valid = wave_loop.valid;
        v.shift = wave_loop.shift;
        for (u32 j = 0; j < WAVE_BINS; ++j)
            v.bins[j] = wave_loop.bins[j];
    } else {
        u32 frames = status[LS_NEXT_FRAMES];
        if (!frames)
            frames = 96000; /* two seconds of rolling record before tempo/length is known */
        if (frames > status[LS_CAPACITY] && status[LS_CAPACITY])
            frames = status[LS_CAPACITY];
        wave_snapshot(&v, wave_end, frames, wave_valid);
    }
    dkrt_irq_restore(sr);
    /* Integer endpoints avoid an expensive divide per pixel. At most three
     * adjacent history bins intersect one column at the selected resolution. */
    u32 from = 0, frac = 0, q = v.frames / LS_WAVE_COLUMNS, rem = v.frames % LS_WAVE_COLUMNS;
    for (u32 x = 0; x < LS_WAVE_COLUMNS; ++x) {
        u32 to = from + q;
        frac += rem;
        if (frac >= LS_WAVE_COLUMNS) {
            ++to;
            frac -= LS_WAVE_COLUMNS;
        }
        u16 peak = 0;
        if (age < STALE && to > from && to > v.frames - v.valid) {
            u32 first = (from < v.frames - v.valid ? v.frames - v.valid : from);
            u32 pos = v.end - v.frames + first, last = v.end - v.frames + to - 1;
            for (;;) {
                peak = wave_merge(peak, v.bins[(pos >> v.shift) & (WAVE_BINS - 1)]);
                u32 advance = (1u << v.shift) - (pos & ((1u << v.shift) - 1));
                if (last - pos < advance)
                    break;
                pos += advance;
            }
        }
        out[x] = peak;
        from = to;
    }
}
