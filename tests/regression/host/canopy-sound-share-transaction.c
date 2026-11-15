/* Host-only integration of the real SRC transaction with the copy-on-share
 * pool. There is no firmware hook, allocator, or device lifecycle here. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#include <assert.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"

typedef struct {
    SsPool *pool;
    uintptr_t owner;
    uint16_t handle,expected,reserved;
    uint8_t staged[SC_BYTES];
    int prepares,attaches,fail_after_prepare;
} Transaction;

/* Called with the same mapper lock held through commit. Eligibility is
 * read-only: even successful prepare must not alter a live snapshot. */
static int prepare(void *context,const CpPreset *preset)
{
    Transaction *t=context;
    assert(masked); ++t->prepares;
    if(ss_can_edit(t->pool,&t->handle,t->owner,t->expected,t->reserved)) return -1;
    if(sc_encode(preset,t->staged)) return -1;
    return t->fail_after_prepare ? -1 : 0;
}
/* The real transaction's attach callback is void. Under the same lock, no
 * other pool, copy or Sound mutation can invalidate the prepared decision. */
static void attach(void *context)
{
    Transaction *t=context;
    assert(masked);
    uint16_t previous=t->handle;
    assert(!ss_edit(t->pool,&t->handle,t->owner,t->expected,t->reserved,t->staged));
    if(t->handle!=previous) t->reserved=0;
    ++t->attaches;
}
static void read_preset(SsPool *pool,uint16_t handle,CpPreset *preset)
{
    uint8_t bytes[SC_BYTES];
    assert(!ss_read(pool,handle,bytes));
    assert(!sc_decode(preset,bytes));
}
static int turn(Transaction *t,int32_t movement)
{
    uint32_t view=instrument_src_context(0,0,1);
    t->expected=t->handle;
    assert(view);
    return instrument_sound_proof_edit(0,0x123400,7,view,3,movement,100,
        prepare,attach,t);
}
static void matches_current(Transaction *t)
{
    CpPreset actual,snapshot; uint8_t actual_bytes[SC_BYTES],snapshot_bytes[SC_BYTES];
    preset_snapshot(cur,&actual);
    read_preset(t->pool,t->handle,&snapshot);
    assert(!sc_encode(&actual,actual_bytes) && !sc_encode(&snapshot,snapshot_bytes));
    assert(!memcmp(actual_bytes,snapshot_bytes,SC_BYTES));
}
static MiEvent key(uint32_t key_token,uint8_t note,uint8_t kind)
{
    MiEvent e={0};
    e.epoch=mi_ledger.epoch; e.generation=mi_ledger.generations[0];
    e.token=key_token; e.kind=kind; e.note=note; e.value=90; e.port=0; e.channel=0;
    return e;
}
static int midi(Transaction *t,const MiEvent *input,uint32_t count,MiEvent *output,int *refused)
{
    t->expected=t->handle;
    uint32_t sr=dkrt_irq_mask();
    int n=instrument_sound_proof_midi(0,0x123400,7,mi_ledger.epoch,mi_ledger.generations[0],
        input,count,output,prepare,attach,t,refused);
    dkrt_irq_restore(sr);
    return n;
}
static void released(uint32_t key_token)
{
    for(uint32_t n=0;n<MI_KEYS;++n)
        assert(keyboard.keys[n].token!=key_token || !keyboard.keys[n].held);
}
static void midi_transaction(void)
{
    /* An independent pool keeps the encoder experiment's exhaustion from
     * affecting this test. This is a host harness, not real Sound ownership. */
    SsEntry entries[2]; SsPool pool; uint16_t initial=0,none=0;
    Transaction t={.pool=&pool,.owner=0x5678};
    CpPreset p; uint8_t bytes[SC_BYTES];
    cur->mode=keyboard.mode=KB_ROOT; cur->type=keyboard.type=0;
    keyboard.selector_base=24; keyboard.selector_channel=MI_NONE;
    kb_reset(&keyboard,mi_ledger.epoch,mi_ledger.generations[0]);
    preset_snapshot(cur,&p); assert(!sc_encode(&p,bytes));
    assert(!ss_init(&pool,entries,2) && !ss_reserve(&pool,&initial));
    assert(!ss_create(&pool,initial,t.owner,&t.handle,bytes));

    /* A real selector event updates an exclusive snapshot without a reserve,
     * while the simultaneous musical key still plays and releases. */
    MiEvent events[2]={key(100,25,MI_NOTE_ON),key(200,60,MI_NOTE_ON)},out[8];
    int refused=0;
    assert(midi(&t,events,2,out,&refused)>0 && !refused && keyboard.type==1);
    assert(t.handle==initial && pool.count==1 && t.attaches==1 && !masked);
    read_preset(&pool,initial,&p); assert(p.control[CP_CHORD]==1);
    matches_current(&t);
    MiEvent off=key(200,60,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)>0 && !refused); released(200);
    off=key(100,25,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)==0 && !refused);

    /* Without a reservation a frozen chord cannot be edited: the selector is
     * filtered, while the other note and its later release still go through. */
    uint32_t sr=dkrt_irq_mask(); assert(!ss_share(&pool,t.handle,t.owner));
    uint16_t copied=t.handle; dkrt_irq_restore(sr);
    events[0]=key(300,26,MI_NOTE_ON); events[1]=key(400,62,MI_NOTE_ON);
    assert(midi(&t,events,2,out,&refused)>0 && refused && keyboard.type==1);
    assert(t.handle==copied && t.attaches==1 && !masked);
    read_preset(&pool,copied,&p); assert(p.control[CP_CHORD]==1);
    matches_current(&t);
    off=key(400,62,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)>0 && !refused); released(400);
    off=key(300,26,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)==0 && !refused);

    /* The same selector with a pre-reserved slot detaches. */
    assert(!masked && !ss_reserve(&pool,&t.reserved));
    uint16_t reserved=t.reserved;
    events[0]=key(500,26,MI_NOTE_ON); events[1]=key(600,62,MI_NOTE_ON);
    assert(midi(&t,events,2,out,&refused)>0 && !refused && keyboard.type==2);
    assert(t.handle==reserved && !t.reserved && t.handle!=copied && t.attaches==2);
    read_preset(&pool,copied,&p); assert(p.control[CP_CHORD]==1);
    read_preset(&pool,t.handle,&p); assert(p.control[CP_CHORD]==2);
    matches_current(&t);
    off=key(600,62,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)>0 && !refused); released(600);
    off=key(500,26,MI_NOTE_OFF);
    assert(midi(&t,&off,1,out,&refused)==0 && !refused);

    /* Exhaustion must not trap any notes in a full mixed MIDI batch. */
    assert(ss_reserve(&pool,&none)<0 && !none);
    sr=dkrt_irq_mask(); assert(!ss_share(&pool,t.handle,t.owner));
    uint16_t second=t.handle; dkrt_irq_restore(sr);
    MiEvent full[8];
    for(uint32_t n=0;n<4;++n) {
        full[n]=key(700+n,(uint8_t)(24+n),MI_NOTE_ON);
        full[4+n]=key(800+n,(uint8_t)(61+n),MI_NOTE_ON);
    }
    assert(midi(&t,full,8,out,&refused)>0 && refused && keyboard.type==2);
    assert(t.handle==second && t.attaches==2);
    for(uint32_t n=0;n<4;++n) full[n]=key(800+n,(uint8_t)(61+n),MI_NOTE_OFF);
    assert(midi(&t,full,4,out,&refused)>0 && !refused && !masked);
    for(uint32_t n=0;n<4;++n) released(800+n);
    read_preset(&pool,copied,&p); assert(p.control[CP_CHORD]==1);
    read_preset(&pool,second,&p); assert(p.control[CP_CHORD]==2);
    matches_current(&t);
}
static void preset_transaction(void)
{
    SsEntry entries[2]; SsPool pool; uint16_t initial=0;
    Transaction t={.pool=&pool,.owner=0x9876};
    CpPreset before,target,stored; uint8_t bytes[SC_BYTES];
    preset_snapshot(cur,&before); assert(!sc_encode(&before,bytes));
    assert(!ss_init(&pool,entries,2) && !ss_reserve(&pool,&initial));
    assert(!ss_create(&pool,initial,t.owner,&t.handle,bytes));
    target=before; target.control[CP_CHORD]=4; target.control[CP_VOICING]=3;
    target.control[CP_STRUM]=12; target.control[CP_WT_POS]=43;
    target.fm[0]=37;
    assert(cp_valid(&target));
    t.expected=t.handle;
    assert(instrument_sound_proof_apply(0,0x123400,8,&target,prepare,attach,&t)<0);
    assert(!t.prepares && !t.attaches && !masked);
    matches_current(&t);

    /* A failed prepare or an unreserved frozen Sound must not apply any part
     * of the preset or change the snapshot, including the remembered FM bank. */
    t.fail_after_prepare=1;
    assert(instrument_sound_proof_apply(0,0x123400,7,&target,prepare,attach,&t)<0);
    assert(!t.attaches && t.handle==initial && !masked); matches_current(&t);
    t.fail_after_prepare=0;
    uint32_t sr=dkrt_irq_mask(); assert(!ss_share(&pool,t.handle,t.owner));
    uint16_t copied=t.handle; dkrt_irq_restore(sr);
    assert(instrument_sound_proof_apply(0,0x123400,7,&target,prepare,attach,&t)<0);
    assert(!t.attaches && t.handle==copied && !masked); matches_current(&t);

    /* Once a task reserves space, applying the complete preset and publishing
     * its snapshot occur at the same reset boundary. The copy stays unchanged. */
    assert(!ss_reserve(&pool,&t.reserved));
    assert(!instrument_sound_proof_apply(0,0x123400,7,&target,prepare,attach,&t));
    assert(t.handle!=copied && !t.reserved && t.attaches==1 && !masked);
    matches_current(&t);
    read_preset(&pool,copied,&stored);
    assert(!sc_encode(&stored,bytes));
    uint8_t original[SC_BYTES]; assert(!sc_encode(&before,original));
    assert(!memcmp(bytes,original,SC_BYTES));
    read_preset(&pool,t.handle,&stored);
    assert(!sc_encode(&stored,bytes) && !sc_encode(&target,original));
    assert(!memcmp(bytes,original,SC_BYTES));
}

int main(void)
{
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; instrument_play_tick();
    ip_admission[0]=1; ip_admission[1]=0x123400; ip_admission[4]=7; ip_admission[5]=admitted;
    cur->mode=keyboard.mode=KB_ROOT;
    instrument_select_track(0);
    SsEntry entries[2]; SsPool pool; uint16_t initial=0,free_handle=0;
    assert(!ss_init(&pool,entries,2));
    assert(!ss_reserve(&pool,&initial));
    Transaction t={.pool=&pool,.owner=0x1234};
    CpPreset p; uint8_t bytes[SC_BYTES];
    preset_snapshot(cur,&p);
    assert(!sc_encode(&p,bytes));
    assert(!ss_create(&pool,initial,t.owner,&t.handle,bytes));
    assert(t.handle==initial && cur->pad_strum==0);
    uint32_t view=instrument_src_context(0,0,1);
    assert(view);
    assert(instrument_sound_proof_edit(0,0x999900,7,view,3,1,100,prepare,attach,&t)<0);
    assert(instrument_sound_proof_edit(0,0x123400,8,view,3,1,100,prepare,attach,&t)<0);
    assert(!t.prepares && !t.attaches && !masked && t.handle==initial);
    matches_current(&t);

    /* Real encoder edits reuse the exclusive Sound handle, without a task
     * allocation/reservation, while the encoded snapshot stays current. */
    for(int n=0;n<1000;++n) {
        assert(turn(&t,(n&1) ? -1 : 1)==CS_EDIT);
        assert(t.handle==initial && pool.count==1 && !masked);
    }
    assert(cur->pad_strum==0 && t.attaches==1000);
    matches_current(&t);

    /* A prepare that passes eligibility but never commits cannot alter the
     * exclusive snapshot or actual control. */
    CpPreset before; read_preset(&pool,t.handle,&before);
    p=before; p.control[CP_STRUM]=5;
    t.expected=t.handle;
    uint32_t sr=dkrt_irq_mask();
    assert(!prepare(&t,&p));
    dkrt_irq_restore(sr);
    matches_current(&t);
    assert(cur->pad_strum==0 && t.handle==initial);

    /* Refusal after encoding, before the actual UI edit, is also inert. */
    t.fail_after_prepare=1;
    assert(turn(&t,1)<0);
    assert(cur->pad_strum==0 && t.handle==initial && t.attaches==1000 && !masked);
    matches_current(&t); t.fail_after_prepare=0;

    /* Freeze and copy under the same exclusion used by the transaction. */
    sr=dkrt_irq_mask();
    assert(!ss_share(&pool,t.handle,t.owner));
    uint16_t copied=t.handle;
    dkrt_irq_restore(sr);
    assert(turn(&t,1)<0);
    assert(t.handle==copied && cur->pad_strum==0 && !masked);
    read_preset(&pool,copied,&p);
    assert(p.control[CP_STRUM]==0);
    matches_current(&t);

    /* A task reservation makes the exact same real edit detach atomically. */
    assert(!masked && !ss_reserve(&pool,&t.reserved));
    uint16_t reservation=t.reserved;
    assert(turn(&t,1)==CS_EDIT);
    assert(t.handle==reservation && t.handle!=copied && !t.reserved && cur->pad_strum==1);
    read_preset(&pool,copied,&p); assert(p.control[CP_STRUM]==0);
    matches_current(&t);
    assert(!masked && pool.count==2);

    /* No more handles: exclusive edits work, but after a second copy the
     * attempted detach is refused with both Sound revisions unchanged. */
    assert(ss_reserve(&pool,&free_handle)<0 && !free_handle);
    assert(turn(&t,1)==CS_EDIT && cur->pad_strum==2);
    matches_current(&t);
    sr=dkrt_irq_mask();
    assert(!ss_share(&pool,t.handle,t.owner));
    uint16_t second_copy=t.handle;
    dkrt_irq_restore(sr);
    assert(turn(&t,1)<0 && cur->pad_strum==2 && t.handle==second_copy && !masked);
    read_preset(&pool,copied,&p); assert(p.control[CP_STRUM]==0);
    read_preset(&pool,second_copy,&p); assert(p.control[CP_STRUM]==2);
    matches_current(&t);
    preset_transaction();
    midi_transaction();
    return 0;
}
