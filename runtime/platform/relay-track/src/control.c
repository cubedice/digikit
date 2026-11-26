#if defined(__m68k__) && defined(__GNUC__)
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include <dk/fxview.h>
#include <dk/platform/ui.h>
#include "../../instrument-play/src/cf/sound-persist.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
#include "files.h"
#include "../../instrument-play/src/cf/src-pages.h"
#include "../../../../sdk/dsp/contract/dk_relay_track.inc"
#include "relay-seam.h"

typedef uint32_t u32;
typedef uint8_t u8;
extern const u32 rows_info[];
extern volatile u32 pp_position[16];
#ifdef DK_SERVICE
/* services/relay.dks: the image's data through the image table (relay-seam.h) */
#define rows_info ((const u32 *)RT_DATA(rows_info))
#define pp_position ((volatile u32 *)RT_DATA(pp_position))
#endif
extern u32 rtm_type(u8 *), rtm_track(u32), pp_record_frames(u32), pp_record_steps(void);
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);
extern void ip_src_draw_rotary(u8 *view, u8 *canvas, u32 cell, u32 value);
extern void ip_src_focus_touch(u8 *, u32);
extern u8 ip_src_focus_active(u8 *, u32);
struct Settings {
    unsigned input, hits, sensitivity;
    int rate;
    unsigned gain, feedback, mix, spacing; // 65536 is one complete lap.
    unsigned loop_q16;
};
static struct Settings settings[16];
static unsigned relay_type, mod_ready;
static unsigned initialized, configured, view_pending, stop_pending;
struct Command {
    u32 sequence, operation, track, source, length, arg, end;
};
static struct Command queue[8];
static unsigned head, count, sequence, track = 16, page, confirm, held;
static int travel[8];
static unsigned edit_gain, source_shortcut, pending_settings, focus, paint_values;
enum { WAVE_COLUMNS = 106 };
static uint16_t live_peaks[RELAY_WAVE_BINS];
static unsigned live_track = 16, live_frames, live_revision, live_mode;
static volatile u32 latest[14], received;
static u32 status[14], seen, read_offset;
static volatile u32 latest_read_offset;
static unsigned play_click_track = 16;
static u32 play_click_at;
extern u32 instrument_src_now(void);
static unsigned scan_slot = 48, scan_revision;
static void num(char *s, unsigned n);
static void refresh_waveform(void);
static void refresh_launch(void);
static void preview_waveform(void);
static const char *notice;
static unsigned redraw = 1;
extern dk_ui *dk_ui_core_handle(void);
static const char *const inputs[] = {"EXT LR", "EXT L", "EXT R", "T01", "T02", "T03", "T04",
                                     "T05",    "T06",   "T07",   "T08", "T09", "T10", "T11",
                                     "T12",    "T13",   "T14",   "T15", "T16", "MAIN"};
static void say(const char *s)
{
    redraw = 1;
    notice = s;
}
static int send_to(unsigned track, unsigned op, unsigned arg, unsigned end)
{
    if (track >= 16)
        return 0;
    struct Settings *s = &settings[track];
    u32 sr = dkrt_irq_mask();
    if (!++sequence)
        ++sequence;
    struct Command command = {sequence, op, track, s->input, 0, arg, end};
    if (op == RT_SETTINGS) {
        command.source |= s->gain << 5 | s->feedback << 12 | s->mix << 19;
        if (mod_ready & (1u << track))
            command.source |= relay_type << 26;
        command.length = (u32)(s->rate * 65536 / 100);
        command.arg = s->spacing;
        command.end = (end & 65535) | s->loop_q16 << 16;
    }
    // Combine adjacent detents without changing the command already in flight.
    if (count > 1 && (op == RT_SEEK_READ || op == RT_SEEK_WRITE) && end) {
        struct Command *tail = &queue[(head + count - 1) & 7];
        if (tail->operation == op && tail->track == track && tail->end == end) {
            command.arg = ((int32_t)tail->arg + (int32_t)arg) % (int32_t)end;
            *tail = command;
            dkrt_irq_restore(sr);
            return 1;
        }
    }
    if (count > 1 && op == RT_SETTINGS && !end) {
        struct Command *tail = &queue[(head + count - 1) & 7];
        if (tail->operation == op && tail->track == track && !(tail->end & 65535)) {
            *tail = command;
            dkrt_irq_restore(sr);
            return 1;
        }
    }
    if (count == 8) {
        dkrt_irq_restore(sr);
        if (op != RT_SETTINGS)
            say("CONTROLS BUSY");
        return 0;
    }
    if (op == RT_SETTINGS)
        configured |= 1u << track;
    queue[(head + count) & 7] = command;
    ++count;
    dkrt_irq_restore(sr);
    return 1;
}
static int send(unsigned op, unsigned arg, unsigned end)
{
    if (track >= 16)
        return 0;
    unsigned bit = 1u << track;
    if (op == RT_SETTINGS || (pending_settings & bit)) {
        if (!send_to(track, RT_SETTINGS, 0, 0)) {
            pending_settings |= bit;
            return 0;
        }
        pending_settings &= ~bit;
        if (op == RT_SETTINGS)
            return 1;
    }
    return send_to(track, op, arg, end);
}
// Resolve the single press only once the double-press window has closed.
static void finish_play_click(void)
{
    if (play_click_track < 16) {
        send_to(play_click_track, RT_PLAY, 0, 0);
        play_click_track = 16;
    }
}
static unsigned loop_frames(unsigned length, unsigned loop_q16)
{
    if (!loop_q16 || length < 4)
        return length;
    unsigned span = (length >> 16) * loop_q16 + ((length & 65535) * loop_q16 >> 16);
    return span < 4 ? 4 : span;
}
static unsigned loop_steps(unsigned steps, unsigned loop_q16)
{
    if (!loop_q16)
        return steps;
    unsigned shown = (steps * loop_q16 + 32768) >> 16;
    return shown ? shown : 1;
}
static int first_pass(void)
{
    return (status[2] & 15) == 1 && (status[2] >> 8) == track;
}
static unsigned current_loop_q16(void)
{
    return first_pass() ? 0 : settings[track].loop_q16;
}
static void play_click(u32 now)
{
    if (play_click_track == track && now - play_click_at < 240) {
        play_click_track = 16;
        if (status[4] == track && status[5])
            send(RT_HOME_READ, 0, loop_frames(status[5], settings[track].loop_q16));
        return;
    }
    finish_play_click();
    play_click_track = track;
    play_click_at = now;
}
int rtm_write_command(volatile uint16_t *dst)
{
    if (!count)
        return 0;
    const struct Command *c = queue + head;
    u32 packet[8] = {RT_MAGIC,  c->sequence, c->operation, c->track,
                     c->source, c->length,   c->arg,       c->end};
    for (unsigned i = 0; i < 8; ++i) {
        dst[4 * i] = packet[i];
        dst[4 * i + 2] = packet[i] >> 16;
    }
    dst[60] = c->sequence;
    dst[62] = c->sequence >> 16;
    return 1;
}
void relay_reply_frame(const volatile uint16_t *p)
{
    relay_files_reply(p);
    if (!p || (p[762] | ((u32)p[763] << 16)) != 0x524c5932)
        return;
    u32 words[14];
    for (unsigned i = 0; i < 14; ++i) {
        unsigned row = i < 8 ? 17 + i / 2 : 29 + (i - 8) / 2,
                 k = (0x1c + 0x54 * row + 0x44 + (i & 1) * 4) / 2;
        words[i] = p[k] | ((u32)p[k + 1] << 16);
    }
    unsigned check = (words[3] >> 24) | ((words[13] >> 16) & 0xff00);
    words[3] &= 0xffffff;
    words[13] &= 0xffffff;
    u32 hash = 0;
    for (unsigned i = 1; i < 14; ++i)
        hash = hash * 31 + words[i];
    if (((hash ^ (hash >> 16)) & 0xffff) != check)
        return;
    latest_read_offset = (words[2] >> 16) | ((words[4] & 0xff00) << 8);
    words[2] &= 0xffff;
    words[4] &= 0xffff00ff;
    for (unsigned i = 0; i < 14; ++i)
        latest[i] = words[i];
    ++received;
}
static void tape_sound_tick(void);
static int tape_saved(void);
static void tape_file_failed(void);
static void share_result(unsigned t, unsigned error);
static int save_tape(unsigned t, unsigned hits, unsigned sensitivity);
static void tape_loaded(void);
static void publish_sound(unsigned t);
static unsigned recall_choice(unsigned key);
void relay_ui_pass(void)
{
    static unsigned last_file_state, last_scan_progress;
    if (play_click_track < 16 && instrument_src_now() - play_click_at >= 240)
        finish_play_click();
    relay_files_tick();
    tape_sound_tick();
    unsigned progress = relay_files.action == RF_SCAN && relay_files.slot == scan_slot &&
                                track < 16 && scan_slot % 16 == track
                            ? relay_files.progress
                            : 0;
    if (progress != last_scan_progress) {
        last_scan_progress = progress;
        redraw = 1;
    }
    if (last_file_state != relay_files.state) {
        last_file_state = relay_files.state;
        redraw = 1;
        if (last_file_state == RF_SAVED && tape_saved())
            say("SAVE SOUND AGAIN");
        if (last_file_state == RF_LOADED)
            tape_loaded();
        if (last_file_state == RF_FAILED)
            tape_file_failed();
        if (last_file_state == RF_FAILED && relay_files.action != RF_SCAN)
            say(relay_files.error ? relay_files.error : "FILE FAILED");
        if (!relay_files.overview.frames && relay_files.action != RF_SCAN &&
            (last_file_state == RF_SAVED || last_file_state == RF_FAILED))
            scan_slot = 48;
    }
    if (notice) {
        if (track < 16)
            dk_ui_toast(dk_ui_core_handle(), notice);
        notice = 0;
    }
    if (seen == received)
        return;
    u32 sr = dkrt_irq_mask();
    int focused = track < 16 && (latest[4] & 255) == track;
    if (focused && (status[4] != track || status[2] != latest[2] || status[5] != latest[5] ||
                    status[6] != latest[6] || (status[7] & 1) != (latest[7] & 1) ||
                    ((status[8] ^ latest[8]) & 7) || status[9] != latest[9] ||
                    ((status[10] ^ latest[10]) & 1) || status[11] != latest[11] ||
                    read_offset != latest_read_offset))
        redraw = 1;
    if (focused)
        for (unsigned i = 0; i < 14; ++i)
            status[i] = latest[i];
    if (focused) {
        status[4] = track;
        read_offset = latest_read_offset;
    }
    seen = received;
    int ack = count && queue[head].sequence == latest[1];
    unsigned ack_track = ack ? queue[head].track : 16;
    if (ack) {
        if (queue[head].operation == RT_SHARE)
            share_result(queue[head].track, latest[13] & 255);
        head = (head + 1) & 7;
        --count;
    }
    dkrt_irq_restore(sr);
    if (ack && pending_settings) {
        for (unsigned t = 0; t < 16; ++t) {
            if ((pending_settings & (1u << t)) && send_to(t, RT_SETTINGS, 0, 0)) {
                pending_settings &= ~(1u << t);
                break;
            }
        }
    }
    static const char *const errors[] = {
        "",      "RAM FULL",    "LANES FULL", "NO TAKE", "RECORDER BUSY",
        "LIMIT", "SET PAT LEN", "SAME TRACK"};
    unsigned error = status[13] & 255;
    if (focused && ack_track == track && error && error < 8) {
        static char detail[24];
        if (error == 1) {
            const char *prefix = "RAM FULL ";
            unsigned i = 0;
            while (prefix[i]) {
                detail[i] = prefix[i];
                ++i;
            }
            num(detail + i, status[3] / 48000);
            while (detail[i])
                ++i;
            detail[i++] = 's';
            detail[i] = 0;
            say(detail);
        } else if (error == 4 && (status[2] & 15)) {
            unsigned writer = 1 + (status[2] >> 8);
            static char owner[] = "REC ON T00";
            owner[8] = '0' + writer / 10;
            owner[9] = '0' + writer % 10;
            say(owner);
        } else
            say(errors[error]);
    }
    if (stop_pending && send_to(0, RT_TRANSPORT_STOP, 0, 0))
        stop_pending = 0;
    if (view_pending && track < 16 && send(RT_VIEW, 0, 0))
        view_pending = 0;
    refresh_launch();
    if (focused) {
        preview_waveform();
        refresh_waveform();
    }
}
int rtm_is_type(unsigned type)
{
    /* the machine rows of core/rows' table rows_info: words M_FIRST 5, M_COUNT 6, M_ROWS 3, M_ROW_SIZE 4, and O_NAME 18 the name's offset in a row */
    unsigned first = rows_info[5];
    if (type < first || type - first >= rows_info[6])
        return 0;
    const char *n =
        *(const char **)(uintptr_t)(rows_info[3] + rows_info[4] * (type - first) + rows_info[18]);
    return n && n[0] == 'R' && n[1] == 'E' && n[2] == 'L' && n[3] == 'A' && n[4] == 'Y' && !n[5];
}
extern const u8 rtm_mod_rate, rtm_mod_spacing, rtm_mod_feedback, rtm_mod_mix, rtm_mod_gain,
    rtm_mod_position;
static const u8 mod_mirrors[] = {26, 27, 28, 31, 32, 33};
u32 rtm_mod_param(u32 type, u32 mirror)
{
    // the registry codes of the params entries (RT_VALUE: the symbol's address in the image, the image table's word in the service)
    const u32 descriptors[] = {RT_VALUE(rtm_mod_rate),     RT_VALUE(rtm_mod_spacing),
                               RT_VALUE(rtm_mod_feedback), RT_VALUE(rtm_mod_mix),
                               RT_VALUE(rtm_mod_gain),     RT_VALUE(rtm_mod_position)};
    if (!rtm_is_type(type))
        return UINT32_MAX;
    for (unsigned i = 0; i < 6; ++i)
        if (mirror == mod_mirrors[i])
            return descriptors[i];
    return mirror == 29 || mirror == 30 ? 0 : UINT32_MAX;
}
int rtm_mod_carrier(u32 type)
{
    return type == relay_type && rtm_is_type(type);
}
static u32 sent_launch;
static volatile u32 need_sound;
extern int ip_refresh_machine_track(u32 track, u32 type);
static void refresh_launch(void)
{
    static unsigned prepared;
    unsigned need = need_sound;
    prepared &= need;
    unsigned missing = need & ~prepared;
    if (!missing || !relay_type)
        return;
    for (unsigned t = 0; t < 16; ++t)
        if ((missing & (1u << t)) && ip_refresh_machine_track(t, relay_type))
            prepared |= 1u << t;
}
static uint16_t gate_frame[0x401];
static unsigned continuous;

// The stock frame driver consumes the returned frame synchronously. Keep its
// source intact: the sequencer reuses it, including while step recording.
static const uint16_t *project_gate(const uint16_t *src, unsigned rising)
{
    unsigned owned = 0;
    for (unsigned t = 0; t < 16; ++t)
        if (src[0x4a + t] == relay_type)
            owned |= 1u << t;
    unsigned inject = rising & owned;
    need_sound = rising & ~inject;
    sent_launch |= inject;
    continuous |= inject;
    unsigned release = continuous & owned & src[0x12];
    unsigned velocity = inject | (continuous & owned & src[0x11]);
    if (!(velocity | release))
        return src;
    for (unsigned i = 0; i < 0x401; ++i)
        ((volatile uint16_t *)gate_frame)[i] = src[i];
    gate_frame[0x11] |= inject;
    gate_frame[0x12] &= ~(inject | release);
    for (unsigned t = 0; t < 16; ++t)
        if (velocity & (1u << t))
            gate_frame[0x1a + t] = 127u << 8;
    return gate_frame;
}
const uint16_t *rtm_gate_frame(const uint16_t *src)
{
    unsigned want = latest[7] >> 16, rising = want & ~sent_launch;
    sent_launch &= want;
    need_sound &= want;
    // A panel/transport launch has no physical key to release. Its carrier
    // stays at full velocity on subsequent note edges until the motor stops.
    continuous &= latest[4] >> 16;
    if (!src || src[0x15] || !(rising | (continuous & (src[0x11] | src[0x12]))) || !relay_type ||
        !rtm_is_type(relay_type))
        return src;
    return project_gate(src, rising);
}
static void leave(void)
{
    finish_play_click();
    notice = 0;
    held = confirm = edit_gain = source_shortcut = focus = 0;
    for (unsigned i = 0; i < 8; ++i)
        travel[i] = 0;
}
static const struct RelayOverview *current_wave(void)
{
    if (track >= 16 || status[4] != track)
        return 0;
    return rtm_wave(track, status[9], status[5], 0, 1);
}
static void preview_waveform(void)
{
    unsigned mode = status[2] & 31, length = status[5], revision = status[9];
    if ((status[2] >> 8) != track || (mode != 1 && mode != 2) || !length) {
        if (live_track == track && revision == live_revision)
            live_track = 16;
        return;
    }
    if (live_track != track || live_frames != length || live_revision != revision ||
        live_mode != mode) {
        for (unsigned i = 0; i < RELAY_WAVE_BINS; ++i)
            ((volatile uint16_t *)live_peaks)[i] = RELAY_WAVE_EMPTY;
        live_track = track;
        live_frames = length;
        live_revision = revision;
        live_mode = mode;
    }
    uint16_t wave = (uint16_t)(status[13] >> 8);
    if (wave == RELAY_WAVE_EMPTY)
        return;
    unsigned span = mode == 1 ? length : loop_frames(length, settings[track].loop_q16);
    unsigned first = status[12] % span, last = status[6] % span;
    if ((last + span - first) % span > span / 2) {
        unsigned swap = first;
        first = last;
        last = swap;
    }
    first = first * RELAY_WAVE_BINS / length;
    last = last * RELAY_WAVE_BINS / length;
    unsigned end = (span - 1) * RELAY_WAVE_BINS / length;
    for (unsigned bin = first;; bin = bin == end ? 0 : bin + 1) {
        uint16_t peak = relay_wave_join(live_peaks[bin], wave);
        if (peak != live_peaks[bin]) {
            live_peaks[bin] = peak;
            redraw = 1;
        }
        if (bin == last)
            break;
    }
}
static void refresh_waveform(void)
{
    if (track >= 16 || !seen || status[4] != track || !status[5])
        return;
    if ((status[2] & 15) && (status[2] >> 8) == track)
        return;
    if (current_wave())
        return;
    if (track == scan_slot && scan_revision == status[9] && relay_files.action == RF_SCAN &&
        (relay_files.state == RF_WORK || relay_files.state == RF_FAILED))
        return;
    if (relay_files.state == RF_WORK && relay_files.action != RF_SCAN)
        return;
    if (!relay_file_start(RF_SCAN, track, track, 1, 0, 1))
        return;
    scan_slot = track;
    scan_revision = status[9];
}
static unsigned clamp(int x, unsigned hi)
{
    return x < 0 ? 0 : (unsigned)x > hi ? hi : (unsigned)x;
}
static int route_ok(unsigned input);
static unsigned grid(void)
{
    unsigned n = status[5] ? status[11] >> 8 : pp_record_steps();
    return n ? n : 1;
}
static void turn(unsigned enc, int delta)
{
    if (enc > 8 || !enc || confirm)
        return;
    struct Settings *s = settings + track;
    int *t = &travel[enc - 1];
    *t += delta;
    delta = *t / 2;
    *t -= delta * 2;
    if (!delta)
        return;
    if (enc == 1 && !edit_gain) {
        int input = clamp((int)s->input + delta, RT_INPUT_MAIN);
        if (input == (int)track + 3)
            input += delta < 0 ? -1 : 1;
        if (!route_ok(input))
            return;
        s->input = input;
    } else if (enc == 1)
        s->gain = clamp((int)s->gain + delta, 100);
    else if (enc == 2) {
        int rate = (int)clamp(s->rate + 200 + delta, 400) - 200;
        static const int stops[] = {-100, -50, 0, 50, 100};
        for (unsigned i = 0; i < 5; ++i) {
            int stop = stops[delta > 0 ? i : 4 - i];
            if ((delta > 0 && s->rate < stop && rate > stop) ||
                (delta < 0 && s->rate > stop && rate < stop)) {
                rate = stop;
                break;
            }
        }
        s->rate = rate;
    } else if (enc == 4)
        s->feedback = clamp((int)s->feedback + delta, 100);
    else if (enc == 5 || enc == 6) {
        if (status[4] == track && status[5]) {
            unsigned fraction = current_loop_q16();
            unsigned steps = loop_steps(grid(), fraction);
            unsigned length = loop_frames(status[5], fraction);
            int ticks = delta % (int)steps;
            unsigned n = ticks < 0 ? -ticks : ticks;
            int frames = (length / steps) * n + ((length % steps) * n + steps / 2) / steps;
            if (frames)
                send(enc == 5 ? RT_SEEK_WRITE : RT_SEEK_READ, ticks < 0 ? -frames : frames, length);
        }
        return;
    } else if (enc == 7 && status[4] == track && status[5] && !first_pass()) {
        unsigned steps = grid();
        unsigned step = clamp((int)loop_steps(steps, s->loop_q16) + delta, steps);
        if (!step)
            step = 1;
        s->loop_q16 = step == steps ? 0 : (step * 65536 + steps / 2) / steps;
    } else if (enc == 8)
        s->mix = clamp((int)s->mix + delta, 100);
    else
        return;
    send(RT_SETTINGS, 0, 0);
    publish_sound(track);
}
static void press(unsigned key)
{
    struct Settings *s = settings + track;
    if (key != 46)
        finish_play_click();
    if (confirm) {
        recall_choice(key);
        return;
    }
    if (key == 42) {
        s->rate = 100;
        send(RT_SETTINGS, 0, 0);
        publish_sound(track);
    } else if (key == 45)
        send(RT_WRITE, pp_record_frames(0), pp_record_steps());
    else if (key == 46 && status[4] == track && status[5] &&
             (!(status[2] & 15) || status[2] >> 8 != track))
        play_click(instrument_src_now());
    else if (key == 47)
        send(RT_UNDO, 0, 0);
}
extern u32 ip_current_machine_sound(u32 track, u32 type);
static int route_ok(unsigned input)
{
    unsigned visited = 1u << track;
    while (input >= 3 && input < 19) {
        unsigned source = input - 3;
        if (visited & (1u << source)) {
            say("CIRCULAR INPUT");
            return 0;
        }
        if (!ip_current_machine_sound(source, relay_type))
            break;
        visited |= 1u << source;
        input = settings[source].input;
    }
    return 1;
}
static void num(char *s, unsigned n)
{
    char rev[10];
    unsigned i = 0;
    do {
        rev[i++] = '0' + n % 10;
        n /= 10;
    } while (n);
    while (i)
        *s++ = rev[--i];
    *s = 0;
}
static int wave_height(unsigned level, unsigned gain)
{
    int sample = (int)level - 128;
    unsigned amplitude = (unsigned)(sample < 0 ? -sample : sample);
    int height = (int)((amplitude * gain + 128) >> 8);
    return sample < 0 ? height : -height;
}
static void draw_waveform(u8 *b, const struct RelayOverview *overview, unsigned live,
                          unsigned writing, unsigned span, unsigned write, unsigned read)
{
    enum { LEFT = 17 };
    int top = page ? 22 : 35, bottom = page ? 54 : 41;
    int middle = (top + bottom) / 2;
    if (page) {
        fxv_box(b, LEFT - 1, top + 1, 1, bottom - top - 1, 1);
        fxv_box(b, LEFT + WAVE_COLUMNS, top + 1, 1, bottom - top - 1, 1);
        fxv_box(b, LEFT, top, WAVE_COLUMNS, 1, 1);
        fxv_box(b, LEFT, bottom, WAVE_COLUMNS, 1, 1);
    }
    uint16_t columns[WAVE_COLUMNS];
    /* Enlarge quiet tape by up to 32x, then scale to the strongest visible peak. */
    unsigned reference = 4;
    for (unsigned col = 0; col < WAVE_COLUMNS; ++col) {
        unsigned bin = col * RELAY_WAVE_BINS / WAVE_COLUMNS;
        uint16_t wave = RELAY_WAVE_EMPTY;
        for (; bin < (col + 1) * RELAY_WAVE_BINS / WAVE_COLUMNS; ++bin) {
            uint16_t peak = live ? live_peaks[bin] : RELAY_WAVE_EMPTY;
            if (peak == RELAY_WAVE_EMPTY && overview)
                peak = overview->peaks[bin];
            wave = relay_wave_join(wave, peak);
        }
        if (wave == RELAY_WAVE_EMPTY)
            wave = RELAY_WAVE_SILENT;
        columns[col] = wave;
        int down = 128 - (int)(wave & 255), up = (int)(wave >> 8) - 128;
        int peak = down > up ? down : up;
        if (peak > (int)reference)
            reference = (unsigned)peak;
    }
    unsigned gain = (((middle - top - 1) << 8) + reference / 2) / reference;
    for (unsigned col = 0; col < WAVE_COLUMNS; ++col) {
        int high = wave_height(columns[col] >> 8, gain);
        int low = wave_height(columns[col] & 255, gain);
        if (low >= bottom - middle)
            low = bottom - middle - 1;
        fxv_box(b, LEFT + col, middle + high, 1, low - high + 1, 1);
    }
    if (!span)
        return;
    unsigned total = status[5];
    if (span < total) {
        unsigned end = LEFT + span * WAVE_COLUMNS / total;
        for (int y = top; y <= bottom; y += 2)
            fxv_box(b, end, y, 1, 1, -1);
    }
    unsigned head = LEFT + write * WAVE_COLUMNS / total;
    unsigned playback = LEFT + read * WAVE_COLUMNS / total;
    if (writing)
        fxv_box(b, head - 1, top, 3, 2, -1);
    fxv_box(b, playback - 1, bottom - 1, 3, 2, -1);
}
static void time_text(char *text, unsigned frames)
{
    unsigned tenths = frames / 4800;
    num(text, tenths / 10);
    while (*text)
        ++text;
    *text++ = '.';
    *text++ = '0' + tenths % 10;
    *text = 0;
}
static void transport_mark(u8 *b, unsigned writing)
{
    int x = 82, y = page ? 12 : 17; /* Encoder C's top cell. */
    if (writing) {
        fxv_box(b, x + 2, y, 3, 7, 1);
        fxv_box(b, x + 1, y + 1, 5, 5, 1);
        fxv_box(b, x, y + 2, 7, 3, 1);
        if (status[2] & 16)
            fxv_box(b, x + 2, y + 2, 3, 3, 0);
    } else if (status[7] & 1) {
        for (int i = 0; i < 4; ++i)
            fxv_box(b, x + 1 + i, y + i, 1, 7 - 2 * i, 1);
    } else {
        fxv_box(b, x + 1, y, 2, 7, 1);
        fxv_box(b, x + 4, y, 2, 7, 1);
    }
}
static void draw(u8 *view, u8 *b, unsigned values)
{
    const struct Settings *s = settings + track;
    static const char *const labels[8] = {"INPT",  "RATE", "",    "FDBK",
                                          "WRITE", "PLAY", "LEN", "MIX"};
    unsigned writing = (status[2] & 15) && (status[2] >> 8) == track;
    unsigned fraction = current_loop_q16();
    unsigned total = status[5], span = loop_frames(total, fraction);
    unsigned write = span ? status[6] % span : 0, read = span ? (write + read_offset) % span : 0;
    unsigned steps = grid(), length = loop_steps(steps, fraction);
    char rate[6], feedback[4], mix[4], gain[4], len[8], write_at[8], read_at[8];
    unsigned speed = s->rate < 0 ? -s->rate : s->rate;
    rate[0] = s->rate < 0 ? '-' : '+';
    rate[1] = '0' + speed / 100;
    rate[2] = '.';
    rate[3] = '0' + speed / 10 % 10;
    rate[4] = '0' + speed % 10;
    rate[5] = 0;
    num(feedback, s->feedback);
    num(mix, s->mix);
    num(gain, s->gain);
    num(len, length);
    time_text(write_at, write);
    time_text(read_at, read);
    const char *input = inputs[s->input];
    const char *const shown[8] = {
        edit_gain ? gain : input, rate, "", feedback, span ? write_at : "-",
        span ? read_at : "-",     len,  mix};

    /* Keep the native header, track column and page indicators. */
    fxv_box(b, 16, 10, 108, 54, 0);
    transport_mark(b, writing);
    if (!page) {
        unsigned knobs[8] = {s->gain * 127 / 100,
                             (s->rate + 200) * 127 / 400,
                             0,
                             s->feedback * 127 / 100,
                             span ? write * 127 / span : 0,
                             span ? read * 127 / span : 0,
                             length * 127 / steps,
                             s->mix * 127 / 100};
        for (unsigned i = edit_gain ? 0 : 1; i < 8; ++i)
            if (i != 2)
                ip_src_draw_rotary(view, b, i, knobs[i]);
        if (!edit_gain && !(values & 1))
            fxv_text(b, 33, 21, input, 1);
    }
    for (unsigned i = 0; i < 8; ++i) {
        if (i == 2)
            continue;
        int x = 33 + (i % 4) * 26;
        const char *label = labels[i];
        if (!i)
            label = edit_gain ? "GAIN" : page ? input : label;
        fxv_text(b, x, i >= 4 ? 58 : page ? 13 : 30, values & (1u << i) ? shown[i] : label, 1);
    }
    const struct RelayOverview *o = current_wave();
    unsigned live =
        live_track == track && live_frames == total &&
        ((writing && live_revision == status[9]) || (!o && live_revision + 1 == status[9]));
    if (!o && live && live_mode == 2)
        o = rtm_wave(track, live_revision, live_frames, 0, 1);
    if (!o && !live)
        o = rtm_wave_pending(track, status[9], total);
    draw_waveform(b, o, live, writing, span, write, read);

    if (confirm >= 3) {
        static const char *const choices[] = {"CANCEL? YES", "SAVE? YES", "DISCARD? YES"};
        fxv_text(b, 70, page ? 33 : 35, choices[confirm - 3], 0);
    }
}
static void initialize_settings(void)
{
    if (!initialized) {
        for (unsigned n = 0; n < 16; ++n) {
            settings[n].sensitivity = 4;
            settings[n].rate = 100;
            settings[n].gain = settings[n].feedback = settings[n].mix = 100;
            settings[n].spacing = 65536;
        }
        initialized = 1;
    }
}
int rtm_src(u8 *view, unsigned action, u8 *data)
{
    initialize_settings();
    unsigned type = rtm_type(view), t = rtm_track(*(u32 *)(view + 0x74));
    if (!rtm_is_type(type) || t >= 16 || action == 4) {
        if (track < 16) {
            leave();
            track = 16;
        }
        return 0;
    }
    relay_type = type;
    unsigned p = *(u32 *)(view + 0x90) == 1;
    if (track != t || page != p) {
        redraw = 1;
        int changed_track = track != t;
        leave();
        track = t;
        page = p;
        if (changed_track) {
            if (!(configured & (1u << t)))
                send(RT_SETTINGS, 0, 0);
            view_pending = !send(RT_VIEW, 0, 0);
        }
        if (!seen || status[4] != track) {
            for (unsigned i = 0; i < 14; ++i)
                status[i] = 0;
            status[4] = 16;
        }
    }
    if (action == 0) {
        static const MfSrcCapability cap = {MF_SRC_ABI, sizeof(cap), 2, RT_MAGIC};
        mf_src_register(type, &cap);
        return 1;
    }
    if (action == 1 || action == 5) {
        unsigned values = view[0x190] ? 255
                          : !page && focus && ip_src_focus_active(view + 0x94, focus - 1)
                              ? 1u << (focus - 1)
                              : 0;
        if (action == 5)
            return redraw || values != paint_values ? 2 : 1;
        draw(view, data, values);
        paint_values = values;
        redraw = 0;
        return 1;
    }
    unsigned key = *(u32 *)(data + 12), v = *(u32 *)(data + 16);
    if (action == 3) {
        if (!key || key > 8)
            return 0;
        turn(key, (int)v);
        focus = key;
        ip_src_focus_touch(view + 0x94, key - 1);
    } else {
        if (!((key >= 41 && key <= 48) ||
              (confirm && (key == 10 || key == 12 || key == 14 || key == 15))))
            return 0;
        unsigned flags = fxv_key_flags(v);
        if (key == 41 && !confirm) {
            if (flags & FXV_PRESS) {
                held = 1;
                source_shortcut = 0;
            }
            if (flags & FXV_RELEASE) {
                if (held && !source_shortcut)
                    edit_gain ^= 1;
                held = 0;
            }
        }
        if ((flags & FXV_PRESS) && ((seen && status[4] == track) || key == 45 || key == 46))
            press(key);
    }
    dk_fw_view_invalidate(view);
    return 1;
}
// A second STOP has no running-to-stopped clock edge. Observe the plain stock
// button as well so it silences a manually launched motor on any SRC page.
void rtm_stop_key(u32 flags)
{
    if (configured && (fxv_key_flags(flags) & FXV_PRESS) && !dk_fw_input_key_down(FXV_FUNC) &&
        !dk_fw_input_key_down(16)) {
        play_click_track = 16;
        if (pp_position[14] == 0)
            stop_pending = !send_to(0, RT_TRANSPORT_STOP, 0, 0);
    }
}
int relay_trig(u8 *e)
{
    unsigned key = *(u32 *)(e + 12);
    if (!held || track >= 16 || key < 25 || key >= 41)
        return 0;
    if (fxv_key_flags(*(u32 *)(e + 16)) & FXV_PRESS) {
        source_shortcut = 1;
        if (key - 25 == track)
            say("SAME TRACK");
        else {
            if (!route_ok(key - 22))
                return 1;
            settings[track].input = key - 22;
            send(RT_SETTINGS, 0, 0);
            publish_sound(track);
        }
    }
    return 1;
}

#include "tape-sound.inc"
