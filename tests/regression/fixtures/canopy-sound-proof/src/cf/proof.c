/* UNSAFE EMULATOR-ONLY EXPERIMENT. Sound+0x13a is a confirmed stock field:
 * the initializer writes it through 0x400e6464. This fixture corrupts that
 * field. Never install it on hardware or treat its results as compatibility.
 * This deliberately does not solve concurrent
 * pool access, allocation failure, stale handles after machine changes, save
 * failure propagation, or inactive-Sound edits. Never select it for hardware. */
#include <stdint.h>
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/instances.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"
#include "../../../../../../modules/core/adapters/instrument-play/src/cf/sound-state.h"
extern uint8_t *fw_alloc(uint32_t);
extern void fw_free(uint8_t *);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);

/* Exposed only for emulator diagnostics; not a production status ABI. */
volatile uint32_t canopy_sound_proof[8];
/* Only test/canopy-load-preflight selects the trampoline that uses these.
 * Diagnostic word 0 forces refusal; words 1-4 count starts, refusals,
 * consumed handles and completions; words 5-8 record pool count before and
 * after the load, returned unused credits and failed tail cancellation;
 * words 9-10 count an injected allocation failure and rollback frees.
 * Never enable this fixture on hardware. */
volatile uint32_t canopy_load_guard[11];
/* Zero in the ordinary proof. Only a disposable emulator test points this at
 * the optional memory-command fixture's UI-side scheduler. */
volatile uintptr_t canopy_memory_probe_entry;
static SpPool *pool;
static uint32_t guard_next,guard_end;
static int32_t inject_remaining=-1;
void canopy_proof_copy_source(uint32_t sound);
static uint32_t seen_sound[IP_INSTANCES],seen_generation[IP_INSTANCES];
static uint16_t seen_handle[IP_INSTANCES];
static void *allocate(uint32_t size,void *context)
{
    (void)context;
    if(inject_remaining==0) { ++canopy_load_guard[9]; return 0; }
    if(inject_remaining>0) --inject_remaining;
    return fw_alloc(size);
}
static void release(void *memory,void *context)
{ (void)context; ++canopy_load_guard[10]; fw_free((uint8_t *)memory); }
static int ensure_pool(void)
{
    if(pool) return 0;
    SpPool *p=(SpPool *)fw_alloc(sizeof(SpPool));
    if(!p) { ++canopy_sound_proof[5]; return -1; }
    sp_init(p,allocate,0); sp_set_deallocator(p,release); pool=p;
    return 0;
}
int canopy_proof_load_begin(void)
{
    uint16_t first;
    ++canopy_load_guard[1];
    if(canopy_load_guard[0]==1 || guard_end || ensure_pool()) {
        ++canopy_load_guard[2]; return -1;
    }
    canopy_load_guard[5]=pool->count;
    if(canopy_load_guard[0]==2) inject_remaining=1;
    int failed=sp_reserve_batch_task(pool,2192,&first);
    inject_remaining=-1;
    if(failed) {
        ++canopy_load_guard[2]; return -1;
    }
    guard_next=first; guard_end=(uint32_t)first+2192;
    return 0;
}
void canopy_proof_load_end(void)
{
    /* Only the still-unpublished tail can be returned. If any concurrent
     * allocation moved the tail, cancellation fails closed and the unused
     * reservations remain consumed. This does not solve concurrency. */
    if(pool && guard_next && guard_next<guard_end) {
        uint32_t unused=guard_end-guard_next;
        if(sp_cancel_reserved_tail(pool,(uint16_t)guard_next,unused)) ++canopy_load_guard[8];
        else canopy_load_guard[7]+=unused;
    }
    canopy_load_guard[6]=pool ? pool->count : 0;
    guard_next=0; guard_end=0; ++canopy_load_guard[4];
}
static volatile uint16_t *handle_at(uint32_t sound)
{ return (volatile uint16_t *)(uintptr_t)(sound+0x13a); }
/* Only the optional integrated emulator fixture uses these. Shared-pool
 * allocation must run in task context, not under the mapper lock. These do
 * not make load/copy/worker pool operations mutually safe in production. */
SpPool *canopy_proof_shared_pool(void)
{ return ensure_pool() ? 0 : pool; }
/* Caller holds mapper exclusion throughout validate -> publish -> attach. */
int canopy_proof_can_attach(uint32_t i,uint32_t sound,uint32_t gen,uint16_t expected)
{
    if(i>=IP_INSTANCES || !sound) return -1;
    volatile const uint32_t *a=&ip_admission[i*8];
    return a[0] && !a[6] && a[1]==sound && a[4]==gen &&
        *handle_at(sound)==expected ? 0 : -1;
}
void canopy_proof_attached(uint32_t i,uint32_t sound,uint32_t gen,uint16_t h)
{
    *handle_at(sound)=h;
    seen_sound[i]=sound; seen_generation[i]=gen; seen_handle[i]=h;
}
/* Decode immutable bytes outside the lock. Recheck identity and handle under
 * the same exclusion as edits, and update the seen marker before unlock.
 * This closes the stale decode/apply window for this emulator fixture only. */
int canopy_proof_try_restore(uint32_t i,uint32_t sound,uint32_t gen,uint16_t h)
{
    uint8_t bytes[SC_BYTES]; CpPreset p;
    if(!pool || !h || sp_read(pool,h,bytes) || sc_decode(&p,bytes)) return -1;
    uint32_t sr=dkrt_irq_mask();
    if(canopy_proof_can_attach(i,sound,gen,h)) { dkrt_irq_restore(sr); return -2; }
    int failed=instrument_sound_restore(i,sound,gen,&p);
    if(!failed) {
        seen_sound[i]=sound; seen_generation[i]=gen; seen_handle[i]=h;
        ++canopy_sound_proof[2];
    }
    dkrt_irq_restore(sr);
    return failed ? -1 : 0;
}
/* Runs under the same mapper/IRQ lock as the ownership check and application. */
static int claim_default(void *context)
{
    volatile uint16_t *handle=(volatile uint16_t *)context;
    if(*handle!=SP_PENDING_DEFAULT) return -1;
    *handle=0; return 0;
}
static void tick(void)
{
    if(canopy_memory_probe_entry)
        ((void (*)(void))canopy_memory_probe_entry)();
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        volatile const uint32_t *a=&ip_admission[i*8];
        uint32_t sound=a[1],gen=a[4];
        if(!a[0] || a[6] || !sound) continue;
        uint16_t h=*handle_at(sound);
        if(seen_sound[i]==sound && seen_generation[i]==gen && seen_handle[i]==h) continue;
        if(!h || h==SP_LOAD_ERROR) continue;
        if(h==SP_PENDING_DEFAULT) {
            uint32_t sr=dkrt_irq_mask();
            if(canopy_proof_can_attach(i,sound,gen,h) ||
               instrument_sound_proof_defaults(i,sound,gen,claim_default,(void *)handle_at(sound))) {
                dkrt_irq_restore(sr);
                ++canopy_sound_proof[6]; continue;
            }
            seen_sound[i]=sound; seen_generation[i]=gen; seen_handle[i]=0;
            dkrt_irq_restore(sr);
            continue;
        }
        int result=canopy_proof_try_restore(i,sound,gen,h);
        if(result==-1) ++canopy_sound_proof[6];
    }
}
/* Called only on the stock deserializer's successful exit. */
void canopy_proof_loaded(uint32_t sound,const uint8_t *record)
{
    ++canopy_sound_proof[0];
    instrument_sound_proof_set(tick);
    instrument_sound_proof_set_leave(canopy_proof_copy_source);
    if(!sound || !record) return;
    *handle_at(sound)=SP_PENDING_DEFAULT;
    CpPreset p; uint8_t bytes[SC_BYTES]; uint16_t h;
    if(sc_sound_record_read(&p,record,0x455)) return;
    int failed=ensure_pool() || sc_encode(&p,bytes);
    if(!failed && guard_end) {
        if(guard_next>=guard_end) failed=1;
        else {
            h=(uint16_t)guard_next;
            failed=sp_publish(pool,h,bytes);
            if(!failed) { ++guard_next; ++canopy_load_guard[3]; }
        }
    } else if(!failed) failed=sp_intern(pool,bytes,&h);
    if(failed) {
        *handle_at(sound)=SP_LOAD_ERROR;
        ++canopy_sound_proof[5]; return;
    }
    *handle_at(sound)=h;
    ++canopy_sound_proof[1];
}
/* Invoked after stock has serialized a Sound. For the selected four live
 * instances capture current settings; otherwise try the loaded handle. */
void canopy_proof_saved(uint8_t *record,uint32_t sound,uint32_t result)
{
    if(!(result&255u) || !record || !sound) return;
    ++canopy_sound_proof[3];
    CpPreset p; uint32_t gen;
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        if(!instrument_sound_snapshot(i,sound,&p,&gen)) {
            if(!sc_sound_record_write(record,0x455,&p)) ++canopy_sound_proof[4];
            else ++canopy_sound_proof[5];
            return;
        }
    }
    if(!pool) return;
    uint8_t bytes[SC_BYTES]; uint16_t h=*handle_at(sound);
    if(h && !sp_read(pool,h,bytes) && !sc_decode(&p,bytes) &&
        !sc_sound_record_write(record,0x455,&p)) ++canopy_sound_proof[4];
}

/* Called only by the separately selected emulator-only clipboard probe, on
 * the one observed full-Sound copy site. This proves a possible handoff, not
 * a safe implementation: allocation, reentrancy and failed-copy behavior
 * have not been solved. In particular stock memcpy cannot refuse a copy if
 * the snapshot cannot be allocated. */
void canopy_proof_copy_source(uint32_t sound)
{
    CpPreset p; uint8_t bytes[SC_BYTES]; uint16_t h; uint32_t generation;
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        if(instrument_sound_snapshot(i,sound,&p,&generation)) continue;
        if(ensure_pool() || sc_encode(&p,bytes) || sp_intern(pool,bytes,&h)) {
            ++canopy_sound_proof[5]; return;
        }
        *handle_at(sound)=h;
        /* Do not reapply our own snapshot to the still-playing source. */
        seen_sound[i]=sound; seen_generation[i]=generation; seen_handle[i]=h;
        ++canopy_sound_proof[7];
        return;
    }
}

/* Only the separately selected emulator bulk-copy hook calls this. Capture
 * admitted Sounds inside the observed DTII kit source before stock copies
 * the kit. This does not cover other owners or make allocation safe. */
void canopy_proof_copy_kit(uint32_t kit)
{
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        volatile const uint32_t *a=&ip_admission[i*8];
        uint32_t sound=a[1], delta=sound-(kit+0x34u);
        if(a[0] && delta<16u*0x450u && !(delta%0x450u))
            canopy_proof_copy_source(sound);
    }
}
