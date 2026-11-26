#include "track-pattern-fw.h"
#include "track-memory.h"
#include <dk/platform/ui.h>

enum {
    NATIVE_TRACK_BYTES = 0x6a5,
    NATIVE_NOTES = 0x6a50,
    NATIVE_LOCKS = 0x17e50,
    NATIVE_LOCK_BYTES = 0x102,
    NATIVE_LOCK_COUNT = 80,
    NATIVE_CLIP_STEP = 0x48,
    NATIVE_PATTERN_SCALE = 0x1db74,
    UNDO_BLOCKS = 6000,
};

typedef struct {
    uintptr_t object, owner;
} StockClip;

typedef struct {
    const int32_t *begin, *end, *capacity;
} StockSteps;

typedef struct {
    uint32_t pattern, data;
    uint8_t track;
} Location;

typedef struct {
    StockClip owner;
    TkStepAttachment attachments[TK_STEPS];
    uint8_t count;
} Copied;

typedef struct {
    StockClip source, before;
    Location location;
    uint32_t seal, time, revision;
    uint8_t operation, anchor, count;
    int32_t steps[TK_STEPS];
    TkStepAttachment attachments[TK_STEPS];
} Undo;

typedef struct {
    Copied copied;
    Undo undo;
} Clipboard;

enum { PASTE = 1, CLEAR };

extern uint32_t dkrt_blocks(void);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
extern dk_ui *dk_ui_core_handle(void);
extern void *tk_fw_alloc(uint32_t bytes);
extern uintptr_t *tk_stock_step_clipboard(void);
extern void tk_stock_step_clip_get(StockClip *out);
extern void tk_stock_step_ref_copy(uintptr_t *target, const uintptr_t *source);
extern void tk_stock_step_ref_drop(uintptr_t *ref);
extern void tk_stock_step_clip_set(uintptr_t *clipboard, const StockClip *clip, uint32_t slot);
extern int32_t tk_stock_step_length(uintptr_t track);
extern int32_t tk_stock_step_copy(uintptr_t track, const StockSteps *steps);
extern int32_t tk_stock_step_paste(uintptr_t track, int32_t step);
extern int32_t tk_stock_step_clear(uintptr_t track, const StockSteps *steps);
extern void tk_stock_step_toggle(uintptr_t track, int32_t step, uint32_t enabled);

static Clipboard *state;
static unsigned mutation;

volatile uint32_t tk_step_clip_info[4]; /* copies, pastes, clears, undos */

static Location locate(uintptr_t descriptor)
{
    typedef uint32_t (*TrackData)(uintptr_t);
    const uintptr_t *object = (const uintptr_t *)descriptor;
    const TrackData *vtable = (const TrackData *)*object;
    Location result = {.data = vtable[0x28 / sizeof(uintptr_t)](descriptor)};
    result.pattern = tk_pattern_fw_track_base(result.data, &result.track);
    if (result.track != object[0xf])
        result.pattern = 0;
    return result;
}

static uint16_t read16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] << 8 | p[1]);
}

static bool has_trig(Location location, uint8_t step)
{
    const uint8_t *native = (const uint8_t *)(uintptr_t)location.data;
    return (native[step * 2u + 1u] & 1u) != 0; /* Note or lock trig. */
}

static uint8_t vector(const StockSteps *input, int32_t steps[TK_STEPS], uint8_t max)
{
    if (!input || !input->begin || input->end <= input->begin ||
        input->end - input->begin > TK_STEPS)
        return 0;
    uint8_t count = (uint8_t)(input->end - input->begin);
    for (uint8_t i = 0; i < count; ++i) {
        if (input->begin[i] < 0 || input->begin[i] >= max)
            return 0;
        steps[i] = input->begin[i];
    }
    return count;
}

static uint8_t destinations(StockClip clip, int32_t anchor, int32_t length, int32_t steps[TK_STEPS],
                            int32_t origins[TK_STEPS], uintptr_t *rows)
{
    if (!clip.object || length < 1 || length > TK_STEPS || anchor < 0 || anchor >= length)
        return 0;
    const uintptr_t *vec = (const uintptr_t *)clip.object;
    uintptr_t begin = vec[1], end = vec[2];
    if (!begin || end <= begin || end - begin > TK_STEPS * NATIVE_CLIP_STEP ||
        (end - begin) % NATIVE_CLIP_STEP)
        return 0;
    int32_t first = *(const int32_t *)begin;
    *rows = begin;
    uint8_t count = 0;
    for (uintptr_t row = begin; row < end; row += NATIVE_CLIP_STEP) {
        const uint8_t *flags = (const uint8_t *)(row + 4u);
        if (!flags[0] && !flags[1])
            return 0;
        int32_t original = *(const int32_t *)row;
        int32_t step = (original + anchor - first) % length;
        if (step < 0)
            return 0;
        origins[count] = original;
        steps[count++] = step;
    }
    return count;
}

static StockClip retain(const uintptr_t *slot)
{
    StockClip holder = {.object = slot[0]};
    tk_stock_step_ref_copy(&holder.owner, slot + 1);
    return holder;
}

static void drop(StockClip *clip)
{
    if (clip->owner)
        tk_stock_step_ref_drop(&clip->owner);
    *clip = (StockClip){0};
}

static void drop_undo(void)
{
    if (state) {
        drop(&state->undo.source);
        drop(&state->undo.before);
        state->undo.operation = 0;
    }
}

static bool ensure_state(void)
{
    if (!state) {
        state = tk_fw_alloc(sizeof *state);
        if (state)
            memset(state, 0, sizeof *state);
    }
    return state != NULL;
}

static bool attachment(Location location, uint8_t step, TkStepAttachment *value)
{
    uint32_t sr = dkrt_irq_mask();
    bool found = tk_pattern_step_attachment(tk_pattern_fw_peek(location.pattern), location.track,
                                            step, value);
    dkrt_irq_restore(sr);
    return found;
}

static enum TkAttachmentResult batch(Location location, const int32_t *steps, uint8_t count,
                                     const TkStepAttachment *values, bool commit, bool *changed)
{
    uint32_t sr = dkrt_irq_mask();
    enum TkAttachmentResult result =
        tk_pattern_attach_batch(tk_pattern_fw_peek(location.pattern), location.track, steps, count,
                                values, commit, changed);
    dkrt_irq_restore(sr);
    return result;
}

static int32_t feedback(const char *message)
{
    dk_ui_toast(dk_ui_core_handle(), message);
    /* The stock UI announces only a positive paste or clear count. */
    return -1;
}

static int32_t refused(enum TkAttachmentResult result)
{
    return feedback(result == TK_ATTACH_FULL ? "TRIG ATTACHMENTS FULL" : "TRIG POSITIONS OVERLAP");
}

static uint32_t hash_bytes(uint32_t hash, const void *data, unsigned size)
{
    const uint8_t *p = data;
    while (size--)
        hash = (hash ^ *p++) * 16777619u;
    return hash;
}

static uint32_t seal(Location location, const int32_t *steps, uint8_t count)
{
    const uint8_t *pattern = (const uint8_t *)(uintptr_t)location.pattern;
    const uint8_t *notes = pattern + NATIVE_NOTES;
    const uint8_t *locks = pattern + NATIVE_LOCKS;
    uint32_t hash =
        hash_bytes(2166136261u, (const void *)(uintptr_t)location.data, NATIVE_TRACK_BYTES);
    hash = hash_bytes(hash, pattern + NATIVE_PATTERN_SCALE, 12);
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t step = (uint8_t)steps[i];
        for (unsigned j = 0; j < NATIVE_LOCK_COUNT; ++j) {
            const uint8_t *lock = locks + j * NATIVE_LOCK_BYTES;
            if (lock[0] != 0xff && lock[1] == location.track) {
                hash = hash_bytes(hash, lock, 2);
                hash = hash_bytes(hash, lock + 2 + 2 * step, 2);
            }
        }
        uint16_t note = read16(notes + 0xc000u + 2u * (location.track * TK_STEPS + step));
        for (unsigned n = 0; note < 8192u && n < 128u; ++n) {
            hash = hash_bytes(hash, notes + 6u * note, 6);
            note = read16(notes + 0xd000u + 2u * note);
        }
        TkStepAttachment value;
        if (attachment(location, step, &value))
            hash = hash_bytes(hash, &value, sizeof value);
    }
    return hash;
}

static bool snapshot(uintptr_t descriptor, const StockSteps *steps, StockClip *before)
{
    uintptr_t *root = tk_stock_step_clipboard();
    StockClip clipboard = retain(root + 1), stock_undo = retain(root + 3);
    int32_t copied = tk_stock_step_copy(descriptor, steps);
    bool complete = copied == steps->end - steps->begin;
    if (complete)
        *before = retain(root + 1);
    tk_stock_step_clip_set(root, &clipboard, 0);
    tk_stock_step_clip_set(root, &stock_undo, 1);
    drop(&clipboard);
    drop(&stock_undo);
    return complete;
}

static bool prepare_undo(uintptr_t descriptor, Location location, uint8_t operation, uint8_t anchor,
                         const int32_t *steps, uint8_t count)
{
    drop_undo();
    if (!ensure_state())
        return false;
    Undo *undo = &state->undo;
    int32_t native[TK_STEPS];
    uint8_t stock_count = 0, saved = 0;
    uint32_t seen[4] = {0};
    for (uint8_t i = 0; i < count; ++i) {
        uint8_t step = (uint8_t)steps[i];
        uint32_t bit = 1u << (step % 32u);
        if (seen[step / 32u] & bit)
            continue;
        seen[step / 32u] |= bit;
        attachment(location, step, &undo->attachments[saved]);
        undo->steps[saved++] = step;
        if (has_trig(location, step))
            native[stock_count++] = step;
    }
    StockSteps previous = {native, native + stock_count, native + stock_count};
    if (stock_count && !snapshot(descriptor, &previous, &undo->before))
        return false;
    undo->source = retain(tk_stock_step_clipboard() + 1);
    undo->location = location;
    undo->operation = operation;
    undo->anchor = anchor;
    undo->count = saved;
    return true;
}

static void finish_undo(void)
{
    Undo *undo = &state->undo;
    undo->seal = seal(undo->location, undo->steps, undo->count);
    undo->time = dkrt_blocks();
    undo->revision = tk_pattern_fw_revision();
}

static void restore_native(uintptr_t descriptor, const Undo *undo, uintptr_t *root)
{
    StockSteps selected = {undo->steps, undo->steps + undo->count, undo->steps + undo->count};
    ++mutation;
    tk_stock_step_clear(descriptor, &selected);
    for (uint8_t i = 0; i < undo->count; ++i)
        tk_stock_step_toggle(descriptor, undo->steps[i], 0);
    if (undo->before.object) {
        const uintptr_t *vec = (const uintptr_t *)undo->before.object;
        int32_t first = *(const int32_t *)vec[1];
        tk_stock_step_clip_set(root, &undo->before, 0);
        tk_stock_step_paste(descriptor, first);
        tk_stock_step_clip_set(root, &undo->source, 0);
    }
    --mutation;
}

static int32_t restore(uintptr_t descriptor, Location location, uint8_t operation, uint8_t anchor,
                       const int32_t *steps, uint8_t count)
{
    if (!state)
        return 0;
    Undo *undo = &state->undo;
    if (undo->operation != operation || undo->location.data != location.data ||
        undo->anchor != anchor || undo->revision != tk_pattern_fw_revision() ||
        dkrt_blocks() - undo->time >= UNDO_BLOCKS)
        return 0;
    uintptr_t *root = tk_stock_step_clipboard();
    if (root[1] != undo->source.object || root[2] != undo->source.owner)
        return 0;
    if (operation == CLEAR) {
        if (count != undo->count)
            return 0;
        for (uint8_t i = 0; i < count; ++i)
            if (steps[i] != undo->steps[i])
                return 0;
    }
    if (seal(location, undo->steps, undo->count) != undo->seal)
        return 0;

    bool changed;
    enum TkAttachmentResult result =
        batch(location, undo->steps, undo->count, undo->attachments, true, &changed);
    if (result != TK_ATTACH_OK)
        return refused(result);
    restore_native(descriptor, undo, root);
    tk_pattern_fw_changed();
    drop_undo();
    ++tk_step_clip_info[3];
    return feedback(operation == PASTE ? "UNDO TRIG PASTE" : "UNDO TRIG CLEAR");
}

int32_t tk_step_copy(uintptr_t descriptor, const StockSteps *input)
{
    Location location = locate(descriptor);
    TkPattern *pattern = location.pattern ? tk_pattern_fw_peek(location.pattern) : NULL;
    int32_t steps[TK_STEPS];
    uint8_t count =
        tk_pattern_has_track(pattern, location.track) ? vector(input, steps, TK_STEPS) : 0;
    bool present = false;
    TkStepAttachment candidate;
    for (uint8_t i = 0; !present && i < count; ++i) {
        uint8_t step = (uint8_t)steps[i];
        present = has_trig(location, step) && attachment(location, step, &candidate);
    }
    if (present && !ensure_state())
        return feedback("TRIG SNAPSHOT UNAVAILABLE");

    int32_t copied = tk_stock_step_copy(descriptor, input);
    if (copied <= 0)
        return copied;
    drop_undo();
    if (state) {
        drop(&state->copied.owner);
        state->copied.count = 0;
    }
    if (!present)
        return copied;
    uint8_t at = 0;
    for (uint8_t i = 0; i < count; ++i)
        if (has_trig(location, (uint8_t)steps[i]))
            attachment(location, (uint8_t)steps[i], &state->copied.attachments[at++]);
    if (at == copied) {
        state->copied.owner = retain(tk_stock_step_clipboard() + 1);
        state->copied.count = at;
        ++tk_step_clip_info[0];
    }
    return copied;
}

int32_t tk_step_paste(uintptr_t descriptor, int32_t anchor)
{
    Location location = locate(descriptor);
    if (!location.pattern || anchor < 0 || anchor >= TK_STEPS)
        return tk_stock_step_paste(descriptor, anchor);
    int32_t restored = restore(descriptor, location, PASTE, (uint8_t)anchor, NULL, 0);
    if (restored)
        return restored;
    StockClip native = {0};
    tk_stock_step_clip_get(&native);
    int32_t steps[TK_STEPS], origins[TK_STEPS];
    uintptr_t rows = 0;
    uint8_t count =
        destinations(native, anchor, tk_stock_step_length(descriptor), steps, origins, &rows);
    bool incoming = state && state->copied.count && native.owner == state->copied.owner.owner;
    drop(&native);
    if (incoming && count != state->copied.count)
        return feedback("TRIG PASTE UNAVAILABLE");
    if (!count)
        return tk_stock_step_paste(descriptor, anchor);
    if (incoming && !tk_pattern_fw_get(location.pattern))
        return refused(TK_ATTACH_FULL);

    const TkStepAttachment *values = incoming ? state->copied.attachments : NULL;
    bool changed;
    enum TkAttachmentResult fit = batch(location, steps, count, values, false, &changed);
    if (fit != TK_ATTACH_OK)
        return refused(fit);
    if (changed && !prepare_undo(descriptor, location, PASTE, (uint8_t)anchor, steps, count))
        return feedback("TRIG SNAPSHOT UNAVAILABLE");
    if (!changed)
        drop_undo();
    ++mutation;
    int32_t pasted = tk_stock_step_paste(descriptor, anchor);
    --mutation;
    if (!changed)
        return pasted;
    /* The retained clipboard entries keep their source spacing after wrap. */
    for (uint8_t i = 0; i < count; ++i)
        *(int32_t *)(rows + i * NATIVE_CLIP_STEP) = origins[i];
    fit =
        pasted == count ? batch(location, steps, count, values, true, &changed) : TK_ATTACH_INVALID;
    if (fit != TK_ATTACH_OK) {
        restore_native(descriptor, &state->undo, tk_stock_step_clipboard());
        drop_undo();
        return feedback("TRIG PASTE UNAVAILABLE");
    }
    tk_pattern_fw_changed();
    ++tk_step_clip_info[1];
    finish_undo();
    return pasted;
}

int32_t tk_step_clear(uintptr_t descriptor, const StockSteps *input)
{
    Location location = locate(descriptor);
    int32_t steps[TK_STEPS];
    uint8_t count = location.pattern ? vector(input, steps, TK_STEPS) : 0;
    if (!count)
        return tk_stock_step_clear(descriptor, input);
    int32_t restored = restore(descriptor, location, CLEAR, (uint8_t)steps[0], steps, count);
    if (restored)
        return restored;
    bool changed;
    enum TkAttachmentResult fit = batch(location, steps, count, NULL, false, &changed);
    if (fit != TK_ATTACH_OK)
        return refused(fit);
    if (changed && !prepare_undo(descriptor, location, CLEAR, (uint8_t)steps[0], steps, count))
        return feedback("TRIG SNAPSHOT UNAVAILABLE");
    if (!changed)
        drop_undo();
    ++mutation;
    int32_t cleared = tk_stock_step_clear(descriptor, input);
    --mutation;
    if (!changed)
        return cleared;
    fit =
        cleared == count ? batch(location, steps, count, NULL, true, &changed) : TK_ATTACH_INVALID;
    if (fit != TK_ATTACH_OK) {
        restore_native(descriptor, &state->undo, tk_stock_step_clipboard());
        drop_undo();
        return feedback("TRIG CLEAR UNAVAILABLE");
    }
    tk_pattern_fw_changed();
    ++tk_step_clip_info[2];
    finish_undo();
    return cleared;
}

void tk_step_toggle(uintptr_t descriptor, int32_t step, uint32_t enabled)
{
    tk_stock_step_toggle(descriptor, step, enabled);
    if (mutation || (enabled & 0xffu) || step < 0 || step >= TK_STEPS)
        return;
    Location location = locate(descriptor);
    bool changed;
    if (location.pattern && !has_trig(location, (uint8_t)step) &&
        batch(location, &step, 1, NULL, true, &changed) == TK_ATTACH_OK && changed) {
        drop_undo();
        tk_pattern_fw_changed();
    }
}
