#include "seam.h"
/* UI-task policy and setup for up to IP_INSTANCES admitted keyboard instruments,
 * one per kit track (finding 345). Instance i is DSP slot i. The UI (SRC page,
 * menus, presets, panel keys) acts on `cur`, the instance on the selected track;
 * the tick services every instance; the track keyboard's notes reach each by track. */
#if defined(__m68k__) && defined(__GNUC__)
/* Keep lookup data out of executable sections checked by the CF ISA gate. */
#pragma GCC optimize("no-jump-tables", "no-tree-loop-distribute-patterns")
#endif
#include <stdint.h>
#include <stddef.h>
#include <dk/platform/ui.h>
#include "keyboard.h"
#include "preset.h"
#include "src-control.h"
#include "../../../keyboard-panel/src/cf/src-clock.h"
#include "src-mod.h"
#include "instances.h"
#include "shared-notes.h"
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
/* instrument_runtime_policy is in the image (image.c): the runtime reads it at boot, before services load. */
/* The tracks the track keyboard shows CANOPY's SRC pages for (instrument_src_track_keyboard; a test reads it). */
static uint32_t healthy, fault_generation;
int instrument_play_faulted(uint32_t generation)
{
    return generation && fault_generation == generation;
}
/* Per instance: phase, external MIDI, healthy, prepared, faults, reset mask,
 * applied revision, type ([0..7] is instance 0). */
volatile uint32_t ip_control[IP_INSTANCES * 8];
static dk_ui_menu *menu;
/* The machine selection view, as core/ui's hook of its builder remembers it (the first word of `ui_machine_view`, 0 until the view exists). */
#ifdef DK_SERVICE
#define ip_view (*(volatile uint32_t *)IP_DATA(ui_machine_view))
#else
extern volatile uint32_t ui_machine_view;
#define ip_view ui_machine_view
#endif
extern void ip_view_build(uint32_t view);
static uint32_t listed_type;

typedef struct {
    uint32_t track; /* admitted track (ip_tracks), IP_NONE when free */
    uint32_t bound; /* track the transport was last bound/reset for */
    uint32_t phase, prepared;
    /* Stage 3 setup. Updates use the existing atomic configuration channel,
     * retain note ownership, and coalesce knob motion while an ACK is pending. */
    int pad_family, pad_color, pad_drift, pad_attack, pad_release;
    int arp_mix, arp_bpm, arp_rate, arp_gate;
    int arp_order, arp_octaves, arp_swing, arp_clock, arp_restart, pad_strum;
    int wt[12], fm[13], wtfm[4];
    uint32_t pad_dirty;
    /* The Sound's performance settings, which the track keyboard plays (instrument_track_keyboard_legacy). */
    int32_t mode, type, voicing;
    KbShape shape;
} IpInst;
/* Exported for the proofs: element 0 is instance 0. */
CsState src_state[IP_INSTANCES];
IsStatus src_status[IP_INSTANCES];
static IpInst inst[IP_INSTANCES];
static uint8_t track_keyboard_claim[MI_TRACKS];
static uint8_t track_keyboard_lanes[MI_TRACKS];
static IsnQueue shared_notes[IP_INSTANCES];
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
static IpSoundProofPrepare sound_transaction_prepare;
static IpSoundProofAttach sound_transaction_attach;
static void *sound_transaction_context;
static uint8_t sound_transaction_busy;
enum { PROOF_UI_PRESET, PROOF_UI_MENU };
/* 1 preserves the unregistered callback, 0 committed, negative refused.
 * -2 denotes forbidden re-entry: no nested UI refresh/toast is allowed. */
static int proof_live_ui(uint32_t operation, int value, const CpPreset *preset);
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
    in->shape.count = 0;
    for (uint32_t i = 0; i < 4; ++i)
        in->shape.intervals[i] = 0;
    in->track = in->bound = IP_NONE;
    in->phase = in->prepared = 0;
}
static void ip_init(void)
{
    if (initialized)
        return;
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        instance_defaults(&inst[i]);
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
    if (in) {
        cur = in;
        /* The DSP reports this instance only after the next tick has moved the focus to it. */
        if (IDX(in) != focus)
            is_await(STS(in));
    }
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

int instrument_midi_ready(uint32_t track, uint32_t epoch, uint32_t generation)
{
    IpInst *in = owner(track);
    return in && healthy && in->prepared && (in->phase == 2 || in->phase == 3) &&
           epoch == mi_ledger.epoch && generation == mi_ledger.generations[track] &&
           mi_transport.applied[track] != 0 && !mi_ledger.reset_mask && !mi_transport.faults;
}
/* CANOPY's SRC pages are always those of the track keyboard (VOICE, FM ENV, DETAIL): there is no layout to switch. */
void instrument_src_track_keyboard(uint32_t track, int active)
{
    (void)track;
    (void)active;
}
static dk_ui_menu *tone_menu;
static void pad_changed(void *ctx, int32_t value)
{
    (void)ctx;
    (void)value;
    cur->pad_dirty = 1;
}
/* The presets and the working copy have been read: until then the track keyboard does not take a track, for it would play
 * the instance's defaults instead of its Sound. */
static int records_loaded;
static uint32_t shown_main = UINT32_MAX;
static dk_ui_menu *status_menu;
static uint32_t status_draw_at;
extern int dkrt_settings_get(const char *, uint8_t *, uint32_t);
extern int dkrt_settings_put(const char *, const uint8_t *, uint32_t);
extern int dkrt_settings_flush(void);
static const char *const preset_keys[CP_SLOTS] = {"canopy/preset/1", "canopy/preset/2",
                                                  "canopy/preset/3", "canopy/preset/4"};
static uint8_t preset_records[CP_SLOTS][CP_RECORD_BYTES], preset_present;
static uint8_t preset_write[CP_RECORD_BYTES];
static int32_t preset_slot = 1, preset_result, preset_pending;
static dk_ui_menu *preset_menu;
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

void instrument_sound_suspend(uint32_t instance)
{
    if (instance >= IP_INSTANCES)
        return;
    IpInst *in = &inst[instance];
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

/* The owner has already stopped its old dispatch before handing over. Enter
 * through the regular reset/configuration handshake so a local voice or a
 * queued message cannot cross into the new mapper's token namespace. */
int instrument_track_keyboard_claim(uint32_t track, int active)
{
    if (track >= MI_TRACKS)
        return 0;
    uint32_t sr = dkrt_irq_mask();
    IpInst *in = for_track(track);
    if (active && (!in || !records_loaded)) {
        dkrt_irq_restore(sr);
        return 0;
    }
    if (track_keyboard_claim[track] != !!active) {
        track_keyboard_claim[track] = (uint8_t)!!active;
        if (in)
            instrument_sound_suspend(IDX(in));
    }
    int ready =
        !active || (in && in->phase == 3 &&
                    instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track]));
    dkrt_irq_restore(sr);
    return ready;
}

int instrument_track_keyboard_routed(uint32_t track)
{
    return track < MI_TRACKS && track_keyboard_claim[track] && track_keyboard_lanes[track];
}

int instrument_track_keyboard_lanes(uint32_t track, uint32_t lanes)
{
    if (track >= MI_TRACKS || lanes > 3)
        return 0;
    uint32_t sr = dkrt_irq_mask();
    uint8_t previous = track_keyboard_lanes[track];
    track_keyboard_lanes[track] = (uint8_t)lanes;
    int ready = instrument_track_keyboard_claim(track, lanes != 0);
    if (previous && lanes && previous != lanes) {
        /* An active claim stays in the same generation. Republish its new
         * lane mask with the next control revision so the DSP updates MIX. */
        IpInst *in = for_track(track);
        if (in)
            cs_touch(SRC(in));
    }
    dkrt_irq_restore(sr);
    return ready;
}

int instrument_track_keyboard_locked_note(uint32_t track, uint32_t token, uint32_t pitch,
                                          uint32_t velocity, uint32_t on, uint32_t lane,
                                          const IsnSnapshot *snapshot)
{
    if (track >= MI_TRACKS || lane > 1 || on > 1 ||
        (snapshot && (snapshot->count > MI_LOCKS || snapshot->lanes > 3)))
        return 0;
    uint32_t sr = dkrt_irq_mask();
    IpInst *in = for_track(track);
    int result = 0;
    /* A reset on another track or a pending config may pause delivery. An
     * accepted note keeps its release reservation across that pause. A reset
     * of this track destroys the old DSP ownership and its queued releases. */
    if (track_keyboard_claim[track] && in &&
        (!on || (in->phase == 3 &&
                 instrument_midi_ready(track, mi_ledger.epoch, mi_ledger.generations[track])))) {
        IsnQueue *q = &shared_notes[IDX(in)];
        isn_sync(q, track, mi_ledger.epoch, mi_ledger.generations[track]);
        result = track_keyboard_lanes[track]
                     ? isn_submit_locked(q, token, pitch, velocity, on != 0, (int)lane, snapshot)
                     : isn_submit(q, token, pitch, velocity, on != 0);
    }
    dkrt_irq_restore(sr);
    return result;
}

/* The mod wheel (CC1), Reset All Controllers (CC121) and pitch bend of a track the track keyboard plays: CANOPY has no MIDI route of
 * its own there, so the track keyboard hands them over with the track's notes and they reach the DSP on the notes' route. */
int instrument_track_keyboard_controller(uint32_t track, uint32_t kind, uint32_t note,
                                         uint32_t value)
{
    if (track >= MI_TRACKS)
        return 0;
    uint32_t sr = dkrt_irq_mask();
    int sent = track_keyboard_claim[track] && track_keyboard_lanes[track] &&
               mi_track_controller(track, kind, note, value);
    dkrt_irq_restore(sr);
    return sent;
}

int instrument_track_keyboard_note(uint32_t track, uint32_t token, uint32_t pitch,
                                   uint32_t velocity, uint32_t on, uint32_t lane)
{
    return instrument_track_keyboard_locked_note(track, token, pitch, velocity, on, lane, 0);
}

int instrument_keyboard_events(uint32_t track, uint32_t epoch, uint32_t generation,
                               const MiEvent *input, uint32_t count, MiEvent *output)
{
    if (track < MI_TRACKS && track_keyboard_claim[track]) {
        /* The shared owner delivers resolved pitches. Consume any local
         * ledger traffic left after handoff without running the old chord
         * mapper; the controllers the track keyboard forwards pass. */
        if (count) {
            if (track_keyboard_lanes[track] && count == 1 &&
                (input[0].kind == MI_BEND ||
                 (input[0].kind == MI_CC && (input[0].note == 1 || input[0].note == 121)))) {
                mi_event_copy(output, input);
                return 1;
            }
            return 0;
        }
        IpInst *in = for_track(track);
        if (!in)
            return 0;
        IsnQueue *q = &shared_notes[IDX(in)];
        isn_sync(q, track, epoch, generation);
        return (int)isn_take(q, output);
    }
    /* The track keyboard is CANOPY's only note engine: until it takes the track, nothing plays. */
    return 0;
}
/* Capture the requested source's remembered bank with the trig, before any
 * later UI edit or packet retry. Caller already holds the MIDI IRQ lock. */
int instrument_lock_source(uint32_t track, uint32_t wave, uint16_t *out)
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
/* Release every instance: each sound suspended at one reset boundary. */
static void release_all(void)
{
    uint32_t sr = dkrt_irq_mask();
    for (uint32_t i = 0; i < IP_INSTANCES; ++i)
        if (inst[i].track < MI_TRACKS || inst[i].phase)
            instrument_sound_suspend(i);
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
    /* the stock confirm shows one line of about 30 characters */
    dk_ui_confirm(ui(), "RECOVER KEYBOARD", "All keys and pedals up?", recover_done, 0);
}
static int quarantined(const IpInst *in);
static const char *readiness(const IpInst *in)
{
    return quarantined(in)       ? "RELEASE KEYS / RECOVER"
           : mi_transport.faults ? "RECOVER OR RESTART"
           : in->phase == 3      ? "READY"
                                 : "SELECT CANOPY ON TRACK";
}
static int quarantined(const IpInst *in)
{
    for (uint32_t i = 0; i < MI_ROUTES; ++i)
        if (mi_ledger.routes[i].track == in->bound && mi_ledger.routes[i].quarantined)
            return 1;
    return 0;
}
/* Snapshot on the UI task under the mapper lock. Live performance state is
 * deliberately absent; the Sound carries its user shape by value. */
static void preset_snapshot(const IpInst *in, CpPreset *p)
{
    uint32_t sr = dkrt_irq_mask();
    uint16_t v[16] = {(uint16_t)in->type,
                      (uint16_t)in->voicing,
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
    p->mode = (uint8_t)in->mode;
    p->clock = CP_ARP_CLOCK;
    p->shape.count = in->shape.count;
    for (uint32_t i = 0; i < 4; ++i)
        p->shape.intervals[i] = in->shape.intervals[i];
    dkrt_irq_restore(sr);
}
int instrument_track_keyboard_legacy(uint32_t track, CpPreset *out)
{
    if (track >= MI_TRACKS || !out)
        return 0;
    uint32_t sr = dkrt_irq_mask();
    const IpInst *in = for_track(track);
    int ready = in && initialized && records_loaded;
    if (ready)
        preset_snapshot(in, out);
    dkrt_irq_restore(sr);
    return ready;
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
        visible && for_track(track) == in && records_loaded && dkrt_instrument_type(), page,
        (uint32_t)in->mode, (uint32_t)in->arp_clock, in->mode == KB_ROOT && in->type == 10, 0);
    is_select(STS(in), track, generation, (uint32_t)in->mode, 0);
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
    case CP_COLOR:
        if (cur->pad_family == 3)
            cur->fm[0] = v;
        else
            cur->pad_color = v;
        break;
    case CP_DRIFT:
        cur->pad_drift = v;
        break;
    case CP_ARP_MIX:
        cur->arp_mix = v;
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
    cv_build(out, SRC(cur), STS(cur), now, tune, level, preset_result);
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
    cv_build_owner(out, &shown, STS(cur), now, tune, level, preset_result, SRC(cur)->mode);
    out->locked = cv_lock_mask(&shown, snapshot);
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
    cv_build(&d, &snapshot, STS(in), now, "", "", preset_result);
    status_pair(m, snapshot.mode == KB_ROOT ? "ROOT" : "PLAY", d.heading);
    status_pair(m, d.notes[0], d.notes[1]);
    status_pair(m, d.notes[2], d.notes[3]);
    int invalid = d.waiting || d.fault == 1 || d.fault == 2;
    dk_ui_menu_add_text(m, invalid ? "PEDAL --" : d.pedal ? "PEDAL DOWN" : "PEDAL UP");
    snapshot.page[track] = 1;
    cv_build(&d, &snapshot, STS(in), now, "", "", preset_result);
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
    /* whether the instance plays: a status, so here and not a row of the setup list */
    dk_ui_menu_add_text(m, readiness(in));
    status_draw_at = now;
}
/* VerticalMenuView reserves a narrow left title column (9 characters of its font): one short word, as the stock titles
 * (SETTINGS; MIDI for MIDI CONFIG). */
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
    in->mode = p->mode;
    in->type = (uint8_t)p->control[CP_CHORD];
    in->voicing = (uint8_t)p->control[CP_VOICING];
    in->shape.count = p->shape.count;
    for (uint32_t i = 0; i < 4; ++i)
        in->shape.intervals[i] = p->shape.intervals[i];
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
    preset_snapshot(&inst[i], &now);
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
    if (!initialized || !records_loaded)
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
    if (!s || !records_loaded)
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
    preset_snapshot(&inst[i], &now);
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
    if (!initialized || !records_loaded || i >= IP_INSTANCES || !sound)
        return 0;
    volatile const uint32_t *a = &ip_admission[i * 8];
    return a[0] && !a[6] && a[1] == sound && a[4] == generation && a[5] &&
           a[5] == dkrt_instrument_type() && inst[i].track < MI_TRACKS &&
           inst[i].track == ip_tracks[i];
}
static int sound_state_owned(uint32_t i, uint32_t sound, uint32_t generation)
{
    return sound_admission_owned(i, sound, generation);
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
        dkrt_irq_restore(sr);
        return -2;
    }
    sound_transaction_busy = 1;
    uint32_t i = IDX(cur);
    ProofLiveOwner owner = {i, ip_admission[8 * i + 1], ip_admission[8 * i + 4]};
    int result = -1;
    if (cur->bound == cur->track) {
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
    dk_ui_menu_add_action(m, "SAVE SOUND", preset_save, 0, 0);
    dk_ui_menu_add_action(m, "LOAD SOUND", preset_load, 0, 0);
}
static const dk_ui_menu_desc preset_desc = {
    .name = "canopy-presets", .title = "PRESETS", .fill = preset_fill};
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
/* LABEL at the left and VALUE ending on the row's 17th character, where the value rows end theirs. */
static void named_row(char out[DK_UI_ROW], const char *label, const char *value)
{
    uint32_t at = 0, n = 0;
    while (value[n])
        ++n;
    while (*label && at + 1 < DK_UI_ROW)
        out[at++] = *label++;
    do
        out[at++] = ' ';
    while (at + n < 17 && at + 1 < DK_UI_ROW);
    while (*value && at + 1 < DK_UI_ROW)
        out[at++] = *value++;
    out[at] = 0;
}
static void tone_fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    wheel_amount = cur->wt[CP_WT_WHEEL - 16] - 100;
    fm_wheel_amount = cur->fm[CP_FM_WHEEL - 15] - 100;
    wtfm_wheel_amount = cur->wtfm[CP_WTFM_WHEEL - 28] - 100;
    /* A row names its setting and shows the value at the right, as the stock rows (CLOCK RECEIVE and its box) and the
     * value rows below: the list's font is 4 pixels a character, 17 to a row. */
    char row[DK_UI_ROW];
    static const char *const clocks[] = {"FREE", "SYNC"};
    named_row(row, "SCAN CLOCK", clocks[cur->wt[CP_WT_CLOCK - 16] & 1]);
    dk_ui_menu_add_action(m, row, scan_clock_choice, 0, 0);
    named_row(row, "SCAN RESET", scan_restarts[cur->wt[CP_WT_RESET - 16] % 3]);
    dk_ui_menu_add_action(m, row, scan_restart_choice, 0, 0);
    dk_ui_menu_add_value(m, "WHEEL POS", &wheel_amount, -100, 100, amount_changed, 0);
    dk_ui_menu_add_value(m, "WHEEL FM", &fm_wheel_amount, -100, 100, amount_changed,
                         (void *)(uintptr_t)1);
    dk_ui_menu_add_value(m, "WHEEL WT FM", &wtfm_wheel_amount, -100, 100, amount_changed,
                         (void *)(uintptr_t)2);
}
static const dk_ui_menu_desc tone_desc = {.name = "canopy-tone", .title = "MOD", .fill = tone_fill};
static void fill(dk_ui_menu *m, void *ctx)
{
    (void)ctx;
    if (!tone_menu)
        tone_menu = dk_ui_menu_new(ui(), &tone_desc);
    if (tone_menu)
        dk_ui_menu_add_submenu(m, "MODULATION", tone_menu);
    if (!preset_menu)
        preset_menu = dk_ui_menu_new(ui(), &preset_desc);
    if (preset_menu)
        dk_ui_menu_add_submenu(m, "PRESETS", preset_menu);
    dk_ui_menu_add_action(m, "PANIC", panic, 0, 0);
    dk_ui_menu_add_action(m, "RECOVER KEYBOARD", recover, 0, 0);
    /* Only actions and settings, as the stock lists: whether CANOPY plays is on STATUS, and the track keyboard plays it from
     * the firmware's MIDI config (the README says so). */
}
/* opened from the DIGIKIT row Canopy, its title is that row's word, as MIDI CONFIG opens as MIDI */
static const dk_ui_menu_desc desc = {
    .name = "instrument-keyboard", .title = "Canopy", .fill = fill};
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
    /* no help rows: the stock lists hold only actions and settings (the README explains LEVEL/DATA, DETAIL H and SRC held) */
    return 1;
}
void dk_app_instrument_settings_rows(dk_ui_menu *parent)
{
    if (!menu)
        menu = dk_ui_menu_new(ui(), &desc);
    if (menu)
        dk_ui_menu_add_submenu(parent, "Canopy", menu);
    if (!status_menu)
        status_menu = dk_ui_menu_new(ui(), &status_desc);
    if (status_menu)
        dk_ui_menu_add_submenu(parent, "Canopy STATUS", status_menu);
}
/* Enable this instance's panel route. CANOPY has no USB or DIN route of its own: the track keyboard plays it from the firmware's
 * MIDI config (its routes, the records canopy/routes/v1 and canopy/channels/v1, stay on the card unread). */
static int routes_enable(IpInst *in)
{
    return !mi_route_enable(MI_PANEL, 0, in->bound, 0);
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
    /* The effective bank suppresses the local arp and strum after MOD and
     * locks. The carrier strips this flag before the machine callback. */
    if (in->track < MI_TRACKS && track_keyboard_claim[in->track]) {
        values[0] |= 0x4000u;
        if (track_keyboard_lanes[in->track])
            values[0] |= 0x2000u | (uint32_t)track_keyboard_lanes[in->track] << 11;
    }
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
    extern void instrument_src_tick(void);
    instrument_src_tick();
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
    if (!records_loaded) {
        /* The learned-shape library (canopy/shapes/v1) of CANOPY's own keyboard stays on the card unread. */
        preset_present = 0;
        for (uint32_t i = 0; i < CP_SLOTS; ++i) {
            CpPreset p;
            int n = dkrt_settings_get(preset_keys[i], preset_records[i], CP_RECORD_BYTES);
            if (n >= 0 && !cp_decode(&p, preset_records[i], (uint32_t)n) &&
                !cp_encode(&p, preset_records[i]))
                preset_present |= (uint8_t)(1u << i);
        }
        uint8_t copy[CW_BYTES];
        int n = dkrt_settings_get(work_key, copy, sizeof(copy));
        if (n < 0 || cw_decode(&work, copy, (uint32_t)n))
            cw_clear(&work);
        records_loaded = 1;
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
        is_await(STS(ui));
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
        is_select(STS(ui), ui->track, mi_ledger.generations[ui->track], (uint32_t)ui->mode, 0);
        instrument_src_poll(instrument_src_now());
    }
    for (uint32_t i = 0; i < IP_INSTANCES; ++i) {
        const IpInst *in = &inst[i];
        volatile uint32_t *c = &ip_control[i * 8];
        uint32_t t = in->bound < MI_TRACKS ? in->bound : 0;
        c[0] = in->phase;
        c[1] = 0; /* was the INPUT page's EXTERNAL MIDI */
        c[2] = healthy;
        c[3] = in->prepared;
        c[4] = mi_transport.faults;
        c[5] = mi_ledger.reset_mask;
        c[6] = mi_transport.applied[t];
        c[7] = itype;
    }
    /* what STATUS's readiness row shows */
    uint32_t view =
        cur->phase | ((uint32_t)(quarantined(cur) || mi_transport.faults) << 4) | (IDX(cur) << 8);
    if (shown_main != view) {
        shown_main = view;
        if (status_menu && dk_ui_menu_is_open(status_menu))
            dk_ui_menu_refresh(status_menu);
    }
}
