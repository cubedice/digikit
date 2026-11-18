/* UI-task policy and setup for up to IP_INSTANCES admitted keyboard instruments,
 * one per kit track (finding 345). Instance i is DSP slot i. The UI (SRC page,
 * menus, presets, panel keys) acts on `cur`, the instance on the selected track;
 * the tick services every instance; the MIDI mapper finds its own by track. */
#if defined(__m68k__) && defined(__GNUC__)
/* Keep lookup data out of executable sections checked by the CF ISA gate. */
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include <stddef.h>
#include <dk/platform/ui.h>
#include "keyboard.h"
#include "preset.h"
#include "routing.h"
#include "src-control.h"
#include "src-clock.h"
#include "src-mod.h"
#include "instances.h"
#include "work-copy.h"
#if defined(__m68k__)
#include "sound-persist.h"
#endif
#ifdef DK_CANOPY_SOUND_PROOF
#include "sound-state.h"
#endif
#include "../../../midi-events/src/cf/bridge.h"

extern dk_ui *dk_ui_core_handle(void);
static dk_ui *ui(void)
{
    static dk_ui *h;
    if (!h)
        h = dk_ui_core_handle();
    return h;
}
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
/* Up to IP_INSTANCES admitted CANOPY instances; process and state are per
 * instance. The runtime multiplies them for the total budget and allocation. */
static const uint32_t limits[8] = {0x49525031, 1, IP_INSTANCES, 100000, 1600, 8, 128, 2976};
const uint32_t *instrument_runtime_policy(void)
{
    return limits;
}
/* Device routing, shared by every instance. CHANNEL is the default for a track
 * without its own channel (track_channel 0). */
static int32_t enabled, usb = 1, din = 1, channel = 1, selector_octave = 1, selector_channel;
static uint8_t track_channel[MI_TRACKS];
static uint32_t healthy, fault_generation;
int instrument_play_faulted(uint32_t generation)
{
    return generation && fault_generation == generation;
}
/* Per instance: phase, external MIDI, healthy, prepared, faults, reset mask,
 * applied revision, type ([0..7] is instance 0). */
volatile uint32_t ip_control[IP_INSTANCES * 8];
static dk_ui_menu *menu;
extern volatile uint32_t ip_view;
extern void ip_view_build(uint32_t view);
static uint32_t listed_type;

typedef struct {
    uint32_t track; /* admitted track (ip_tracks), IP_NONE when free */
    uint32_t bound; /* track the transport was last bound/reset for */
    uint32_t phase, prepared, conflict;
    /* Stage 3 setup. Updates use the existing atomic configuration channel,
     * retain note ownership, and coalesce knob motion while an ACK is pending. */
    int pad_family, pad_color, pad_drift, pad_attack, pad_release;
    int arp_mix, arp_bpm, arp_rate, arp_gate;
    int arp_order, arp_octaves, arp_swing, arp_clock, arp_restart, pad_strum;
    int wt[12], fm[13], wtfm[4];
    uint32_t pad_dirty;
    int32_t mode, type, voicing, slot;
    KbState keyboard;
} IpInst;
/* Exported for the proofs: element 0 is instance 0. */
CsState src_state[IP_INSTANCES];
IsStatus src_status[IP_INSTANCES];
static IpInst inst[IP_INSTANCES];
static IpInst *cur = &inst[0];
#ifdef DK_CANOPY_SOUND_PROOF
/* An emulator-only, separately linked fixture may request a post-admission
 * tick. No production module sets this; never use it for persistence. */
static void (*sound_proof_tick)(void);
void instrument_sound_proof_set(void (*tick)(void))
{
    sound_proof_tick = tick;
}
static int proof_live_encoder(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now);
static int proof_live_keyboard(uint32_t track, uint32_t epoch, uint32_t generation,
                               const MiEvent *input, uint32_t count, MiEvent *output);
static IpSoundProofPrepare sound_transaction_prepare;
static IpSoundProofAttach sound_transaction_attach;
static void *sound_transaction_context;
static uint8_t sound_transaction_busy;
enum { PROOF_UI_PRESET, PROOF_UI_SHAPE, PROOF_UI_MENU };
/* 1 preserves the unregistered callback, 0 committed, negative refused.
 * -2 denotes forbidden re-entry: no nested UI refresh/toast is allowed. */
static int proof_live_ui(uint32_t operation, int value, const CpPreset *preset);
static int proof_live_learn(uint32_t session, uint32_t revision);
#endif
static uint32_t selected = IP_NONE, focus = IP_NONE, initialized;
#define IDX(p) ((uint32_t)((p) - inst))
#define SRC(p) (&src_state[IDX(p)])
#define STS(p) (&src_status[IDX(p)])

static void instance_defaults(IpInst *in)
{
    static const int wt0[12] = {0, 0, 0, 10, 0, 0, 0, 100, 5, 15, 100, 100};
    static const int fm0[13] = {32, 0, 50, 3, 8, 0, 5, 40, 50, 15, 100, 100, 100};
    static const int wtfm0[4] = {0, 3, 100, 100};
    in->pad_family = 0;
    in->pad_color = 64;
    in->pad_drift = 0;
    in->pad_attack = 5;
    in->pad_release = 15;
    in->arp_mix = 0;
    in->arp_bpm = CP_ARP_BPM_RESERVED;
    in->arp_rate = 4;
    in->arp_gate = 50;
    in->arp_order = 0;
    in->arp_octaves = 0;
    in->arp_swing = 0;
    in->arp_clock = CP_ARP_CLOCK;
    in->arp_restart = 0;
    in->pad_strum = 0;
    for (uint32_t i = 0; i < 12; ++i)
        in->wt[i] = wt0[i];
    for (uint32_t i = 0; i < 13; ++i)
        in->fm[i] = fm0[i];
    for (uint32_t i = 0; i < 4; ++i)
        in->wtfm[i] = wtfm0[i];
    in->pad_dirty = 1;
    in->mode = 0;
    in->type = 0;
    in->voicing = 0;
    in->slot = 1;
    in->track = in->bound = IP_NONE;
    in->phase = in->prepared = in->conflict = 0;
}
static void ip_init(void)
{
    if (initialized)
        return;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        instance_defaults(&inst[i]);
        kb_init(&inst[i].keyboard);
        cs_init(&src_state[i]);
    }
    cur = &inst[0];
    initialized = 1;
}
static IpInst *for_track(uint32_t track)
{
    for (uint32_t i = 0; track < MI_TRACKS && i < IP_INSTANCES; ++i)
        if (inst[i].track == track)
            return &inst[i];
    return 0;
}
/* The instance bound and admitted for this track (mapper/ISR safe). */
static IpInst *owner(uint32_t track)
{
    for (uint32_t i = 0; track < MI_TRACKS && i < IP_INSTANCES; ++i)
        if (inst[i].track == track && inst[i].bound == track)
            return &inst[i];
    return 0;
}
int instrument_track_owned(uint32_t track)
{
    return for_track(track) != 0;
}
void instrument_select_track(uint32_t track)
{
    ip_init();
    if (track >= MI_TRACKS)
        return;
    selected = track;
    IpInst *in = for_track(track);
    if (in)
        cur = in;
}
/* The UI target after admission changes: the selected track's instance, else
 * the lowest admitted one, else instance 0. */
static IpInst *ui_target(void)
{
    IpInst *in = for_track(selected);
    if (in)
        return in;
    IpInst *low = 0;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (inst[i].track < MI_TRACKS && (!low || inst[i].track < low->track))
            low = &inst[i];
    if (low)
        return low;
    return for_track(cur->track) ? cur : &inst[0];
}
static uint32_t channel_of(const IpInst *in)
{
    uint32_t t = in->track < MI_TRACKS ? in->track : in->bound;
    return t < MI_TRACKS && track_channel[t] ? track_channel[t] : (uint32_t)channel;
}

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{
    IpInst *in = owner(track);
    return in && healthy && in->prepared && (in->phase == 2 || in->phase == 3) &&
           epoch == mi_ledger.epoch && generation == mi_ledger.generations[track] &&
           mi_transport.applied[track] != 0 && !mi_ledger.reset_mask && !mi_transport.faults;
}
uint32_t instrument_src_pages(void)
{
    return 4;
}
uint32_t instrument_src_tune_page(uint32_t page)
{
    return page == 0;
}
static dk_ui_menu *tone_menu;
static dk_ui_menu *input_menu, *shape_menu;
static void pad_changed(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    cur->pad_dirty = 1;
}
static int keyboard_initialized, storage_result, save_pending;
/* Held keys sound as separate notes (PLAY), so a recorded press stores the
 * other held keys as its chord. A ROOT chord is recorded by its root alone. */
int instrument_track_plays_keys(uint32_t track)
{
    const IpInst *in = for_track(track);
    return in && keyboard_initialized && in->keyboard.mode == KB_PLAY;
}
static uint32_t shown_session, shown_revision, refreshed_revision, refreshed_status;
static uint32_t shown_main = UINT32_MAX;
static dk_ui_menu *learn_menu;
static dk_ui_menu *status_menu;
static uint32_t status_draw_at;
volatile uint32_t ip_keyboard_status[12];
extern int dkrt_settings_get(const char *, uint8_t *, uint32_t);
extern int dkrt_settings_put(const char *, const uint8_t *, uint32_t);
extern int dkrt_settings_flush(void);
static const char shape_key[] = "canopy/shapes/v1";
static const char *const preset_keys[CP_SLOTS] = {"canopy/preset/1", "canopy/preset/2",
                                                  "canopy/preset/3", "canopy/preset/4"};
static uint8_t preset_records[CP_SLOTS][CP_RECORD_BYTES], preset_present;
static uint8_t preset_write[CP_RECORD_BYTES];
static int32_t preset_slot = 1, preset_result, preset_pending;
static dk_ui_menu *preset_menu;
static const char route_key[] = "canopy/routes/v1";
static const char channels_key[] = "canopy/channels/v1";
/* The working copy (work-copy.h): written WORK_QUIET_TICKS UI ticks after the last change (about two seconds), and
 * applied to first binds until WORK_WINDOW_TICKS ticks of a healthy instrument have passed since boot (about 20 s),
 * or WORK_GRACE_TICKS (about 3 s) after the first bind of the boot: the tracks of the boot project bind together, and a
 * project loaded after that brings its own settings. */
static const char work_key[] = "canopy/work/v1";
enum { WORK_QUIET_TICKS = 60, WORK_WINDOW_TICKS = 600, WORK_GRACE_TICKS = 90 };
static CwStore work;
static uint32_t work_quiet, work_window;
/* [0] boot copies applied, [1] writes of the record, [2] the last write's result. */
volatile uint32_t ip_work_info[4];
static uint8_t route_write[CR_BYTES], channels_write[CC_BYTES];
static int route_pending, route_result;
static CrRoute restore_route;
static uint8_t restore_channels[MI_TRACKS];
static int route_restore;

void instrument_sound_suspend(uint32_t instance)
{
    if (instance >= IP_INSTANCES)
        return;
    IpInst *in = &inst[instance];
    if (keyboard_initialized && in->keyboard.mode == KB_LEARN)
        kb_learn_cancel(&in->keyboard);
    /* One reset boundary on the instance's track (its bound track while it
     * moves). The reset carries the slot, so DSP slot i follows the instance;
     * binding to a new track happens in the tick after this release. */
    uint32_t track = in->bound < MI_TRACKS ? in->bound : in->track;
    if (track < MI_TRACKS) {
        in->bound = track;
        mi_instance_bind(track, instance);
    }
    in->prepared = 0;
    in->phase = 1;
}
static void suspend_all(void)
{
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (inst[i].track < MI_TRACKS || inst[i].phase)
            instrument_sound_suspend(i);
}

static int keyboard_events_raw(uint32_t track, uint32_t epoch, uint32_t generation,
                               const MiEvent *input, uint32_t count, MiEvent *output,
                               int allow_selector)
{
    uint32_t i;
    /* The instance admitted for this track maps its keys; the transport only
     * delivers a track's batches after that track was bound and configured. */
    IpInst *in = for_track(track);
    if (!in) {
        for (i = 0; i < count; ++i)
            mi_event_copy(&output[i], &input[i]);
        return (int)count;
    }
    if (!keyboard_initialized)
        return -1;
    if (in->keyboard.epoch != epoch || in->keyboard.generation != generation)
        kb_reset(&in->keyboard, epoch, generation);
#ifdef DK_CANOPY_SOUND_PROOF
    int mapped = allow_selector
                     ? kb_process_locked(&in->keyboard, input, count, output)
                     : kb_process_locked_selector(&in->keyboard, input, count, output, 0);
#else
    (void)allow_selector;
    int mapped = kb_process_locked(&in->keyboard, input, count, output);
#endif
    /* The mapper is track-agnostic; the wire batch names the admitted track. */
    for (i = 0; mapped > 0 && i < (uint32_t)mapped; ++i)
        output[i].track = (uint8_t)track;
    return mapped;
}
int instrument_keyboard_events(uint32_t track, uint32_t epoch, uint32_t generation,
                               const MiEvent *input, uint32_t count, MiEvent *output)
{
#ifdef DK_CANOPY_SOUND_PROOF
    return proof_live_keyboard(track, epoch, generation, input, count, output);
#else
    return keyboard_events_raw(track, epoch, generation, input, count, output, 1);
#endif
}
/* Capture the requested source's remembered bank with the trig, before any
 * later UI edit or packet retry. Caller already holds the MIDI IRQ lock. */
int instrument_lock_source(uint32_t track, uint32_t wave, uint16_t out[MI_SOURCE_VALUES])
{
    IpInst *in = for_track(track);
    if (!in || !out || wave > 7)
        return 0;
    const CsState *s = SRC(in);
    int fm = wave == 7;
    out[0] = fm ? s->fm[0] : s->legacy_color;
    for (uint32_t i = 0; i < 12; ++i)
        out[1 + i] = fm ? s->fm[1 + i] : s->wavetable[i];
    for (uint32_t i = 0; i < 4; ++i)
        out[13 + i] = s->value[28 + i];
    if (wave >= 2 && wave < 7)
        out[1] = (uint16_t)(wave - 2);
    return 1;
}
/* Global routing edits re-route every instance; mode, chord, voicing and shape
 * slot belong to the selected instance. */
static void changed(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    uint32_t sr = dkrt_irq_mask();
    route_restore = 0; /* An explicit user edit supersedes deferred boot setup. */
    cur->type = cur->keyboard.type;
    if (cur->keyboard.mode == KB_LEARN)
        kb_learn_cancel(&cur->keyboard);
    if (selector_channel == (int)channel_of(cur))
        selector_channel = 0;
    cur->keyboard.mode = (uint8_t)cur->mode;
    cur->keyboard.type = (uint8_t)cur->type;
    cur->keyboard.voicing = (uint8_t)cur->voicing;
    if (cur->keyboard.slot != (uint8_t)(cur->slot - 1))
        cur->keyboard.preset_shape_active = 0;
    cur->keyboard.slot = (uint8_t)(cur->slot - 1);
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        inst[i].keyboard.selector_base = (uint8_t)((selector_octave + 1) * 12);
        inst[i].keyboard.selector_channel =
            selector_channel ? (uint8_t)(selector_channel - 1) : MI_NONE;
    }
    suspend_all();
    dkrt_irq_restore(sr);
}
/* The CHANNEL row edits the selected CANOPY track's channel. */
static int32_t ui_channel = 1;
static void channel_changed(void *ctx, int32_t value)
{
    (void)value;
    if (cur->track < MI_TRACKS)
        track_channel[cur->track] = (uint8_t)ui_channel;
    else
        channel = ui_channel;
    changed(ctx, ui_channel);
}
/* Caller holds the mapper lock. A performance-mode boundary retires the old
 * physical-note generation; choosing another root chord keeps held roots live. */
static void chord_apply(uint32_t next_mode, uint32_t next_type)
{
    if (cur->keyboard.mode != next_mode)
        instrument_sound_suspend(IDX(cur));
    cur->mode = cur->keyboard.mode = (uint8_t)next_mode;
    cur->type = cur->keyboard.type = (uint8_t)next_type;
    cur->keyboard.update = 1;
}
/* Release every instance: learning off, sounds suspended, keyboards reset to the ledger's epoch. */
static void release_all(void)
{
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        IpInst *in = &inst[i];
        kb_learn_cancel(&in->keyboard);
        if (in->track < MI_TRACKS || in->phase)
            instrument_sound_suspend(i);
        uint32_t t = in->bound < MI_TRACKS ? in->bound : 0;
        kb_reset(&in->keyboard, mi_ledger.epoch, mi_ledger.generations[t]);
    }
    dkrt_irq_restore(sr);
}
/* PANIC releases every instance. */
static void panic(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    release_all();
    dk_ui_toast(ui(), "PANIC - RELEASE HELD KEYS");
}
/* A faulted note transport sends nothing until it starts a new epoch, which only RECOVER KEYBOARD did. Do the same
 * without the menu: after about a second of fault (UI ticks), when the reply guard is still valid and the DSP has
 * not reported a voice fault, start the new epoch and release every instance; the instances then configure their
 * tracks again. At most RECOVER_LIMIT times per boot: a link that keeps failing needs the restart the menu names.
 * ip_recoveries counts them (a diagnostic word). */
enum { RECOVER_TICKS = 45, RECOVER_LIMIT = 8 };
volatile uint32_t ip_recoveries;
static void recover_tick(void)
{
    static uint32_t faulted_ticks;
    if (!mi_transport.faults) {
        faulted_ticks = 0;
        return;
    }
    if (++faulted_ticks < RECOVER_TICKS || ip_recoveries >= RECOVER_LIMIT)
        return;
    faulted_ticks = 0;
    if (instrument_play_faulted(dkrt_instrument_generation()) || mi_keyboard_recover())
        return;
    ++ip_recoveries;
    release_all();
}
/* UI task. Close the boot window of the working copy, and write the copy once it has been quiet for a moment. */
static void work_tick(void)
{
    if (work_window < WORK_WINDOW_TICKS && healthy && ++work_window == WORK_WINDOW_TICKS) {
        uint32_t sr = dkrt_irq_mask();
        cw_expire(&work);
        dkrt_irq_restore(sr);
    }
    if (!work.dirty || ++work_quiet < WORK_QUIET_TICKS)
        return;
    uint8_t record[CW_BYTES];
    uint32_t sr = dkrt_irq_mask();
    cw_encode(&work, record);
    work.dirty = 0;
    work_quiet = 0;
    dkrt_irq_restore(sr);
    ++ip_work_info[1];
    ip_work_info[2] = (uint32_t)dkrt_settings_put(work_key, record, sizeof(record));
}
/* Learned shapes are one library shared by every instance. */
static void shapes_share(const IpInst *from)
{
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        if (&inst[i] == from)
            continue;
        for (uint32_t s = 0; s < 8; ++s)
            inst[i].keyboard.shapes[s] = from->keyboard.shapes[s];
        inst[i].keyboard.dirty = from->keyboard.dirty;
    }
}
static void learn_close(void *ctx, int32_t save)
{
    (void)ctx;
#ifdef DK_CANOPY_SOUND_PROOF
    if (sound_transaction_busy)
        return;
#endif
    uint32_t sr = dkrt_irq_mask();
    int result = 0;
#ifdef DK_CANOPY_SOUND_PROOF
    int transaction = save ? proof_live_learn(shown_session, shown_revision) : 1;
    if (transaction != 1)
        result = transaction;
    else {
#endif
        if (save)
            result = kb_learn_save(&cur->keyboard, shown_session, shown_revision);
        else
            kb_learn_cancel(&cur->keyboard);
        if (!result) {
            if (save)
                shapes_share(cur);
            instrument_sound_suspend(IDX(cur));
            if (save)
                save_pending = 1;
        }
#ifdef DK_CANOPY_SOUND_PROOF
    }
#endif
    dkrt_irq_restore(sr);
    if (result) {
#ifdef DK_CANOPY_SOUND_PROOF
        dk_ui_toast(ui(), transaction != 1 ? "SAVE REFUSED - RETRY" : "CAPTURE CHANGED OR EMPTY");
#else
        dk_ui_toast(ui(), "CAPTURE CHANGED OR EMPTY");
#endif
        dk_ui_menu_refresh(learn_menu);
    } else {
        dk_ui_menu_close(learn_menu);
        if (save)
            dk_ui_toast(ui(), "LEARNED - UNSAVED");
    }
}
/* We close explicitly: consume NO so the stock handler cannot close twice. */
static int32_t learn_back(void *ctx)
{
    learn_close(ctx, 0);
    return 1;
}
/* YES saves from every visible row. NO is handled by learn_back; it must not
 * depend on which informational row the native list currently selects. */
static void learn_row(dk_ui_menu *m, const char *text)
{
    dk_ui_menu_add_action(m, text, learn_close, 0, 1);
}
static void learn_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    uint32_t sr = dkrt_irq_mask();
    shown_session = cur->keyboard.session;
    shown_revision = cur->keyboard.revision;
    uint8_t notes[4], count = cur->keyboard.capture_count, status = cur->keyboard.status,
                      shape_slot = cur->keyboard.slot;
    for (uint32_t i = 0; i < 4; ++i)
        notes[i] = cur->keyboard.capture[i];
    dkrt_irq_restore(sr);
    char title[18];
    for (uint32_t i = 0; i < sizeof(title); ++i)
        ((volatile char *)title)[i] = "SLOT 1  0/4 NOTES"[i];
    title[5] = (char)('1' + shape_slot);
    title[8] = (char)('0' + count);
    learn_row(m, title);
    /* Note zero is C-1, never an empty sentinel. Two pitches per row fit the
     * native menu font even at the four-character C#-1 boundary. */
    for (uint32_t row = 0; row < 2; ++row) {
        char line[14];
        uint32_t at = 0;
        for (uint32_t col = 0; col < 2; ++col) {
            uint32_t i = row * 2 + col;
            char note[6];
            cv_note(note, notes[i], i < count);
            if (col) {
                line[at++] = ' ';
                line[at++] = ' ';
            }
            for (uint32_t j = 0; note[j]; ++j)
                line[at++] = note[j];
        }
        line[at] = 0;
        learn_row(m, line);
    }
    char root[11] = {'R', 'O', 'O', 'T', ' ', 0};
    cv_note(root + 5, notes[0], count != 0);
    learn_row(m, root);
    learn_row(m, status == KB_FULL ? "FULL - FOUR NOTES"
                 : count           ? "YES SAVE / NO CANCEL"
                                   : "PLAY 1-4 NOTES");
}
static const dk_ui_menu_desc learn_desc = {.name = "canopy-learn",
                                           .title = "LEARN TO ROOT",
                                           .flags = DK_UI_LIST,
                                           .fill = learn_fill,
                                           .back = learn_back};
static void learn_begin(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    if (!learn_menu)
        learn_menu = dk_ui_menu_new(ui(), &learn_desc);
    if (!learn_menu) {
        dk_ui_toast(ui(), "CANNOT OPEN LEARN");
        return;
    }
    uint32_t sr = dkrt_irq_mask();
    int result = -1;
    if (cur->keyboard.mode != KB_LEARN && cur->keyboard.session != UINT32_MAX) {
        instrument_sound_suspend(IDX(cur));
        result = kb_learn_begin(&cur->keyboard);
    }
    dkrt_irq_restore(sr);
    if (!result && dk_ui_menu_open(learn_menu))
        learn_close(0, 0);
}
static void recover_done(void *ctx, int32_t yes)
{
    (void)ctx;
    if (!yes)
        return;
    if (fault_generation == dkrt_instrument_generation() || mi_keyboard_recover()) {
        dk_ui_toast(ui(), "RESTART REQUIRED");
        return;
    }
    panic(0, 0);
}
static void recover(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    dk_ui_confirm(ui(), "RECOVER KEYBOARD", "Release ALL keyboard keys and pedals. Ready?",
                  recover_done, 0);
}
static void retry_save(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    save_pending = 1;
}
static int quarantined(const IpInst *in)
{
    for (uint32_t i = 0; i < MI_ROUTES; ++i)
        if (mi_ledger.routes[i].track == in->bound && mi_ledger.routes[i].quarantined)
            return 1;
    return 0;
}
/* Snapshot on the UI task under the mapper lock. Routes and performance state
 * are deliberately absent. Presets include the effective custom shape, not a
 * mutable library slot number. */
static void preset_snapshot(const IpInst *in, CpPreset *p)
{
    uint32_t sr = dkrt_irq_mask();
    const KbState *k = &in->keyboard;
    uint16_t v[16] = {(uint16_t)k->type,
                      (uint16_t)k->voicing,
                      (uint16_t)in->pad_color,
                      (uint16_t)in->pad_drift,
                      (uint16_t)in->pad_strum,
                      (uint16_t)in->arp_mix,
                      (uint16_t)in->arp_rate,
                      (uint16_t)in->arp_order,
                      (uint16_t)(in->arp_octaves + 3),
                      (uint16_t)in->arp_gate,
                      (uint16_t)in->arp_swing,
                      (uint16_t)in->arp_restart,
                      0,
                      0,
                      (uint16_t)in->pad_attack,
                      (uint16_t)in->pad_release};
    for (uint32_t i = 0; i < 16; ++i)
        p->control[i] = v[i];
    for (uint32_t i = 0; i < 12; ++i)
        p->control[16 + i] = (uint16_t)in->wt[i];
    for (uint32_t i = 0; i < 13; ++i)
        p->fm[i] = (uint16_t)in->fm[i];
    for (uint32_t i = 0; i < 4; ++i)
        p->control[28 + i] = (uint16_t)in->wtfm[i];
    p->bpm = CP_ARP_BPM_RESERVED;
    p->family = (uint8_t)in->pad_family;
    p->mode = k->mode;
    p->clock = CP_ARP_CLOCK;
    const KbShape *shape = k->preset_shape_active ? &k->preset_shape : &k->shapes[k->slot];
    p->shape.count = shape->count;
    for (uint32_t i = 0; i < 4; ++i)
        p->shape.intervals[i] = shape->intervals[i];
    dkrt_irq_restore(sr);
}
static void src_requested(IpInst *in, CpPreset *current)
{
    ip_init();
    cs_native_tune(SRC(in), instrument_sound_tune(IDX(in)));
    preset_snapshot(in, current);
    cs_import(SRC(in), current);
}
uint32_t instrument_src_context(uint32_t track, uint32_t page, int visible)
{
    ip_init();
    IpInst *in = for_track(track);
    if (in)
        cur = in;
    else
        in = cur;
    CpPreset current;
    src_requested(in, &current);
    uint32_t generation = track < MI_TRACKS ? mi_ledger.generations[track] : 0;
    uint32_t token = cs_context(
        SRC(in), track, generation,
        visible && for_track(track) == in && keyboard_initialized && dkrt_instrument_type(), page,
        in->keyboard.mode, (uint32_t)in->arp_clock,
        in->keyboard.mode == KB_ROOT && in->keyboard.type == 10,
        in->keyboard.mode == KB_LEARN || (learn_menu && dk_ui_menu_is_open(learn_menu)));
    is_select(STS(in), track, generation, in->keyboard.mode,
              in->keyboard.mode == KB_LEARN ? in->keyboard.session : 0);
    return token;
}
static int src_encoder_raw(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now)
{
    if (!initialized)
        return CS_STALE;
    /* A queued event can outlive a preset load or sound reset without another
     * paint. Revalidate ownership before using its captured token. */
    CsState *s = SRC(cur);
    (void)instrument_src_context(s->track, s->page[s->track < 16 ? s->track : 0], s->visible);
    s = SRC(cur);
    uint32_t id = CS_NONE;
    int result = cs_edit(s, token, encoder, detents, now, &id);
    if (result != CS_EDIT)
        return result;
    uint32_t sr = dkrt_irq_mask();
    int v = id < CP_CONTROLS ? s->value[id] : 0;
    switch (id) {
    case CP_CHORD:
        chord_apply(s->mode, (uint32_t)v);
        break;
    case CP_VOICING:
        cur->voicing = v;
        cur->keyboard.voicing = (uint8_t)v;
        cur->keyboard.update = 1;
        break;
    case CP_COLOR:
        if (cur->pad_family == 3)
            cur->fm[0] = v;
        else
            cur->pad_color = v;
        break;
    case CP_DRIFT:
        cur->pad_drift = v;
        break;
    case CP_STRUM:
        cur->pad_strum = v;
        break;
    case CP_ARP_MIX:
        cur->arp_mix = v;
        break;
    case CP_RATE:
        cur->arp_rate = v;
        break;
    case CP_ORDER:
        cur->arp_order = v;
        break;
    case CP_OCTAVES:
        cur->arp_octaves = v - 3;
        break;
    case CP_GATE:
        cur->arp_gate = v;
        break;
    case CP_SWING:
        cur->arp_swing = v;
        break;
    case CP_RESTART:
        cur->arp_restart = v;
        break;
    case CP_WT_CLOCK:
        if (cur->pad_family == 2) {
            cur->wt[CP_WT_CLOCK - 16] = v;
            cur->wt[CP_WT_RATE - 16] = s->value[CP_WT_RATE];
        } else
            cur->fm[CP_FM_DECAY - 15] = v;
        break;
    case CS_WAVE:
        cur->pad_family = (int)((s->metadata >> 4) & 3u);
        cur->pad_color = s->legacy_color;
        for (uint32_t i = 0; i < 12; ++i)
            cur->wt[i] = s->wavetable[i];
        for (uint32_t i = 0; i < 13; ++i)
            cur->fm[i] = s->fm[i];
        break;
    default:
        if (id >= 28 && id < CP_CONTROLS)
            cur->wtfm[id - 28] = v;
        else if (id >= 16 && id < 28) {
            if (cur->pad_family == 3)
                cur->fm[id - 15] = v;
            else
                cur->wt[id - 16] = v;
        }
        break;
    }
    cur->pad_dirty = 1;
    dkrt_irq_restore(sr);
    return result;
}
int instrument_src_encoder(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now)
{
#ifdef DK_CANOPY_SOUND_PROOF
    return proof_live_encoder(token, encoder, detents, now);
#else
    return src_encoder_raw(token, encoder, detents, now);
#endif
}
/* Only the focused instance's status reaches the link (the DSP publishes the
 * flagged slot), so drain into that instance. */
static IpInst *focused(void)
{
    return focus < IP_INSTANCES ? &inst[focus] : cur;
}
void instrument_src_poll(uint32_t now)
{
    if (!initialized)
        return;
    IpInst *in = focused();
    IsStatus *st = STS(in);
    is_poll(st, now, mi_status_reply);
    if (is_detail_confirmed(st, now)) {
        if (in->pad_family == 3 && st->detail_schema < 4)
            return;
        if (st->detail_schema >= 5)
            cs_applied(SRC(in), st->generation, st->detail_control, st->detail, st->detail[42]);
        else {
            uint32_t legacy[32];
            uint32_t count = st->detail_schema >= 3 ? 28 : 16;
            for (uint32_t i = 0; i < 32; ++i)
                legacy[i] = i < count ? st->detail[i] : UINT32_MAX;
            cs_applied(SRC(in), st->generation, st->detail_control, legacy,
                       st->detail[st->detail_schema >= 3 ? 28 : 16]);
        }
    }
}
void instrument_src_display(CvDisplay *out, uint32_t now, const char *tune, const char *level)
{
    ip_init();
    CsState *s = SRC(cur);
    (void)instrument_src_context(s->track, s->page[s->track < 16 ? s->track : 0], s->visible);
    instrument_src_poll(now);
    cv_build(out, SRC(cur), STS(cur), now, tune, level, preset_result || storage_result);
    uint32_t sr = dkrt_irq_mask();
    cv_ownership(out, STS(cur), &cur->keyboard, now);
    dkrt_irq_restore(sr);
}
int instrument_src_receive(const uint32_t words[IS_WORDS], uint32_t now)
{
    return initialized ? is_receive(STS(focused()), words, now) : -1;
}
void instrument_src_feedback(char out[32], uint32_t encoder)
{
    cv_feedback(out, SRC(cur), encoder);
}
static uint32_t family_of(const CsState *s)
{
    return (s->metadata >> 4) & 3u;
}
static void lock_state(CsState *shown, const IlpSnapshot *snapshot);
int instrument_src_lock_target(uint32_t encoder, const IlpSnapshot *snapshot, uint32_t *control_out,
                               uint32_t *id, uint32_t *base)
{
    if (!initialized)
        return CS_STALE;
    CsState *s = SRC(cur);
    (void)instrument_src_context(s->track, s->page[s->track < 16 ? s->track : 0], s->visible);
    s = SRC(cur);
    CsState shown;
    lock_state(&shown, snapshot);
    uint32_t control = cs_mapped_control(&shown, encoder);
    if (control == CS_TUNE || control == CS_LEVEL)
        return CS_NATIVE;
    if ((control >= CP_CONTROLS && control != CS_WAVE) || control == CP_RESERVED12 ||
        control == CP_SOURCE)
        return CS_READONLY;
    uint32_t wire = ilp_id_for(family_of(&shown), control);
    if (wire == UINT32_MAX)
        return CS_READONLY;
    *control_out = control;
    *id = wire;
    *base = cs_choice_value(&shown, control);
    return CS_EDIT;
}
int instrument_src_lock_step(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now,
                             const IlpSnapshot *snapshot, uint32_t current, uint32_t *next)
{
    if (!initialized)
        return CS_STALE;
    CsState shown;
    lock_state(&shown, snapshot);
    int result = cs_lock_step(&shown, token, encoder, detents, now, current, 0, next);
    CsState *s = SRC(cur);
    s->focus = shown.focus;
    s->focus_id = shown.focus_id;
    s->focus_at = shown.focus_at;
    for (uint32_t i = 0; i < 8; ++i) {
        s->list_delta[i] = shown.list_delta[i];
        s->list_id[i] = shown.list_id[i];
    }
    return result;
}
static void lock_show(CsState *shown, uint32_t control, uint32_t value)
{
    if (control == CP_CHORD) {
        shown->mode = value ? KB_ROOT : KB_PLAY;
        shown->learned = (uint8_t)(value == 11);
        if (value)
            shown->value[control] = (uint16_t)(value - 1);
    } else if (control == CS_WAVE) {
        uint32_t family = value < 2 ? value : value == 7 ? 3 : 2;
        shown->metadata = (shown->metadata & ~0x30u) | (family << 4);
        shown->value[CP_COLOR] = family == 3 ? shown->fm[0] : shown->legacy_color;
        for (uint32_t i = 0; i < 12; ++i)
            shown->value[16 + i] = family == 3 ? shown->fm[1 + i] : shown->wavetable[i];
        if (value >= 2 && value < 7)
            shown->value[CP_WT_TABLE] = (uint16_t)(value - 2);
    } else if (control < CP_CONTROLS)
        shown->value[control] = (uint16_t)value;
}
static void lock_state(CsState *shown, const IlpSnapshot *snapshot)
{
    for (uint32_t i = 0; i < sizeof(*shown); ++i)
        ((volatile uint8_t *)shown)[i] = ((const volatile uint8_t *)SRC(cur))[i];
    if (!snapshot)
        return;
    for (uint32_t i = 0; i < snapshot->count && i < ILP_LANES; ++i)
        if (snapshot->pair[i].id == CS_WAVE)
            lock_show(shown, CS_WAVE, snapshot->pair[i].value);
    for (uint32_t i = 0; i < snapshot->count && i < ILP_LANES; ++i) {
        if (snapshot->pair[i].id == CS_WAVE)
            continue;
        uint32_t control = ilp_control_for(family_of(shown), snapshot->pair[i].id);
        if (control != UINT32_MAX)
            lock_show(shown, control, snapshot->pair[i].value);
    }
    /* The DSP clamps a free-Hz rate to the last sync division when a clock
     * lock enables SYNC. The held-step view must show that effective value. */
    if (family_of(shown) == 2 && shown->value[CP_WT_CLOCK] && shown->value[CP_WT_RATE] > 7)
        shown->value[CP_WT_RATE] = 7;
}
void instrument_src_lock_feedback(char out[32], uint32_t encoder, const IlpSnapshot *snapshot,
                                  uint32_t control, uint32_t value)
{
    CsState shown;
    lock_state(&shown, snapshot);
    lock_show(&shown, control, value);
    cv_feedback(out, &shown, encoder);
}
void instrument_src_display_locked(CvDisplay *out, uint32_t now, const char *tune,
                                   const char *level, const IlpSnapshot *snapshot)
{
    instrument_src_display(out, now, tune, level);
    CsState shown;
    lock_state(&shown, snapshot);
    cv_build_owner(out, &shown, STS(cur), now, tune, level, preset_result || storage_result,
                   SRC(cur)->mode);
    out->locked = cv_lock_mask(&shown, snapshot);
    uint32_t sr = dkrt_irq_mask();
    cv_ownership(out, STS(cur), &cur->keyboard, now);
    dkrt_irq_restore(sr);
}
void instrument_src_preview_step(CvDisplay *out, const IlpSnapshot *snapshot, int32_t note)
{
    if (!snapshot || note < 0 || note > 127 || out->page || snapshot->count > MI_LOCKS)
        return;
    /* Private UI scratch keeps held-step rendering out of live ownership. */
    static KbState scratch;
    uint32_t sr = dkrt_irq_mask();
    /* Only configuration and chord templates precede keys. The preview resets
     * its own key/voice ledger, so never copy that large live ledger while
     * interrupts are masked. */
    for (uint32_t i = 0; i < offsetof(KbState, keys); ++i)
        ((volatile uint8_t *)&scratch)[i] = ((const volatile uint8_t *)&cur->keyboard)[i];
    dkrt_irq_restore(sr);
    uint16_t locks[MI_LOCKS];
    uint8_t pitches[4];
    for (uint32_t i = 0; i < snapshot->count; ++i)
        locks[i] = (uint16_t)((snapshot->pair[i].id << 10) | snapshot->pair[i].value);
    int count = kb_preview_locked(&scratch, (uint8_t)note, locks, snapshot->count, pitches);
    if (count > 0)
        cv_preview_notes(out, pitches, (uint32_t)count);
}
static void status_pair(dk_ui_menu *m, const char *a, const char *b)
{
    const char *first = a, *second = b;
    char row[DK_UI_ROW];
    uint32_t at = 0;
    while (*a && at + 1 < sizeof(row))
        row[at++] = *a++;
    if (at + 1 < sizeof(row))
        row[at++] = ' ';
    while (*b && at + 1 < sizeof(row))
        row[at++] = *b++;
    row[at] = 0;
    /* The native right column fits 18 parameter-font characters. Keep long
     * chord/bass names and excluded-note counts whole on a separate row. */
    if (at > 18) {
        dk_ui_menu_add_text(m, first);
        dk_ui_menu_add_text(m, second);
    } else
        dk_ui_menu_add_text(m, row);
}
static void status_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    ip_init();
    uint32_t now = instrument_src_now();
    IpInst *in = cur;
    uint32_t track = in->track < MI_TRACKS ? in->track : 0;
    (void)instrument_src_context(track, SRC(in)->page[track], 0);
    instrument_src_poll(now);
    CsState snapshot;
    for (uint32_t i = 0; i < sizeof(snapshot); ++i)
        ((volatile uint8_t *)&snapshot)[i] = ((const volatile uint8_t *)SRC(in))[i];
    snapshot.focus = 0;
    snapshot.track = track;
    snapshot.page[track] = 0;
    CvDisplay d;
    cv_build(&d, &snapshot, STS(in), now, "", "", preset_result || storage_result);
    uint32_t sr = dkrt_irq_mask();
    cv_ownership(&d, STS(in), &in->keyboard, now);
    dkrt_irq_restore(sr);
    status_pair(m,
                snapshot.mode == KB_PLAY   ? "PLAY"
                : snapshot.mode == KB_ROOT ? "ROOT"
                                           : "LEARN",
                d.heading);
    status_pair(m, d.notes[0], d.notes[1]);
    status_pair(m, d.notes[2], d.notes[3]);
    int invalid = d.fault == 1 || d.fault == 2;
    dk_ui_menu_add_text(m, invalid ? "PEDAL --" : d.pedal ? "PEDAL DOWN" : "PEDAL UP");
    snapshot.page[track] = 1;
    cv_build(&d, &snapshot, STS(in), now, "", "", preset_result || storage_result);
    dk_ui_menu_add_text(m, d.heading);
    /* The carrier track, as the panel numbers it (T1..T16). */
    char where[9] = {'T', 'R', 'A', 'C', 'K', ' ', '-', '-', 0};
    if (in->track < MI_TRACKS) {
        uint32_t n = in->track + 1;
        if (n >= 10) {
            where[6] = '1';
            where[7] = (char)('0' + n - 10);
        } else {
            where[6] = (char)('0' + n);
            where[7] = 0;
        }
    }
    dk_ui_menu_add_text(m, where);
    status_draw_at = now;
}
/* VerticalMenuView reserves a narrow left title column. */
static const dk_ui_menu_desc status_desc = {
    .name = "canopy-status", .title = "STATUS", .flags = DK_UI_TEXT_PAGE, .fill = status_fill};
static int preset_dirty(void)
{
    CpPreset p;
    uint8_t b[CP_RECORD_BYTES];
    preset_snapshot(cur, &p);
    if (cp_encode(&p, b) || !(preset_present & (1u << (preset_slot - 1))))
        return 1;
    /* Compare current encodings: migrating a V1 preset alone is not an edit. */
    CpPreset saved;
    uint8_t canonical[CP_RECORD_BYTES];
    if (cp_decode(&saved, preset_records[preset_slot - 1], CP_RECORD_BYTES))
        return 1;
    saved.clock = CP_ARP_CLOCK;
    saved.bpm = CP_ARP_BPM_RESERVED;
    if (cp_encode(&saved, canonical))
        return 1;
    for (uint32_t i = 0; i < CP_RECORD_BYTES; ++i)
        if (b[i] != canonical[i])
            return 1;
    return 0;
}
static void preset_save(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    if (cur->keyboard.mode == KB_LEARN) {
        dk_ui_toast(ui(), "CLOSE LEARN FIRST");
        return;
    }
    CpPreset p;
    preset_snapshot(cur, &p);
    if (cp_encode(&p, preset_write)) {
        dk_ui_toast(ui(), "INVALID SOUND - NOT SAVED");
        return;
    }
    preset_pending = preset_slot;
}
static void preset_select(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    if (preset_menu)
        dk_ui_menu_refresh(preset_menu);
}
/* Publish a validated snapshot at one reset boundary. The caller holds the
 * mapper lock. */
static void preset_apply_locked(IpInst *in, const CpPreset *p)
{
    instrument_sound_suspend(IDX(in));
    in->mode = in->keyboard.mode = p->mode;
    in->type = in->keyboard.type = (uint8_t)p->control[CP_CHORD];
    in->voicing = in->keyboard.voicing = (uint8_t)p->control[CP_VOICING];
    in->keyboard.preset_shape.count = p->shape.count;
    for (uint32_t i = 0; i < 4; ++i)
        in->keyboard.preset_shape.intervals[i] = p->shape.intervals[i];
    in->keyboard.preset_shape_active = 1;
    in->pad_color = p->control[CP_COLOR];
    in->pad_drift = p->control[CP_DRIFT];
    in->pad_strum = p->control[CP_STRUM];
    in->arp_mix = p->control[CP_ARP_MIX];
    in->arp_rate = p->control[CP_RATE];
    in->arp_order = p->control[CP_ORDER];
    in->arp_octaves = (int)p->control[CP_OCTAVES] - 3;
    in->arp_gate = p->control[CP_GATE];
    in->arp_swing = p->control[CP_SWING];
    in->arp_restart = p->control[CP_RESTART];
    in->pad_attack = p->control[CP_ATTACK];
    in->pad_release = p->control[CP_RELEASE];
    for (uint32_t i = 0; i < 12; ++i)
        in->wt[i] = p->control[16 + i];
    for (uint32_t i = 0; i < 13; ++i)
        in->fm[i] = p->fm[i];
    for (uint32_t i = 0; i < 4; ++i)
        in->wtfm[i] = p->control[28 + i];
    in->pad_family = p->family;
    in->arp_clock = CP_ARP_CLOCK;
    in->arp_bpm = CP_ARP_BPM_RESERVED;
    in->pad_dirty = 1;
}

#if defined(__m68k__)
/* Sound persistence (sound-persist.h). Each instance follows the Sound it is
 * admitted on: it adopts that Sound's CANOPY extension when it binds, or when
 * stock replaces the Sound under it (a load, paste or reset advances the
 * slot's sequence number), and publishes its own state back whenever that
 * state changes. A Sound without an extension keeps the instance's current
 * state, as every Sound did before persistence, and the instance claims it.
 * An extension that does not decode (another format, corruption) is left in
 * place, unchanged, until the instance's state changes.
 * Target only: host tests that include this file run without the store, which
 * has its own host test; CANOPY's persistence device tests cover it. */
typedef struct {
    uintptr_t sound;
    uint32_t seq;
    CpPreset state;
    uint8_t bound;
    /* The carrier track at the bind: the working copy's entry of this Sound. */
    uint8_t track;
} IpSaBind;
static IpSaBind sa_bind[IP_INSTANCES];
static void persist_snapshot(const IpInst *in, CpPreset *p)
{
    preset_snapshot(in, p);
    /* LEARN is temporary; its Sound keeps the mode it had before. */
    if (in->keyboard.mode == KB_LEARN)
        p->mode = in->keyboard.previous_mode;
}
static void copy_state(CpPreset *d, const CpPreset *s)
{
    for (uint32_t n = 0; n < sizeof(*d); ++n)
        ((volatile uint8_t *)d)[n] = ((const uint8_t *)s)[n];
}
/* Every field the record extension carries (bpm and clock are fixed). */
static int same_state(const CpPreset *a, const CpPreset *b)
{
    for (uint32_t n = 0; n < CP_CONTROLS; ++n)
        if (a->control[n] != b->control[n])
            return 0;
    for (uint32_t n = 0; n < 13; ++n)
        if (a->fm[n] != b->fm[n])
            return 0;
    if (a->family != b->family || a->mode != b->mode || a->shape.count != b->shape.count)
        return 0;
    for (uint32_t n = 0; n < 4; ++n)
        if (a->shape.intervals[n] != b->shape.intervals[n])
            return 0;
    return 1;
}
/* Interrupts masked. Instance i is admitted on sound, with no load in flight. */
static int sa_owned(uint32_t i, uintptr_t sound)
{
    volatile const uint32_t *a = &ip_admission[i * 8];
    uint32_t type = dkrt_instrument_type();
    return sound && a[0] && !a[6] && a[1] == sound && type &&
           *(const volatile uint8_t *)(sound + 0xa2) == type;
}
/* Publish instance i's state to its bound Sound if it changed and nothing
 * else has replaced that Sound since. owned: still admitted on it (else the
 * instance is leaving it, and the Sound must still carry the type). */
static void sa_publish(uint32_t i, int owned)
{
    SaStore *s = ip_sa_store();
    IpSaBind *b = &sa_bind[i];
    if (!s || !b->bound)
        return;
    CpPreset now;
    persist_snapshot(&inst[i], &now);
    if (same_state(&now, &b->state))
        return;
    uint8_t payload[SR_BYTES];
    if (sr_encode(&now, payload))
        return;
    uint32_t sr = dkrt_irq_mask();
    uint32_t type = dkrt_instrument_type();
    int mine = b->bound && sa_seq(s, b->sound) == b->seq &&
               (owned ? sa_owned(i, b->sound)
                      : type && *(const volatile uint8_t *)(b->sound + 0xa2) == type);
    if (mine) {
        b->seq = sa_put(s, b->sound, payload);
        copy_state(&b->state, &now);
        if (cw_set(&work, b->track, payload))
            work_quiet = 0;
    }
    dkrt_irq_restore(sr);
}
void instrument_sound_flush(uintptr_t lo, uintptr_t hi)
{
    if (!initialized || !keyboard_initialized)
        return;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        const IpSaBind *b = &sa_bind[i];
        if (b->bound && b->sound >= lo && b->sound + SA_SOUND <= hi)
            sa_publish(i, 1);
    }
}
/* UI task, after the admission step of instance i. */
static void sa_follow(uint32_t i)
{
    SaStore *s = ip_sa_ensure();
    IpSaBind *b = &sa_bind[i];
    if (!s || !keyboard_initialized)
        return;
    uint32_t sr = dkrt_irq_mask();
    uintptr_t sound = ip_admission[i * 8 + 1];
    int owned = sa_owned(i, sound);
    uint32_t seq = owned ? sa_seq(s, sound) : 0;
    int same = b->bound && b->sound == sound && b->seq == seq;
    dkrt_irq_restore(sr);
    if (!owned || sa_slot(s, sound) < 0)
        return;
    if (same) {
        sa_publish(i, 1);
        return;
    }
    if (b->bound && b->sound != sound)
        sa_publish(i, 0);
    uint8_t payload[SR_BYTES];
    CpPreset want, now;
    sr = dkrt_irq_mask();
    int have = sa_get(s, sound, payload, &seq);
    dkrt_irq_restore(sr);
    int decoded = have && !sr_decode(&want, payload);
    /* The first bind of a track after boot takes the working copy: the settings it had when the power went, saved
     * with a project or not. It wins over the extension of the boot records, which is the last save's. */
    uint32_t track = inst[i].track;
    uint8_t boot[SR_BYTES];
    int from_work = cw_boot(&work, track, boot) && !sr_decode(&want, boot);
    if (from_work)
        decoded = 1;
    persist_snapshot(&inst[i], &now);
    if (decoded && !same_state(&want, &now)) {
        sr = dkrt_irq_mask();
        if (sa_owned(i, sound) && sa_seq(s, sound) == seq)
            preset_apply_locked(&inst[i], &want);
        else
            decoded = -1;
        dkrt_irq_restore(sr);
        if (decoded < 0)
            return; /* replaced meanwhile: adopt on the next tick */
    }
    /* A Sound without an extension is claimed with the state it sounds with. */
    uint8_t claim[SR_BYTES];
    int claiming = from_work || (!have && !sr_encode(&now, claim));
    uint8_t bound_now[SR_BYTES];
    int known = !sr_encode(decoded ? &want : &now, bound_now);
    sr = dkrt_irq_mask();
    b->sound = sound;
    b->seq = seq;
    copy_state(&b->state, decoded ? &want : &now);
    b->bound = 1;
    b->track = (uint8_t)(track < MI_TRACKS ? track : CW_TRACKS);
    if (claiming && sa_owned(i, sound) && sa_seq(s, sound) == seq)
        b->seq = sa_put(s, sound, from_work ? boot : claim);
    cw_taken(&work, track);
    if (work_window < WORK_WINDOW_TICKS - WORK_GRACE_TICKS)
        work_window = WORK_WINDOW_TICKS - WORK_GRACE_TICKS;
    if (from_work)
        ++ip_work_info[0];
    if (known && cw_set(&work, b->track, bound_now))
        work_quiet = 0;
    dkrt_irq_restore(sr);
}
#else
static void sa_follow(uint32_t i)
{
    (void)i;
}
static void ip_sa_tick(void)
{
}
#endif
#ifdef DK_CANOPY_SOUND_PROOF
/* Must hold the mapper lock. A source address alone is not an identity: an
 * in-place load invalidates admission and advances its generation. */
static int sound_admission_owned(uint32_t i, uint32_t sound, uint32_t generation)
{
    if (!initialized || !keyboard_initialized || i >= IP_INSTANCES || !sound)
        return 0;
    volatile const uint32_t *a = &ip_admission[i * 8];
    return a[0] && !a[6] && a[1] == sound && a[4] == generation && a[5] &&
           a[5] == dkrt_instrument_type() && inst[i].track < MI_TRACKS &&
           inst[i].track == ip_tracks[i];
}
static int sound_state_owned(uint32_t i, uint32_t sound, uint32_t generation)
{
    return sound_admission_owned(i, sound, generation) && inst[i].keyboard.mode != KB_LEARN;
}
int instrument_sound_snapshot(uint32_t i, uint32_t sound, CpPreset *out, uint32_t *generation)
{
    if (!out || !generation || i >= IP_INSTANCES)
        return -1;
    uint32_t sr = dkrt_irq_mask(), gen = ip_admission[i * 8 + 4];
    CpPreset p;
    int ok = sound_state_owned(i, sound, gen);
    if (ok)
        preset_snapshot(&inst[i], &p);
    dkrt_irq_restore(sr);
    if (!ok || !cp_valid(&p) || p.clock != CP_ARP_CLOCK || p.bpm != CP_ARP_BPM_RESERVED)
        return -1;
    for (uint32_t n = 0; n < sizeof(p); ++n)
        ((uint8_t *)out)[n] = ((const uint8_t *)&p)[n];
    *generation = gen;
    return 0;
}
int instrument_sound_proof_snapshot(uint32_t i, uint32_t sound, uint32_t generation, CpPreset *out)
{
    if (!out || i >= IP_INSTANCES)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    if (!sound_admission_owned(i, sound, generation)) {
        dkrt_irq_restore(sr);
        return -1;
    }
    CpPreset p;
    preset_snapshot(&inst[i], &p);
    /* LEARN is temporary performance/UI state. Its accepted Sound still has
     * the previous mode and effective shape until SAVE commits all owners. */
    if (inst[i].keyboard.mode == KB_LEARN)
        p.mode = inst[i].keyboard.previous_mode;
    if (!cp_valid(&p) || p.clock != CP_ARP_CLOCK || p.bpm != CP_ARP_BPM_RESERVED) {
        dkrt_irq_restore(sr);
        return -1;
    }
    for (uint32_t n = 0; n < sizeof(p); ++n)
        ((volatile uint8_t *)out)[n] = ((const uint8_t *)&p)[n];
    dkrt_irq_restore(sr);
    return 0;
}
int instrument_sound_restore(uint32_t i, uint32_t sound, uint32_t generation, const CpPreset *p)
{
    if (!p || !cp_valid(p) || p->clock != CP_ARP_CLOCK || p->bpm != CP_ARP_BPM_RESERVED)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    int ok = sound_state_owned(i, sound, generation);
    if (ok)
        preset_apply_locked(&inst[i], p);
    dkrt_irq_restore(sr);
    return ok ? 0 : -1;
}
/* Test-only preset-load transaction. The preset has already been decoded;
 * preparation must not mutate the Sound, and publication must be infallible
 * while the mapper lock remains held. Only opt-in proof registration wires
 * the real preset LOAD callback to this helper. */
int instrument_sound_proof_apply(uint32_t i, uint32_t sound, uint32_t generation, const CpPreset *p,
                                 int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                 void *context)
{
    if (!p || !prepare || !attach || i >= IP_INSTANCES || !cp_valid(p) ||
        p->clock != CP_ARP_CLOCK || p->bpm != CP_ARP_BPM_RESERVED)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    int ok = sound_state_owned(i, sound, generation) && cur == &inst[i];
    if (ok && !prepare(context, p)) {
        preset_apply_locked(&inst[i], p);
        attach(context);
    } else
        ok = 0;
    dkrt_irq_restore(sr);
    return ok ? 0 : -1;
}
/* A single preset and shape are enough to preview a shared-library save: only
 * the source's mode/effective shape and the peers' effective shapes change.
 * No full keyboard/instance copy goes on either the stack or in this scratch.
 * The mapper lock serializes callers; busy also rejects callback re-entry. */
static struct {
    CpPreset preset;
    KbShape shape;
    uint8_t busy;
} proof_learn_scratch;
int instrument_sound_proof_learn(uint32_t source, uint32_t sound, uint32_t generation,
                                 uint32_t session, uint32_t revision,
                                 int (*prepare)(void *, uint32_t, uint32_t, uint32_t,
                                                const CpPreset *),
                                 void (*attach)(void *, uint32_t), void *context)
{
    if (!prepare || !attach || source >= IP_INSTANCES)
        return -1;
    uint32_t sr = dkrt_irq_mask(), affected = 0;
    if (proof_learn_scratch.busy) {
        dkrt_irq_restore(sr);
        return -1;
    }
    proof_learn_scratch.busy = 1;
    if (cur != &inst[source] || !sound_admission_owned(source, sound, generation) ||
        cur->bound != cur->track)
        goto refuse;
    KbState *learner = &cur->keyboard;
    if (learner->mode != KB_LEARN || learner->session != session || learner->revision != revision ||
        !learner->capture_count || learner->capture_count > 4 || learner->slot >= 8)
        goto refuse;
    /* Preview the actual kb_learn_save result without mutating its status on
     * refusal. The real save remains the commit operation below. */
    proof_learn_scratch.shape.count = learner->capture_count;
    for (uint32_t n = 0; n < 4; ++n)
        proof_learn_scratch.shape.intervals[n] =
            n < learner->capture_count ? learner->capture[n] - learner->capture[0] : 0;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        IpInst *in = &inst[i];
        KbState *k = &in->keyboard;
        if (in->track >= MI_TRACKS)
            continue;
        const KbShape *shape = &proof_learn_scratch.shape;
        if (i != source) {
            if (k->preset_shape_active)
                continue;
            if (k->slot >= 8)
                goto refuse;
            /* shapes_share copies all eight slots. Compare the actual future
             * effective shape, including a divergent peer's other slot and
             * PLAY/ordinary ROOT modes which still persist that shape. */
            if (k->slot != learner->slot)
                shape = &learner->shapes[k->slot];
            const KbShape *old = &k->shapes[k->slot];
            int differs = old->count != shape->count;
            for (uint32_t n = 0; n < 4; ++n)
                differs |= old->intervals[n] != shape->intervals[n];
            if (!differs)
                continue;
        }
        uint32_t peer_sound = ip_admission[8 * i + 1], peer_generation = ip_admission[8 * i + 4];
        if (!sound_admission_owned(i, peer_sound, peer_generation) || in->bound != in->track ||
            (i != source && k->mode == KB_LEARN))
            goto refuse;
        CpPreset *p = &proof_learn_scratch.preset;
        preset_snapshot(in, p);
        if (i == source)
            p->mode = learner->previous_mode;
        p->shape.count = shape->count;
        for (uint32_t n = 0; n < 4; ++n)
            p->shape.intervals[n] = shape->intervals[n];
        if (!cp_valid(p) || prepare(context, i, peer_sound, peer_generation, p))
            goto refuse;
        affected |= 1u << i;
    }
    /* Each prepare only stages immutable bytes and verifies its reservation;
     * it must not alter live state. The uninterrupted lock keeps validation
     * valid for this real save and every infallible attachment. */
    if (kb_learn_save(learner, session, revision))
        goto refuse;
    shapes_share(cur);
    instrument_sound_suspend(source);
    save_pending = 1;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (affected & (1u << i))
            attach(context, i);
    proof_learn_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return 0;
refuse:
    proof_learn_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return -1;
}
/* The menu writes the signed 1-based backing before calling changed(). Keep
 * that staging separate from the accepted keyboard slot during preparation,
 * so refusal cannot expose a new slot or drop a loaded preset override. */
static struct {
    CpPreset preset;
    uint8_t busy;
} proof_shape_scratch;
int instrument_sound_proof_shape_slot(uint32_t i, uint32_t sound, uint32_t generation, int slot,
                                      int (*prepare)(void *, const CpPreset *),
                                      void (*attach)(void *), void *context)
{
    if (i >= IP_INSTANCES)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    if (proof_shape_scratch.busy) {
        dkrt_irq_restore(sr);
        return -1;
    }
    proof_shape_scratch.busy = 1;
    IpInst *in = &inst[i];
    KbState *k = &in->keyboard;
    if (!initialized || !keyboard_initialized || k->slot >= 8 || in->slot != slot)
        goto refuse;
    /* Only undo the matching staged request, never a newer backing value.
     * The accepted keyboard slot is authoritative even if focus or admission
     * changed between the menu write and this callback. */
    in->slot = (int)k->slot + 1;
    if (!prepare || !attach || slot < 1 || slot > 8 || cur != in ||
        !sound_state_owned(i, sound, generation) || in->bound != in->track)
        goto refuse;
    CpPreset *p = &proof_shape_scratch.preset;
    preset_snapshot(in, p);
    /* changed() preserves the mapper's selected chord, but reapplies these
     * two UI fields. Include that actual callback behavior in the preview. */
    p->mode = (uint8_t)in->mode;
    p->control[CP_VOICING] = (uint8_t)in->voicing;
    if (k->slot != (uint8_t)(slot - 1)) {
        const KbShape *shape = &k->shapes[slot - 1];
        p->shape.count = shape->count;
        for (uint32_t n = 0; n < 4; ++n)
            p->shape.intervals[n] = shape->intervals[n];
    }
    if (!cp_valid(p) || prepare(context, p))
        goto refuse;
    in->slot = slot;
    /* Preserve the real override rule, selector/routing updates and all
     * instance reset boundaries. No part of changed() runs on refusal. */
    changed(0, slot);
    attach(context);
    proof_shape_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return 0;
refuse:
    proof_shape_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return -1;
}
int instrument_sound_proof_defaults(uint32_t i, uint32_t sound, uint32_t generation,
                                    int (*claim)(void *), void *context)
{
    /* Use the ordinary SRC initializer for its canonical defaults instead of
     * keeping a second list of values in the persistence experiment. This is
     * called from the UI tick, never the audio/MIDI interrupt. */
    if (!claim)
        return -1;
    CsState defaults;
    CpPreset p;
    cs_init(&defaults);
    for (uint32_t n = 0; n < sizeof(p); ++n)
        ((volatile uint8_t *)&p)[n] = 0;
    for (uint32_t n = 0; n < CP_CONTROLS; ++n)
        p.control[n] = defaults.value[n];
    for (uint32_t n = 0; n < 13; ++n)
        p.fm[n] = defaults.fm[n];
    p.bpm = CP_ARP_BPM_RESERVED;
    p.clock = CP_ARP_CLOCK;
    p.mode = KB_PLAY;
    if (!cp_valid(&p))
        return -1;
    uint32_t sr = dkrt_irq_mask();
    int ok = sound_state_owned(i, sound, generation) && !claim(context);
    if (ok) {
        preset_apply_locked(&inst[i], &p);
        /* Default Sounds use the shared learned-shape library, just as a new
         * keyboard does, instead of overriding it with an empty embedded USER. */
        inst[i].keyboard.preset_shape_active = 0;
        inst[i].keyboard.slot = 0;
        inst[i].slot = 1;
    }
    dkrt_irq_restore(sr);
    return ok ? 0 : -1;
}
/* Test-only experiment: preview a real SRC encoder edit in a
 * copied SRC state, prepare its snapshot in already-reserved memory, then
 * apply the same edit and attach its handle without releasing the mapper lock.
 * Opt-in proof registration wires this to the emulator UI, never production.
 * The critical section's latency remains unproved. */
int instrument_sound_proof_edit(uint32_t i, uint32_t sound, uint32_t generation, uint32_t token,
                                uint32_t encoder, int32_t detents, uint32_t now,
                                int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                void *context)
{
    if (!prepare || !attach || i >= IP_INSTANCES)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    if (!sound_state_owned(i, sound, generation) || cur != &inst[i]) {
        dkrt_irq_restore(sr);
        return -1;
    }
    CpPreset current;
    uint32_t track = inst[i].track, page = SRC(&inst[i])->page[track];
    (void)instrument_src_context(track, page, 1);
    preset_snapshot(&inst[i], &current);
    /* Freestanding proof builds do not link libc; a large structure
     * assignment can otherwise lower to an unresolved memcpy call. */
    CsState preview;
    volatile uint8_t *dst = (volatile uint8_t *)&preview;
    const volatile uint8_t *src = (const volatile uint8_t *)SRC(&inst[i]);
    for (uint32_t n = 0; n < sizeof(preview); ++n)
        dst[n] = src[n];
    uint32_t id = CS_NONE;
    int step = cs_edit(&preview, token, encoder, detents, now, &id);
    if (step != CS_EDIT) {
        /* Keep list-detent remainders and native-control dispatch exactly as
         * the ordinary encoder path does; no Sound snapshot is consumed. */
        int result = src_encoder_raw(token, encoder, detents, now);
        dkrt_irq_restore(sr);
        return result;
    }
    for (uint32_t n = 0; n < 16; ++n)
        current.control[n] = preview.value[n];
    current.control[CP_COLOR] = preview.legacy_color;
    for (uint32_t n = 0; n < 12; ++n)
        current.control[16 + n] = preview.wavetable[n];
    for (uint32_t n = 0; n < 13; ++n)
        current.fm[n] = preview.fm[n];
    for (uint32_t n = 28; n < CP_CONTROLS; ++n)
        current.control[n] = preview.value[n];
    current.mode = preview.mode;
    current.family = (uint8_t)((preview.metadata >> 4) & 3u);
    if (!cp_valid(&current) || prepare(context, &current)) {
        dkrt_irq_restore(sr);
        return -1;
    }
    if (src_encoder_raw(token, encoder, detents, now) != CS_EDIT) {
        dkrt_irq_restore(sr);
        return -1;
    }
    attach(context);
    dkrt_irq_restore(sr);
    return CS_EDIT;
}
/* Mapper-serialized scratch avoids putting the complete sound and an event
 * batch on the interrupt stack. This proof is never called without the mapper
 * lock and cannot be re-entered while it is in use. */
static CpPreset proof_midi_scratch;
int instrument_sound_proof_midi(uint32_t i, uint32_t sound, uint32_t sound_generation,
                                uint32_t epoch, uint32_t midi_generation, const MiEvent *input,
                                uint32_t count, MiEvent *output,
                                int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                void *context, int *refused)
{
    if (!output || !prepare || !attach || !refused || i >= IP_INSTANCES ||
        !sound_state_owned(i, sound, sound_generation))
        return -1;
    *refused = 0;
    IpInst *in = &inst[i];
    KbState *k = &in->keyboard;
    uint32_t track = in->track;
    if (k->epoch != epoch || k->generation != midi_generation)
        kb_reset(k, epoch, midi_generation);
    /* Most input cannot persistently change the chord. Skip the additional
     * validation/snapshot path for it, including all controllers and releases. */
    if (k->mode != KB_ROOT || !count || (count == 1 && input && input[0].kind != MI_NOTE_ON))
        return keyboard_events_raw(track, epoch, midi_generation, input, count, output, 1);
    uint32_t desired;
    if (kb_selector_candidate(k, input, count, &desired))
        return keyboard_events_raw(track, epoch, midi_generation, input, count, output, 1);
    if (desired == k->type)
        return keyboard_events_raw(track, epoch, midi_generation, input, count, output, 1);
    preset_snapshot(in, &proof_midi_scratch);
    proof_midi_scratch.control[CP_CHORD] = (uint16_t)desired;
    if (!cp_valid(&proof_midi_scratch) || prepare(context, &proof_midi_scratch)) {
        /* Filter only selectors: musical note-ons and every release still
         * reach the ordinary mapper, even on persistence backpressure. */
        *refused = 1;
        return keyboard_events_raw(track, epoch, midi_generation, input, count, output, 0);
    }
    int mapped = keyboard_events_raw(track, epoch, midi_generation, input, count, output, 1);
    if (k->type == desired)
        attach(context);
    return mapped;
}
/* A separately linked emulator fixture may opt the actual public SRC/MIDI
 * and persistent UI entry points into these transactions. Production has neither this
 * registration nor dispatch. No allocator, storage pool, or hook installation
 * is hidden in this adapter: the fixture must pre-reserve immutable entries. */
int instrument_sound_proof_set_transactions(IpSoundProofPrepare prepare, IpSoundProofAttach attach,
                                            void *context)
{
    if ((prepare == 0) != (attach == 0))
        return -1;
    uint32_t sr = dkrt_irq_mask();
    if (sound_transaction_busy) {
        dkrt_irq_restore(sr);
        return -1;
    }
    sound_transaction_prepare = prepare;
    sound_transaction_attach = attach;
    sound_transaction_context = prepare ? context : 0;
    dkrt_irq_restore(sr);
    return 0;
}
typedef struct {
    uint32_t instance, sound, generation;
} ProofLiveOwner;
static int proof_live_prepare(void *context, const CpPreset *preset)
{
    const ProofLiveOwner *owner = context;
    return sound_transaction_prepare(sound_transaction_context, owner->instance, owner->sound,
                                     owner->generation, preset);
}
static void proof_live_attach(void *context)
{
    const ProofLiveOwner *owner = context;
    sound_transaction_attach(sound_transaction_context, owner->instance, owner->sound,
                             owner->generation);
}
static int proof_live_ui(uint32_t operation, int value, const CpPreset *preset)
{
    uint32_t sr = dkrt_irq_mask();
    if (!sound_transaction_prepare) {
        dkrt_irq_restore(sr);
        return 1;
    }
    if (sound_transaction_busy) {
        /* dk_ui_value already staged this signed backing before dispatch. */
        if (operation == PROOF_UI_SHAPE && cur->slot == value && cur->keyboard.slot < 8)
            cur->slot = (int)cur->keyboard.slot + 1;
        dkrt_irq_restore(sr);
        return -2;
    }
    sound_transaction_busy = 1;
    uint32_t i = IDX(cur);
    ProofLiveOwner owner = {i, ip_admission[8 * i + 1], ip_admission[8 * i + 4]};
    int result = -1;
    if (operation == PROOF_UI_SHAPE) {
        /* This helper also restores a staged value on stale admission. */
        result = instrument_sound_proof_shape_slot(i, owner.sound, owner.generation, value,
                                                   proof_live_prepare, proof_live_attach, &owner);
    } else if (cur->bound == cur->track) {
        if (operation == PROOF_UI_PRESET)
            result = instrument_sound_proof_apply(i, owner.sound, owner.generation, preset,
                                                  proof_live_prepare, proof_live_attach, &owner);
        else if (operation >= PROOF_UI_MENU && operation <= PROOF_UI_MENU + IP_PROOF_WTFM_WHEEL)
            result = instrument_sound_proof_menu(i, owner.sound, owner.generation,
                                                 operation - PROOF_UI_MENU, value,
                                                 proof_live_prepare, proof_live_attach, &owner);
    }
    sound_transaction_busy = 0;
    dkrt_irq_restore(sr);
    return result;
}
/* A shared learned shape can change several accepted Sounds. Keep only the
 * prepared owner tuples; the callbacks own their separate snapshot staging. */
static struct {
    uint32_t sound[IP_INSTANCES], generation[IP_INSTANCES], mask;
} proof_live_batch;
static int proof_live_prepare_many(void *context, uint32_t i, uint32_t sound, uint32_t generation,
                                   const CpPreset *p)
{
    (void)context;
    if (i >= IP_INSTANCES || (proof_live_batch.mask & (1u << i)))
        return -1;
    if (sound_transaction_prepare(sound_transaction_context, i, sound, generation, p))
        return -1;
    proof_live_batch.sound[i] = sound;
    proof_live_batch.generation[i] = generation;
    proof_live_batch.mask |= 1u << i;
    return 0;
}
static void proof_live_attach_many(void *context, uint32_t i)
{
    (void)context;
    sound_transaction_attach(sound_transaction_context, i, proof_live_batch.sound[i],
                             proof_live_batch.generation[i]);
}
static int proof_live_learn(uint32_t session, uint32_t revision)
{
    uint32_t sr = dkrt_irq_mask();
    if (!sound_transaction_prepare) {
        dkrt_irq_restore(sr);
        return 1;
    }
    if (sound_transaction_busy) {
        dkrt_irq_restore(sr);
        return -2;
    }
    sound_transaction_busy = 1;
    proof_live_batch.mask = 0;
    uint32_t i = IDX(cur);
    int result =
        instrument_sound_proof_learn(i, ip_admission[8 * i + 1], ip_admission[8 * i + 4], session,
                                     revision, proof_live_prepare_many, proof_live_attach_many, 0);
    sound_transaction_busy = 0;
    dkrt_irq_restore(sr);
    return result;
}
static int proof_live_encoder(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now)
{
    uint32_t sr = dkrt_irq_mask();
    if (!sound_transaction_prepare) {
        int result = src_encoder_raw(token, encoder, detents, now);
        dkrt_irq_restore(sr);
        return result;
    }
    if (sound_transaction_busy || !initialized) {
        dkrt_irq_restore(sr);
        return CS_STALE;
    }
    sound_transaction_busy = 1;
    uint32_t i = IDX(cur);
    ProofLiveOwner owner = {i, ip_admission[8 * i + 1], ip_admission[8 * i + 4]};
    int result =
        instrument_sound_proof_edit(i, owner.sound, owner.generation, token, encoder, detents, now,
                                    proof_live_prepare, proof_live_attach, &owner);
    sound_transaction_busy = 0;
    dkrt_irq_restore(sr);
    return result;
}
static int proof_live_keyboard(uint32_t track, uint32_t epoch, uint32_t generation,
                               const MiEvent *input, uint32_t count, MiEvent *output)
{
    uint32_t sr = dkrt_irq_mask();
    if (sound_transaction_busy) {
        dkrt_irq_restore(sr);
        return -1;
    }
    IpInst *in = for_track(track);
    if (!sound_transaction_prepare || !in || !keyboard_initialized ||
        in->keyboard.mode != KB_ROOT) {
        int result = keyboard_events_raw(track, epoch, generation, input, count, output, 1);
        dkrt_irq_restore(sr);
        return result;
    }
    sound_transaction_busy = 1;
    uint32_t i = IDX(in);
    ProofLiveOwner owner = {i, ip_admission[8 * i + 1], ip_admission[8 * i + 4]};
    int refused = 0, result;
    if (!sound_state_owned(i, owner.sound, owner.generation) || in->bound != track) {
        /* While admission is unavailable, refuse only persistent selector
         * changes. Ordinary notes/controllers and releases still reach the
         * real mapper, so a pending Sound cannot leave held notes behind. */
        result = keyboard_events_raw(track, epoch, generation, input, count, output, 0);
    } else
        result = instrument_sound_proof_midi(i, owner.sound, owner.generation, epoch, generation,
                                             input, count, output, proof_live_prepare,
                                             proof_live_attach, &owner, &refused);
    sound_transaction_busy = 0;
    dkrt_irq_restore(sr);
    return result;
}
#endif
static void preset_load(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    CpPreset p;
#ifdef DK_CANOPY_SOUND_PROOF
    if (sound_transaction_busy)
        return;
#endif
    if (cur->keyboard.mode == KB_LEARN) {
        dk_ui_toast(ui(), "CLOSE LEARN FIRST");
        return;
    }
    if (!(preset_present & (1u << (preset_slot - 1))) ||
        cp_decode(&p, preset_records[preset_slot - 1], CP_RECORD_BYTES)) {
        dk_ui_toast(ui(), "PRESET EMPTY OR INVALID");
        return;
    }
    uint32_t sr = dkrt_irq_mask();
    IpInst *in = cur;
    /* One reset boundary prevents notes from observing a partial sound change.
     * The physical ledger retains old-key tombstones. */
    int result = 0;
#ifdef DK_CANOPY_SOUND_PROOF
    int transaction = proof_live_ui(PROOF_UI_PRESET, 0, &p);
    if (transaction != 1)
        result = transaction;
    else
#endif
        preset_apply_locked(in, &p);
    if (!result)
        preset_pending = 0;
    dkrt_irq_restore(sr);
    dk_ui_toast(ui(), result ? "LOAD REFUSED - RETRY" : "PRESET LOADED - PRESS NEW KEYS");
    if (preset_menu)
        dk_ui_menu_refresh(preset_menu);
}
static void preset_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    dk_ui_menu_add_value(m, "PRESET", &preset_slot, 1, CP_SLOTS, preset_select, 0);
    dk_ui_menu_add_text(m, preset_result    ? "NOT SAVED - RETRY SAVE"
                           : preset_dirty() ? "UNSAVED CHANGES"
                                            : "SAVED");
    dk_ui_menu_add_action(m, "SAVE CURRENT SOUND", preset_save, 0, 0);
    dk_ui_menu_add_action(m, "LOAD SAVED SOUND", preset_load, 0, 0);
    dk_ui_menu_add_text(m, "ROUTING IS NOT IN PRESETS");
}
static const dk_ui_menu_desc preset_desc = {
    .name = "canopy-presets", .title = "CANOPY PRESETS", .fill = preset_fill};
static void route_save(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    /* Version 1 keeps the selected track's channel as the default, so a
     * single-instance setup restores exactly as before. */
    CrRoute r = {(uint8_t)enabled,
                 (uint8_t)usb,
                 (uint8_t)din,
                 (uint8_t)channel_of(cur),
                 (uint8_t)(selector_octave + 1),
                 (uint8_t)selector_channel};
    if (cr_encode(&r, route_write) || cc_encode(track_channel, channels_write)) {
        dk_ui_toast(ui(), "INVALID ROUTING - NOT SAVED");
        return;
    }
    route_pending = 1;
}
static const char *const scan_clocks[] = {"FREE HZ", "TEMPO SYNC"};
static void scan_clock_apply(int value)
{
    cur->wt[CP_WT_CLOCK - 16] = value;
    cur->wt[CP_WT_RATE - 16] = value ? 3 : 10;
    pad_changed(0, 0);
}
static void scan_clock_done(void *ctx, int32_t value)
{
    (void)ctx;
    if (value < 0 || value > 1)
        return;
#ifdef DK_CANOPY_SOUND_PROOF
    int transaction = proof_live_ui(PROOF_UI_MENU + IP_PROOF_SCAN_CLOCK, value, 0);
    if (transaction == -2)
        return;
    if (transaction < 0)
        dk_ui_toast(ui(), "CHANGE REFUSED - RETRY");
    if (transaction == 1)
#endif
        scan_clock_apply(value);
    if (tone_menu)
        dk_ui_menu_refresh(tone_menu);
}
static void scan_clock_choice(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    dk_ui_choice(ui(), "SCAN CLOCK", scan_clocks, 2, scan_clock_done, 0);
}
static const char *const scan_restarts[] = {"FREE", "TRIG", "PATT"};
static void scan_restart_apply(int value)
{
    cur->wt[CP_WT_RESET - 16] = value;
    pad_changed(0, 0);
}
static void scan_restart_done(void *ctx, int32_t value)
{
    (void)ctx;
    if (value < 0 || value > 2)
        return;
#ifdef DK_CANOPY_SOUND_PROOF
    int transaction = proof_live_ui(PROOF_UI_MENU + IP_PROOF_SCAN_RESET, value, 0);
    if (transaction == -2)
        return;
    if (transaction < 0)
        dk_ui_toast(ui(), "CHANGE REFUSED - RETRY");
    if (transaction == 1)
#endif
        scan_restart_apply(value);
    if (tone_menu)
        dk_ui_menu_refresh(tone_menu);
}
static void scan_restart_choice(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    dk_ui_choice(ui(), "SCAN RESTART", scan_restarts, 3, scan_restart_done, 0);
}
/* Biased amount storage is an explicit wire detail; the menu exposes a signed
 * amount, including a neutral zero, rather than unexplained 0..200 values. */
static int32_t wheel_amount, fm_wheel_amount, wtfm_wheel_amount;
static void amount_apply(void *ctx, int32_t value)
{
    uint32_t id = (uint32_t)(uintptr_t)ctx;
    /* The FM wheel and WT wheel share a wire ID in their different banks.
     * Menu contexts identify the bank explicitly. */
    if (id == 0)
        cur->wt[CP_WT_WHEEL - 16] = value + 100;
    else if (id == 1)
        cur->fm[CP_FM_WHEEL - 15] = value + 100;
    else if (id == 2)
        cur->wtfm[CP_WTFM_WHEEL - 28] = value + 100;
    pad_changed(0, 0);
}
static void amount_changed(void *ctx, int32_t value)
{
#ifdef DK_CANOPY_SOUND_PROOF
    uint32_t id = (uint32_t)(uintptr_t)ctx;
    int transaction =
        id <= 2 ? proof_live_ui(PROOF_UI_MENU + IP_PROOF_WT_WHEEL + id, value, 0) : -1;
    if (transaction != 1) {
        wheel_amount = cur->wt[CP_WT_WHEEL - 16] - 100;
        fm_wheel_amount = cur->fm[CP_FM_WHEEL - 15] - 100;
        wtfm_wheel_amount = cur->wtfm[CP_WTFM_WHEEL - 28] - 100;
        if (transaction == -2)
            return;
        if (transaction < 0)
            dk_ui_toast(ui(), "CHANGE REFUSED - RETRY");
        if (tone_menu)
            dk_ui_menu_refresh(tone_menu);
        return;
    }
#endif
    amount_apply(ctx, value);
}
#ifdef DK_CANOPY_SOUND_PROOF
/* The menu's bank is explicit: WT and FM reuse a wire ID, but changing an
 * inactive bank must not edit the active family. Only a single prospective
 * preset is retained; mapper exclusion and busy guard protect the scratch. */
static struct {
    CpPreset preset;
    uint8_t busy;
} proof_menu_scratch;
int instrument_sound_proof_menu(uint32_t i, uint32_t sound, uint32_t generation, uint32_t setting,
                                int value, int (*prepare)(void *, const CpPreset *),
                                void (*attach)(void *), void *context)
{
    if (!prepare || !attach || i >= IP_INSTANCES || setting > IP_PROOF_WTFM_WHEEL)
        return -1;
    if (setting == IP_PROOF_SCAN_CLOCK   ? value < 0 || value > 1
        : setting == IP_PROOF_SCAN_RESET ? value < 0 || value > 2
                                         : value < -100 || value > 100)
        return -1;
    uint32_t sr = dkrt_irq_mask();
    if (proof_menu_scratch.busy) {
        dkrt_irq_restore(sr);
        return -1;
    }
    proof_menu_scratch.busy = 1;
    if (!sound_state_owned(i, sound, generation) || cur != &inst[i] || cur->bound != cur->track)
        goto refuse;
    CpPreset *p = &proof_menu_scratch.preset;
    preset_snapshot(cur, p);
    switch (setting) {
    case IP_PROOF_SCAN_CLOCK:
        p->control[CP_WT_CLOCK] = (uint16_t)value;
        p->control[CP_WT_RATE] = value ? 3 : 10;
        break;
    case IP_PROOF_SCAN_RESET:
        p->control[CP_WT_RESET] = (uint16_t)value;
        break;
    case IP_PROOF_WT_WHEEL:
        p->control[CP_WT_WHEEL] = (uint16_t)(value + 100);
        break;
    case IP_PROOF_FM_WHEEL:
        p->fm[CP_FM_WHEEL - 15] = (uint16_t)(value + 100);
        break;
    case IP_PROOF_WTFM_WHEEL:
        p->control[CP_WTFM_WHEEL] = (uint16_t)(value + 100);
        break;
    }
    if (!cp_valid(p) || prepare(context, p))
        goto refuse;
    /* The ordinary menu and this proof call the same setters. The clock/rate
     * pair and its snapshot attach are covered by one uninterrupted lock.
     * UI refresh/toasts remain the caller's work after this function returns. */
    if (setting == IP_PROOF_SCAN_CLOCK)
        scan_clock_apply(value);
    else if (setting == IP_PROOF_SCAN_RESET)
        scan_restart_apply(value);
    else
        amount_apply((void *)(uintptr_t)(setting - IP_PROOF_WT_WHEEL), value);
    attach(context);
    proof_menu_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return 0;
refuse:
    proof_menu_scratch.busy = 0;
    dkrt_irq_restore(sr);
    return -1;
}
#endif
static void tone_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    wheel_amount = cur->wt[CP_WT_WHEEL - 16] - 100;
    fm_wheel_amount = cur->fm[CP_FM_WHEEL - 15] - 100;
    wtfm_wheel_amount = cur->wtfm[CP_WTFM_WHEEL - 28] - 100;
    dk_ui_menu_add_action(m, scan_clocks[cur->wt[CP_WT_CLOCK - 16]], scan_clock_choice, 0, 0);
    dk_ui_menu_add_action(m, scan_restarts[cur->wt[CP_WT_RESET - 16]], scan_restart_choice, 0, 0);
    dk_ui_menu_add_value(m, "WHEEL TO POS", &wheel_amount, -100, 100, amount_changed, 0);
    dk_ui_menu_add_value(m, "WHEEL TO FM", &fm_wheel_amount, -100, 100, amount_changed,
                         (void *)(uintptr_t)1);
    dk_ui_menu_add_value(m, "WHEEL TO WT FM", &wtfm_wheel_amount, -100, 100, amount_changed,
                         (void *)(uintptr_t)2);
}
static const dk_ui_menu_desc tone_desc = {
    .name = "canopy-tone", .title = "CANOPY MODULATION", .fill = tone_fill};
static void input_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    ui_channel = (int)channel_of(cur);
    dk_ui_menu_add_toggle(m, "EXTERNAL MIDI", &enabled, changed, 0);
    dk_ui_menu_add_toggle(m, "USB", &usb, changed, 0);
    dk_ui_menu_add_toggle(m, "DIN", &din, changed, 0);
    dk_ui_menu_add_value(m, "CHANNEL", &ui_channel, 1, 16, channel_changed, 0);
    dk_ui_menu_add_action(m, "SAVE ROUTING", route_save, 0, 0);
    dk_ui_menu_add_value(m, "SELECTOR OCTAVE", &selector_octave, -1, 8, changed, 0);
    dk_ui_menu_add_value(m, "SELECT CH (0=SAME)", &selector_channel, 0, 16, changed, 0);
    dk_ui_menu_add_action(m, "PANIC", panic, 0, 0);
    dk_ui_menu_add_action(m, "RECOVER KEYBOARD", recover, 0, 0);
    dk_ui_menu_add_text(m, quarantined(cur)                   ? "RELEASE KEYS / RECOVER"
                           : mi_transport.faults              ? "RECOVER OR RESTART"
                           : cur->keyboard.status == KB_RANGE ? "CHORD OUT OF RANGE"
                           : cur->keyboard.status == KB_EMPTY ? "CUSTOM SHAPE EMPTY"
                           : cur->conflict                    ? "CHANNEL IN USE"
                           : cur->phase == 3                  ? "READY"
                                                              : "SELECT CANOPY ON A TRACK");
    if (route_result)
        dk_ui_menu_add_text(m, "ROUTING NOT SAVED");
    dk_ui_menu_add_text(m, "PANEL: ITS TRACK / KEYS");
}
static const dk_ui_menu_desc input_desc = {
    .name = "canopy-input", .title = "CANOPY INPUT", .fill = input_fill};
static void shape_changed(void *ctx, int32_t value)
{
#ifdef DK_CANOPY_SOUND_PROOF
    int transaction = proof_live_ui(PROOF_UI_SHAPE, value, 0);
    if (transaction != 1) {
        if (transaction == -2)
            return;
        if (transaction < 0)
            dk_ui_toast(ui(), "CHANGE REFUSED - RETRY");
        if (shape_menu)
            dk_ui_menu_refresh(shape_menu);
        return;
    }
#endif
    changed(ctx, value);
}
static void shape_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    dk_ui_menu_add_value(m, "SHAPE SLOT", &cur->slot, 1, 8, shape_changed, 0);
    dk_ui_menu_add_action(m, "LEARN TO ROOT", learn_begin, 0, 0);
    dk_ui_menu_add_text(m, storage_result        ? "SHAPES NOT SAVED"
                           : cur->keyboard.dirty ? "SAVE PENDING"
                                                 : "SHAPES SAVED");
    dk_ui_menu_add_action(m, "RETRY SHAPE SAVE", retry_save, 0, 0);
    dk_ui_menu_add_text(m, "PLAY SHAPE: CHRD USER");
}
static const dk_ui_menu_desc shape_desc = {
    .name = "canopy-shapes", .title = "CANOPY CHORD LEARN", .fill = shape_fill};
static void fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    if (!input_menu)
        input_menu = dk_ui_menu_new(ui(), &input_desc);
    if (input_menu)
        dk_ui_menu_add_submenu(m, "INPUT", input_menu);
    if (!shape_menu)
        shape_menu = dk_ui_menu_new(ui(), &shape_desc);
    if (shape_menu)
        dk_ui_menu_add_submenu(m, "CHORD LEARN", shape_menu);
    if (!tone_menu)
        tone_menu = dk_ui_menu_new(ui(), &tone_desc);
    if (tone_menu)
        dk_ui_menu_add_submenu(m, "MODULATION", tone_menu);
    if (!preset_menu)
        preset_menu = dk_ui_menu_new(ui(), &preset_desc);
    if (preset_menu)
        dk_ui_menu_add_submenu(m, "PRESETS", preset_menu);
}
static const dk_ui_menu_desc desc = {
    .name = "instrument-keyboard", .title = "CANOPY SETUP", .fill = fill};
/* CANOPY is the only admitted instrument policy today. The menu asks with a
 * loaded module's validated id; a null parent is a capability query. */
int dk_app_instrument_module_settings(const uint8_t *name, dk_ui_menu *parent)
{
    static const char id[] = "digikit/canopy";
    if (!name || !dkrt_instrument_type())
        return 0;
    for (uint32_t i = 0; i < sizeof(id); ++i)
        if (name[i] != (uint8_t)id[i])
            return 0;
    if (!parent)
        return 1;
    if (!status_menu)
        status_menu = dk_ui_menu_new(ui(), &status_desc);
    if (status_menu)
        dk_ui_menu_add_submenu(parent, "STATUS", status_menu);
    if (!menu)
        menu = dk_ui_menu_new(ui(), &desc);
    if (menu)
        dk_ui_menu_add_submenu(parent, "SETUP", menu);
    dk_ui_menu_add_text(parent, "LEVEL/DATA: TRACK LEVEL");
    dk_ui_menu_add_text(parent, "SOUND H: SAMPLE LEVEL");
    dk_ui_menu_add_text(parent, "SRC HOLD: SHOW VALUES");
    return 1;
}
void dk_app_instrument_settings_rows(dk_ui_menu *parent)
{
    if (!menu)
        menu = dk_ui_menu_new(ui(), &desc);
    if (menu)
        dk_ui_menu_add_submenu(parent, "CANOPY", menu);
    if (!status_menu)
        status_menu = dk_ui_menu_new(ui(), &status_desc);
    if (status_menu)
        dk_ui_menu_add_submenu(parent, "CANOPY STATUS", status_menu);
}
/* Enable this instance's panel and external routes. A channel already played
 * by another instance (the lower track keeps it) is skipped and reported. */
static int route_one(IpInst *in, uint32_t port, uint32_t ch)
{
    uint32_t owner_track = mi_route_track(port, ch);
    if (owner_track == in->bound)
        return 0;
    if (owner_track < MI_TRACKS) {
        in->conflict = 1;
        return 0;
    }
    return mi_route_enable(port, ch, in->bound, 0);
}
static int routes_enable(IpInst *in)
{
    uint32_t ch = channel_of(in) - 1;
    in->conflict = 0;
    if (mi_route_enable(MI_PANEL, 0, in->bound, 0))
        return 0;
    if (!enabled)
        return 1;
    if (usb && route_one(in, 0, ch))
        return 0;
    if (din && route_one(in, 1, ch))
        return 0;
    if (in->keyboard.mode == KB_ROOT && selector_channel) {
        uint32_t sel = (uint32_t)selector_channel - 1;
        if (usb && route_one(in, 0, sel))
            return 0;
        if (din && route_one(in, 1, sel))
            return 0;
    }
    return 1;
}
static void tick_instance(uint32_t i, const IpInst *ui)
{
    IpInst *in = &inst[i];
    cur = in;
    in->track = ip_tracks[i];
    /* The DSP bank and its presentation values come from one locked snapshot.
     * The same revision owns SRC pending markers and the audio configuration. */
    CpPreset current;
    src_requested(in, &current);
    if (in == ui)
        instrument_mod_publish(current.family);
    const CsState *s = SRC(in);
    const uint16_t *v = s->value;
    uint32_t values[32] = {11 | ((uint32_t)current.mode << 8) | (s->native_tune << 16),
                           current.family,
                           v[CP_COLOR],
                           v[CP_DRIFT],
                           v[CP_ATTACK],
                           v[CP_RELEASE],
                           v[CP_ARP_MIX],
                           current.bpm,
                           v[CP_RATE],
                           v[CP_GATE],
                           v[CP_ORDER],
                           v[CP_OCTAVES],
                           v[CP_SWING],
                           current.clock,
                           v[CP_RESTART],
                           (uint32_t)v[CP_STRUM] * 3 / 2,
                           v[0],
                           v[1],
                           v[2],
                           v[3],
                           v[4],
                           v[5],
                           v[6],
                           v[7],
                           v[8],
                           v[9],
                           v[10],
                           v[11],
                           v[12],
                           v[13],
                           v[14],
                           v[15]};
    for (uint32_t k = 0; k < 16; ++k)
        values[16 + k] |= (uint32_t)v[16 + k] << 16;
    /* Adapter-owned flag: this instance's status reaches the link. */
    if (i == focus)
        values[0] |= 0x8000u;
    if (in->track >= MI_TRACKS) {
        /* Freed: release the old track; keep the sound for the next track. */
        if (in->phase) {
            instrument_sound_suspend(i);
            in->phase = 0;
        }
        return;
    }
    if (!healthy || quarantined(in)) {
        if (in->phase == 2 || in->phase == 3)
            instrument_sound_suspend(i);
        return;
    }
    if (!in->phase)
        in->phase = 1;
    /* Another track's reset can cancel an in-flight configuration. If that
     * happens while this instance is in phase 2, its commit will never arrive;
     * restart the handshake rather than waiting forever for an applied bank. */
    uint32_t reset_sr = dkrt_irq_mask();
    if ((in->phase == 2 || in->phase == 3) &&
        (mi_ledger.reset_mask || in->bound != in->track ||
         (in->phase == 3 && !mi_transport.applied[in->bound]) ||
         (in->phase == 2 && !mi_transport.applied[in->bound] &&
          (!mi_transport.config_active || mi_transport.config_track != in->bound))))
        instrument_sound_suspend(i);
    dkrt_irq_restore(reset_sr);
    {
        int result = instrument_sound_prepare(i, in->phase == 1 && !mi_ledger.reset_mask);
        if (result < 0)
            instrument_sound_suspend(i);
        else
            in->prepared = (uint32_t)result;
    }
    sa_follow(i);
    /* Bind DSP slot i to the instance's track before its first configuration
     * there. The reset ACK gates the submission below; the old track was
     * already released by the suspension that preceded this change. */
    if (in->phase == 1 && in->prepared && in->bound != in->track) {
        in->bound = in->track;
        mi_instance_bind(in->bound, i);
    }
    uint32_t track = in->bound;
    if (in->phase == 1 && in->prepared && !mi_ledger.reset_mask &&
        !mi_config_submit(track, s->revision, 0x43505342, values, 32)) {
        in->phase = 2;
        in->pad_dirty = 0;
    }
    if (in->phase == 3 && s->revision > mi_transport.applied[track] &&
        !mi_config_submit(track, s->revision, 0x43505342, values, 32))
        in->pad_dirty = 0;
    if (in->phase == 2 &&
        instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track])) {
        if (routes_enable(in))
            in->phase = 3;
        else
            instrument_sound_suspend(i);
    }
}
void instrument_play_tick(void)
{
    ip_init();
#if defined(__m68k__)
    extern void instrument_src_tick(void), ip_panel_tick(void);
    instrument_src_tick();
    ip_panel_tick(); /* recorded chords: steps committed after the key event */
    if (status_menu && dk_ui_menu_is_open(status_menu)) {
        uint32_t now = instrument_src_now();
        /* Drain at the UI tick cadence, independently of the slower repaint.
         * Waiting 100 ms to drain can overflow the bounded telemetry ring. */
        instrument_src_poll(now);
        if (now - status_draw_at >= 100)
            dk_ui_menu_refresh(status_menu);
    }
#endif
    uint32_t reply[16];
    if (!keyboard_initialized) {
        uint8_t record[KB_RECORD_BYTES];
        int length = dkrt_settings_get(shape_key, record, sizeof(record));
        if (length >= 0)
            storage_result = kb_decode(&inst[0].keyboard, record, (uint32_t)length);
        else if (length != -68)
            storage_result = length;
        shapes_share(&inst[0]);
        preset_present = 0;
        for (uint32_t i = 0; i < CP_SLOTS; ++i) {
            CpPreset p;
            int n = dkrt_settings_get(preset_keys[i], preset_records[i], CP_RECORD_BYTES);
            if (n >= 0 && !cp_decode(&p, preset_records[i], (uint32_t)n) &&
                !cp_encode(&p, preset_records[i]))
                preset_present |= (uint8_t)(1u << i);
        }
        uint8_t route[CR_BYTES];
        int n = dkrt_settings_get(route_key, route, sizeof(route));
        route_restore = n >= 0 && !cr_decode(&restore_route, route, (uint32_t)n);
        route_result = (n == -68 || route_restore) ? 0 : n < 0 ? n : -1;
        uint8_t channels[CC_BYTES];
        for (uint32_t t = 0; t < MI_TRACKS; ++t)
            restore_channels[t] = 0;
        n = dkrt_settings_get(channels_key, channels, sizeof(channels));
        if (n >= 0 && cc_decode(restore_channels, channels, (uint32_t)n))
            for (uint32_t t = 0; t < MI_TRACKS; ++t)
                restore_channels[t] = 0;
        uint8_t copy[CW_BYTES];
        n = dkrt_settings_get(work_key, copy, sizeof(copy));
        if (n < 0 || cw_decode(&work, copy, (uint32_t)n))
            cw_clear(&work);
        keyboard_initialized = 1;
    }
    /* Reading a valid record cannot open MIDI routes. Restore requested setup
     * only after this boot has an admitted instrument generation; the ordinary
     * healthy/prepared/configured handshake still owns actual route activation. */
    if (route_restore && dkrt_instrument_type() && dkrt_instrument_generation()) {
        enabled = restore_route.enabled;
        usb = restore_route.usb;
        din = restore_route.din;
        channel = restore_route.channel;
        selector_octave = (int)restore_route.selector_octave - 1;
        selector_channel = restore_route.selector_channel;
        for (uint32_t t = 0; t < MI_TRACKS; ++t)
            track_channel[t] = restore_channels[t];
        changed(0, 0);
    }
    if (route_pending) {
        route_result = dkrt_settings_put(route_key, route_write, sizeof(route_write));
        if (!route_result)
            route_result = dkrt_settings_put(channels_key, channels_write, sizeof(channels_write));
        if (!route_result)
            route_result = dkrt_settings_flush();
        route_pending = 0;
        dk_ui_toast(ui(), route_result ? "ROUTING IN RAM - SAVE FAILED" : "ROUTING SAVED");
        if (input_menu && dk_ui_menu_is_open(input_menu))
            dk_ui_menu_refresh(input_menu);
    }
    if (preset_pending) {
        uint32_t index = (uint32_t)preset_pending - 1;
        /* settings_put checks shared capacity before changing its RAM record.
         * A failed flush never promotes our last confirmed durable copy. */
        preset_result = dkrt_settings_put(preset_keys[index], preset_write, sizeof(preset_write));
        if (!preset_result)
            preset_result = dkrt_settings_flush();
        if (!preset_result) {
            for (uint32_t i = 0; i < CP_RECORD_BYTES; ++i)
                preset_records[index][i] = preset_write[i];
            preset_present |= (uint8_t)(1u << index);
        }
        preset_pending = 0;
        dk_ui_toast(ui(), preset_result ? "SOUND IN RAM - SAVE FAILED" : "PRESET SAVED");
        if (preset_menu && dk_ui_menu_is_open(preset_menu))
            dk_ui_menu_refresh(preset_menu);
    }
    if (save_pending) {
        uint8_t record[KB_RECORD_BYTES];
        uint32_t sr = dkrt_irq_mask();
        kb_encode(&cur->keyboard, record);
        dkrt_irq_restore(sr);
        storage_result = dkrt_settings_put(shape_key, record, sizeof(record));
        if (!storage_result)
            storage_result = dkrt_settings_flush();
        if (!storage_result) {
            for (uint32_t i = 0; i < IP_INSTANCES; ++i)
                inst[i].keyboard.dirty = 0;
        }
        save_pending = 0;
        dk_ui_toast(ui(), storage_result ? "SHAPE IN RAM - SAVE FAILED" : "SHAPES SAVED");
    }
    if (learn_menu && dk_ui_menu_is_open(learn_menu) && cur->keyboard.mode != KB_LEARN)
        dk_ui_menu_close(learn_menu);
    if (learn_menu && dk_ui_menu_is_open(learn_menu) &&
        (refreshed_revision != cur->keyboard.revision ||
         refreshed_status != cur->keyboard.status)) {
        refreshed_revision = cur->keyboard.revision;
        refreshed_status = cur->keyboard.status;
        dk_ui_menu_refresh(learn_menu);
    }
    uint32_t itype = dkrt_instrument_type();
    /* V2 publication completes after the stock view's boot constructor.
     * Rebuild once on this owning UI task, after the admitted rows are visible.
     * The retained row remains listed after a fault; no unload or hot reload. */
    if (itype && ip_view && listed_type != itype) {
        ip_view_build(ip_view);
        listed_type = itype;
    }
    int count = mi_registry_reply(reply, 16);
    /* A temporarily missing/reordered reply cannot revoke a healthy route;
     * the transport's independent deadline and reset guard remain authoritative. */
    if (count == 16 && reply[0] == MT_PLAY_REPLY_MAGIC && reply[13] == 5 &&
        reply[14] == dkrt_instrument_generation())
        fault_generation = reply[14];
    if (count == -2 || !dkrt_instrument_type())
        healthy = 0;
    else if (count == 16)
        healthy = reply[0] == MT_PLAY_REPLY_MAGIC && reply[13] == 3 &&
                  reply[14] == dkrt_instrument_generation();
    recover_tick();
    work_tick();
    instrument_sound_scan();
    ip_sa_tick();
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        inst[i].track = ip_tracks[i];
    IpInst *ui = ui_target();
    /* Status focus follows the UI target; a revision bump resends its bank
     * with the focus flag, which moves the DSP's status slot. */
    if (ui->track < MI_TRACKS && focus != IDX(ui)) {
        focus = IDX(ui);
        cs_touch(SRC(ui));
    }
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        tick_instance(i, ui);
    cur = ui;
#ifdef DK_CANOPY_SOUND_PROOF
    if (sound_proof_tick)
        sound_proof_tick();
#endif
    /* The DSP publishes continuously, including while another page or the
     * preset menu covers SRC. Drain every UI tick; otherwise its bounded
     * queue can overflow during a menu visit and the display may never catch
     * up. Keep the track/generation check even when no SRC view is visible. */
    if (ui->track < MI_TRACKS && focus < IP_INSTANCES) {
        is_select(STS(ui), ui->track, mi_ledger.generations[ui->track], ui->keyboard.mode,
                  ui->keyboard.mode == KB_LEARN ? ui->keyboard.session : 0);
        instrument_src_poll(instrument_src_now());
    }
    ip_keyboard_status[0] = cur->keyboard.mode;
    ip_keyboard_status[1] = cur->keyboard.type;
    ip_keyboard_status[2] = 0;
    ip_keyboard_status[3] = cur->keyboard.capture_count;
    ip_keyboard_status[4] = cur->keyboard.session;
    ip_keyboard_status[5] = cur->keyboard.revision;
    ip_keyboard_status[6] = cur->keyboard.status;
    ip_keyboard_status[7] = cur->keyboard.dirty;
    ip_keyboard_status[8] = (uint32_t)storage_result;
    for (uint32_t i = 0; i < 3; ++i)
        ip_keyboard_status[9 + i] = cur->keyboard.capture[i];
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        const IpInst *in = &inst[i];
        volatile uint32_t *c = &ip_control[i * 8];
        uint32_t t = in->bound < MI_TRACKS ? in->bound : 0;
        c[0] = in->phase;
        c[1] = (uint32_t)enabled;
        c[2] = healthy;
        c[3] = in->prepared;
        c[4] = mi_transport.faults;
        c[5] = mi_ledger.reset_mask;
        c[6] = mi_transport.applied[t];
        c[7] = itype;
    }
    uint32_t view = cur->keyboard.type | ((uint32_t)cur->keyboard.mode << 4) |
                    ((uint32_t)cur->keyboard.status << 6) | (cur->phase << 9) |
                    ((uint32_t)!!storage_result << 12) | ((uint32_t)cur->keyboard.dirty << 13) |
                    ((uint32_t)(quarantined(cur) || mi_transport.faults) << 14) |
                    ((uint32_t)cur->conflict << 15) | (IDX(cur) << 16);
    if ((!learn_menu || !dk_ui_menu_is_open(learn_menu)) && shown_main != view) {
        shown_main = view;
        if (input_menu && dk_ui_menu_is_open(input_menu))
            dk_ui_menu_refresh(input_menu);
        if (shape_menu && dk_ui_menu_is_open(shape_menu))
            dk_ui_menu_refresh(shape_menu);
    }
}
