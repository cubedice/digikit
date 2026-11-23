#include "../src/control.c"
#include <assert.h>
#include <string.h>
const u32 machines_info[8];
struct RelayFiles relay_files;
static unsigned rotary_cells, text_count;
static struct {
    int x, y;
    char text[24];
} labels[16];
void ip_src_draw_rotary(u8 *view, u8 *canvas, u32 cell, u32 value)
{
    (void)view;
    (void)canvas;
    assert(cell < 8 && value <= 127);
    rotary_cells |= 1u << cell;
}
void fxv_text(void *bitmap, int x, int y, const char *text, int ink)
{
    (void)bitmap;
    (void)ink;
    assert(text_count < sizeof(labels) / sizeof(labels[0]) &&
           strlen(text) < sizeof(labels[0].text));
    labels[text_count].x = x;
    labels[text_count].y = y;
    strcpy(labels[text_count++].text, text);
}
static int has_text(int x, int y, const char *text)
{
    for (unsigned i = 0; i < text_count; ++i)
        if (x == labels[i].x && y == labels[i].y && !strcmp(labels[i].text, text))
            return 1;
    return 0;
}
void fxv_box(void *bitmap, int x, int y, int width, int height, int ink)
{
    uint8_t (*pixels)[128] = bitmap;
    assert(x >= 0 && y >= 0 && x + width <= 128 && y + height <= 64);
    for (int row = y; row < y + height; ++row)
        for (int col = x; col < x + width; ++col)
            pixels[row][col] = ink < 0 ? !pixels[row][col] : ink;
}
const struct RelayOverview *rtm_wave(unsigned slot, unsigned revision, unsigned length,
                                     unsigned hits, unsigned sens)
{
    (void)slot;
    (void)revision;
    (void)length;
    (void)hits;
    (void)sens;
    return 0;
}
const struct RelayOverview *rtm_wave_pending(unsigned slot, unsigned revision, unsigned length)
{
    (void)slot;
    (void)revision;
    (void)length;
    return 0;
}
void relay_files_reply(const volatile uint16_t *frame)
{
    (void)frame;
}
static SaStore store;
static u8 sound[0x450], record[SR_BYTES];
static u32 serial;
int ip_refresh_machine_track(u32 t, u32 type)
{
    return ip_current_machine_sound(t, type) != 0;
}
u32 pp_record_steps(void)
{
    return 16;
}
u32 dkrt_irq_mask(void)
{
    return 0;
}
void dkrt_irq_restore(u32 flags)
{
    (void)flags;
}
SaStore *ip_sa_store(void)
{
    return &store;
}
u32 ip_current_machine_sound(unsigned t, unsigned type)
{
    return !t && type == sound[0xa2] ? (u32)(uintptr_t)sound : 0;
}
int sa_slot(const SaStore *s, uintptr_t addr)
{
    (void)s;
    return addr == (uintptr_t)sound ? 0 : -1;
}
u32 sa_seq(const SaStore *s, uintptr_t addr)
{
    (void)s;
    (void)addr;
    return serial;
}
u32 sa_put(SaStore *s, uintptr_t addr, const u8 bytes[SR_BYTES])
{
    (void)s;
    assert(addr == (uintptr_t)sound);
    memcpy(record, bytes, SR_BYTES);
    return ++serial;
}
int sa_get(SaStore *s, uintptr_t addr, u8 out[SR_BYTES], u32 *seq)
{
    (void)s;
    assert(addr == (uintptr_t)sound);
    *seq = serial;
    memcpy(out, record, SR_BYTES);
    return serial != 0;
}
int relay_file_start(unsigned a, unsigned t, unsigned source, unsigned take, unsigned hits,
                     unsigned sens)
{
    (void)hits;
    (void)sens;
    assert(!t);
    relay_files.action = a;
    relay_files.source = source;
    relay_files.number = take;
    relay_files.state = RF_WORK;
    return 1;
}
static void tick(void)
{
    for (unsigned i = 0; i < 16; ++i)
        tape_sound_tick();
}
int main(void)
{
    relay_type = sound[0xa2] = 21;
    track = 0;
    tick();
    assert(binding[0].sound == (uintptr_t)sound);
    settings[0].rate = -73;
    settings[0].spacing = 12288;
    settings[0].mix = 29;
    publish_sound(0);
    struct TapeRecord value;
    assert(decode_tape(record, &value) && value.rate == -73 && value.take == 0);
    latest[8] = latest[10] = 1u << 16;
    assert(!rtm_can_save(binding[0].sound));
    tick();
    assert(file_track == 0 && relay_files.action == RF_SAVE && !rtm_can_save(binding[0].sound));
    relay_files.number = 37;
    relay_files.grid = 32;
    relay_files.state = RF_SAVED;
    latest[10] = 0;
    assert(tape_saved());
    assert(decode_tape(record, &value) && value.take == 37 && value.grid == 32 &&
           rtm_can_save(binding[0].sound));
    memset(binding, 0, sizeof(binding));
    memset(settings, 0, sizeof(settings));
    initialized = 0;
    relay_files.state = RF_IDLE;
    latest[8] = 0;
    count = 0;
    tick();
    assert(loading_tape && file_track == 0 && relay_files.action == RF_LOAD &&
           relay_files.number == 37);
    assert(rtm_load_current(0) && settings[0].rate == 100);
    tape_loaded();
    tick();
    assert(settings[0].rate == -73 && settings[0].mix == 29 && settings[0].spacing == 12288 &&
           binding[0].grid == 32);
    // A run of encoder events keeps the in-flight value stable and replaces the waiting value.
    count = head = 0;
    for (unsigned i = 1; i <= 100; ++i) {
        settings[0].mix = i;
        assert(send(RT_SETTINGS, 0, 0));
    }
    assert(count == 2 && ((queue[0].source >> 19) & 127) == 1 &&
           ((queue[1].source >> 19) & 127) == 100);
    // A queued recall carries the recorded grid even if the performer adjusts a knob immediately.
    assert(send_to(0, RT_SETTINGS, 0, 32));
    assert(send(RT_SETTINGS, 0, 0));
    assert(count == 4 && queue[2].end == 32 && !queue[3].end);
    // Commands keep their order when a full queue delays the latest encoder value.
    while (count < 8)
        assert(send(RT_FOCUS, 0, 0));
    assert(!send(RT_SETTINGS, 0, 0) && pending_settings == 1);
    head = (head + 2) & 7;
    count -= 2;
    assert(send(RT_RECORD, 100, 4) && !pending_settings && count == 8);
    assert(queue[0].operation == RT_SETTINGS && queue[1].operation == RT_RECORD);
    // Fast turns land on a musical ratio before continuing on the next turn.
    head = count = 0;
    settings[0].rate = 49;
    turn(2, 8);
    assert(settings[0].rate == 50);
    turn(2, 2);
    assert(settings[0].rate == 51);
    turn(2, -8);
    assert(settings[0].rate == 50);
    unsigned generation = binding[0].generation;
    latest[8] = latest[10] = 1u << 16;
    ++serial;
    tick();
    assert(binding[0].generation == generation && !binding[0].pending && file_track == 16);
    record[12] ^= 1;
    assert(!decode_tape(record, &value));
    sound[0xa2] = 17;
    assert(rtm_can_save((u32)(uintptr_t)sound));
    head = count = 0;
    status[4] = track;
    status[5] = 512;
    status[11] = 16u << 8;
    turn(5, -2);
    turn(6, 2);
    turn(6, 4);
    assert(count == 2 && queue[0].operation == RT_SEEK_WRITE && (int)queue[0].arg == -32);
    assert(queue[1].operation == RT_SEEK_READ && queue[1].arg == 96);
    play_click(0xfffffff0u);
    play_click(17);
    assert(count == 3 && queue[2].operation == RT_HOME_READ);
    play_click(300);
    finish_play_click();
    assert(count == 4 && queue[3].operation == RT_PLAY);
    // Accept an intact wire snapshot, then drop a reused row with its old tag.
    uint16_t packet[0x600] = {0}, native[0x401] = {0};
    u32 words[14] = {0x524c5932, 4, 0, 1000, 1u << 16, 512, 96, 1u << 16};
    u32 hash = 0;
    for (unsigned i = 1; i < 14; ++i)
        hash = hash * 31 + words[i];
    hash ^= hash >> 16;
    words[3] |= hash << 24;
    words[13] |= (hash >> 8) << 24;
    for (unsigned i = 0; i < 14; ++i) {
        unsigned row = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2,
                 k = (0x60 + 0x54 * row + (i & 1) * 4) / 2;
        packet[k] = words[i];
        packet[k + 1] = words[i] >> 16;
    }
    relay_reply_frame(packet);
    assert(received == 1 && latest[5] == 512);
    packet[847] = 0; // zero the running mask inside a previously signed reply
    relay_reply_frame(packet);
    assert(received == 1 && (latest[4] >> 16) == 1);
    native[0x4a] = relay_type;
    native[0x11] = 1;
    native[0x12] = 1;
    native[0x1a] = 12 << 8;
    const uint16_t *projected = project_gate(native, 1);
    assert(projected[0x11] == 1 && projected[0x12] == 0 && projected[0x1a] == 127 << 8);
    assert(native[0x12] == 1 && native[0x1a] == 12 << 8);
    latest[7] = 0;
    native[0x11] = 0;
    assert(project_gate(native, 0)[0x12] == 0);
    native[0x4a] = 3;
    assert(project_gate(native, 0) == native);

    // Full-scale peaks fill the envelope and silence stays centered.
    uint8_t pixels[64][128] = {0};
    struct RelayOverview wave = {.frames = RELAY_WAVE_BINS};
    for (unsigned i = 0; i < RELAY_WAVE_BINS; ++i)
        wave.peaks[i] = RELAY_WAVE_SILENT;
    wave.peaks[0] = 0xff00;
    status[5] = 0;
    page = 1;
    assert(draw_waveform(pixels[0], &wave, 0, 0, 0, 0));
    for (unsigned y = 19; y <= 49; ++y) {
        assert(pixels[y][17]);
        assert(pixels[y][18] == (y == 34));
    }
    // Quiet transients expand while keeping their relative heights.
    memset(pixels, 0, sizeof(pixels));
    wave.peaks[0] = 0x837d;
    wave.peaks[1] = 0x827e;
    assert(draw_waveform(pixels[0], &wave, 0, 0, 0, 0));
    for (unsigned y = 19; y <= 49; ++y) {
        assert(pixels[y][17] == (y >= 23 && y <= 45));
        assert(pixels[y][18] == (y >= 26 && y <= 42));
    }
    page = 0;
    memset(pixels, 0, sizeof(pixels));
    wave.peaks[0] = 0xff00;
    assert(draw_waveform(pixels[0], &wave, 0, 0, 0, 0));
    for (unsigned y = 0; y < 64; ++y)
        assert(pixels[y][17] == (y >= 36 && y <= 40));
    assert(!pixels[38][124]);

    // Capture one telemetry window across the loop boundary without losing its
    // previously measured column or coloring the unwritten middle of the tape.
    status[2] = 1;
    status[5] = 1200;
    status[6] = 22;
    status[12] = 1190;
    status[13] = (u32)wave.peaks[0] << 8;
    preview_waveform();
    assert(live_track == track && live_peaks[0] == wave.peaks[0] &&
           live_peaks[2] == wave.peaks[0] && live_peaks[119] == wave.peaks[0] &&
           live_peaks[30] == RELAY_WAVE_EMPTY);
    status[2] = 0;
    preview_waveform();
    assert(live_track == 16); // A canceled recording never becomes the fallback waveform.

    // The compact control page and larger take page keep their geometry when values are shown.
    settings[track].rate = -50;
    settings[track].mix = 37;
    settings[track].lane = 0;
    relay_files.state = RF_IDLE;
    memset(pixels, 0, sizeof(pixels));
    draw(0, pixels[0], 0);
    assert(rotary_cells == 0x8e && has_text(59, 30, "RATE") && has_text(111, 58, "MIX"));
    assert(pixels[38][64] && !pixels[35][64] && !pixels[18][64]);
    text_count = 0;
    draw(0, pixels[0], 255);
    assert(has_text(59, 30, "-0.50") && has_text(111, 58, "37") && !has_text(59, 30, "RATE"));
    text_count = rotary_cells = 0;
    page = 1;
    draw(0, pixels[0], 0);
    assert(!rotary_cells && has_text(33, 11, "LANE") && has_text(85, 58, "EXPT") && pixels[18][17]);
    text_count = 0;
    draw(0, pixels[0], 255);
    assert(has_text(33, 11, "MAIN") && !has_text(33, 11, "LANE") && pixels[18][17]);
    return 0;
}
