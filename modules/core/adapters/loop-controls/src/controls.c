/* UI-task adapter: run before fxpage::tick, never from the audio ISR.
 * The kit owns the visible knobs; writing DSP parameters alone is insufficient.
 * Only the loaded Overloop row is eligible. No callbacks into module memory.
 */
typedef unsigned int u32;
typedef unsigned short u16;
typedef unsigned char u8;
extern const u32 fxpages_info[];
extern volatile u32 pp_position[];
extern u32 pp_current_pattern(void);
#define U32(a) (*(volatile u32 *)(a))
#define OVERLOOP_ID 0xca1bu /* register::effect_id("digikit/overloop", "loop") */

static u32 have, row, pattern, target, pending, last_array, last_load;
static u32 awaiting_load, handoff_boundary;
static u16 source[4];
static u16 handoff[4];

/* Explicit arguments make the handoff state machine executable in CF tests.
 * The two identities distinguish scheduling from audible commitment. */
void lc_poll(u32 selected, u32 committed, u32 array, u32 load, u32 slot,
             const u8 *indices, u32 boundary) {
    volatile u16 *values = (volatile u16 *)array;
    if (!have || row != slot || !selected) {
        have = selected != 0;
        row = slot;
        pattern = target = selected;
        pending = 0;
        awaiting_load = 0;
        last_array = array;
        last_load = load;
        for (u32 j = 0; j < 4; ++j) source[j] = values[indices[j]];
        return;
    }
    if (selected != pattern) {
        target = selected;
        pending = 1;
        awaiting_load = 0;
    }
    if (pending) {
        if (selected == pattern) { /* cancelled selection */
            pending = 0;
        } else if (committed == target) {
            values[indices[1]] = source[1];
            values[indices[3]] = source[3];
            values[indices[2]] = source[0] >= 0x3f80 ? 0x7f00 : 0;
            values[indices[0]] = 0;
            for (u32 j = 0; j < 4; ++j) handoff[j] = values[indices[j]];
            awaiting_load = array == last_array && load == last_load;
            handoff_boundary = boundary;
            pattern = target;
            pending = 0;
        } else {
            /* Destination values may be visible before its first audible step.
             * Keep the source snapshot until that step commits. */
            return;
        }
    }
    /* A kit load completing after the boundary must not undo the handoff.
     * Stop guarding at the next step, so a later manual kit load is ordinary. */
    if (awaiting_load) {
        if (boundary != handoff_boundary) awaiting_load = 0;
        else if (array != last_array || load != last_load) {
            for (u32 j = 0; j < 4; ++j) values[indices[j]] = handoff[j];
            awaiting_load = 0;
        }
    }
    last_array = array;
    last_load = load;
    for (u32 j = 0; j < 4; ++j) source[j] = values[indices[j]];
}

void fx_controls_tick(void) {
    const u32 *info = fxpages_info;
    if (info[0] != 0x46585047 || info[2] != 1) return;
    u32 kit = U32(info[19]);
    if (kit < 0x40000000 || kit >= 0x48000000 || (kit & 1)) return;
    u32 array = kit + info[14];
    volatile u16 *values = (volatile u16 *)array;
    if (values[info[37]] != info[38]) return; /* runtime seeds it first */
    volatile u16 *ids = (volatile u16 *)info[28];
    for (u32 k = 0; k < info[6] && k < 4; ++k) {
        if (ids[k] != OVERLOOP_ID) continue;
        u32 selected = pp_current_pattern();
        u32 committed = pp_position[14] == 1 ? pp_position[6] : selected;
        lc_poll(selected, committed, array, U32(info[31]),
                k, (const u8 *)info + 80 + 8 * k, pp_position[3]);
        return;
    }
    have = pending = 0; /* unload/reload never inherits another instance's cue */
}
