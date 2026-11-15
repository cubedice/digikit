/* Host-only boundary-capture experiment. Not a firmware memcpy replacement.
 * This test models exactly two admitted Sounds with verified lifetimes, a
 * common IRQ lock, and infallible copies; it deliberately rejects partial
 * handle reads and destination overwrites. Real firmware does not yet provide
 * those guarantees or a complete boundary inventory. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef keyboard
#include <assert.h>
#include <stdlib.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-pool.h"

enum { SOUND_BYTES=0x450, HANDLE=0x13a, COUNT=2, ARENA_BYTES=0x1000 };
typedef struct {
    uint8_t *sound;
    uint32_t id,generation,instance;
    uint16_t handle,reserved;
    int dirty;
    uint8_t staged[SC_BYTES];
} Active;
typedef struct {
    SpPool pool;
    Active active[COUNT];
    int attempt_interrupt,pending;
} Harness;

static void *alloc_slab(uint32_t bytes,void *unused)
{ (void)unused; assert(!masked); return calloc(1,bytes); }
static void free_slab(void *p,void *unused) { (void)unused; free(p); }
static void write_handle(uint8_t *p,uint16_t v) { memcpy(p,&v,sizeof(v)); }
static uint16_t read_handle(const uint8_t *p) { uint16_t v; memcpy(&v,p,sizeof(v)); return v; }

/* Encoder preview is the real instrument_sound_proof_edit transaction; a
 * successful edit only marks this Sound dirty, leaving its published handle
 * untouched. One reservation is needed before accepting the first edit. */
static int prepare(void *ctx,const CpPreset *p)
{
    Active *a=ctx; uint8_t b[SC_BYTES]; assert(masked);
    return !a->reserved || sc_encode(p,b);
}
static void attach(void *ctx) { Active *a=ctx; assert(masked); a->dirty=1; }
static int edit(Active *a,int detents)
{
    instrument_select_track(a->instance);
    uint32_t v=instrument_src_context(a->instance,0,1);
    assert(v);
    return instrument_sound_proof_edit(a->instance,a->id,a->generation,v,3,detents,100,prepare,attach,a);
}

/* In this model reserved handles are exclusively owned by their Active, and
 * the publication, Sound bytes and stock copy cannot fail after preflight. */
static int overlaps(uintptr_t a,size_t an,uintptr_t b,size_t bn)
{ return a<b+bn && b<a+an; }
static int guarded_copy(Harness *h,void *destination,const void *source,size_t n)
{
    uintptr_t s=(uintptr_t)source,d=(uintptr_t)destination;
    if(!destination || !source || s>UINTPTR_MAX-n || d>UINTPTR_MAX-n ||
       (n && overlaps(s,n,d,n))) return -1;
    uint32_t sr=dkrt_irq_mask();
    int hit[COUNT]={0};
    for(unsigned i=0;i<COUNT;++i) {
        Active *a=&h->active[i]; uintptr_t hp=(uintptr_t)(a->sound+HANDLE);
        if(n && overlaps(d,n,(uintptr_t)a->sound,SOUND_BYTES)) goto reject;
        if(!n || !overlaps(s,n,hp,2)) continue;
        /* Never pretend separate byte copies are an atomic Sound copy. */
        if(s>hp || s+n<hp+2 || !sound_state_owned(a->instance,a->id,a->generation) ||
           read_handle(a->sound+HANDLE)!=a->handle) goto reject;
        hit[i]=1;
        if(a->dirty) {
            CpPreset p;
            if(!a->reserved) goto reject;
            preset_snapshot(&inst[a->instance],&p);
            if(sc_encode(&p,a->staged)) goto reject;
        }
    }
    for(unsigned i=0;i<COUNT;++i) {
        Active *a=&h->active[i];
        if(hit[i] && a->dirty) {
            assert(!sp_publish(&h->pool,a->reserved,a->staged));
            a->handle=a->reserved; a->reserved=0; a->dirty=0;
            write_handle(a->sound+HANDLE,a->handle);
        }
    }
    /* Model an interrupt arriving during stock copy. It is delivered only
     * after restoration below, because the real scheduler is not exercised. */
    if(h->attempt_interrupt) { assert(masked); h->pending=1; }
    memcpy(destination,source,n);
    dkrt_irq_restore(sr); return 0;
reject:
    dkrt_irq_restore(sr); return -1;
}
static void verify_strum(Harness *h,uint16_t handle,uint16_t expected)
{
    uint8_t b[SC_BYTES]; CpPreset p;
    assert(!sp_read(&h->pool,handle,b) && !sc_decode(&p,b));
    assert(p.control[CP_STRUM]==expected);
}
static void reserve(Harness *h,Active *a)
{ assert(!masked && !a->reserved && !sp_reserve_task(&h->pool,&a->reserved)); }

int main(void)
{
    uint8_t arena[ARENA_BYTES]={0},out[ARENA_BYTES],before[ARENA_BYTES];
    Harness h={0}; sp_init(&h.pool,alloc_slab,0); sp_set_deallocator(&h.pool,free_slab);
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; mi_ledger.generations[1]=1;
    instrument_play_tick(); ip_tracks[1]=1; inst[1].track=1;
    for(unsigned i=0;i<COUNT;++i) {
        Active *a=&h.active[i]; a->sound=arena+0x40+i*SOUND_BYTES;
        a->instance=i; a->id=0x123400+0x1000*i; a->generation=7+i;
        volatile uint32_t *adm=&ip_admission[i*8];
        adm[0]=1; adm[1]=a->id; adm[4]=a->generation; adm[5]=admitted;
        inst[i].mode=inst[i].keyboard.mode=KB_ROOT;
        reserve(&h,a);
        CpPreset p; uint8_t b[SC_BYTES]; preset_snapshot(&inst[i],&p);
        assert(!sc_encode(&p,b) && !sp_publish(&h.pool,a->reserved,b));
        a->handle=a->reserved; a->reserved=0; write_handle(a->sound+HANDLE,a->handle);
    }
    Active *a=&h.active[0],*b=&h.active[1]; uint16_t original=a->handle;
    reserve(&h,a); assert(edit(a,1)==CS_EDIT && a->dirty && a->handle==original);
    /* Any number of real edits before capture reuse a single reservation. */
    assert(edit(a,1)==CS_EDIT && a->handle==original); verify_strum(&h,original,0);
    memset(out,0xa5,sizeof(out));
    assert(!guarded_copy(&h,out,arena,0x80));
    assert(a->dirty && a->handle==original && a->reserved);
    /* A one-byte overlap is refused without publication or destination writes. */
    memcpy(before,out,sizeof(out));
    assert(guarded_copy(&h,out,a->sound+HANDLE,1)<0 && !memcmp(before,out,sizeof(out)));
    assert(a->dirty && a->handle==original);
    /* Minimal complete handle subrange publishes the latest real UI edit. */
    assert(!guarded_copy(&h,out,a->sound+HANDLE,2));
    uint16_t first=read_handle(out); assert(first==a->handle && first!=original && !a->dirty && !a->reserved);
    verify_strum(&h,first,2); verify_strum(&h,original,0);
    assert(edit(a,1)<0 && !a->dirty && inst[0].pad_strum==2);
    /* Both Sounds dirty: an aggregate captures both before copying. */
    reserve(&h,a); reserve(&h,b);
    assert(edit(a,1)==CS_EDIT && edit(b,1)==CS_EDIT);
    h.attempt_interrupt=1;
    assert(!guarded_copy(&h,out,arena,sizeof(arena)) && h.pending && !masked);
    uint16_t copied_a=read_handle(out+0x40+HANDLE);
    uint16_t copied_b=read_handle(out+0x40+SOUND_BYTES+HANDLE);
    verify_strum(&h,copied_a,3); verify_strum(&h,copied_b,1);
    /* Interrupt delivery after copy cannot edit without the next reservation. */
    assert(edit(a,1)<0 && inst[0].pad_strum==3);
    reserve(&h,a); assert(edit(a,1)==CS_EDIT && inst[0].pad_strum==4);
    verify_strum(&h,copied_a,3); verify_strum(&h,copied_b,1);
    /* A failed ownership recheck is transactional across all candidates. */
    reserve(&h,b); assert(edit(b,1)==CS_EDIT);
    memcpy(before,out,sizeof(out)); uint16_t prev=a->handle,prev_b=b->handle;
    ip_admission[8+4]++;
    assert(guarded_copy(&h,out,arena,sizeof(arena))<0 && !memcmp(before,out,sizeof(out)));
    assert(a->dirty && b->dirty && a->handle==prev && b->handle==prev_b);
    ip_admission[8+4]--;
    /* Destination overwrite is deliberately outside this prototype's model. */
    assert(guarded_copy(&h,a->sound,out,8)<0);
    assert(!guarded_copy(&h,out,arena,sizeof(arena)) && !a->dirty && !b->dirty);
    uint16_t last=a->handle,spare=0;
    while(h.pool.count<SP_PER_SLAB) assert(!sp_reserve_task(&h.pool,&spare));
    /* Simulate a task-side allocator failure on the next slab. Failed
     * pre-reservation cannot accept a new edit or alter published copies. */
    h.pool.allocate=0; spare=0;
    assert(sp_reserve_task(&h.pool,&spare)<0 && !spare);
    assert(edit(a,1)<0 && !a->dirty && a->handle==last && inst[0].pad_strum==4);
    verify_strum(&h,last,4); verify_strum(&h,copied_a,3);
    for(unsigned i=0;i<SP_SLABS;++i) if(h.pool.slab[i]) free(h.pool.slab[i]);
    return 0;
}
