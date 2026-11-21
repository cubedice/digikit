// Stock Sound records carry the settings and the source +Drive folder/number.
// The asynchronous file job must finish before a new reference is published.
struct TapeRecord {
    unsigned source, take, grid, input, gain, feedback, mix, spacing, hits, sensitivity;
    int rate;
};
struct TapeBinding {
    u32 sound, sequence, generation;
    unsigned folder, take, grid, pending, discard, has_record;
    u8 record[SR_BYTES];
    struct TapeRecord incoming;
};
static struct TapeBinding binding[16];
static unsigned save_requests, file_track = 16, file_generation, loading_tape;

static unsigned record_check(const u8 *p)
{
    unsigned crc = 0xffff;
    for (unsigned i = 0; i < SR_BYTES - 2; ++i) {
        crc ^= (unsigned)p[i] << 8;
        for (unsigned j = 0; j < 8; ++j)
            crc = ((crc << 1) ^ (crc & 0x8000 ? 0x1021 : 0)) & 65535;
    }
    return crc;
}
static void put16(u8 *p, unsigned n)
{
    p[0] = n >> 8;
    p[1] = n;
}
static unsigned get16(const u8 *p)
{
    return (unsigned)p[0] << 8 | p[1];
}
static int decode_tape(const u8 *p, struct TapeRecord *v)
{
    if (p[0] != 0xd1 || p[1] != 'R' || p[2] != 'L' || p[3] != 'Y' ||
        get16(p + 38) != record_check(p))
        return 0;
    v->hits = p[18];
    v->sensitivity = p[19];
    v->source = p[4];
    v->take = get16(p + 5);
    v->grid = get16(p + 7);
    v->input = p[9];
    v->gain = p[10];
    v->feedback = p[11];
    v->mix = p[12];
    v->rate = (int)get16(p + 13) - 200;
    v->spacing = p[15] ? 65536 : get16(p + 16);
    return v->source < 16 && v->take <= 9999 && v->grid <= 1024 && v->input < 19 && v->hits <= 1 &&
           v->sensitivity <= 8 && v->gain <= 100 && v->feedback <= 100 && v->mix <= 100 &&
           v->rate >= -200 && v->rate <= 200;
}
static void publish_sound(unsigned t)
{
    struct TapeBinding *b = binding + t;
    SaStore *store = ip_sa_store();
    if (!store || !b->sound || b->pending || ip_current_machine_sound(t, relay_type) != b->sound)
        return;
    const struct Settings *s = settings + t;
    u8 bytes[SR_BYTES];
    for (unsigned i = 0; i < SR_BYTES; ++i)
        ((volatile u8 *)bytes)[i] = 0;
    bytes[0] = 0xd1;
    bytes[1] = 'R';
    bytes[2] = 'L';
    bytes[3] = 'Y';
    bytes[4] = b->folder;
    put16(bytes + 5, b->take);
    put16(bytes + 7, b->grid);
    bytes[9] = s->input;
    bytes[10] = s->gain;
    bytes[11] = s->feedback;
    bytes[12] = s->mix;
    put16(bytes + 13, (unsigned)(s->rate + 200));
    bytes[15] = s->spacing == 65536;
    put16(bytes + 16, s->spacing);
    bytes[18] = s->hits;
    bytes[19] = s->sensitivity;
    put16(bytes + 38, record_check(bytes));
    u32 sr = dkrt_irq_mask();
    if (sa_seq(store, b->sound) == b->sequence) {
        b->sequence = sa_put(store, b->sound, bytes);
        b->has_record = 1;
        for (unsigned i = 0; i < SR_BYTES; ++i)
            b->record[i] = bytes[i];
    }
    dkrt_irq_restore(sr);
}
static int save_tape(unsigned t, unsigned hits, unsigned sens)
{
    if (file_track < 16 || !relay_file_start(RF_SAVE, t, t, 1, hits, sens))
        return 0;
    file_track = t;
    file_generation = binding[t].generation;
    loading_tape = 0;
    return 1;
}
// Called by the synchronous stock serializer. No I/O in its calling context.
int rtm_can_save(u32 sound)
{
    if (!relay_type || !sound || *(const volatile u8 *)(uintptr_t)(sound + 0xa2) != relay_type)
        return 1;
    for (unsigned t = 0; t < 16; ++t) {
        if (!sound || binding[t].sound != sound)
            continue;
        if ((latest[2] & 15) && latest[2] >> 8 == t)
            return 0;
        if (binding[t].pending || file_track == t)
            return 0;
        if (((latest[8] & latest[10]) >> (16 + t)) & 1u) {
            save_requests |= 1u << t;
            return 0;
        }
    }
    return 1;
}
static int tape_saved(void)
{
    if (file_track >= 16 || loading_tape)
        return 0;
    unsigned t = file_track;
    file_track = 16;
    struct TapeBinding *b = binding + t;
    if (file_generation != b->generation)
        return 0;
    b->folder = relay_files.source;
    b->take = relay_files.number;
    b->grid = relay_files.grid;
    if (b->pending == 3) {
        b->discard = 1;
        b->pending = 1;
    } else
        publish_sound(t);
    unsigned retry = save_requests & (1u << t);
    save_requests &= ~(1u << t);
    return retry != 0;
}
static int restore_controls(unsigned t)
{
    struct TapeBinding *b = binding + t;
    struct TapeRecord *r = &b->incoming;
    struct Settings *s = settings + t;
    s->input = r->input == 3 + t ? 0 : r->input;
    s->hits = r->hits;
    s->sensitivity = r->sensitivity;
    s->gain = r->gain;
    s->feedback = r->feedback;
    s->mix = r->mix;
    s->spacing = r->spacing;
    s->rate = r->rate;
    if (!send_to(t, RT_SETTINGS, 0, r->grid))
        return 0;
    b->folder = r->source;
    b->take = r->take;
    b->grid = r->grid;
    b->pending = b->discard = 0;
    return 1;
}
static void share_result(unsigned t, unsigned error)
{
    if (binding[t].pending != 5)
        return;
    binding[t].pending = error ? 1 : 4;
    if (error)
        binding[t].discard = 2; // Source changed before the DSP accepted. Load the file.
}
static void tape_loaded(void)
{
    if (file_track >= 16 || !loading_tape)
        return;
    unsigned t = file_track;
    file_track = 16;
    if (binding[t].generation == file_generation)
        binding[t].pending = 4;
}
static void tape_file_failed(void)
{
    if (relay_files.action == RF_SCAN || file_track >= 16)
        return;
    save_requests &= ~(1u << file_track);
    if (file_generation == binding[file_track].generation && binding[file_track].pending)
        binding[file_track].pending = 2;
    file_track = 16;
}
int rtm_load_current(unsigned t)
{
    return t < 16 && file_track == t && loading_tape && binding[t].generation == file_generation &&
           ip_current_machine_sound(t, relay_type) == binding[t].sound && ip_sa_store() &&
           binding[t].sequence == sa_seq(ip_sa_store(), binding[t].sound);
}
static unsigned recall_choice(unsigned key)
{
    if (track >= 16)
        return 0;
    struct TapeBinding *b = binding + track;
    if (key == 14 || key == 15) {
        confirm = 3 + (confirm - 3 + (key == 14 ? 2 : 1)) % 3;
        return 1;
    }
    if (key == 12 || (key == 10 && confirm == 3)) {
        b->pending = b->discard = confirm = 0;
        publish_sound(track);
        return 1;
    }
    if (key != 10)
        return 0;
    if (confirm == 4) {
        if (!save_tape(track, 0, 1)) {
            say("FILE BUSY");
            return 1;
        }
        b->pending = 3;
    } else {
        b->pending = 1;
        b->discard = 1;
    }
    confirm = 0;
    return 1;
}
static void tape_sound_tick(void)
{
    static unsigned next;
    initialize_settings();
    if (!relay_type) {
        unsigned first = machines_info[3], count_types = machines_info[4];
        if (count_types > 128)
            return;
        for (unsigned i = 0; i < count_types; ++i)
            if (owns(first + i)) {
                relay_type = first + i;
                break;
            }
        if (!relay_type)
            return;
    }
    SaStore *store = ip_sa_store();
    if (!store)
        return;
    unsigned t = next;
    next = (next + 1) & 15;
    u32 sound = ip_current_machine_sound(t, relay_type);
    struct TapeBinding *b = binding + t;
    if (!sound || sa_slot(store, sound) < 0) {
        mod_ready &= ~(1u << t);
        return;
    }
    // Only the unused base mirrors are neutralized. Stock LFO routing and
    // depth belong to the Sound, and effective offsets live in its DSP frame.
    if (!(mod_ready & (1u << t)) || sound != b->sound || sa_seq(store, sound) != b->sequence) {
        int refresh = !(mod_ready & (1u << t));
        for (unsigned i = 0; i < 6; ++i) {
            volatile uint16_t *cell =
                (volatile uint16_t *)(uintptr_t)(sound + 0x14 + 2 * mod_mirrors[i]);
            if (*cell != 0x4000) {
                *cell = 0x4000;
                refresh = 1;
            }
        }
        if (refresh && ip_refresh_machine_track(t, relay_type)) {
            mod_ready |= 1u << t;
            if (!send_to(t, RT_SETTINGS, 0, 0))
                pending_settings |= 1u << t;
        }
    }
    u32 sr = dkrt_irq_mask(), seq = sa_seq(store, sound);
    if (sound != b->sound || seq != b->sequence) {
        u8 payload[SR_BYTES];
        int has = sa_get(store, sound, payload, &seq);
        dkrt_irq_restore(sr);
        // Stock republishes a Sound association during navigation. Only different
        // record bytes represent a recall; an unchanged association owns the live tape.
        unsigned same = b->sound && b->has_record == (unsigned)has && (has || sound == b->sound);
        for (unsigned i = 0; same && has && i < SR_BYTES; ++i)
            same = b->record[i] == payload[i];
        if (same) {
            b->sequence = seq;
            b->sound = sound;
        } else {
            struct TapeRecord incoming;
            incoming.source = incoming.take = incoming.grid = incoming.input = incoming.hits = 0;
            incoming.sensitivity = 4;
            incoming.gain = incoming.feedback = incoming.mix = 100;
            incoming.spacing = 65536;
            incoming.rate = 100;
            int valid = has && decode_tape(payload, &incoming);
            if (has && !valid) {
                say("SOUND DATA ERROR");
                return;
            }
            if (valid && incoming.take && !incoming.grid) {
                incoming.grid = pp_record_steps();
                if (!incoming.grid)
                    incoming.grid = 1;
            }
            b->has_record = has;
            for (unsigned i = 0; has && i < SR_BYTES; ++i)
                b->record[i] = payload[i];
            unsigned previous = b->sound;
            save_requests &= ~(1u << t);
            pending_settings &= ~(1u << t);
            ++b->generation;
            b->sequence = seq;
            b->sound = sound;
            b->incoming = incoming;
            if (!valid && !previous) {
                send_to(t, RT_SETTINGS, 0, 0);
            } else
                b->pending = 1;
        }
    } else
        dkrt_irq_restore(sr);
    if (file_track == t)
        return;
    unsigned dirty = (latest[10] >> (16 + t)) & 1, has_audio = (latest[8] >> (16 + t)) & 1;
    if (!has_audio && dirty && b->take && !b->pending) {
        b->take = b->grid = 0;
        publish_sound(t);
    }
    if (b->pending == 4) {
        restore_controls(t);
        return;
    }
    if (b->pending == 1) {
        if (b->incoming.take == b->take && b->incoming.source == b->folder && !b->discard &&
            (b->incoming.take ? has_audio && !dirty : !has_audio)) {
            restore_controls(t);
            return;
        }
        if (has_audio && dirty && !b->discard)
            b->pending = 2;
        else if (b->incoming.take) {
            if (b->discard != 2) {
                unsigned clean = latest[8] & ~latest[10];
                for (unsigned other = 0; other < 16; ++other) {
                    if (other == t || !(clean & (1u << (16 + other))) || binding[other].pending ||
                        binding[other].folder != b->incoming.source ||
                        binding[other].take != b->incoming.take)
                        continue;
                    if (send_to(t, RT_SHARE, other, 0))
                        b->pending = 5;
                    return;
                }
            }
            if (file_track < 16 || relay_files.state == RF_WORK)
                return;
            if (!relay_file_start(RF_LOAD, t, b->incoming.source, b->incoming.take, 0, 1))
                return;
            file_track = t;
            file_generation = b->generation;
            loading_tape = 1;
            return;
        } else {
            if (has_audio && !send_to(t, RT_CLEAR, 0, 0))
                return;
            b->pending = 4;
            return;
        }
    }
    if (b->pending == 2 && track == t && !confirm && !audition)
        confirm = 3;
    if ((save_requests & (1u << t)) && file_track >= 16 && !b->pending && save_tape(t, 0, 1))
        say("SAVING TAPE");
}
