/* All filesystem calls and transient analysis run on the runtime's I/O task.
 * One outstanding link request has a monotonically increasing sequence. The
 * audio ISR copies a fixed packet; it never calls a filesystem function. */
#include "files.h"
#include "dk_fw_storage.h"
#include "../../../../../sdk/dsp/contract/dk_relay_transfer.inc"
typedef uint32_t u32;
typedef uint16_t u16;
typedef uint8_t u8;
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);
extern int dkrt_io_post_image(int (*step)(void *), void (*done)(void *, int), void *);
extern void dkrt_io_wake(int);
extern int dkrt_image_file_create(void *, u32, const u8 *, u32, const u8 *, u32);
extern int dkrt_image_file_write(void *, int, u32, const u8 *, u32);
extern int dkrt_image_file_close(void *, int, u8 *);
extern int dkrt_image_file_abort(void *, int);
enum { MORE = 1, WAIT = 2, E_EXISTS = -18 };
enum {
    START,
    PIN,
    SCAN,
    OPEN_FILE,
    WRITE_FILE,
    CLOSE_FILE,
    LOAD_OPEN,
    LOAD_BEGIN,
    LOAD_PCM,
    LOAD_COMMIT,
    RELEASE,
    CANCEL
};
struct RelayFiles relay_files;
static struct RelayOverview analysis;
static unsigned stage, hits, sensitivity, pinned, loading, failed, slice;
static unsigned next_action, next_slot, next_source, next_take, next_hits, next_sens;
static volatile unsigned stop_scan;
static u32 frames, cursor, end, bytes, used, file_length, last_cut, envelope, analysis_pos;
static u32 quiet[64];
static int handle, input_open;
static u32 input_handle[4], folder_state[32];
static u8 path[40];
static u8 buffer[2048] __attribute__((aligned(16)));
static u32 outbound[16], inbound[14];
static volatile u32 seq, pending, acknowledged, block_clock, sent_at, timed_out;
static volatile int job;
static volatile unsigned retry_wait, retry_at;
static unsigned retries;

static unsigned reply_index(unsigned i)
{
    unsigned row = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2;
    return (0x1c + 0x54 * row + 0x44 + (i & 1) * 4) / 2;
}
extern void pp_record_timing(u32 out[4]);
void relay_write_frame(uintptr_t queue, u32 at)
{
    volatile u16 *dst = (volatile u16 *)(queue + 4 * at + 2);
    u32 timing[4];
    ++block_clock;
    pp_record_timing(timing);
    for (unsigned i = 0; i < 4; ++i) {
        dst[0] = (u16)timing[i];
        dst[2] = (u16)(timing[i] >> 16);
        dst += 4;
    }
    extern int rtm_write_command(volatile u16 *);
    // Controls and file reads share these slots. Give each four consecutive
    // frames when both are pending, so neither waits for the UI queue to drain.
    if ((!pending || (block_clock & 4)) && rtm_write_command(dst))
        return;
    if (!pending) {
        dst[0] = dst[2] = 0;
        return;
    }
    for (unsigned i = 0; i < 16; ++i) {
        u32 v = outbound[i];
        dst[0] = (u16)v;
        dst[2] = (u16)(v >> 16);
        dst += 4;
    }
}
void relay_files_reply(const volatile u16 *p)
{
    if (!pending || !p || acknowledged || (p[762] | ((u32)p[763] << 16)) != RX_REPLY)
        return;
    unsigned k = reply_index(1);
    if ((p[k] | ((u32)p[k + 1] << 16)) != seq)
        return;
    for (unsigned i = 2; i < 13; ++i) {
        k = reply_index(i);
        inbound[i] = p[k] | ((u32)p[k + 1] << 16);
    }
    // The native reply slots are shared with other machines. Ignore a mixed
    // frame; the DSP repeats the pinned response until it sees the next id.
    u32 check = seq;
    for (unsigned i = 2; i < 13; ++i)
        check = check * 31 + inbound[i];
    k = reply_index(13);
    if (check != (p[k] | ((u32)p[k + 1] << 16)))
        return;
    acknowledged = 1;
    dkrt_io_wake(job);
}
void relay_files_tick(void)
{
    if (job > 0 && retry_wait && block_clock - retry_at >= 16) {
        retry_wait = 0;
        dkrt_io_wake(job);
    }
    if (job > 0 && pending && !acknowledged && !timed_out && block_clock - sent_at >= 3000) {
        timed_out = 1;
        dkrt_io_wake(job);
    }
}
// Return 0 while pending, 1 for a valid reply, -1 on failure. data is PCM in
// native big endian order; reads and writes use the same channel ordering.
static int exchange(u32 op, u32 a, u32 b, const u8 *data)
{
    u32 sr = dkrt_irq_mask();
    if (timed_out && !acknowledged) {
        // The DSP can have accepted a reservation even when its reply was lost.
        // Release that reservation through the normal cancellation path.
        if (op == RX_PIN)
            pinned = 1;
        if (op == RX_LOAD)
            loading = 1;
        timed_out = pending = acknowledged = 0;
        dkrt_irq_restore(sr);
        return -1;
    }
    if (pending) {
        if (!acknowledged) {
            dkrt_irq_restore(sr);
            return 0;
        }
        timed_out = acknowledged = pending = 0;
        dkrt_irq_restore(sr);
        return (int)inbound[2] < 0 ? -1 : 1;
    }
    if (!++seq)
        ++seq;
    outbound[0] = RX_MAGIC;
    outbound[1] = outbound[15] = seq;
    outbound[2] = op;
    outbound[3] = a;
    outbound[4] = b;
    if (data)
        for (unsigned i = 0; i < b; ++i) {
            const u8 *p = data + i * 4;
            outbound[5 + i] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    sent_at = block_clock;
    pending = 1;
    dkrt_irq_restore(sr);
    return 0;
}

static void name(int folder)
{
    u32 n = relay_files.number;
    const char *prefix = "/RELAY/";
    unsigned i = 0;
    while (prefix[i]) {
        path[i] = (u8)prefix[i];
        ++i;
    }
    path[i++] = 'T';
    path[i++] = '0' + (relay_files.source + 1) / 10;
    path[i++] = '0' + (relay_files.source + 1) % 10;
    path[i++] = '-';
    for (unsigned j = 0; j < 4; ++j) {
        path[i + 3 - j] = (u8)('0' + n % 10);
        n /= 10;
    }
    i += 4;
    if (!folder) {
        path[i++] = '/';
        if (!slice) {
            path[i++] = 'F';
            path[i++] = 'U';
            path[i++] = 'L';
            path[i++] = 'L';
        } else {
            path[i++] = (u8)('0' + slice / 10);
            path[i++] = (u8)('0' + slice % 10);
        }
    }
    path[i] = 0;
}
static void be32(u8 *p, u32 v)
{
    p[0] = v >> 24;
    p[1] = v >> 16;
    p[2] = v >> 8;
    p[3] = v;
}
static u32 word(const u8 *p)
{
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}
static void header(u32 samples)
{
    for (unsigned i = 0; i < 64; ++i)
        ((volatile u8 *)buffer)[i] = 0;
    buffer[1] = 1;
    be32(buffer + 4, samples * 4);
    be32(buffer + 8, 48000);
    buffer[20] = 127;
    bytes = 0;
    used = 64;
    file_length = 64 + samples * 4;
}
static unsigned magnitude(short v)
{
    return v < 0 ? (unsigned)-(int)v : (unsigned)v;
}
static void analyze(u32 sample)
{
    u32 l = magnitude((short)(sample >> 16)), r = magnitude((short)sample), e = l > r ? l : r;
    u32 f = analysis_pos++, bin = f * 120 / frames;
    if (e > analysis.peaks[bin])
        analysis.peaks[bin] = (u16)e;
    // Look for an attack above a decaying peak. Keep stereo cuts coincident.
    // At most 63 hits plus the compulsory frame zero.
    unsigned floor = (10 - sensitivity) * 220;
    if (e > floor && e > envelope + floor && f - last_cut >= 1504 && analysis.count < 64) {
        u32 cut = f - 1, low = quiet[cut & 63];
        for (unsigned j = 2; j <= 64; ++j)
            if (quiet[(f - j) & 63] < low) {
                cut = f - j;
                low = quiet[cut & 63];
            }
        if (cut - last_cut >= 1440) {
            analysis.starts[analysis.count++] = cut;
            last_cut = cut;
        }
    }
    quiet[f & 63] = e;
    if (e > envelope)
        envelope = e;
    else
        envelope -= envelope / 256 + (envelope != 0);
}
static void scan_begin(void)
{
    analysis.frames = frames;
    analysis.count = 1;
    analysis.starts[0] = 0;
    analysis.slot = relay_files.slot;
    for (unsigned i = 0; i < 120; ++i)
        ((volatile u16 *)analysis.peaks)[i] = 0;
    envelope = last_cut = analysis_pos = cursor = 0;
    end = frames;
    for (unsigned i = 0; i < 64; ++i)
        ((volatile u32 *)quiet)[i] = 0;
}
static int cancel(void)
{
    if (!relay_files.error)
        relay_files.error = stage == PIN || stage == SCAN ? "TAKE READ FAILED" : "FILE FAILED";
    failed = 1;
    stage = CANCEL;
    return MORE;
}
// A concurrent storage call can briefly hold the +Drive. Keep the same file
// handle, offset and bytes until it accepts the operation. Never retry a write
// that may have consumed data.
static int storage_error(int error)
{
    if (error == -12 && ++retries <= 150) {
        retry_at = block_clock;
        retry_wait = 1;
        return WAIT;
    }
    relay_files.error = error == -52 ? "DRIVE FULL" : error == -12 ? "DRIVE BUSY" : "DRIVE ERROR";
    return cancel();
}
__attribute__((optimize("no-jump-tables"))) static int step(void *arg)
{
    (void)arg;
    int x;
    unsigned n;
    switch (stage) {
    case START:
        stage = relay_files.action == RF_LOAD ? LOAD_OPEN : PIN;
        return MORE;
    case PIN:
        x = exchange(RX_PIN, relay_files.slot, 0, 0);
        if (!x)
            return WAIT;
        if (x < 0 || !inbound[2])
            return cancel();
        frames = inbound[2];
        relay_files.revision = inbound[3];
        relay_files.grid = inbound[4];
        pinned = 1;
        scan_begin();
        stage = relay_files.action == RF_SCAN ? SCAN : OPEN_FILE;
        return MORE;
    case SCAN:
        if (!hits) {
            // Measure every frame of the bin on the DSP, including quiet, narrow attacks.
            // The DSP returns the span consumed, stopping at a physical pool page boundary.
            unsigned bin = cursor * 120 / frames;
            if (frames >= 120) {
                // Each endpoint is an exclusive frame offset. Ten exact stereo maxima per reply.
                // Bound the batch on long tapes so the DSP job fits the link timeout.
                unsigned bins = frames > 4000000 ? 4 : 10;
                if (bins > 120 - bin)
                    bins = 120 - bin;
                u8 ends[40];
                for (unsigned i = 0; i < bins; ++i)
                    be32(ends + 4 * i, ((bin + i + 1) * frames + 119) / 120);
                x = exchange(RX_PEAKS, cursor, bins, ends);
                if (!x)
                    return WAIT;
                if (x < 0 || inbound[2] != bins)
                    return cancel();
                for (unsigned i = 0; i < bins; ++i) {
                    if (inbound[3 + i] > 32768)
                        return cancel();
                    analysis.peaks[bin + i] = (u16)inbound[3 + i];
                }
                cursor = word(ends + 4 * (bins - 1));
                if (stop_scan || cursor == end)
                    stage = RELEASE;
                return MORE;
            }
            unsigned limit = ((bin + 1) * frames + 119) / 120;
            n = limit - cursor;
            if (n > RX_PEAK_FRAMES)
                n = RX_PEAK_FRAMES;
            x = exchange(RX_PEAK, cursor, n, 0);
            if (!x)
                return WAIT;
            if (x < 0 || !inbound[2] || inbound[2] > n || inbound[3] > 32768)
                return cancel();
            if (inbound[3] > analysis.peaks[bin])
                analysis.peaks[bin] = (u16)inbound[3];
            cursor += inbound[2];
            if (stop_scan || cursor == end)
                stage = RELEASE;
            return MORE;
        }
        n = end - cursor > 10 ? 10 : end - cursor;
        x = exchange(RX_READ, cursor, n, 0);
        if (!x)
            return WAIT;
        if (x < 0 || inbound[2] != n)
            return cancel();
        for (unsigned i = 0; i < n; ++i)
            analyze(inbound[3 + i]);
        cursor += n;
        if (stop_scan) {
            stage = RELEASE;
            return MORE;
        }
        if (cursor == end) {
            if (relay_files.action == RF_SCAN) {
                stage = RELEASE;
                return MORE;
            }
            stage = OPEN_FILE;
        }
        return MORE;
    case OPEN_FILE:
        if (!slice) {
            name(1);
            if (dk_fw_storage_directory_open(path, folder_state) == 0) {
                if (++relay_files.number > 9999)
                    return cancel();
                return MORE;
            }
        }
        name(0);
        cursor = slice ? analysis.starts[slice - 1] : 0;
        end = slice && slice < analysis.count ? analysis.starts[slice] : frames;
        header(end - cursor);
        handle = dkrt_image_file_create(0, 1, path + 1, file_length, 0, 0);
        if (handle < 0) {
            if (handle == E_EXISTS && !slice && relay_files.number < 9999) {
                ++relay_files.number;
                return MORE;
            }
            return storage_error(handle);
        }
        retries = 0;
        relay_files.progress = slice + 1;
        relay_files.total = 1 + (hits ? analysis.count : 0);
        stage = WRITE_FILE;
        return MORE;
    case WRITE_FILE:
        if (used == sizeof(buffer) || cursor == end) {
            if (used) {
                x = dkrt_image_file_write(0, handle, bytes, buffer, used);
                if (x < 0)
                    return storage_error(x);
                retries = 0;
            }
            bytes += used;
            used = 0;
            if (cursor == end)
                stage = CLOSE_FILE;
            return MORE;
        }
        n = (sizeof(buffer) - used) / 4;
        if (slice) {
            if (n > end - cursor)
                n = end - cursor;
            if (dk_fw_storage_path_read(buffer + used, 4 * n, input_handle) != (int)(4 * n))
                return cancel();
            cursor += n;
            used += 4 * n;
            return MORE;
        }
        if (n > 10)
            n = 10;
        if (n > end - cursor)
            n = end - cursor;
        x = exchange(RX_READ, cursor, n, 0);
        if (!x)
            return WAIT;
        if (x < 0 || inbound[2] != n) {
            relay_files.error = "TAKE READ FAILED";
            return cancel();
        }
        for (unsigned i = 0; i < n; ++i) {
            be32(buffer + used + 4 * i, inbound[3 + i]);
            if (hits)
                analyze(inbound[3 + i]);
        }
        used += 4 * n;
        cursor += n;
        return MORE;
    case CLOSE_FILE:
        x = dkrt_image_file_close(0, handle, 0);
        if (x < 0)
            return storage_error(x);
        retries = 0;
        handle = -1;
        if (hits && slice < analysis.count) {
            // FULL is already finalized. Read it sequentially through every hit
            // so enabling HITS only transfers one copy of the take from DSP RAM.
            if (!slice) {
                name(0);
                if (dk_fw_storage_path_open(path, (const u8 *)"r", input_handle) != 0)
                    return cancel();
                input_open = 1;
                if (dk_fw_storage_path_read(buffer, 64, input_handle) != 64)
                    return cancel();
            }
            ++slice;
            stage = OPEN_FILE;
        } else {
            if (input_open) {
                dk_fw_storage_path_close(input_handle);
                input_open = 0;
            }
            stage = RELEASE;
        }
        return MORE;
    case LOAD_OPEN:
        name(0);
        if (dk_fw_storage_path_open(path, (const u8 *)"r", input_handle) != 0)
            return cancel();
        input_open = 1;
        if (dk_fw_storage_path_read(buffer, 64, input_handle) != 64 || buffer[1] != 1 ||
            word(buffer + 8) != 48000)
            return cancel();
        file_length = word(buffer + 4);
        if (!file_length || (file_length & 3) || file_length > 511u * 65536 ||
            dk_fw_storage_path_size(input_handle[0]) != 64 + file_length + 16)
            return cancel();
        frames = file_length / 4;
        cursor = bytes = used = 0;
        stage = LOAD_BEGIN;
        return MORE;
    case LOAD_BEGIN:
        x = exchange(RX_LOAD, relay_files.slot, frames, 0);
        if (!x)
            return WAIT;
        if (x < 0)
            return cancel();
        loading = 1;
        stage = LOAD_PCM;
        return MORE;
    case LOAD_PCM:
        if (!used) {
            n = (frames - cursor) * 4;
            if (n > sizeof(buffer))
                n = sizeof(buffer);
            if (dk_fw_storage_path_read(buffer, n, input_handle) != (int)n)
                return cancel();
            bytes = 0;
            used = n;
        }
        n = used / 4 > 10 ? 10 : used / 4;
        x = exchange(RX_WRITE, cursor, n, buffer + bytes);
        if (!x)
            return WAIT;
        if (x < 0 || inbound[2] != n)
            return cancel();
        bytes += 4 * n;
        used -= 4 * n;
        cursor += n;
        if (cursor == frames)
            stage = LOAD_COMMIT;
        return MORE;
    case LOAD_COMMIT:
    {
        extern int rtm_load_current(unsigned);
        if (!pending && !rtm_load_current(relay_files.slot))
            return cancel();
    }
        x = exchange(RX_COMMIT, 0, 0, 0);
        if (!x)
            return WAIT;
        if (x < 0)
            return cancel();
        loading = 0;
        dk_fw_storage_path_close(input_handle);
        input_open = 0;
        return 0;
    case CANCEL:
        if (input_open) {
            dk_fw_storage_path_close(input_handle);
            input_open = 0;
        }
        if (handle >= 0) {
            dkrt_image_file_abort(0, handle);
            handle = -1;
        }
        if (loading) {
            x = exchange(RX_ABORT, 0, 0, 0);
            if (!x)
                return WAIT;
            loading = 0;
        }
        if (!pinned)
            return -1;
        stage = RELEASE;
        return MORE;
    case RELEASE:
        x = exchange(RX_UNPIN, 0, !failed && relay_files.action == RF_SAVE, 0);
        if (!x)
            return WAIT;
        pinned = 0;
        return x < 0 || failed ? -1 : 0;
    }
    return -1;
}
// A scan belongs to the pinned tape revision, independently of the current SRC view.
static struct WaveCache {
    struct RelayOverview overview;
    unsigned revision, sensitivity;
} wave_cache[48];

const struct RelayOverview *rtm_wave(unsigned slot, unsigned revision, unsigned length,
                                     unsigned need_hits, unsigned sens)
{
    if (slot >= 48 || !length)
        return 0;
    const struct WaveCache *c = wave_cache + slot;
    return c->overview.frames == length && c->revision == revision &&
                   (!need_hits || c->sensitivity == sens)
               ? &c->overview
               : 0;
}
static void cache_wave(void)
{
    struct WaveCache *c = wave_cache + analysis.slot;
    u32 sr = dkrt_irq_mask();
    c->overview.slot = analysis.slot;
    c->overview.count = analysis.count;
    for (unsigned i = 0; i < analysis.count; ++i)
        c->overview.starts[i] = analysis.starts[i];
    for (unsigned i = 0; i < 120; ++i)
        c->overview.peaks[i] = analysis.peaks[i];
    c->revision = relay_files.revision;
    c->sensitivity = hits ? sensitivity : 0;
    c->overview.frames = analysis.frames;
    dkrt_irq_restore(sr);
}
static void done(void *arg, int status)
{
    (void)arg;
    if (status < 0)
        relay_files.state = RF_FAILED;
    else if (relay_files.action == RF_LOAD) {
        relay_files.overview.frames = 0;
        relay_files.state = RF_LOADED;
    } else {
        if (!stop_scan && (hits || relay_files.action == RF_SCAN)) {
            cache_wave();
            struct RelayOverview *o = &relay_files.overview;
            o->frames = analysis.frames;
            o->count = analysis.count;
            o->slot = analysis.slot;
            for (unsigned i = 0; i < analysis.count; ++i)
                o->starts[i] = analysis.starts[i];
            for (unsigned i = 0; i < 120; ++i)
                o->peaks[i] = analysis.peaks[i];
        }
        relay_files.state = relay_files.action == RF_SCAN ? RF_SCANNED : RF_SAVED;
    }
    job = 0;
    if (next_action) {
        unsigned a = next_action;
        next_action = 0;
        relay_file_start(a, next_slot, next_source, next_take, next_hits, next_sens);
    }
}
void relay_stop_scan(void)
{
    if (job && relay_files.action == RF_SCAN) {
        stop_scan = 1;
        if (next_action == RF_SCAN)
            next_action = 0;
    }
}
int relay_file_start(unsigned action, unsigned slot, unsigned source, unsigned take,
                     unsigned detect, unsigned sens)
{
    const unsigned last = action == RF_SCAN ? 47 : 15;
    if (slot > last || source > last || !take || take > 9999 || sens < 1 || sens > 9)
        return 0;
    if (job) {
        if (relay_files.action != RF_SCAN || (next_action && next_action != RF_SCAN))
            return 0;
        next_action = action;
        next_slot = slot;
        next_source = source;
        next_take = take;
        next_hits = detect;
        next_sens = sens;
        stop_scan = 1;
        return 1;
    }
    retries = retry_wait = 0;
    relay_files.error = 0;
    stop_scan = 0;
    relay_files.action = action;
    relay_files.slot = slot;
    relay_files.source = source;
    relay_files.number = take;
    relay_files.progress = 0;
    relay_files.total = 1;
    relay_files.state = RF_WORK;
    stage = START;
    slice = failed = loading = pinned = input_open = 0;
    hits = detect;
    sensitivity = sens;
    handle = -1;
    job = dkrt_io_post_image(step, done, 0);
    if (job <= 0) {
        job = 0;
        relay_files.state = RF_FAILED;
        return 0;
    }
    // If the I/O task ran before post returned it may already be asleep.
    dkrt_io_wake(job);
    return 1;
}
