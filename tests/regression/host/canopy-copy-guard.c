/* Host-only synchronization experiment. Mock masking defers a real proof MIDI
 * selector transaction until after each attempted interruption. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"
#include "../fixtures/canopy-copy-guard/copy-guard.h"

typedef struct {
    SsPool *pool;
    uint16_t *live_handle,reserved;
    uint8_t *sound_handle_addr;
    uint16_t expected;
    uintptr_t owner;
    uint8_t staged[SC_BYTES];
    uint32_t calls,fail_at,copy_at,pending,delivered;
} Harness;
static int prepare(void *context,const CpPreset *p)
{
    Harness *h=context; assert(masked);
    return ss_can_edit(h->pool,h->live_handle,h->owner,h->expected,h->reserved) || sc_encode(p,h->staged);
}
static void attach(void *context)
{
    Harness *h=context; assert(masked);
    assert(!ss_edit(h->pool,h->live_handle,h->owner,h->expected,h->reserved,h->staged));
    memcpy(h->sound_handle_addr,h->live_handle,sizeof(*h->live_handle));
}
static uint32_t mask_irq(void *context) { (void)context; return dkrt_irq_mask(); }
static void restore_irq(void *context,uint32_t sr) { (void)context; dkrt_irq_restore(sr); }
static int freeze(void *context,const uint8_t *sound)
{
    Harness *h=context; assert(masked); ++h->calls;
    if(h->fail_at && h->calls==h->fail_at) return -1;
    uint16_t value; memcpy(&value,sound+0x13a,2);
    if(!value) return 0;
    assert(value==*h->live_handle);
    return ss_share(h->pool,value,h->owner);
}
static void copy(void *context,void *dst,const void *src,uint32_t len)
{
    Harness *h=context; assert(masked);
    for(uint32_t i=0;i<len;i+=16) {
        if(i/16==h->copy_at) { assert(masked); ++h->pending; }
        uint32_t n=len-i<16 ? len-i : 16;
        memcpy((uint8_t *)dst+i,(const uint8_t *)src+i,n);
    }
}
static MiEvent key(uint32_t key_token,uint8_t note,uint8_t kind)
{
    MiEvent e={0}; e.epoch=mi_ledger.epoch; e.generation=mi_ledger.generations[0];
    e.token=key_token; e.kind=kind; e.note=note; e.value=90; return e;
}
static int midi(Harness *h,const MiEvent *in,uint32_t n,MiEvent *out,int *refused)
{
    h->expected=*h->live_handle;
    uint32_t sr=dkrt_irq_mask();
    int r=instrument_sound_proof_midi(0,0x123400,7,mi_ledger.epoch,mi_ledger.generations[0],
        in,n,out,prepare,attach,h,refused);
    dkrt_irq_restore(sr); return r;
}
static void one_copy(uint32_t length,uint32_t interrupt_at,uint32_t track)
{
    /* Aligned, non-overlapping storage; exactly one CANOPY Sound per copy. */
    uint32_t in_words[(CG_KIT+3)/4],out_words[(CG_KIT+3)/4];
    uint8_t *in=(uint8_t *)in_words,*out=(uint8_t *)out_words;
    memset(in,0,sizeof(in_words)); memset(out,0xa5,sizeof(out_words));
    assert(track<CG_TRACKS);
    uint8_t *in_sound=in+(length==CG_KIT ? CG_KIT_FIRST+track*CG_SOUND : 0);
    uint8_t *out_sound=out+(length==CG_KIT ? CG_KIT_FIRST+track*CG_SOUND : 0);
    uint16_t live=0;
    SsEntry slots[2]; SsPool pool; uint16_t initial=0,reserved=0;
    CpPreset initial_p; uint8_t bytes[SC_BYTES];
    cur->mode=keyboard.mode=KB_ROOT; cur->type=keyboard.type=0;
    keyboard.selector_base=24; keyboard.selector_channel=MI_NONE;
    kb_reset(&keyboard,mi_ledger.epoch,mi_ledger.generations[0]);
    preset_snapshot(cur,&initial_p);
    assert(!sc_encode(&initial_p,bytes));
    assert(!ss_init(&pool,slots,2) && !ss_reserve(&pool,&initial));
    assert(!ss_create(&pool,initial,0x1111,&live,bytes));
    memcpy(in_sound+0x13a,&live,sizeof(live));
    assert(!ss_reserve(&pool,&reserved));
    Harness h={.pool=&pool,.live_handle=&live,.sound_handle_addr=in_sound+0x13a,
               .reserved=reserved,.owner=0x1111,.copy_at=interrupt_at};
    CopyGuard g={mask_irq,restore_irq,freeze,copy,&h};
    assert(!cg_copy(&g,out,in,length));
    assert(!masked && h.pending==1 && h.delivered==0);
    assert(h.calls==(length==CG_KIT ? 16u : 1u));
    uint16_t copied; memcpy(&copied,out_sound+0x13a,2);
    assert(copied==initial && live==initial);
    /* The IRQ is now delivered after the full copy and changes only source. */
    MiEvent ev[2]={key(100,25,MI_NOTE_ON),key(200,60,MI_NOTE_ON)},out_ev[8];
    int refused=0; --h.pending; ++h.delivered;
    assert(midi(&h,ev,2,out_ev,&refused)>0 && !refused && keyboard.type==1);
    assert(live==reserved && copied==initial);
    CpPreset before,after; assert(!ss_read(&pool,copied,bytes) && !sc_decode(&before,bytes));
    assert(!ss_read(&pool,live,bytes) && !sc_decode(&after,bytes));
    assert(before.control[CP_CHORD]==0 && after.control[CP_CHORD]==1);
    MiEvent off=key(200,60,MI_NOTE_OFF);
    assert(midi(&h,&off,1,out_ev,&refused)>0 && !refused);
    for(uint32_t i=0;i<MI_KEYS;++i) assert(keyboard.keys[i].token!=200 || !keyboard.keys[i].held);
    off=key(100,25,MI_NOTE_OFF); assert(midi(&h,&off,1,out_ev,&refused)==0 && !refused);
}
int main(void)
{
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; instrument_play_tick();
    ip_admission[0]=1; ip_admission[1]=0x123400; ip_admission[4]=7; ip_admission[5]=admitted;
    instrument_select_track(0);
    for(uint32_t i=0;i<(CG_SOUND+15)/16;++i) one_copy(CG_SOUND,i,0);
    for(uint32_t i=0;i<(CG_KIT+15)/16;++i) one_copy(CG_KIT,i,7);
    /* Exercise the handle-containing copy chunk in each kit track. */
    for(uint32_t t=0;t<CG_TRACKS;++t)
        one_copy(CG_KIT,(CG_KIT_FIRST+t*CG_SOUND+0x13a)/16,t);
    /* A refusal never copies even after earlier freeze callbacks. */
    uint32_t a[(CG_KIT+3)/4]={0},b[(CG_KIT+3)/4]; memset(b,0x5a,sizeof(b));
    Harness h={.fail_at=9}; CopyGuard g={mask_irq,restore_irq,freeze,copy,&h};
    assert(cg_copy(&g,b,a,CG_KIT)<0 && h.calls==9 && !h.pending && !masked);
    for(uint32_t i=0;i<sizeof(b);++i) assert(((uint8_t *)b)[i]==0x5a);
    assert(cg_copy(&g,b,a,17)<0 && !masked);
    assert(cg_copy(&g,a,a,CG_SOUND)<0 && !masked);
    /* A nested caller must retain its pre-existing mask, not enable IRQs. */
    h.calls=0; h.fail_at=0; h.copy_at=0;
    uint32_t prior=dkrt_irq_mask();
    assert(!cg_copy(&g,b,a,CG_KIT) && masked==1 && h.pending==1);
    dkrt_irq_restore(prior); assert(!masked);
    return 0;
}
