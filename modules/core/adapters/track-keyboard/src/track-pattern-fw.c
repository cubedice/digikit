#include "track-pattern-fw.h"

#include "dkrt.h"
#include "track-memory.h"
#include "track-record.h"

enum {
    PATTERNS = 128,
    TEMPORARIES = 16,
    PATTERN_BYTES = 0x1db8c,
    TRACK_BYTES = 0x6a5,
    PROJECT_PATTERNS = 0x41776cb8,
};

typedef struct {
    uint32_t address, age, seal, bytes;
    TkPattern *pattern;
} Temporary;

extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t sr);
extern void *tk_fw_alloc(uint32_t size);
extern void tk_fw_free(void *memory);
extern uintptr_t tk_fw_project(void);

static TkPattern *pattern_slots[PATTERNS];
static Temporary temporary[TEMPORARIES];
static uint8_t *record_bytes;
static volatile uint32_t revision;
static uint32_t age;

volatile uint32_t tk_storage_info[5]; /* loads, saves, copies, full records, allocation failures */

static int slot(uint32_t address)
{
    uint32_t offset = address - PROJECT_PATTERNS;
    if (offset >= PATTERNS * PATTERN_BYTES || offset % PATTERN_BYTES)
        return -1;
    return (int)(offset / PATTERN_BYTES);
}

static uint32_t seal(uint32_t address, uint32_t bytes)
{
    const uint8_t *native = (const uint8_t *)(uintptr_t)address;
    uint32_t h = 2166136261u;
    for (unsigned i = 0; i < 64; ++i)
        h = (h ^ native[i]) * 16777619u;
    for (unsigned i = bytes - 44; i < bytes - 4; ++i)
        h = (h ^ native[i]) * 16777619u;
    return h;
}

static bool temporary_address(uint32_t address, uint32_t bytes)
{
    return address >= 0x40000000u && address <= 0x48000000u - bytes &&
           (bytes == TRACK_BYTES || !(address & 1u));
}

static Temporary *find_temporary(uint32_t address)
{
    for (unsigned i = 0; i < TEMPORARIES; ++i)
        if (temporary[i].address == address)
            return &temporary[i];
    return NULL;
}

static TkPattern *lookup(uint32_t address, uint32_t bytes)
{
    int index = bytes == PATTERN_BYTES ? slot(address) : -1;
    if (index >= 0)
        return pattern_slots[index];
    Temporary *t = find_temporary(address);
    return t && t->bytes == bytes && t->seal == seal(address, bytes) ? t->pattern : NULL;
}

static void *allocate(uint32_t size)
{
    uint32_t sr = dkrt_irq_mask();
    dkrt_irq_restore(sr);
    if (sr & 0x0700u)
        return NULL;
    void *p = tk_fw_alloc(size);
    if (!p)
        ++tk_storage_info[4];
    return p;
}

TkPattern *tk_pattern_fw_get(uint32_t address)
{
    int index = slot(address);
    if (index < 0)
        return NULL;
    if (pattern_slots[index])
        return pattern_slots[index];
    TkPattern *p = allocate(sizeof *p);
    if (!p)
        return NULL;
    tk_pattern_init(p);
    uint32_t sr = dkrt_irq_mask();
    if (!pattern_slots[index]) {
        pattern_slots[index] = p;
        p = NULL;
    }
    TkPattern *result = pattern_slots[index];
    dkrt_irq_restore(sr);
    if (p)
        tk_fw_free(p);
    return result;
}

uint32_t tk_pattern_fw_revision(void)
{
    return revision;
}

uint32_t tk_pattern_fw_selected(void)
{
    uintptr_t project = tk_fw_project();
    uint32_t index = project ? *(const volatile uint32_t *)(project + 0x138u) : UINT32_MAX;
    return index < PATTERNS ? PROJECT_PATTERNS + index * PATTERN_BYTES : 0;
}

static TkPattern *destination(uint32_t address, uint32_t bytes)
{
    if (bytes == PATTERN_BYTES && slot(address) >= 0)
        return tk_pattern_fw_get(address);
    if (!temporary_address(address, bytes))
        return NULL;
    Temporary *t = find_temporary(address);
    if (!t) {
        t = &temporary[0];
        for (unsigned i = 1; i < TEMPORARIES; ++i)
            if (age - temporary[i].age >= age - t->age || !temporary[i].address)
                t = &temporary[i];
    }
    if (!t->pattern)
        t->pattern = allocate(sizeof *t->pattern);
    if (!t->pattern)
        return NULL;
    if (bytes == TRACK_BYTES && (t->address != address || t->bytes != bytes))
        tk_pattern_init(t->pattern);
    t->address = address;
    t->age = ++age;
    t->seal = seal(address, bytes);
    t->bytes = bytes;
    return t->pattern;
}

void tk_pattern_fw_reset(uint32_t address)
{
    uint32_t sr = dkrt_irq_mask();
    int index = slot(address);
    if (index >= 0) {
        if (pattern_slots[index]) {
            tk_pattern_init(pattern_slots[index]);
            ++revision;
        }
    } else {
        Temporary *t = find_temporary(address);
        if (t)
            t->address = 0;
    }
    dkrt_irq_restore(sr);
}

static uint32_t track_base(uint32_t address, uint8_t *track)
{
    uint32_t offset = address - PROJECT_PATTERNS;
    if (offset >= PATTERNS * PATTERN_BYTES)
        return 0;
    uint32_t position = offset % PATTERN_BYTES;
    if (position >= TK_TRACKS * TRACK_BYTES || position % TRACK_BYTES)
        return 0;
    *track = (uint8_t)(position / TRACK_BYTES);
    return address - position;
}

void tk_pattern_fw_track_clear(uint32_t track, uint32_t address)
{
    uint8_t known;
    uint32_t base = track_base(address, &known);
    if (!base || known != track)
        return;
    uint32_t sr = dkrt_irq_mask();
    TkPattern *p = lookup(base, PATTERN_BYTES);
    if (tk_pattern_has_track(p, known)) {
        tk_pattern_copy_track(p, known, NULL, 0);
        ++revision;
    }
    dkrt_irq_restore(sr);
}

static void track_copied(uint32_t to, uint32_t from)
{
    uint8_t source_track = 0, target_track = 0;
    uint32_t source_base = track_base(from, &source_track);
    uint32_t target_base = track_base(to, &target_track);
    if (!source_base && !target_base)
        return;
    const TkPattern *src =
        source_base ? lookup(source_base, PATTERN_BYTES) : lookup(from, TRACK_BYTES);
    if (!tk_pattern_has_track(src, source_track)) {
        if (target_base) {
            tk_pattern_fw_track_clear(target_track, to);
        } else {
            Temporary *t = find_temporary(to);
            if (t)
                t->address = 0;
        }
        return;
    }
    TkPattern *dst =
        target_base ? destination(target_base, PATTERN_BYTES) : destination(to, TRACK_BYTES);
    if (!dst)
        return;
    uint32_t sr = dkrt_irq_mask();
    if (tk_pattern_copy_track(dst, target_track, src, source_track)) {
        ++tk_storage_info[2];
    } else {
        /* Stock has completed the paste. Discard the superseded attachment
         * if the new track exceeds the destination pattern's bounded storage.
         */
        tk_pattern_copy_track(dst, target_track, NULL, 0);
        ++tk_storage_info[3];
    }
    if (target_base)
        ++revision;
    dkrt_irq_restore(sr);
}

void tk_pattern_copied(uint32_t to, uint32_t from, uint32_t bytes)
{
    if (from == to)
        return;
    if (bytes == TRACK_BYTES) {
        track_copied(to, from);
        return;
    }
    if (bytes != PATTERN_BYTES)
        return;
    TkPattern *src = lookup(from, PATTERN_BYTES);
    if (!src || tk_pattern_encoded_size(src) == TK_PATTERN_HEADER) {
        tk_pattern_fw_reset(to);
        return;
    }
    TkPattern *dst = destination(to, PATTERN_BYTES);
    if (!dst || dst == src)
        return;
    uint32_t sr = dkrt_irq_mask();
    *dst = *src;
    if (slot(to) >= 0)
        ++revision;
    ++tk_storage_info[2];
    dkrt_irq_restore(sr);
}

void tk_pattern_cleared(uint32_t address, uint32_t bytes)
{
    if (bytes == PATTERN_BYTES)
        tk_pattern_fw_reset(address);
}

static bool ensure_record(void)
{
    if (record_bytes)
        return true;
    uint8_t *memory = allocate(TK_PATTERN_BYTES);
    if (!memory)
        return false;
    uint32_t sr = dkrt_irq_mask();
    if (!record_bytes) {
        record_bytes = memory;
        memory = NULL;
    }
    dkrt_irq_restore(sr);
    if (memory)
        tk_fw_free(memory);
    return true;
}

uint32_t tk_pattern_fw_save(uint8_t *record, uint32_t address, uint32_t result)
{
    if (!(result & 0xffu))
        return result;
    const TkPattern *p = lookup(address, PATTERN_BYTES);
    if (p && !ensure_record())
        return 0;
    uint32_t sr = dkrt_irq_mask();
    size_t size = p ? tk_pattern_encoded_size(p) : 0;
    if (size == TK_PATTERN_HEADER)
        size = 0;
    bool valid = (!size || tk_pattern_encode(p, record_bytes, size)) &&
                 tk_record_write(record, record_bytes, size);
    if (!valid)
        ++tk_storage_info[3];
    else if (size)
        ++tk_storage_info[1];
    dkrt_irq_restore(sr);
    return valid ? result : 0;
}

void tk_pattern_fw_load(uint32_t address, const uint8_t *record, uint32_t result)
{
    if (!(result & 0xffu))
        return;
    if (!tk_bytes_equal(record + TK_NATIVE_TAIL_OFFSET, "TKR1", 4) || !ensure_record()) {
        tk_pattern_fw_reset(address);
        return;
    }
    TkPattern *p = destination(address, PATTERN_BYTES);
    if (!p)
        return;
    uint32_t sr = dkrt_irq_mask();
    size_t size = tk_record_read(record, record_bytes, TK_PATTERN_BYTES);
    if (size && tk_pattern_decode(p, record_bytes, size)) {
        ++tk_storage_info[0];
        if (slot(address) >= 0)
            ++revision;
    } else {
        tk_pattern_fw_reset(address);
    }
    dkrt_irq_restore(sr);
}
