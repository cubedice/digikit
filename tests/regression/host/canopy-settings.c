/* Execute real UI callbacks with a failing/in-memory card and minimal UI host. */
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/control.c"
/* The driver plays the instance admitted on track 0 (instance 0); these names
 * keep its commands readable against the per-instance bridge. */
#define keyboard (cur->keyboard)
#define pad_family (cur->pad_family)
#define pad_color (cur->pad_color)
#define arp_bpm (cur->arp_bpm)
#define arp_mix (cur->arp_mix)
#define arp_rate (cur->arp_rate)
#define pad_dirty (cur->pad_dirty)
#define wt (cur->wt)
#define phase (cur->phase)
#define prepared (cur->prepared)
#define src_state (src_state[IDX(cur)])
#define src_status (src_status[IDX(cur)])
struct dk_ui_menu {
    const dk_ui_menu_desc *desc;
    int open, count;
    struct {
        dk_ui_action_fn *fn;
        void *ctx;
        int32_t arg;
        int32_t *value;
        int32_t low, high;
        char label[DK_UI_ROW];
    } rows[32];
};
/* The core UI handle control.c takes: the stubs below ignore it. */
struct dk_ui {
    int unused;
};
static struct dk_ui host_ui;
dk_ui *dk_ui_core_handle(void)
{
    return &host_ui;
}
MiLedger mi_ledger;
MiTransport mi_transport;
volatile uint32_t ip_view;
volatile uint32_t ip_track;
volatile uint32_t ip_tracks[IP_INSTANCES] = {0, IP_NONE, IP_NONE, IP_NONE};
volatile uint32_t ip_admission[IP_INSTANCES * 8];
static struct dk_ui_menu menus[12];
static unsigned menu_count, masked, unsafe_io, token = 1;
static int exists, staged, put_error, flush_error, open_error;
static uint8_t card[KB_RECORD_BYTES], pending_card[KB_RECORD_BYTES];
static uint8_t preset_card[CP_SLOTS][CP_RECORD_BYTES], preset_staged[CP_SLOTS][CP_RECORD_BYTES];
static unsigned preset_exists, preset_staged_mask, preset_bytes[CP_SLOTS];
static uint8_t route_card[CR_BYTES], route_staged[CR_BYTES];
static int route_exists, route_staged_valid;
static uint8_t channels_card[CC_BYTES], channels_staged[CC_BYTES];
static int channels_exists, channels_staged_valid;
static uint32_t admitted = 1, route_calls, status_reads;
static uint32_t config_words[32], config_revision, config_schema, config_count;
static int preset_index(const char *key)
{
    for (int i = 0; i < CP_SLOTS; ++i)
        if (!strcmp(key, preset_keys[i]))
            return i;
    return -1;
}
uint32_t dkrt_instrument_type(void)
{
    return admitted;
}
uint32_t dkrt_instrument_generation(void)
{
    return admitted;
}
uint32_t dkrt_irq_mask(void)
{
    unsigned before = masked;
    ++masked;
    return before;
}
void dkrt_irq_restore(uint32_t sr)
{
    masked = sr;
}
int instrument_sound_prepare(uint32_t i, int refresh)
{
    (void)i;
    (void)refresh;
    return 1;
}
uint32_t instrument_sound_tune(uint32_t i)
{
    (void)i;
    return 0x4000;
}
void instrument_sound_scan(void)
{
}
uint32_t instrument_src_now(void)
{
    return 100;
}
void instrument_mod_publish(uint32_t family)
{
    (void)family;
}
void ip_view_build(uint32_t view)
{
    (void)view;
}
void mi_instance_reset(uint32_t track)
{
    ++mi_ledger.generations[track];
}
void mi_instance_bind(uint32_t track, uint32_t slot)
{
    (void)slot;
    mi_instance_reset(track);
}
uint32_t mi_route_track(uint32_t p, uint32_t c)
{
    (void)p;
    (void)c;
    return MI_NONE;
}
int mi_registry_reply(uint32_t *out, uint32_t words)
{
    (void)out;
    (void)words;
    return 0;
}
int mi_status_reply(uint32_t *out, uint32_t words, uint32_t *age)
{
    (void)out;
    (void)words;
    (void)age;
    ++status_reads;
    return 0;
}
int mi_config_submit(uint32_t a, uint32_t b, uint32_t c, const uint32_t *d, uint32_t e)
{
    (void)a;
    config_revision = b;
    config_schema = c;
    config_count = e;
    for (uint32_t i = 0; i < e && i < 32; ++i)
        config_words[i] = d[i];
    return -2;
}
int mi_route_enable(uint32_t p, uint32_t c, uint32_t t, int s)
{
    (void)p;
    (void)c;
    (void)t;
    (void)s;
    ++route_calls;
    return -2;
}
int mi_keyboard_recover(void)
{
    return -1;
}
int dkrt_settings_get(const char *key, uint8_t *out, uint32_t capacity)
{
    if (masked)
        ++unsafe_io;
    if (!strcmp(key, channels_key)) {
        if (capacity < CC_BYTES)
            return -69;
        if (!channels_exists)
            return -68;
        memcpy(out, channels_card, CC_BYTES);
        return CC_BYTES;
    }
    if (!strcmp(key, route_key)) {
        if (capacity < CR_BYTES)
            return -69;
        if (!route_exists)
            return -68;
        memcpy(out, route_card, CR_BYTES);
        return CR_BYTES;
    }
    int index = preset_index(key);
    if (index >= 0) {
        if (capacity < CP_RECORD_BYTES)
            return -69;
        if (!(preset_exists & (1u << index)))
            return -68;
        memcpy(out, preset_card[index], preset_bytes[index]);
        return (int)preset_bytes[index];
    }
    if (strcmp(key, "canopy/shapes/v1") || capacity < sizeof(card))
        return -69;
    if (!exists)
        return -68;
    memcpy(out, card, sizeof(card));
    return sizeof(card);
}
int dkrt_settings_put(const char *key, const uint8_t *in, uint32_t size)
{
    if (masked)
        ++unsafe_io;
    if (!strcmp(key, channels_key)) {
        if (size != CC_BYTES)
            return -69;
        if (put_error)
            return put_error;
        memcpy(channels_staged, in, CC_BYTES);
        channels_staged_valid = 1;
        return 0;
    }
    if (!strcmp(key, route_key)) {
        if (size != CR_BYTES)
            return -69;
        if (put_error)
            return put_error;
        memcpy(route_staged, in, CR_BYTES);
        route_staged_valid = 1;
        return 0;
    }
    int index = preset_index(key);
    if (index >= 0) {
        if (size != CP_RECORD_BYTES)
            return -69;
        if (put_error)
            return put_error;
        memcpy(preset_staged[index], in, size);
        preset_staged_mask |= 1u << index;
        return 0;
    }
    if (strcmp(key, "canopy/shapes/v1") || size != sizeof(card))
        return -69;
    if (put_error)
        return put_error;
    memcpy(pending_card, in, size);
    staged = 1;
    return 0;
}
int dkrt_settings_flush(void)
{
    if (masked)
        ++unsafe_io;
    if (flush_error)
        return flush_error;
    if (route_staged_valid) {
        memcpy(route_card, route_staged, CR_BYTES);
        route_exists = 1;
        route_staged_valid = 0;
    }
    if (channels_staged_valid) {
        memcpy(channels_card, channels_staged, CC_BYTES);
        channels_exists = 1;
        channels_staged_valid = 0;
    }
    for (unsigned i = 0; i < CP_SLOTS; ++i)
        if (preset_staged_mask & (1u << i)) {
            memcpy(preset_card[i], preset_staged[i], CP_RECORD_BYTES);
            preset_bytes[i] = CP_RECORD_BYTES;
            preset_exists |= 1u << i;
        }
    preset_staged_mask = 0;
    if (staged) {
        memcpy(card, pending_card, sizeof(card));
        exists = 1;
        staged = 0;
    }
    return 0;
}
dk_ui_menu *dk_ui_menu_new(dk_ui *ui, const dk_ui_menu_desc *d)
{
    (void)ui;
    if (menu_count == 12)
        return 0;
    menus[menu_count].desc = d;
    return &menus[menu_count++];
}
int32_t dk_ui_menu_open(dk_ui_menu *m)
{
    if (open_error)
        return -1;
    m->open = 1;
    m->count = 0;
    m->desc->fill(m, m->desc->arg);
    return 0;
}
void dk_ui_menu_close(dk_ui_menu *m)
{
    m->open = 0;
}
int32_t dk_ui_menu_is_open(const dk_ui_menu *m)
{
    return m->open;
}
void dk_ui_menu_refresh(dk_ui_menu *m)
{
    m->count = 0;
    m->desc->fill(m, m->desc->arg);
}
void dk_ui_menu_add_action(dk_ui_menu *m, const char *s, dk_ui_action_fn *f, void *c, int32_t a)
{
    if (m->count < 32) {
        int i = m->count++;
        m->rows[i].fn = f;
        m->rows[i].ctx = c;
        m->rows[i].arg = a;
        m->rows[i].value = 0;
        snprintf(m->rows[i].label, sizeof(m->rows[i].label), "%s", s);
    }
}
void dk_ui_menu_add_text(dk_ui_menu *m, const char *s)
{
    dk_ui_menu_add_action(m, s, 0, 0, 0);
}
void dk_ui_menu_add_value(dk_ui_menu *m, const char *s, int32_t *v, int32_t a, int32_t b,
                          dk_ui_action_fn *f, void *c)
{
    int i = m->count;
    dk_ui_menu_add_action(m, s, f, c, 0);
    if (i < m->count) {
        m->rows[i].value = v;
        m->rows[i].low = a;
        m->rows[i].high = b;
    }
}
void dk_ui_menu_add_toggle(dk_ui_menu *m, const char *s, int32_t *v, dk_ui_action_fn *f, void *c)
{
    dk_ui_menu_add_value(m, s, v, 0, 1, f, c);
}
void dk_ui_menu_add_submenu(dk_ui_menu *m, const char *s, dk_ui_menu *child)
{
    dk_ui_menu_add_action(m, s, 0, child, 0);
}
int32_t dk_ui_choice(dk_ui *ui, const char *t, const char *const *i, int32_t n,
                     dk_ui_choice_fn *done, void *c)
{
    (void)ui;
    (void)t;
    (void)i;
    (void)n;
    (void)done;
    (void)c;
    return 0;
}
int32_t dk_ui_confirm(dk_ui *ui, const char *t, const char *v, dk_ui_confirm_fn *done, void *c)
{
    (void)ui;
    (void)t;
    (void)v;
    (void)done;
    (void)c;
    return 0;
}
int32_t dk_ui_toast(dk_ui *ui, const char *text)
{
    (void)ui;
    (void)text;
    return 0;
}
int main(void)
{
    char line[64], cmd[24];
    int value;
    uint32_t src_token = 0;
    int src_result = 0;
    mi_ledger.epoch = 1;
    mi_ledger.generations[0] = 1;
    instrument_play_tick();
    while (fgets(line, sizeof(line), stdin)) {
        value = 0;
        if (sscanf(line, "%23s %d", cmd, &value) < 1)
            continue;
        if (!strcmp(cmd, "learn"))
            learn_begin(0, 0);
        else if (!strcmp(cmd, "key")) {
            MiEvent in = {0}, out[8];
            in.kind = MI_NOTE_ON;
            in.note = (uint8_t)value;
            in.value = 90;
            in.epoch = mi_ledger.epoch;
            in.generation = mi_ledger.generations[0];
            in.token = token++;
            instrument_keyboard_events(0, in.epoch, in.generation, &in, 1, out);
        } else if (!strcmp(cmd, "tick"))
            instrument_play_tick();
        else if (!strcmp(cmd, "save"))
            learn_close(0, 1);
        else if (!strcmp(cmd, "yes")) {
            if (learn_menu && learn_menu->open && value >= 0 && value < learn_menu->count &&
                learn_menu->rows[value].fn)
                learn_menu->rows[value].fn(learn_menu->rows[value].ctx,
                                           learn_menu->rows[value].arg);
        } else if (!strcmp(cmd, "suspend"))
            instrument_sound_suspend(0);
        else if (!strcmp(cmd, "back"))
            learn_back(0);
        else if (!strcmp(cmd, "puterror"))
            put_error = value;
        else if (!strcmp(cmd, "flusherror"))
            flush_error = value;
        else if (!strcmp(cmd, "openerror"))
            open_error = value;
        else if (!strcmp(cmd, "retry"))
            retry_save(0, 0);
        else if (!strcmp(cmd, "routesave"))
            route_save(0, 0);
        else if (!strcmp(cmd, "enable")) {
            enabled = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "channel")) {
            ui_channel = value;
            channel_changed(0, value);
        } else if (!strcmp(cmd, "usb")) {
            usb = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "din")) {
            din = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "selectorchannel")) {
            selector_channel = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "selectoroctave")) {
            selector_octave = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "admitted"))
            admitted = (uint32_t)value;
        else if (!strcmp(cmd, "corruptroute"))
            route_card[value] ^= 1;
        else if (!strcmp(cmd, "resignroute")) {
            route_card[14] = route_card[15] = 0;
            for (unsigned i = 0; i < 14; ++i) {
                route_card[14] += route_card[i];
                route_card[15] += route_card[14];
            }
        } else if (!strcmp(cmd, "routestate"))
            printf(
                "{\"enabled\":%d,\"usb\":%d,\"din\":%d,\"channel\":%d,\"octave\":%d,\"selector\":%d,\"result\":%d,\"pending\":%d,\"restore\":%d,\"calls\":%u,\"unsafe\":%u}\n",
                enabled, usb, din, (int)channel_of(cur), selector_octave, selector_channel,
                route_result, route_pending, route_restore, route_calls, unsafe_io);
        else if (!strcmp(cmd, "presetsave"))
            preset_save(0, 0);
        else if (!strcmp(cmd, "presetload"))
            preset_load(0, 0);
        else if (!strcmp(cmd, "presetpoll")) {
            if (!preset_menu)
                preset_menu = dk_ui_menu_new(ui(), &preset_desc);
            dk_ui_menu_open(preset_menu);
            status_reads = 0;
            instrument_play_tick();
            printf("{\"reads\":%u,\"open\":%d,\"track\":%u}\n", status_reads,
                   dk_ui_menu_is_open(preset_menu), src_status.track);
        } else if (!strcmp(cmd, "confighandshake")) {
            IpInst *in = &inst[0];
            cur = in;
            in->track = in->bound = 0;
            phase = 2;
            prepared = 1;
            healthy = 1;
            mi_transport.applied[0] = 0;
            mi_transport.config_active = value == 1;
            mi_transport.config_track = value == 2 ? 1 : 0;
            if (value == 2)
                mi_transport.config_active = 1;
            mi_ledger.reset_mask = value == 3 ? 2 : 0;
            uint32_t before = mi_ledger.generations[0];
            tick_instance(0, in);
            printf("{\"phase\":%u,\"resets\":%u}\n", phase, mi_ledger.generations[0] - before);
        } else if (!strcmp(cmd, "presetslot"))
            preset_slot = value;
        else if (!strcmp(cmd, "legacyarp")) {
            CpPreset p;
            preset_snapshot(cur, &p);
            p.clock = (uint8_t)value;
            p.bpm = 237;
            p.control[CP_RATE] = 6;
            if (cp_encode(&p, preset_card[preset_slot - 1]))
                return 2;
            memcpy(preset_records[preset_slot - 1], preset_card[preset_slot - 1], CP_RECORD_BYTES);
            preset_present |= 1u << (preset_slot - 1);
            preset_exists |= 1u << (preset_slot - 1);
            preset_bytes[preset_slot - 1] = CP_RECORD_BYTES;
        } else if (!strcmp(cmd, "family")) {
            pad_family = value;
            pad_changed(0, value);
        } else if (!strcmp(cmd, "scanclock"))
            scan_clock_done(0, value);
        else if (!strcmp(cmd, "color")) {
            pad_color = value;
            pad_changed(0, value);
        } else if (!strcmp(cmd, "bpm")) {
            arp_bpm = value;
            pad_changed(0, value);
        } else if (!strcmp(cmd, "config")) {
            healthy = enabled = prepared = 1;
            phase = 1;
            instrument_play_tick();
            printf(
                "{\"revision\":%u,\"srcRevision\":%u,\"schema\":%u,\"count\":%u,\"pending\":%u,\"values\":[",
                config_revision, src_state.revision, config_schema, config_count,
                src_state.pending);
            for (uint32_t i = 0; i < config_count; ++i)
                printf("%s%u", i ? "," : "", config_words[i]);
            puts("]}");
        } else if (!strcmp(cmd, "ackdetail")) {
            src_state.pending = UINT32_MAX;
            is_select(&src_status, 0, src_state.generation, src_state.mode, 0);
            src_status.fast_valid = src_status.detail_valid = src_status.poll_valid = 1;
            src_status.fast_at = src_status.poll_at = 100;
            src_status.fast[3] = src_status.detail_control = src_state.revision;
            src_status.fast[11] = src_status.detail_revision = 9;
            src_status.detail_schema = (uint32_t)value;
            for (unsigned i = 0; i < (value >= 5 ? 32 : 28); ++i)
                src_status.detail[i] = src_state.value[i];
            src_status.detail[value >= 5 ? 42 : 28] = src_state.native_tune;
            instrument_src_poll(100);
            printf("{\"pending\":%u}\n", src_state.pending);
        } else if (!strcmp(cmd, "mode")) {
            uint32_t sr = dkrt_irq_mask();
            chord_apply((uint32_t)value, keyboard.type);
            dkrt_irq_restore(sr);
        } else if (!strcmp(cmd, "srcpage"))
            src_token = instrument_src_context(0, (uint32_t)value, 1);
        else if (!strcmp(cmd, "srcturn")) {
            unsigned knob;
            int delta;
            if (sscanf(line, "%*s %u %d", &knob, &delta) != 2)
                return 2;
            src_result = instrument_src_encoder(src_token, knob, delta, 100);
        } else if (!strcmp(cmd, "srcstate")) {
            /* srcstate [encoder]: "value" is that encoder's display (default 0). */
            CvDisplay display;
            instrument_src_display(&display, 100, "0", "100");
            unsigned shown = value > 0 && value < 8 ? (unsigned)value : 0u;
            printf(
                "{\"result\":%d,\"color\":%d,\"mix\":%d,\"rate\":%d,\"type\":%u,\"dirty\":%u,\"value\":\"%s\",\"heading\":\"%s\"}\n",
                src_result, pad_color, arp_mix, arp_rate, keyboard.type, pad_dirty,
                display.value[shown], display.heading);
        } else if (!strcmp(cmd, "statuswrap")) {
            struct dk_ui_menu status = {0};
            status_pair(&status, "ROOT", "C#maj7/G# +123");
            printf("[\"%s\",\"%s\"]\n", status.rows[0].label, status.rows[1].label);
        } else if (!strcmp(cmd, "statusview")) {
            struct dk_ui_menu parent = {0};
            int owns =
                dk_app_instrument_module_settings((const uint8_t *)"digikit/canopy", &parent);
            int foreign =
                dk_app_instrument_module_settings((const uint8_t *)"test/other-module", 0);
            (void)instrument_src_context(0, 0, 0);
            instrument_src_poll(100);
            src_status.fast_valid = (uint8_t)value;
            src_status.fast_at = 100;
            src_status.fast[1] = (15u << 16) | (4u << 20) | (1u << 8);
            src_status.fast[5] = 1 | (3u << 8) | (8u << 16) | (10u << 24);
            struct dk_ui_menu status = {0};
            status_fill(&status, 0);
            printf("{\"owns\":%d,\"foreign\":%d,\"entry\":\"%s\",\"visible\":%u,\"rows\":[", owns,
                   foreign, parent.rows[0].label, src_state.visible);
            for (int i = 0; i < status.count; ++i)
                printf("%s\"%s\"", i ? "," : "", status.rows[i].label);
            puts("]}");
        } else if (!strcmp(cmd, "type")) {
            uint32_t sr = dkrt_irq_mask();
            chord_apply(keyboard.mode, (uint32_t)value);
            dkrt_irq_restore(sr);
        } else if (!strcmp(cmd, "voicing")) {
            cur->voicing = value;
            keyboard.voicing = (uint8_t)value;
            keyboard.update = 1;
        } else if (!strcmp(cmd, "voices")) {
            MiEvent out[8];
            instrument_keyboard_events(0, mi_ledger.epoch, mi_ledger.generations[0], 0, 0, out);
            printf(
                "{\"mode\":%u,\"type\":%u,\"generation\":%u,\"family\":%d,\"table\":%d,\"voices\":[",
                keyboard.mode, keyboard.type, mi_ledger.generations[0], pad_family, wt[0]);
            unsigned n = 0;
            for (unsigned i = 0; i < 4; ++i)
                if (keyboard.voices[i].token)
                    printf("%s[%u,%u]", n++ ? "," : "", keyboard.voices[i].note,
                           keyboard.voices[i].token);
            puts("]}");
        } else if (!strcmp(cmd, "menus")) {
            struct dk_ui_menu parent = {0};
            dk_app_instrument_settings_rows(&parent);
            dk_ui_menu_open(menu);
            printf("{\"entry\":\"%s\",\"home\":[", parent.rows[0].label);
            for (int i = 0; i < menu->count; ++i)
                printf("%s\"%s\"", i ? "," : "", menu->rows[i].label);
            printf("],\"children\":[");
            for (int i = 0; i < menu->count; ++i) {
                struct dk_ui_menu *child = menu->rows[i].ctx;
                if (!child)
                    return 2;
                dk_ui_menu_open(child);
                printf("%s[", i ? "," : "");
                for (int row = 0; row < child->count; ++row)
                    printf("%s\"%s\"", row ? "," : "", child->rows[row].label);
                printf("]");
                dk_ui_menu_close(child);
            }
            printf("]}\n");
        } else if (!strcmp(cmd, "menuset")) {
            unsigned child_index, row;
            int setting;
            if (sscanf(line, "%*s %u %u %d", &child_index, &row, &setting) != 3)
                return 2;
            if (!menu || child_index >= (unsigned)menu->count)
                return 2;
            struct dk_ui_menu *child = menu->rows[child_index].ctx;
            dk_ui_menu_open(child);
            if (row >= (unsigned)child->count || !child->rows[row].value ||
                setting < child->rows[row].low || setting > child->rows[row].high)
                return 2;
            *child->rows[row].value = setting;
            if (child->rows[row].fn)
                child->rows[row].fn(child->rows[row].ctx, setting);
            dk_ui_menu_close(child);
        } else if (!strcmp(cmd, "shapeslot")) {
            cur->slot = value;
            changed(0, 0);
        } else if (!strcmp(cmd, "legacypreset")) {
            uint8_t *b = preset_card[preset_slot - 1];
            b[4] = (uint8_t)value;
            b[5] = value <= 2 ? 16 : 28;
            for (unsigned i = value <= 2 ? 51 : value == 3 ? 92 : 119; i < CP_RECORD_BYTES; ++i)
                b[i] = 0;
            unsigned end = value <= 2 ? 60 : value == 3 ? 92 : 124;
            uint32_t h = 2166136261u;
            for (unsigned i = 0; i < end; ++i)
                h = (h ^ b[i]) * 16777619u;
            for (unsigned i = 0; i < 4; ++i)
                b[end + i] = (uint8_t)(h >> (24 - 8 * i));
            preset_bytes[preset_slot - 1] = end + 4;
        } else if (!strcmp(cmd, "corruptpreset"))
            preset_card[preset_slot - 1][value] ^= 1;
        else if (!strcmp(cmd, "presetstate")) {
            printf(
                "{\"color\":%d,\"dirty\":%d,\"result\":%d,\"present\":%u,\"durable\":%u,\"pending\":%d,\"override\":%u,\"unsafe\":%u,\"shape\":[",
                pad_color, preset_dirty(), preset_result, preset_present, preset_exists,
                preset_pending, keyboard.preset_shape_active, unsafe_io);
            const KbShape *s = keyboard.preset_shape_active ? &keyboard.preset_shape
                                                            : &keyboard.shapes[keyboard.slot];
            for (unsigned i = 0; i < s->count; ++i)
                printf("%s%u", i ? "," : "", s->intervals[i]);
            puts("]}");
        } else if (!strcmp(cmd, "restart")) {
            enabled = 0;
            usb = din = channel = 1;
            selector_channel = 0;
            selector_octave = 1;
            for (unsigned t = 0; t < MI_TRACKS; ++t)
                track_channel[t] = 0;
            keyboard_initialized = 0;
            instrument_play_tick();
        }
#ifdef DK_CANOPY_SOUND_PROOF
        else if (!strcmp(cmd, "soundstateproof")) {
            CpPreset p, unchanged;
            uint32_t generation = 0;
            memset(&p, 0x5a, sizeof(p));
            memcpy(&unchanged, &p, sizeof(p));
            ip_admission[0] = 1;
            ip_admission[1] = 0x123400;
            ip_admission[4] = 7;
            ip_admission[5] = admitted;
            ip_admission[6] = 0;
            cur->voicing = keyboard.voicing = 2;
            int bad_snapshot = instrument_sound_snapshot(0, 0x999900, &p, &generation);
            int preserved = !memcmp(&p, &unchanged, sizeof(p)) && generation == 0;
            int saved = instrument_sound_snapshot(0, 0x123400, &p, &generation);
            cur->voicing = keyboard.voicing = 0;
            int bad_generation = instrument_sound_restore(0, 0x123400, generation + 1, &p);
            int bad_sound = instrument_sound_restore(0, 0x999900, generation, &p);
            ip_admission[6] = 1;
            int during_load = instrument_sound_restore(0, 0x123400, generation, &p);
            ip_admission[6] = 0;
            int restored = instrument_sound_restore(0, 0x123400, generation, &p);
            printf(
                "{\"badSnapshot\":%d,\"preserved\":%d,\"saved\":%d,\"generation\":%u,\"badGeneration\":%d,\"badSound\":%d,\"duringLoad\":%d,\"restored\":%d,\"voicing\":%u,\"masked\":%u}\n",
                bad_snapshot, preserved, saved, generation, bad_generation, bad_sound, during_load,
                restored, keyboard.voicing, masked);
        }
#endif
        else if (!strcmp(cmd, "snapshot")) {
            printf(
                "{\"mode\":%u,\"dirty\":%u,\"status\":%u,\"storage\":%d,\"pending\":%d,\"exists\":%d,\"unsafe\":%u,\"shape\":[",
                keyboard.mode, keyboard.dirty, keyboard.status, storage_result, save_pending,
                exists, unsafe_io);
            for (unsigned i = 0; i < keyboard.shapes[0].count; ++i)
                printf("%s%u", i ? "," : "", keyboard.shapes[0].intervals[i]);
            puts("]}");
        } else
            return 2;
    }
    return 0;
}
