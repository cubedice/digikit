#include "track-keyboard.h"

#include "track-memory.h"

static const uint8_t divisions[] = {1,  2,  3,  4,  5,  6,  8,  10, 12, 16,
                                    20, 24, 32, 40, 48, 64, 80, 7,  5};

TkParams tk_defaults(void)
{
    return (TkParams){.order = TK_ORDER_PLAY, .gate = 50, .rate = 9, .swing = 50, .voices = 4};
}

bool tk_rate_division(uint8_t rate, uint8_t *numerator, uint8_t *denominator)
{
    if (rate >= sizeof divisions)
        return false;
    *numerator = rate >= TK_RATE_7Q ? 1 : 4;
    *denominator = divisions[rate];
    return true;
}

bool tk_valid(const TkParams *p)
{
    return p && p->arp <= 1 && p->poly <= 1 && p->order <= TK_ORDER_RANDOM && p->gate >= 1 &&
           p->gate <= 100 && p->rate < sizeof divisions && p->octaves >= -3 && p->octaves <= 3 &&
           p->swing >= 50 && p->swing <= 75 && p->legacy_swing <= 75 &&
           p->reset <= TK_RESET_CONTINUE && p->chord < TK_CHORD_COUNT &&
           p->voicing <= TK_VOICE_INV2 && p->strum_ms <= 500 && p->voices > 0 &&
           p->voices <= TK_NOTES;
}

bool tk_valid_recipe(const TkRecipe *recipe)
{
    if (!recipe || recipe->count > TK_NOTES)
        return false;
    for (uint8_t i = 0; i < recipe->count; ++i)
        if (recipe->notes[i].interval < -127 || recipe->notes[i].velocity > 127)
            return false;
    return true;
}

void tk_init(TkState *s, TkOutput output, void *context)
{
    memset(s, 0, sizeof *s);
    s->params = tk_defaults();
    s->rng = 0x41c64e6du;
    s->voice_limit = 1;
    s->output = output;
    s->context = context;
}

static uint8_t find_pitch(const TkPitch *notes, uint8_t count, uint8_t pitch)
{
    for (uint8_t i = 0; i < count; ++i)
        if (notes[i].pitch == pitch)
            return i;
    return count;
}

static void append(TkPitch *notes, uint8_t *count, uint8_t limit, int pitch, uint8_t velocity)
{
    if (pitch < 0 || pitch > 127 || *count == limit ||
        find_pitch(notes, *count, (uint8_t)pitch) != *count)
        return;
    notes[(*count)++] = (TkPitch){(uint8_t)pitch, velocity};
}

static void unsend(TkState *s, TkVoice *voice)
{
    if (!voice->sent)
        return;
    TkNoteEvent off = voice->note;
    off.on = 0;
    off.velocity = 0;
    s->output(s->context, &off);
    voice->sent = false;
}

static void stop(TkState *s, TkVoice *voice)
{
    unsend(s, voice);
    *voice = (TkVoice){0};
}

static void start(TkState *s, TkVoice *voice, TkPitch pitch, uint8_t lane)
{
    if (!s->output)
        return;
    s->serial = (s->serial + 1u) & 0x7fffffffu;
    if (!s->serial)
        s->serial = 1;
    TkNoteEvent event = {.token = s->serial,
                         .pitch = pitch.pitch,
                         .velocity = pitch.velocity,
                         .lane = lane,
                         .on = 1,
                         .lanes = s->params.poly | s->params.arp << 1};
    if (s->muted || s->output(s->context, &event))
        *voice = (TkVoice){.note = event, .order = ++s->voice_order, .sent = !s->muted};
}

void tk_mute_output(TkState *s, bool muted)
{
    if (s->muted == muted)
        return;
    s->muted = muted;
    if (muted) {
        for (uint8_t i = 0; i < TK_NOTES; ++i)
            unsend(s, &s->voices[i]);
        unsend(s, &s->arp_voice);
        return;
    }
    if (s->output)
        for (uint8_t i = 0; i < TK_NOTES; ++i)
            if (s->voices[i].note.token)
                s->voices[i].sent = s->output(s->context, &s->voices[i].note);
}

static bool live_input(const TkState *s)
{
    for (uint8_t i = 0; i < s->key_count; ++i)
        if (s->keys[i].source != TK_SEQUENCER)
            return true;
    return false;
}

static bool selected(TkKey key, bool live)
{
    return (key.source != TK_SEQUENCER) == live;
}

static uint8_t chord(const TkState *s, TkKey root, TkPitch *notes)
{
    static const int8_t shape[TK_CHORD_COUNT][4] = {
        [TK_CHORD_MAJ] = {0, 4, 7, -1},  [TK_CHORD_MAJ6] = {0, 4, 7, 9},
        [TK_CHORD_SUS2] = {0, 2, 7, -1}, [TK_CHORD_MIN6] = {0, 3, 7, 9},
        [TK_CHORD_MIN] = {0, 3, 7, -1},  [TK_CHORD_SUS4] = {0, 5, 7, -1},
        [TK_CHORD_ADD9] = {0, 4, 7, 14}, [TK_CHORD_DOM7] = {0, 4, 7, 10},
        [TK_CHORD_DIM] = {0, 3, 6, -1},  [TK_CHORD_MIN7] = {0, 3, 7, 10},
        [TK_CHORD_MAJ7] = {0, 4, 7, 11}, [TK_CHORD_AUG] = {0, 4, 8, -1},
    };
    uint8_t n = 0;
    if (s->params.chord == TK_CHORD_USER) {
        for (uint8_t i = 0; i < s->user.count; ++i) {
            TkRecipeNote note = s->user.notes[i];
            append(notes, &n, TK_NOTES, root.pitch + note.interval,
                   note.velocity ? note.velocity : root.velocity);
        }
        return n;
    }
    const int8_t *intervals = shape[s->params.chord];
    uint8_t size = intervals[3] < 0 ? 3 : 4;
    uint8_t inv =
        s->params.voicing >= TK_VOICE_INV1 ? (uint8_t)(s->params.voicing - TK_VOICE_INV1 + 1) : 0;
    for (uint8_t i = 0; i < size; ++i) {
        uint8_t j = s->params.voicing == TK_VOICE_OPEN
                        ? (i == 0 ? 0 : (i == size - 1 ? 1 : (uint8_t)(i + 1)))
                        : (uint8_t)((i + inv) % size);
        int offset =
            intervals[j] + (j < inv || (s->params.voicing == TK_VOICE_OPEN && j == 1) ? 12 : 0);
        append(notes, &n, TK_NOTES, root.pitch + offset, root.velocity);
    }
    return n;
}

static uint8_t input(TkState *s, bool live, bool direct, TkPitch *notes)
{
    uint8_t n = 0;
    if (s->params.chord != TK_CHORD_PLAY) {
        for (uint8_t i = s->key_count; i > 0; --i)
            if (selected(s->keys[i - 1], live))
                return chord(s, s->keys[i - 1], notes);
        return 0;
    }
    for (uint8_t i = 0; i < s->key_count; ++i) {
        TkKey key = s->keys[i];
        if (selected(key, live) && (!direct || !key.stolen))
            append(notes, &n, TK_NOTES, key.pitch, key.velocity);
    }
    return n;
}

static uint8_t duration_count(const TkState *s)
{
    uint8_t n = s->candidate_count;
    return s->params.order == TK_ORDER_UPDN && n > 1 ? (uint8_t)(2u * n - 2u) : n;
}

static void rebuild_arp(TkState *s, const TkPitch *input_notes, uint8_t n, bool restart)
{
    unsigned layers =
        s->params.octaves < 0 ? (unsigned)-s->params.octaves : (unsigned)s->params.octaves;
    int sign = s->params.octaves < 0 ? -1 : 1;
    s->candidate_count = 0;
    for (unsigned layer = 0; layer <= layers; ++layer)
        for (uint8_t i = 0; i < n; ++i)
            append(s->candidates, &s->candidate_count, TK_CANDIDATES,
                   input_notes[i].pitch + sign * 12 * (int)layer, input_notes[i].velocity);
    if (s->params.order == TK_ORDER_UP || s->params.order == TK_ORDER_DOWN ||
        s->params.order == TK_ORDER_UPDN) {
        for (uint8_t i = 1; i < s->candidate_count; ++i) {
            TkPitch pitch = s->candidates[i];
            uint8_t j = i;
            while (j &&
                   (s->params.order == TK_ORDER_DOWN ? s->candidates[j - 1].pitch < pitch.pitch
                                                     : s->candidates[j - 1].pitch > pitch.pitch)) {
                s->candidates[j] = s->candidates[j - 1];
                --j;
            }
            s->candidates[j] = pitch;
        }
    }
    if (!s->params.arp || !s->candidate_count) {
        s->playing = false;
        stop(s, &s->arp_voice);
        return;
    }
    if (s->arp_voice.note.token && find_pitch(s->candidates, s->candidate_count,
                                              s->arp_voice.note.pitch) == s->candidate_count)
        stop(s, &s->arp_voice);
    if (restart || !s->playing) {
        uint8_t count = duration_count(s);
        if (s->params.reset == TK_RESET_TRIG ||
            (s->params.reset == TK_RESET_PATTERN && !s->have_cycle))
            s->cursor = 0;
        else if (s->params.reset == TK_RESET_PATTERN) {
            uint8_t num, den;
            if (!tk_rate_division(s->params.rate, &num, &den))
                return;
            uint64_t span = (uint64_t)TK_TICKS_PER_QUARTER * num;
            uint64_t elapsed = s->clock - s->cycle;
            s->cursor = (uint8_t)(((elapsed % (span * count)) * den / span) % count);
        }
        s->swung = false;
        s->interval_remainder = 0;
        s->next = s->clock;
        s->playing = true;
    }
}

static void strum(TkState *s)
{
    while (s->strum_index < s->direct_count) {
        uint8_t i = s->strum_index;
        if (s->direct_count > 1 && s->params.chord != TK_CHORD_PLAY &&
            s->time_us - s->strum_start <
                (uint64_t)i * s->params.strum_ms * 1000u / (s->direct_count - 1u))
            break;
        ++s->strum_index;
        uint8_t free_slot = TK_NOTES;
        bool already_playing = false;
        for (uint8_t j = 0; j < TK_NOTES; ++j) {
            const TkNoteEvent *note = &s->voices[j].note;
            if (!note->token)
                free_slot = j;
            else if (note->pitch == s->direct[i].pitch)
                already_playing = true;
        }
        if (!already_playing && free_slot < TK_NOTES)
            start(s, &s->voices[free_slot], s->direct[i], TK_DIRECT);
    }
}

static void rebuild(TkState *s, bool restart)
{
    bool live = live_input(s);
    TkPitch notes[TK_NOTES];
    uint8_t n = input(s, live, false, notes);
    rebuild_arp(s, notes, n, restart);
    n = 0;
    if (s->params.poly && (!s->params.arp || s->blend))
        n = input(s, live, true, notes);
    uint8_t limit = s->params.voices < s->voice_limit ? s->params.voices : s->voice_limit;
    if (n > limit && s->params.chord == TK_CHORD_PLAY) {
        while (n > limit) {
            uint8_t cut = 0;
            uint64_t oldest = UINT64_MAX;
            for (uint8_t i = 0; i < TK_NOTES; ++i) {
                TkVoice voice = s->voices[i];
                uint8_t j = find_pitch(notes, n, voice.note.pitch);
                if (voice.note.token && j < n && voice.order < oldest) {
                    oldest = voice.order;
                    cut = j;
                }
            }
            for (uint8_t i = 0; i < s->key_count; ++i)
                if (selected(s->keys[i], live) && s->keys[i].pitch == notes[cut].pitch)
                    s->keys[i].stolen = true;
            --n;
            for (uint8_t i = cut; i < n; ++i)
                notes[i] = notes[i + 1];
        }
    } else if (n > limit) {
        n = limit;
    }
    for (uint8_t i = 0; i < TK_NOTES; ++i)
        if (s->voices[i].note.token && find_pitch(notes, n, s->voices[i].note.pitch) == n)
            stop(s, &s->voices[i]);
    if (n != s->direct_count || !tk_bytes_equal(notes, s->direct, n * sizeof notes[0])) {
        memcpy(s->direct, notes, n * sizeof notes[0]);
        s->direct_count = n;
        s->strum_index = 0;
        s->strum_start = s->time_us;
    }
    strum(s);
}

void tk_set_backend(TkState *s, uint8_t max_voices, bool blend)
{
    s->voice_limit = max_voices <= TK_NOTES ? max_voices : TK_NOTES;
    s->blend = blend;
    rebuild(s, false);
}

bool tk_configure(TkState *s, const TkParams *params, const TkRecipe *user)
{
    if (!tk_valid(params) || !tk_valid_recipe(user))
        return false;
    if (s->params.rate != params->rate)
        s->interval_remainder = 0;
    if (!s->params.poly && params->poly)
        for (uint8_t i = 0; i < s->key_count; ++i)
            s->keys[i].stolen = false;
    s->params = *params;
    s->user = *user;
    rebuild(s, false);
    return true;
}

static uint8_t key_index(TkState *s, uint8_t source, uint8_t channel, uint32_t id)
{
    for (uint8_t i = 0; i < s->key_count; ++i)
        if (s->keys[i].source == source && s->keys[i].channel == channel && s->keys[i].id == id)
            return i;
    return s->key_count;
}

static void remove_key(TkState *s, uint8_t i)
{
    --s->key_count;
    for (; i < s->key_count; ++i)
        s->keys[i] = s->keys[i + 1];
}

static void remove_seed(TkState *s)
{
    for (uint8_t i = s->key_count; i > 0; --i)
        if (s->keys[i - 1].source == TK_SEQUENCER)
            remove_key(s, (uint8_t)(i - 1));
    s->seed.count = 0;
    s->seed_started = 0;
}

static void advance_seed(TkState *s, bool fresh)
{
    if (!s->seed.count)
        return;
    uint64_t ticks = s->clock - s->seed_start;
    uint64_t elapsed_us = s->time_us - s->seed_time_us;
    if (s->seed.length_ticks && ticks >= s->seed.length_ticks) {
        remove_seed(s);
        rebuild(s, false);
        return;
    }
    bool changed = fresh;
    for (uint8_t i = 0; i < s->seed.count; ++i) {
        TkSeedNote note = s->seed.notes[i];
        uint16_t bit = (uint16_t)(1u << i);
        if (elapsed_us < note.delay_us)
            continue;
        if (!(s->seed_started & bit)) {
            if (note.gate_ticks && ticks >= note.gate_ticks) {
                s->seed_started |= bit;
                continue;
            }
            if (s->key_count == TK_KEYS)
                continue;
            s->keys[s->key_count++] = (TkKey){
                .source = TK_SEQUENCER,
                .id = i,
                .pitch = note.pitch,
                .velocity = note.velocity,
                .down = true,
            };
            s->seed_started |= bit;
            changed = true;
        } else if (note.gate_ticks && ticks >= note.gate_ticks) {
            uint8_t j = key_index(s, TK_SEQUENCER, 0, i);
            if (j < s->key_count) {
                remove_key(s, j);
                changed = true;
            }
        }
    }
    if (changed && !live_input(s))
        rebuild(s, fresh);
}

bool tk_seed_valid(const TkSeed *seed)
{
    if (!seed || !seed->count || seed->count > TK_NOTES)
        return false;
    if (seed->notes[0].delay_us != 0)
        return false;
    uint32_t last_delay = 0;
    for (uint8_t i = 0; i < seed->count; ++i) {
        TkSeedNote note = seed->notes[i];
        if (note.pitch > 127 || !note.velocity || note.velocity > 127 ||
            note.delay_us < last_delay ||
            (seed->length_ticks && note.gate_ticks > seed->length_ticks))
            return false;
        last_delay = note.delay_us;
    }
    return true;
}

bool tk_seed(TkState *s, const TkSeed *seed)
{
    if (!tk_seed_valid(seed) || (s->params.chord != TK_CHORD_PLAY && seed->count != 1))
        return false;
    if (!live_input(s)) {
        for (uint8_t i = 0; i < TK_NOTES; ++i)
            stop(s, &s->voices[i]);
        stop(s, &s->arp_voice);
        s->direct_count = 0;
        s->strum_index = 0;
    }
    remove_seed(s);
    s->seed = *seed;
    s->seed_start = s->clock;
    s->seed_time_us = s->time_us;
    advance_seed(s, true);
    return true;
}

void tk_stop_seed(TkState *s)
{
    remove_seed(s);
    rebuild(s, false);
}

void tk_stop_transport(TkState *s)
{
    s->have_cycle = false;
    tk_stop_seed(s);
}
bool tk_press(TkState *s, uint8_t source, uint8_t channel, uint32_t id, int pitch, int velocity)
{
    if (source >= TK_SEQUENCER || channel > 15 || pitch < 0 || pitch > 127 || velocity < 1 ||
        velocity > 127)
        return false;
    uint8_t i = key_index(s, source, channel, id);
    if (i < s->key_count && s->keys[i].down)
        return false;
    /* Retain independent input owners while reserving room for a seed. */
    uint8_t live_keys = 0;
    for (uint8_t j = 0; j < s->key_count; ++j)
        if (s->keys[j].source != TK_SEQUENCER)
            ++live_keys;
    if (i == s->key_count && live_keys == TK_INPUT_KEYS)
        return false;
    bool live = live_input(s);
    bool audible = source != TK_SEQUENCER || !live;
    if (audible && s->params.chord == TK_CHORD_PLAY) {
        TkPitch pitches[TK_NOTES];
        uint8_t n = input(s, source != TK_SEQUENCER, false, pitches);
        if (n == TK_NOTES && find_pitch(pitches, n, (uint8_t)pitch) == n)
            return false;
    }
    if (i < s->key_count)
        remove_key(s, i);
    s->keys[s->key_count++] = (TkKey){
        .source = source,
        .channel = channel,
        .id = id,
        .pitch = (uint8_t)pitch,
        .velocity = (uint8_t)velocity,
        .down = true,
    };
    rebuild(s, !live && source != TK_SEQUENCER);
    return true;
}

void tk_release(TkState *s, uint8_t source, uint8_t channel, uint32_t id)
{
    uint8_t i = key_index(s, source, channel, id);
    if (i == s->key_count)
        return;
    if (source < TK_SEQUENCER && (s->pedal[source] & (1u << channel)))
        s->keys[i].down = false;
    else
        remove_key(s, i);
    rebuild(s, false);
}

void tk_pedal(TkState *s, uint8_t source, uint8_t channel, bool down)
{
    if (source >= TK_SEQUENCER || channel > 15)
        return;
    uint16_t bit = (uint16_t)(1u << channel);
    if (down) {
        s->pedal[source] |= bit;
        return;
    }
    s->pedal[source] &= (uint16_t)~bit;
    for (uint8_t i = s->key_count; i > 0; --i) {
        TkKey key = s->keys[i - 1];
        if (key.source == source && key.channel == channel && !key.down)
            remove_key(s, (uint8_t)(i - 1));
    }
    rebuild(s, false);
}

void tk_clear_source(TkState *s, uint8_t source, uint8_t channel)
{
    if (channel > 15)
        return;
    if (source < TK_SEQUENCER)
        s->pedal[source] &= (uint16_t)~(1u << channel);
    for (uint8_t i = s->key_count; i > 0; --i)
        if (s->keys[i - 1].source == source && s->keys[i - 1].channel == channel)
            remove_key(s, (uint8_t)(i - 1));
    rebuild(s, false);
}

void tk_panic(TkState *s)
{
    remove_seed(s);
    s->key_count = 0;
    memset(s->pedal, 0, sizeof s->pedal);
    s->cursor = 0;
    s->have_last = false;
    rebuild(s, false);
}

void tk_pattern_boundary(TkState *s)
{
    s->have_cycle = true;
    s->cycle = s->clock;
    if (s->params.reset == TK_RESET_PATTERN)
        s->cursor = 0;
}

static uint8_t arp_index(TkState *s)
{
    uint8_t n = s->candidate_count, count = duration_count(s);
    uint8_t i = s->cursor % count;
    s->cursor = (uint8_t)((i + 1u) % count);
    if (s->params.order == TK_ORDER_RANDOM) {
        s->rng ^= s->rng << 13;
        s->rng ^= s->rng >> 17;
        s->rng ^= s->rng << 5;
        uint8_t prev = s->have_last ? find_pitch(s->candidates, n, s->last_pitch) : n;
        if (n > 1 && prev < n) {
            i = (uint8_t)(s->rng % (n - 1u));
            return i >= prev ? (uint8_t)(i + 1u) : i;
        }
        return (uint8_t)(s->rng % n);
    }
    return i < n ? i : (uint8_t)(count - i);
}

bool tk_valid_tempo(uint16_t bpm)
{
    return bpm >= 30u * 120u && bpm <= 300u * 120u;
}

void tk_tick(TkState *s, uint32_t elapsed_us, uint16_t bpm)
{
    s->time_us += elapsed_us;
    if (tk_valid_tempo(bpm))
        s->bpm = bpm;
    /* 21.6M ticks per quarter; bpm is 120 times BPM. Carry fractional ticks. */
    uint64_t advance = (uint64_t)elapsed_us * s->bpm * 3u + s->remainder;
    s->clock += advance / 1000u;
    s->remainder = (uint32_t)(advance % 1000u);
    advance_seed(s, false);
    strum(s);
    if (!s->playing || !s->bpm)
        return;
    if (s->arp_voice.note.token && s->clock >= s->arp_off)
        stop(s, &s->arp_voice);
    if (s->clock < s->next)
        return;
    uint8_t num, den;
    if (!tk_rate_division(s->params.rate, &num, &den))
        return;
    uint64_t span = (uint64_t)TK_TICKS_PER_QUARTER * num;
    unsigned deviation =
        s->params.legacy_swing ? s->params.legacy_swing : 2u * (s->params.swing - 50u);
    unsigned weight = s->swung ? 100u - deviation : 100u + deviation;
    uint64_t scaled = span * weight + s->interval_remainder;
    uint64_t duration = scaled / (den * 100u);
    s->interval_remainder = (uint32_t)(scaled % (den * 100u));
    s->swung = !s->swung;
    /* After a missed interval start from now, without bursting overdue notes. */
    uint64_t due = s->next;
    if (s->clock - due >= duration * s->params.gate / 100u)
        due = s->clock;
    s->next = due + duration;
    s->arp_off = due + duration * s->params.gate / 100u;
    stop(s, &s->arp_voice);
    TkPitch pitch = s->candidates[arp_index(s)];
    start(s, &s->arp_voice, pitch, TK_ARP);
    s->last_pitch = pitch.pitch;
    s->have_last = true;
}
