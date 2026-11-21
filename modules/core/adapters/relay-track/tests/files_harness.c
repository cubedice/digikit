/* Host the production file worker on a byte-addressed fake +Drive and exchange
 * its real wire frames with a small PCM peer. Assertions cover every byte and
 * that failure never commits a replacement or clears dirty state. */
#include "../src/files.h"
#include "dk_fw_storage.h"
#include "../../../../../sdk/dsp/contract/dk_relay_transfer.inc"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef uint32_t u32;
typedef uint8_t u8;
extern void relay_write_frame(uintptr_t, u32);
enum { N = 4800 };
static struct Entry {
    char path[50];
    u8 bytes[64 + N * 4 + 16];
    unsigned used;
} files[16];
static int nf, active = -1, reader, read_at, fail_write, busy_write;
static int (*work)(void *);
static void (*finish)(void *, int);
static u32 pcm[N], loaded[N], staging[N], read_count, write_count, target, expected, dirty = 1;
static uint16_t tx[100], rx[1400];
u32 dkrt_irq_mask(void)
{
    return 0;
}
void dkrt_irq_restore(u32 v)
{
    (void)v;
}
static int awoken;
int rtm_load_current(unsigned track)
{
    return track < 16;
}
int rtm_write_command(volatile uint16_t *dst)
{
    (void)dst;
    return 0;
}
void dkrt_io_wake(int id)
{
    assert(id == 1);
    awoken = 1;
}
void pp_record_timing(u32 out[4])
{
    memset(out, 0, 16);
}
int dkrt_io_post_image(int (*s)(void *), void (*d)(void *, int), void *p)
{
    (void)p;
    work = s;
    finish = d;
    return 1;
}
int dk_fw_storage_directory_open(const u8 *path, u32 *state)
{
    (void)state;
    for (int i = 0; i < nf; i++)
        if (!strncmp(files[i].path, (const char *)path + 1, strlen((const char *)path) - 1))
            return 0;
    return -1;
}
int dkrt_image_file_create(void *f, u32 root, const u8 *p, u32 size, const u8 *hash, u32 flags)
{
    (void)f;
    (void)hash;
    assert(root == 1 && !flags && size <= 64 + N * 4 && active < 0);
    for (int i = 0; i < nf; i++)
        if (!strcmp((const char *)p, files[i].path))
            return -18;
    active = nf;
    snprintf(files[nf].path, 50, "%s", p);
    files[nf].used = 0;
    return 12;
}
int dkrt_image_file_write(void *f, int h, u32 off, const u8 *buf, u32 n)
{
    (void)f;
    assert(h == 12 && active == nf && off == files[nf].used);
    assert(!((uintptr_t)buf & 15) && !(off % 512));
    if (busy_write) {
        --busy_write;
        return -12;
    }
    if (fail_write)
        return -57;
    memcpy(files[nf].bytes + off, buf, n);
    files[nf].used += n;
    return 0;
}
int dkrt_image_file_close(void *f, int h, u8 *hash)
{
    (void)f;
    (void)hash;
    assert(active == nf && h == 12);
    memset(files[nf].bytes + files[nf].used, 0, 16);
    files[nf].used += 16;
    active = -1;
    nf++;
    return 0;
}
int dkrt_image_file_abort(void *f, int h)
{
    (void)f;
    (void)h;
    active = -1;
    return 0;
}
int dk_fw_storage_path_open(const u8 *p, const u8 *mode, u32 *handle)
{
    assert(mode[0] == 'r');
    for (int i = 0; i < nf; i++)
        if (!strcmp(files[i].path, (const char *)p + 1)) {
            reader = i;
            handle[0] = i;
            read_at = 0;
            return 0;
        }
    return -1;
}
u32 dk_fw_storage_path_size(u32 i)
{
    return files[i].used;
}
int dk_fw_storage_path_read(u8 *p, u32 n, u32 *h)
{
    assert(h[0] == (u32)reader);
    if (read_at + n > files[reader].used)
        return -1;
    memcpy(p, files[reader].bytes + read_at, n);
    read_at += n;
    return (int)n;
}
int dk_fw_storage_path_close(u32 *h)
{
    (void)h;
    return 0;
}
static u32 wire(unsigned i)
{
    return tx[16 + 4 * i + 1] | ((u32)tx[16 + 4 * i + 3] << 16);
}
static void reply(unsigned i, u32 v)
{
    unsigned row = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2;
    unsigned k = (0x1c + 0x54 * row + 0x44 + (i & 1) * 4) / 2;
    rx[k] = v;
    rx[k + 1] = v >> 16;
}
static unsigned peak_batches;
static void run(void)
{
    for (unsigned tick = 0; tick < 40000; tick++) {
        awoken = 0;
        int s = work(0);
        if (s <= 0) {
            finish(0, s);
            return;
        }
        if (s == 1)
            continue;
        relay_write_frame((uintptr_t)tx, 0);
        if (!wire(0)) {
            for (unsigned i = 0; !awoken && i < 32; ++i) {
                relay_write_frame((uintptr_t)tx, 0);
                relay_files_tick();
            }
            assert(awoken);
            continue;
        }
        assert(wire(0) == RX_MAGIC && wire(1) == wire(15));
        reply(0, RX_REPLY);
        reply(1, wire(1));
        u32 op = wire(2), a = wire(3), b = wire(4), result = 0;
        if (op == RX_PIN) {
            reply(3, a + 1);
            reply(4, 16);
            result = N;
        } else if (op == RX_READ) {
            assert(a + b <= N && b <= 10);
            for (unsigned i = 0; i < b; i++)
                reply(3 + i, pcm[a + i]);
            result = b;
            read_count++;
        } else if (op == RX_PEAKS) {
            assert(b && b <= 10);
            ++peak_batches;
            for (unsigned i = 0; i < b; ++i) {
                unsigned limit = wire(5 + i), peak = 0;
                assert(a < limit && limit <= N);
                while (a < limit) {
                    unsigned v = pcm[a++];
                    unsigned l = abs((short)(v >> 16)), r = abs((short)v);
                    if (l > peak)
                        peak = l;
                    if (r > peak)
                        peak = r;
                }
                reply(3 + i, peak);
            }
            result = b;
        } else if (op == RX_PEAK) {
            assert(a + b <= N && b && b <= RX_PEAK_FRAMES);
            unsigned peak = 0;
            for (unsigned i = 0; i < b; ++i) {
                int left = (int16_t)(pcm[a + i] >> 16), right = (int16_t)pcm[a + i];
                unsigned l = left < 0 ? -left : left, r = right < 0 ? -right : right;
                if (l > peak)
                    peak = l;
                if (r > peak)
                    peak = r;
            }
            reply(3, peak);
            result = b;
        } else if (op == RX_UNPIN) {
            if (b)
                dirty = 0;
        } else if (op == RX_LOAD) {
            target = a;
            expected = b;
            write_count = 0;
        } else if (op == RX_WRITE) {
            assert(a == write_count && b <= 10);
            for (unsigned i = 0; i < b; i++)
                staging[write_count++] = wire(5 + i);
            result = b;
        } else if (op == RX_COMMIT) {
            assert(write_count == expected);
            memcpy(loaded, staging, N * 4);
        } else
            assert(op == RX_ABORT);
        reply(2, result);
        u32 check = wire(1);
        for (unsigned i = 2; i < 13; ++i) {
            unsigned row = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2;
            unsigned k = (0x1c + 0x54 * row + 0x44 + (i & 1) * 4) / 2;
            check = check * 31 + (rx[k] | ((u32)rx[k + 1] << 16));
        }
        reply(13, check ^ 1);
        relay_files_reply(rx);
        assert(!awoken);
        reply(13, check);
        relay_files_reply(rx);
        assert(awoken);
    }
    assert(0);
}
int main(void)
{
    for (int i = 0; i < N; i++) {
        short l = (i == 2200 || i == 4200) ? 12000 : (short)(i % 9 - 4);
        short r = (short)-l;
        pcm[i] = ((u32)(uint16_t)l << 16) | (uint16_t)r;
    }
    assert(relay_file_start(RF_SCAN, 12, 12, 1, 0, 1));
    run();
    assert(relay_files.state == RF_SCANNED && relay_files.overview.slot == 12);
    assert(relay_files.overview.peaks[55] == 12000 && relay_files.overview.peaks[105] == 12000);
    assert(peak_batches == 12 && read_count == 0 && nf == 0);
    unsigned main_revision = relay_files.revision;
    const struct RelayOverview *cached = rtm_wave(12, main_revision, N, 0, 1);
    assert(cached && cached->peaks[55] == 12000 &&
           !rtm_wave(12, relay_files.revision + 1, N, 0, 1));
    assert(relay_file_start(RF_SCAN, 6, 6, 1, 1, 5));
    run();
    assert(rtm_wave(6, relay_files.revision, N, 1, 5) &&
           !rtm_wave(6, relay_files.revision, N, 1, 3));
    assert(rtm_wave(12, main_revision, N, 0, 1) == cached);

    const char *full = "RELAY/T02-0001/FULL";
    busy_write = 2;
    assert(relay_file_start(RF_SAVE, 1, 1, 1, 1, 5));
    run();
    assert(relay_files.state == RF_SAVED && !dirty && nf == 4 && relay_files.overview.count == 3);
    assert(!strcmp(files[0].path, full) && files[0].used == 64 + N * 4 + 16);
    unsigned offset = 0;
    for (int i = 1; i < nf; i++) {
        unsigned n = files[i].used - 80;
        assert(!memcmp(files[0].bytes + 64 + offset, files[i].bytes + 64, n));
        offset += n;
    }
    assert(offset == N * 4);
    assert(relay_file_start(RF_LOAD, 2, 1, 1, 0, 5));
    run();
    assert(relay_files.state == RF_LOADED && target == 2 && !memcmp(pcm, loaded, N * 4));
    dirty = 1;
    fail_write = 1;
    assert(relay_file_start(RF_SAVE, 1, 1, 1, 0, 5));
    run();
    assert(relay_files.state == RF_FAILED && dirty && nf == 4 && active == -1 &&
           relay_files.number == 2);
    files[0].bytes[1] = 0;
    assert(relay_file_start(RF_LOAD, 0, 1, 1, 0, 5));
    run();
    assert(relay_files.state == RF_FAILED && target == 2 && !memcmp(pcm, loaded, N * 4));
    puts("RELAY native FULL, HITS, recall and failure recovery passed");
}
