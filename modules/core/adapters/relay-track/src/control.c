#if defined(__m68k__) && defined(__GNUC__)
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include <dk/fxview.h>
#include "../../instrument-play/src/cf/sound-persist.h"
#include "dk_fw_view.h"
#include "dk_fw_input.h"
#include "files.h"
#include "../../machines/src/cf/dt2/src-pages.h"
#include "../../../../../sdk/dsp/contract/dk_relay_track.inc"

typedef uint32_t u32;
typedef uint8_t u8;
extern const u32 machines_info[];
extern u32 rtm_type(u8 *), rtm_track(u32), pp_record_frames(u32), pp_record_steps(void);
extern u32 dkrt_irq_mask(void);
extern void dkrt_irq_restore(u32);
struct Settings {
    unsigned input, lane, hits, sensitivity, mark;
    int rate;
    unsigned gain, feedback, mix, spacing; // 65536 is one complete lap.
};
static struct Settings settings[16];
static unsigned relay_type, mod_ready;
static unsigned initialized, configured, view_pending, stop_pending;
struct Command {
    u32 sequence, operation, track, source, length, arg, end;
};
static struct Command queue[8];
static unsigned head, count, sequence, track = 16, page, audition, confirm, held;
static int travel[8];
static unsigned edit_gain, source_shortcut, pending_settings;
enum { WAVE_COLUMNS = 110 };
static uint16_t live_peaks[RELAY_WAVE_BINS];
static unsigned live_track = 16, live_frames, live_revision, live_mode;
static volatile u32 latest[14], received;
static u32 status[14], seen, read_offset;
static volatile u32 latest_read_offset;
static unsigned play_click_track = 16;
static u32 play_click_at;
extern u32 instrument_src_now(void);
static unsigned scan_slot = 48, scan_revision, scan_hits, scan_sens;
static void num(char *s, unsigned n);
static void refresh_waveform(void);
static void refresh_launch(void);
static void preview_waveform(void);
static const char *notice;
static unsigned notice_time, redraw = 1;
static const char *const inputs[] = {"EXT LR", "EXT L", "EXT R", "T01", "T02", "T03", "T04",
                                     "T05",    "T06",   "T07",   "T08", "T09", "T10", "T11",
                                     "T12",    "T13",   "T14",   "T15", "T16"};
static const char *const lanes[] = {"MAIN", "T1", "T2"};
static void say(const char *s)
{
    redraw = 1;
    notice = s;
    notice_time = 60;
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
        if (tail->operation == op && tail->track == track && !tail->end) {
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
static void play_click(u32 now)
{
    if (play_click_track == track && now - play_click_at < 240) {
        play_click_track = 16;
        if (status[4] == track && status[5])
            send(RT_HOME_READ, 0, status[5]);
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
        if (last_file_state == RF_SAVED)
            say(tape_saved() ? "SAVE SOUND AGAIN" : "SAVED");
        if (last_file_state == RF_LOADED)
            tape_loaded();
        if (last_file_state == RF_FAILED)
            tape_file_failed();
        if (last_file_state == RF_FAILED &&
            (relay_files.action != RF_SCAN || scan_slot % 16 == track))
            say(relay_files.action == RF_SCAN ? "NO WAVE"
                : relay_files.error           ? relay_files.error
                                              : "FILE FAILED");
        if (!relay_files.overview.frames && relay_files.action != RF_SCAN &&
            (last_file_state == RF_SAVED || last_file_state == RF_FAILED))
            scan_slot = 48;
    }
    if (notice_time && !--notice_time)
        redraw = 1;
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
    if (ack) {
        if (queue[head].operation == RT_SHARE)
            share_result(queue[head].track, latest[13] & 255);
        head = (head + 1) & 7;
        --count;
    }
    if (focused && !count)
        audition = (status[8] >> 2) & 1;
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
        "",      "RAM FULL",    "LANES FULL", "RECORD MAIN FIRST", "RECORDER BUSY",
        "LIMIT", "SET PAT LEN", "SAME TRACK"};
    unsigned error = status[13] & 255;
    if (focused && ack && error && error < 8) {
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
    unsigned first = machines_info[3];
    if (type < first || type - first >= machines_info[4])
        return 0;
    const char *n = *(const char **)(uintptr_t)(machines_info[6] + 64 * (type - first) + 56);
    return n && n[0] == 'R' && n[1] == 'E' && n[2] == 'L' && n[3] == 'A' && n[4] == 'Y' && !n[5];
}
extern const u8 rtm_mod_rate, rtm_mod_spacing, rtm_mod_feedback, rtm_mod_mix, rtm_mod_gain,
    rtm_mod_position;
static const u8 mod_mirrors[] = {26, 27, 28, 31, 32, 33};
u32 rtm_mod_param(u32 type, u32 mirror)
{
    static const u8 *const descriptors[] = {&rtm_mod_rate, &rtm_mod_spacing, &rtm_mod_feedback,
                                            &rtm_mod_mix,  &rtm_mod_gain,    &rtm_mod_position};
    if (!rtm_is_type(type))
        return UINT32_MAX;
    for (unsigned i = 0; i < 6; ++i)
        if (mirror == mod_mirrors[i])
            return (u32)(uintptr_t)descriptors[i];
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
    notice_time = 0;
    audition = held = confirm = edit_gain = source_shortcut = 0;
    for (unsigned i = 0; i < 8; ++i)
        travel[i] = 0;
}
static const struct RelayOverview *current_wave(unsigned markers)
{
    if (track >= 16 || status[4] != track)
        return 0;
    struct Settings *s = settings + track;
    unsigned lane = page ? s->lane : 0;
    return rtm_wave(track + 16 * lane, status[9], status[5], markers, s->sensitivity + 1);
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
    unsigned first = status[12] % length, last = status[6] % length;
    if ((last + length - first) % length > length / 2) {
        unsigned swap = first;
        first = last;
        last = swap;
    }
    first = first * RELAY_WAVE_BINS / length;
    last = last * RELAY_WAVE_BINS / length;
    for (unsigned bin = first;; bin = (bin + 1) % RELAY_WAVE_BINS) {
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
    if (track >= 16 || !seen || status[4] != track)
        return;
    if ((status[2] & 15) && (status[2] >> 8) == track)
        return;
    struct Settings *s = settings + track;
    if (s->lane && !(status[8] & (1u << (s->lane - 1))))
        s->lane = 0;
    unsigned lane = page ? s->lane : 0;
    unsigned slot = track + 16 * lane, hits = page && !lane && s->hits;
    unsigned sens = hits ? s->sensitivity + 1 : 1;
    if (current_wave(hits) || !status[5])
        return;
    if (slot == scan_slot && scan_revision == status[9] && hits == scan_hits && sens == scan_sens &&
        relay_files.action == RF_SCAN &&
        (relay_files.state == RF_WORK || relay_files.state == RF_FAILED))
        return;
    if (relay_files.state == RF_WORK && relay_files.action != RF_SCAN)
        return;
    s->mark = 0;
    if (status[5] && !relay_file_start(RF_SCAN, slot, track, 1, hits, sens))
        return;
    scan_slot = slot;
    scan_revision = status[9];
    scan_hits = hits;
    scan_sens = sens;
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
    if (!page) {
        if (enc == 1 && !edit_gain) {
            int v = clamp((int)s->input + delta, 18);
            if (v == (int)track + 3) {
                v += delta < 0 ? -1 : 1;
                if (v > 18)
                    v = 17;
            }
            if (!route_ok(clamp(v, 18)))
                return;
            s->input = clamp(v, 18);
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
        } else if (enc == 3) {
            unsigned n = grid();
            unsigned step = clamp((int)((s->spacing * n + 32768) >> 16) + delta, n);
            s->spacing = (step * 65536 + n / 2) / n;
        } else if (enc == 4)
            s->feedback = clamp((int)s->feedback + delta, 100);
        else if (enc == 5 || enc == 6) {
            if (status[4] == track && status[5]) {
                unsigned steps = grid(), length = status[5];
                int ticks = delta % (int)steps;
                unsigned n = ticks < 0 ? -ticks : ticks;
                int frames = (length / steps) * n + ((length % steps) * n + steps / 2) / steps;
                if (frames)
                    send(enc == 5 ? RT_SEEK_WRITE : RT_SEEK_READ, ticks < 0 ? -frames : frames,
                         length);
            }
            return;
        } else if (enc == 8)
            s->mix = clamp((int)s->mix + delta, 100);
        else
            return;
        send(RT_SETTINGS, 0, 0);
        publish_sound(track);
    } else if (enc == 1) {
        int l = s->lane, step = delta < 0 ? -1 : 1;
        do {
            l += step;
            if (l < 0 || l > 2)
                return;
        } while (l && !(status[8] & (1u << (l - 1))));
        s->lane = l;
        if (audition)
            send(RT_AUD, l, 0);
        refresh_waveform();
    } else if (enc == 2) {
        s->hits = clamp((int)s->hits + delta, 1);
        publish_sound(track);
        refresh_waveform();
    } else if (enc == 3 && s->hits) {
        s->sensitivity = clamp((int)s->sensitivity + delta, 8);
        publish_sound(track);
        refresh_waveform();
    } else if (enc == 4 && !s->lane && s->hits) {
        const struct RelayOverview *o = current_wave(1);
        if (o && o->count)
            s->mark = clamp((int)s->mark + delta, o->count - 1);
    }
}
static void press(unsigned key)
{
    struct Settings *s = settings + track;
    if (key != 46 || page)
        finish_play_click();
    if (confirm >= 3) {
        recall_choice(key);
        return;
    }
    if (confirm) {
        if (key == 10 && confirm == 1)
            send(RT_CLEAR, s->lane, 0);
        if (key == 10 && confirm == 2 && !save_tape(track, s->hits, s->sensitivity + 1))
            say("FILE BUSY");
        if (key == 10 || key == 12)
            confirm = 0;
        return;
    }
    if (audition) {
        if (key == 10 || key == 12 || key == 46) {
            send(key == 10 ? RT_KEEP : RT_EXIT, 0, 0);
            audition = 0;
            s->lane = 0;
        }
        return;
    }
    int writer = status[2] & 15, own = writer && (status[2] >> 8) == track;
    if (!page) {
        if (key == 42 || key == 43) {
            if (key == 42)
                s->rate = 100;
            else
                s->spacing = 65536;
            send(RT_SETTINGS, 0, 0);
            publish_sound(track);
        }
        if (key == 45)
            send(RT_WRITE, pp_record_frames(0), pp_record_steps());
        if (key == 46)
            play_click(instrument_src_now());
        if (key == 47)
            send(RT_UNDO, 0, 0);
    } else {
        const struct RelayOverview *o = current_wave(1);
        if (key == 44 && !s->lane && s->hits && o && o->slot == track && s->mark < o->count)
            send(RT_PREVIEW, o->starts[s->mark],
                 s->mark + 1 < o->count ? o->starts[s->mark + 1] : o->frames);
        if (key == 45)
            send(own ? RT_FINISH : RT_ALT, 0, 0);
        if (key == 46 && status[5])
            audition = send(RT_AUD, s->lane, 0);
        if (key == 47 && !s->lane && status[5]) {
            if (own)
                say("CLOSE WRITE");
            else
                confirm = 2;
        }
        if (key == 48 && status[5])
            confirm = 1;
    }
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
static unsigned draw_waveform(u8 *b, const struct RelayOverview *overview,
                              const struct RelayOverview *hits, unsigned mark, int live,
                              unsigned writing)
{
    enum { LEFT = 17, TOP = 28, BOTTOM = 50, MID = 39 };
    unsigned lo = WAVE_COLUMNS, hi = WAVE_COLUMNS, audible = 0;
    if (hits && mark < hits->count) {
        lo = hits->starts[mark] * WAVE_COLUMNS / hits->frames;
        hi = mark + 1 < hits->count ? hits->starts[mark + 1] * WAVE_COLUMNS / hits->frames
                                    : WAVE_COLUMNS;
        if (hi == lo)
            ++hi;
    }
    fxv_box(b, LEFT - 1, TOP + 1, 1, BOTTOM - TOP - 1, 1);
    fxv_box(b, LEFT + WAVE_COLUMNS, TOP + 1, 1, BOTTOM - TOP - 1, 1);
    fxv_box(b, LEFT, TOP, WAVE_COLUMNS, 1, 1);
    fxv_box(b, LEFT, BOTTOM, WAVE_COLUMNS, 1, 1);
    uint16_t columns[WAVE_COLUMNS];
    /* Fit the loudest peak in the view, with a -18 dB reference floor. */
    unsigned reference = 16;
    for (unsigned col = 0; col < WAVE_COLUMNS; ++col) {
        unsigned bin = col * RELAY_WAVE_BINS / WAVE_COLUMNS;
        uint16_t wave = RELAY_WAVE_EMPTY;
        for (; bin < (col + 1) * RELAY_WAVE_BINS / WAVE_COLUMNS; ++bin) {
            uint16_t peak = live ? live_peaks[bin] : RELAY_WAVE_EMPTY;
            if (peak == RELAY_WAVE_EMPTY && overview)
                peak = overview->peaks[bin];
            wave = relay_wave_join(wave, peak);
        }
        if (wave != RELAY_WAVE_EMPTY && wave != RELAY_WAVE_SILENT)
            audible = 1;
        else
            wave = RELAY_WAVE_SILENT;
        columns[col] = wave;
        int down = 128 - (int)(wave & 255), up = (int)(wave >> 8) - 128;
        int peak = down > up ? down : up;
        if (peak > (int)reference)
            reference = (unsigned)peak;
    }
    unsigned gain = ((10u << 8) + reference / 2) / reference;
    for (unsigned col = 0; col < WAVE_COLUMNS; ++col) {
        int selected = col >= lo && col < hi;
        if (selected)
            fxv_box(b, LEFT + col, TOP + 1, 1, BOTTOM - TOP - 1, 1);
        int top = wave_height(columns[col] >> 8, gain);
        int bottom = wave_height(columns[col] & 255, gain);
        fxv_box(b, LEFT + col, MID + top, 1, bottom - top + 1, !selected);
    }
    if (hits)
        for (unsigned i = 0; i < hits->count; ++i)
            fxv_box(b, LEFT + hits->starts[i] * WAVE_COLUMNS / hits->frames, TOP, 1, 3, -1);
    if (status[5] && status[6] < status[5]) {
        unsigned length = status[5];
        unsigned phase = status[6] * WAVE_COLUMNS / length;
        unsigned read = ((status[6] + read_offset) % length) * WAVE_COLUMNS / length;
        fxv_box(b, LEFT + read - 1, BOTTOM - 1, 3, 2, -1);
        if (!page) {
            fxv_box(b, LEFT + phase - 1, TOP, 3, 2, -1);
            if (writing && !(status[2] & 16))
                fxv_box(b, LEFT + phase, TOP + 2, 1, 1, 1);
        }
    }
    return audible;
}
static void draw(u8 *b)
{
    struct Settings *s = settings + track;
    static const char *const top[2][4] = {{"INPT", "RATE", "HEAD", "FDBK"},
                                          {"LANE", "SLCE", "SENS", "MARK"}};
    static const char *const bottom[2][4] = {{"WRITE", "PLAY", "BACK", "MIX"},
                                             {"ALT", "AUD", "EXPORT", "CLEAR"}};
    char time[12], sens[12], mark[12];
    unsigned writing = (status[2] & 15) && (status[2] >> 8) == track;
    unsigned tenths = status[6] / 4800;
    num(time, tenths / 10);
    unsigned end = 0;
    while (time[end])
        ++end;
    time[end++] = '.';
    time[end++] = '0' + tenths % 10;
    time[end++] = 's';
    time[end] = 0;
    num(sens, s->sensitivity + 1);
    num(mark, s->mark + 1);
    char rate[8], spacing[14], feedback[8], mix[8], gain[8];
    unsigned speed = s->rate < 0 ? -s->rate : s->rate;
    rate[0] = s->rate < 0 ? '-' : '+';
    rate[1] = '0' + speed / 100;
    rate[2] = '.';
    rate[3] = '0' + speed / 10 % 10;
    rate[4] = '0' + speed % 10;
    rate[5] = 0;
    unsigned n = grid();
    num(spacing, (s->spacing * n + 32768) >> 16);
    unsigned k = 0;
    while (spacing[k])
        ++k;
    spacing[k++] = '/';
    num(spacing + k, n);
    num(feedback, s->feedback);
    num(mix, s->mix);
    num(gain, s->gain);
    const char *values[4] = {edit_gain ? gain : inputs[s->input], rate,
                             s->spacing == 65536 ? "FULL" : spacing, feedback};
    int busy = writing;
    const char *actions[4] = {busy ? ((status[2] & 16) || (status[2] & 15) == 1 ? "CNCL" : "CLOSE")
                              : status[5] ? "OPEN"
                                          : "REC",
                              status[5] ? ((status[7] & 1) ? "STOP" : "PLAY") : "-",
                              (status[11] & 1) ? ((status[10] & 1) ? "REDO" : "UNDO") : "-", mix};
    if (audition)
        actions[1] = "EXIT";
    if (page) {
        values[0] = lanes[s->lane];
        values[1] = s->hits ? "HITS" : "OFF";
        values[2] = s->hits ? sens : "-";
        values[3] = s->hits && !s->lane ? mark : "-";
        actions[0] = status[5] ? "REC" : "-";
        actions[1] = audition ? "EXIT" : "PLAY";
        actions[2] = s->lane ? "-" : "SAVE";
        actions[3] = "DEL";
    }
    fxv_box(b, 16, 10, 112, 54, 0);
    for (unsigned i = 0; i < 4; ++i) {
        int x = 30 + i * 28;
        fxv_text(b, x, 10, !page && i == 0 && edit_gain ? "GAIN" : top[page][i], 1);
        fxv_text(b, x, 16, values[i], 1);
        fxv_text(b, x, 52, bottom[page][i], 1);
        fxv_text(b, x, 58, actions[i], 1);
    }

    const struct RelayOverview *o = current_wave(0);
    const struct RelayOverview *hits = page && !s->lane && s->hits ? current_wave(1) : 0;
    int live = live_track == track && live_frames == status[5] && (!page || !s->lane) &&
               ((writing && live_revision == status[9]) || (!o && live_revision + 1 == status[9]));
    int ready = o != 0;
    if (!o && live && live_mode == 2)
        o = rtm_wave(track, live_revision, live_frames, 0, 1);
    if (!o && !live)
        o = rtm_wave_pending(track + 16 * (page ? s->lane : 0), status[9], status[5]);
    unsigned audible = draw_waveform(b, o, hits, s->mark, live, writing);
    char duration[24];
    if (!page) {
        char total[12];
        unsigned tenths_total = status[5] / 4800;
        num(total, tenths_total / 10);
        unsigned j = 0;
        while (total[j])
            ++j;
        total[j++] = '.';
        total[j++] = '0' + tenths_total % 10;
        total[j] = 0;
        j = 0;
        while (time[j] && time[j] != 's') {
            duration[j] = time[j];
            ++j;
        }
        duration[j++] = '/';
        for (unsigned i = 0; (duration[j++] = total[i]); ++i) {
        }
    }
    const char *msg = confirm >= 3   ? (confirm == 3   ? "CANCEL? YES"
                                        : confirm == 4 ? "SAVE? YES"
                                                       : "DISCARD? YES")
                      : confirm == 1 ? "CLEAR? YES/NO"
                      : confirm == 2 ? (s->hits ? "FULL+HITS?" : "EXPORT FULL?")
                      : notice_time  ? notice
                      : relay_files.state == RF_WORK && relay_files.action == RF_SAVE ? "SAVING"
                      : audition                                                      ? "AUDITION"
                      : !page && busy              ? (status[2] & 16          ? "WAIT 1"
                                                      : (status[2] & 15) == 1 ? "FIRST"
                                                                              : "WRITE")
                      : !page && !s->rate          ? "PARK"
                      : !busy && ready && !audible ? "SILENT TAKE"
                      : !ready && scan_slot % 16 == track && relay_files.state == RF_WORK &&
                              relay_files.action == RF_SCAN
                          ? "READING"
                      : !status[5] ? (busy ? "RECORDING" : "EMPTY")
                                   : 0;
    if (msg && (page || confirm || notice_time || !busy))
        fxv_text(b, 72, 22, msg, 0);
    else if (!page) {
        fxv_text(b, 37, 22, msg ? msg : status[7] & 1 ? "PLAY" : "STOP", msg ? 0 : 1);
        fxv_text(b, 95, 22, duration, 1);
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
    if (action == 1) {
        draw(data);
        redraw = 0;
        return 1;
    }
    if (action == 5)
        return redraw ? 2 : 1;
    unsigned key = *(u32 *)(data + 12), v = *(u32 *)(data + 16);
    if (action == 3) {
        if (!key || key > 8)
            return 0;
        turn(key, (int)v);
    } else {
        if (!((key >= 41 && key <= 48) ||
              ((confirm || audition) &&
               (key == 10 || key == 12 || (confirm >= 3 && (key == 14 || key == 15))))))
            return 0;
        unsigned flags = fxv_key_flags(v);
        if (key == 41 && !page) {
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
        if ((flags & FXV_PRESS) &&
            ((seen && status[4] == track) || (!page && (key == 45 || key == 46))))
            press(key);
    }
    dk_fw_view_invalidate(view);
    return 1;
}
// A second STOP has no running-to-stopped clock edge. Observe the plain stock
// button as well so it silences a manually launched motor on any SRC page.
void rtm_stop_key(u32 flags)
{
    extern volatile u32 pp_position[16];
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
