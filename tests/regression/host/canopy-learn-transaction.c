/* Host integration of the proof-only shared LEARN transaction in control.c.
 * No firmware hook, Sound lifetime discovery, allocator, or persistence I/O
 * is supplied here. The owner records are explicit fixtures, not addresses
 * claimed to be safe boot-lifetime identities on the device. */
#define main settings_driver_main
#include "canopy-settings.c"
#undef main
#undef keyboard
#undef pad_family
#undef pad_color
#undef arp_bpm
#undef arp_mix
#undef arp_rate
#undef pad_dirty
#undef wt
#undef phase
#undef prepared
#undef src_state
#undef src_status
#include <assert.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-codec.h"
#include "../../../modules/core/adapters/instrument-play/src/cf/sound-share-pool.h"

/* Link the production keyboard.c so capture and save are the actual code.
 * These unrelated selector-proof entry points belong to another fixture;
 * fail loudly if this LEARN suite accidentally exercises that experiment. */
int kb_selector_candidate(const KbState *s,const MiEvent *input,uint32_t count,uint32_t *type)
{
    (void)s; (void)input; (void)count; (void)type;
    assert(!"selector proof is outside the LEARN test"); return -1;
}
int kb_process_locked_selector(KbState *s,const MiEvent *input,uint32_t count,MiEvent output[8],int allow)
{
    (void)s; (void)input; (void)count; (void)output; (void)allow;
    assert(!"selector proof is outside the LEARN test"); return -1;
}

enum { POOL_CAPACITY=12 };
typedef struct {
    uintptr_t identity;
    uint32_t sound,generation;
    uint16_t handle,reserved;
} LearnOwner;
typedef struct {
    SsPool pool;
    SsEntry entries[POOL_CAPACITY];
    LearnOwner owners[IP_INSTANCES];
    uint16_t copies[IP_INSTANCES];
    uint8_t original[IP_INSTANCES][SC_BYTES];
    uint8_t staged_bytes[IP_INSTANCES][SC_BYTES];
    uint16_t expected[IP_INSTANCES],detach[IP_INSTANCES];
    unsigned prepares,commits,prepared_mask;
    int probe_reentry,reentries;
} LearnTransaction;

static void snapshot_bytes(const IpInst *in,uint8_t out[SC_BYTES])
{
    CpPreset p;
    preset_snapshot(in,&p);
    assert(!sc_encode(&p,out));
}

static void attach_learn(void *context,uint32_t i);
/* A prepare only writes caller-owned staging. The real helper owns admission,
 * prospective sound construction and the atomic save/attachment boundary. */
static int prepare_learn(void *context,uint32_t i,uint32_t sound,uint32_t generation,const CpPreset *p)
{
    LearnTransaction *tx=context;
    assert(masked && i<IP_INSTANCES && dk_ui_is_open(learn_menu));
    if(tx->probe_reentry && !tx->reentries) {
        ++tx->reentries;
        LearnOwner *source=&tx->owners[0];
        assert(instrument_sound_proof_learn(0,source->sound,source->generation,
            shown_session,shown_revision,prepare_learn,attach_learn,tx)<0);
        assert(masked);
    }
    LearnOwner *o=&tx->owners[i];
    if(sound!=o->sound || generation!=o->generation) return -1;
    tx->expected[i]=o->handle;
    if(ss_can_edit(&tx->pool,&o->handle,o->identity,tx->expected[i],o->reserved)) return -1;
    if(tx->pool.entries[tx->expected[i]-1].state==SS_FROZEN) {
        tx->detach[i]=o->reserved;
        /* Distinct frozen owners must not promise the same reservation. */
        for(uint32_t j=0;j<IP_INSTANCES;++j)
            if((tx->prepared_mask & (1u<<j)) && tx->detach[j]==tx->detach[i]) return -1;
    }
    if(sc_encode(p,tx->staged_bytes[i])) return -1;
    tx->prepared_mask|=1u<<i;
    ++tx->prepares;
    return 0;
}
static void attach_learn(void *context,uint32_t i)
{
    LearnTransaction *tx=context;
    assert(masked && (tx->prepared_mask & (1u<<i)) && dk_ui_is_open(learn_menu));
    if(!tx->commits) {
        /* Actual kb_learn_save/shapes_share must already agree with every
         * prepared snapshot before the first owner handle is published. */
        for(uint32_t j=0;j<IP_INSTANCES;++j) if(tx->prepared_mask & (1u<<j)) {
            uint8_t actual[SC_BYTES];
            snapshot_bytes(&inst[j],actual);
            assert(!memcmp(actual,tx->staged_bytes[j],SC_BYTES));
        }
    }
    LearnOwner *o=&tx->owners[i];
    assert(!ss_edit(&tx->pool,&o->handle,o->identity,tx->expected[i],o->reserved,tx->staged_bytes[i]));
    if(tx->detach[i]) o->reserved=0;
    ++tx->commits;
}
static int save_transaction(LearnTransaction *tx)
{
    tx->prepares=tx->commits=tx->prepared_mask=0;
    memset(tx->detach,0,sizeof(tx->detach));
    assert(!masked && learn_menu && dk_ui_is_open(learn_menu));
    LearnOwner *source=&tx->owners[0];
    int result=instrument_sound_proof_learn(0,source->sound,source->generation,
        shown_session,shown_revision,prepare_learn,attach_learn,tx);
    /* The helper deliberately leaves UI work to its caller outside the lock. */
    assert(!masked && dk_ui_is_open(learn_menu));
    if(!result) dk_ui_close(learn_menu);
    return result;
}

static void reserve_owner(LearnTransaction *tx,uint32_t i)
{
    assert(!masked && !tx->owners[i].reserved);
    assert(!ss_reserve(&tx->pool,&tx->owners[i].reserved));
}
static void freeze_owner(LearnTransaction *tx,uint32_t i)
{
    uint32_t sr=dkrt_irq_mask();
    assert(!ss_share(&tx->pool,tx->owners[i].handle,tx->owners[i].identity));
    tx->copies[i]=tx->owners[i].handle;
    dkrt_irq_restore(sr);
}
static void input_note(uint8_t note)
{
    MiEvent input={0},output[8];
    input.kind=MI_NOTE_ON; input.note=note; input.value=90;
    input.epoch=mi_ledger.epoch; input.generation=mi_ledger.generations[0];
    input.token=token++;
    uint32_t sr=dkrt_irq_mask();
    assert(instrument_keyboard_events(0,input.epoch,input.generation,&input,1,output)>=0);
    dkrt_irq_restore(sr);
}
static void capture_chord(void)
{
    learn_begin(0,0);
    assert(learn_menu && dk_ui_is_open(learn_menu) && cur->keyboard.mode==KB_LEARN);
    /* Deliberately unsorted input exercises the actual keyboard capture path. */
    input_note(69); input_note(60); input_note(65);
    dk_ui_refresh(learn_menu);
    assert(cur->keyboard.capture_count==3 && cur->keyboard.capture[0]==60 &&
        cur->keyboard.capture[1]==65 && cur->keyboard.capture[2]==69);
    assert(shown_session==cur->keyboard.session && shown_revision==cur->keyboard.revision);
}
static void setup(LearnTransaction *tx,uint32_t capacity,int mixed)
{
    memset(tx,0,sizeof(*tx));
    ip_init(); keyboard_initialized=1;
    mi_ledger.epoch=1;
    assert(IP_INSTANCES==4 && !ss_init(&tx->pool,tx->entries,capacity));
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        IpInst *in=&inst[i];
        in->track=in->bound=ip_tracks[i]=i;
        in->phase=3; in->prepared=1;
        in->mode=in->keyboard.mode=i==1 ? KB_PLAY : KB_ROOT;
        in->type=in->keyboard.type=(i==1 || i==2) ? 0 : 10;
        in->slot=1; in->keyboard.slot=0;
        for(uint32_t s=0;s<8;++s) {
            const KbShape initial={3,{0,4,7,0}};
            in->keyboard.shapes[s]=initial;
        }
        in->pad_color=40+(int)i;
        mi_ledger.generations[i]=1;
        LearnOwner *o=&tx->owners[i];
        o->identity=0x100+i; o->sound=0x123400+0x450*i; o->generation=7+i;
        ip_admission[8*i]=1; ip_admission[8*i+1]=o->sound;
        ip_admission[8*i+4]=o->generation; ip_admission[8*i+5]=admitted;
    }
    /* Source save must replace an active preset override as well as edit the
     * library. Its persisted keyboard mode is ROOT, not transient LEARN. */
    inst[0].keyboard.preset_shape=(KbShape){3,{0,7,12,0}};
    inst[0].keyboard.preset_shape_active=1;
    if(mixed) {
        inst[2].keyboard.preset_shape=(KbShape){3,{0,3,8,0}};
        inst[2].keyboard.preset_shape_active=1;
        inst[3].keyboard.slot=1; inst[3].slot=2;
        if(mixed==2) inst[3].keyboard.shapes[1]=(KbShape){3,{0,2,7,0}};
    }
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        uint16_t reservation=0;
        snapshot_bytes(&inst[i],tx->original[i]);
        assert(!ss_reserve(&tx->pool,&reservation));
        assert(!ss_create(&tx->pool,reservation,tx->owners[i].identity,
            &tx->owners[i].handle,tx->original[i]));
    }
    cur=&inst[0]; selected=focus=0;
    capture_chord();
}

typedef struct {
    IpInst instances[IP_INSTANCES];
    CsState source_states[IP_INSTANCES];
    IsStatus statuses[IP_INSTANCES];
    MiLedger ledger;
    SsEntry entries[POOL_CAPACITY];
    LearnOwner owners[IP_INSTANCES];
    uint32_t pool_count;
    int pending,menu_open;
} Unchanged;
static void remember(const LearnTransaction *tx,Unchanged *before)
{
    memcpy(before->instances,inst,sizeof(inst));
    memcpy(before->source_states,src_state,sizeof(src_state));
    memcpy(before->statuses,src_status,sizeof(src_status));
    before->ledger=mi_ledger;
    memcpy(before->entries,tx->entries,sizeof(tx->entries));
    memcpy(before->owners,tx->owners,sizeof(tx->owners));
    before->pool_count=tx->pool.count;
    before->pending=save_pending; before->menu_open=dk_ui_is_open(learn_menu);
}
static void unchanged(const LearnTransaction *tx,const Unchanged *before)
{
    assert(!memcmp(before->instances,inst,sizeof(inst)));
    assert(!memcmp(before->source_states,src_state,sizeof(src_state)));
    assert(!memcmp(before->statuses,src_status,sizeof(src_status)));
    assert(!memcmp(&before->ledger,&mi_ledger,sizeof(mi_ledger)));
    assert(!memcmp(before->entries,tx->entries,sizeof(tx->entries)));
    assert(!memcmp(before->owners,tx->owners,sizeof(tx->owners)));
    assert(before->pool_count==tx->pool.count && before->pending==save_pending);
    assert(before->menu_open==dk_ui_is_open(learn_menu));
    assert(!tx->commits && !masked && !unsafe_io);
}
static void check_saved(const LearnTransaction *tx,unsigned affected)
{
    const uint8_t intervals[4]={0,5,9,0};
    assert(save_pending && !dk_ui_is_open(learn_menu) && !masked && !unsafe_io);
    assert(!inst[0].keyboard.preset_shape_active && inst[0].keyboard.mode==KB_ROOT);
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        uint8_t actual[SC_BYTES],stored[SC_BYTES];
        snapshot_bytes(&inst[i],actual);
        assert(!ss_read(&tx->pool,tx->owners[i].handle,stored));
        assert(!memcmp(actual,stored,SC_BYTES));
        assert(inst[i].keyboard.shapes[0].count==3 &&
            !memcmp(inst[i].keyboard.shapes[0].intervals,intervals,sizeof(intervals)));
        if(affected & (1u<<i)) assert(memcmp(tx->original[i],stored,SC_BYTES));
        else assert(!memcmp(tx->original[i],stored,SC_BYTES));
        if(tx->copies[i]) {
            assert(!ss_read(&tx->pool,tx->copies[i],stored));
            assert(!memcmp(tx->original[i],stored,SC_BYTES));
        }
    }
}
static void mixed_owners(void)
{
    LearnTransaction tx; setup(&tx,6,1);
    for(uint32_t i=0;i<IP_INSTANCES;++i) freeze_owner(&tx,i);
    reserve_owner(&tx,0); reserve_owner(&tx,1);
    uint16_t source_next=tx.owners[0].reserved,play_next=tx.owners[1].reserved;
    assert(!save_transaction(&tx) && tx.prepares==2 && tx.commits==2);
    assert(tx.owners[0].handle==source_next && tx.owners[1].handle==play_next);
    assert(!tx.owners[0].reserved && !tx.owners[1].reserved && tx.pool.count==6);
    assert(tx.owners[2].handle==tx.copies[2] && tx.owners[3].handle==tx.copies[3]);
    assert(inst[1].keyboard.mode==KB_PLAY && inst[2].keyboard.preset_shape_active);
    check_saved(&tx,3);
}
static void exhausted_later_owner(void)
{
    LearnTransaction tx; Unchanged before; setup(&tx,5,0);
    /* Owner zero is exclusive: its successful prepare must not rewrite its
     * entry when a later frozen peer has no detach reservation. */
    for(uint32_t i=1;i<IP_INSTANCES;++i) freeze_owner(&tx,i);
    reserve_owner(&tx,1);
    uint16_t unavailable=0;
    assert(ss_reserve(&tx.pool,&unavailable)<0 && !unavailable);
    remember(&tx,&before);
    assert(save_transaction(&tx)<0 && tx.prepares==2);
    unchanged(&tx,&before);
    /* Repeating the same request must retain the capture and the first
     * reservation; failed preparation cannot consume either resource. */
    assert(save_transaction(&tx)<0 && tx.prepares==2);
    unchanged(&tx,&before);
}
static void reserve_and_retry(void)
{
    LearnTransaction tx; Unchanged before; setup(&tx,8,0);
    for(uint32_t i=0;i<IP_INSTANCES;++i) freeze_owner(&tx,i);
    reserve_owner(&tx,0); reserve_owner(&tx,1);
    remember(&tx,&before);
    assert(save_transaction(&tx)<0 && tx.prepares==2);
    unchanged(&tx,&before);
    reserve_owner(&tx,2); reserve_owner(&tx,3);
    assert(!save_transaction(&tx) && tx.prepares==4 && tx.commits==4);
    assert(inst[2].keyboard.mode==KB_ROOT && inst[2].keyboard.type==0);
    check_saved(&tx,15);
}
static void duplicate_reservation(void)
{
    LearnTransaction tx; Unchanged before; setup(&tx,5,1);
    freeze_owner(&tx,0); freeze_owner(&tx,1); reserve_owner(&tx,0);
    tx.owners[1].reserved=tx.owners[0].reserved;
    remember(&tx,&before);
    assert(save_transaction(&tx)<0 && tx.prepares==1);
    unchanged(&tx,&before);
}
static void stale_capture(void)
{
    LearnTransaction tx; Unchanged before; setup(&tx,4,1);
    input_note(72); /* Fourth note arrives after the menu's displayed revision. */
    remember(&tx,&before);
    assert(save_transaction(&tx)<0 && !tx.prepares);
    unchanged(&tx,&before);
    dk_ui_refresh(learn_menu);
    assert(!save_transaction(&tx) && tx.prepares==2 && tx.commits==2);
    assert(inst[0].keyboard.shapes[0].count==4 &&
        inst[0].keyboard.shapes[0].intervals[3]==12);
    for(uint32_t i=0;i<IP_INSTANCES;++i) {
        uint8_t actual[SC_BYTES],stored[SC_BYTES];
        snapshot_bytes(&inst[i],actual);
        assert(!ss_read(&tx.pool,tx.owners[i].handle,stored) && !memcmp(actual,stored,SC_BYTES));
    }
    assert(!masked && !unsafe_io);
}
static void changed_owner(void)
{
    LearnTransaction tx; Unchanged before; setup(&tx,4,0);
    ++ip_admission[8*2+4]; /* Same Sound address, new generation. */
    remember(&tx,&before);
    assert(save_transaction(&tx)<0 && tx.prepares==2);
    unchanged(&tx,&before);
}
static void reentrant_prepare(void)
{
    LearnTransaction tx; setup(&tx,4,1);
    tx.probe_reentry=1;
    assert(!save_transaction(&tx) && tx.prepares==2 && tx.commits==2 && tx.reentries==1);
    check_saved(&tx,3);
}
static void divergent_library(void)
{
    LearnTransaction tx; setup(&tx,7,2);
    for(uint32_t i=0;i<IP_INSTANCES;++i) freeze_owner(&tx,i);
    reserve_owner(&tx,0); reserve_owner(&tx,1); reserve_owner(&tx,3);
    /* The real shared-library publication replaces every slot, so a peer
     * using a divergent other slot also needs a detached persistent copy. */
    assert(!save_transaction(&tx) && tx.prepares==3 && tx.commits==3);
    assert(inst[3].keyboard.shapes[1].intervals[1]==4);
    check_saved(&tx,11);
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"mixed")) mixed_owners();
    else if(!strcmp(argv[1],"exhausted")) exhausted_later_owner();
    else if(!strcmp(argv[1],"retry")) reserve_and_retry();
    else if(!strcmp(argv[1],"alias")) duplicate_reservation();
    else if(!strcmp(argv[1],"stale")) stale_capture();
    else if(!strcmp(argv[1],"owner")) changed_owner();
    else if(!strcmp(argv[1],"reentry")) reentrant_prepare();
    else if(!strcmp(argv[1],"divergent")) divergent_library();
    else return 2;
    return 0;
}
