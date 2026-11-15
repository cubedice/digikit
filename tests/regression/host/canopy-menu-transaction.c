/* Host proof for the real modulation-menu setters and their isolated Sound
 * transaction helper. No native Sound identity/lifecycle or firmware hook. */
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

/* The suite links production keyboard.c. Unrelated selector-proof paths must
 * not be reached; their separate fixture is deliberately not substituted. */
int kb_selector_candidate(const KbState *s,const MiEvent *input,uint32_t count,uint32_t *type)
{
    (void)s; (void)input; (void)count; (void)type;
    assert(!"selector proof is outside the menu test"); return -1;
}
int kb_process_locked_selector(KbState *s,const MiEvent *input,uint32_t count,MiEvent output[8],int allow)
{
    (void)s; (void)input; (void)count; (void)output; (void)allow;
    assert(!"selector proof is outside the menu test"); return -1;
}

typedef struct {
    SsPool pool;
    SsEntry entries[2];
    uint16_t handle,expected,reserved;
    uint8_t before[SC_BYTES],staged[SC_BYTES];
    uint32_t before_dirty;
    int prepares,attaches,fail_after_prepare,probe_reentry,reentries;
} MenuTransaction;
static void bytes_current(uint8_t out[SC_BYTES])
{
    CpPreset p; preset_snapshot(cur,&p); assert(!sc_encode(&p,out));
}
static void attach_menu(void *context);
static int prepare_menu(void *context,const CpPreset *p)
{
    MenuTransaction *tx=context;
    uint8_t live[SC_BYTES];
    assert(masked && dk_ui_is_open(tone_menu));
    bytes_current(live);
    assert(!memcmp(live,tx->before,sizeof(live)) && cur->pad_dirty==tx->before_dirty);
    ++tx->prepares;
    if(tx->probe_reentry && !tx->reentries) {
        ++tx->reentries;
        assert(instrument_sound_proof_menu(0,0x123400,7,IP_PROOF_FM_WHEEL,-99,
            prepare_menu,attach_menu,tx)<0);
        assert(masked);
    }
    if(ss_can_edit(&tx->pool,&tx->handle,0x100,tx->expected,tx->reserved)) return -1;
    if(sc_encode(p,tx->staged)) return -1;
    return tx->fail_after_prepare ? -1 : 0;
}
static void attach_menu(void *context)
{
    MenuTransaction *tx=context;
    uint8_t live[SC_BYTES];
    assert(masked && tx->prepares==1 && dk_ui_is_open(tone_menu));
    bytes_current(live);
    assert(!memcmp(live,tx->staged,sizeof(live)) && cur->pad_dirty==1);
    assert(!ss_edit(&tx->pool,&tx->handle,0x100,tx->expected,tx->reserved,tx->staged));
    if(tx->handle!=tx->expected) tx->reserved=0;
    ++tx->attaches;
}
static void setup(MenuTransaction *tx,int family,uint32_t capacity)
{
    memset(tx,0,sizeof(*tx));
    ip_init(); keyboard_initialized=1;
    cur=&inst[0]; selected=focus=0;
    cur->track=cur->bound=ip_tracks[0]=0;
    cur->phase=3; cur->prepared=1; cur->pad_dirty=0;
    cur->pad_family=family;
    cur->wt[CP_WT_RATE-16]=27;
    cur->wt[CP_WT_WHEEL-16]=81;
    cur->fm[CP_FM_WHEEL-15]=122;
    cur->wtfm[CP_WTFM_WHEEL-28]=143;
    mi_ledger.epoch=1; mi_ledger.generations[0]=1;
    ip_admission[0]=1; ip_admission[1]=0x123400; ip_admission[4]=7; ip_admission[5]=admitted;
    uint8_t bytes[SC_BYTES]; uint16_t reservation=0;
    bytes_current(bytes);
    assert(!ss_init(&tx->pool,tx->entries,capacity) && !ss_reserve(&tx->pool,&reservation));
    assert(!ss_create(&tx->pool,reservation,0x100,&tx->handle,bytes));
    tone_menu=dk_ui_menu_new(&tone_desc);
    assert(tone_menu && !dk_ui_open(tone_menu));
}
static int edit(MenuTransaction *tx,uint32_t setting,int value)
{
    assert(!masked);
    tx->prepares=tx->attaches=0; tx->expected=tx->handle;
    tx->before_dirty=cur->pad_dirty;
    bytes_current(tx->before);
    /* dk_ui_value stages the signed value before invoking the real callback.
     * The proof caller refreshes outside the lock after either result, so a
     * refused edit cannot leave the menu displaying an unaccepted amount. */
    if(setting==IP_PROOF_WT_WHEEL) wheel_amount=value;
    if(setting==IP_PROOF_FM_WHEEL) fm_wheel_amount=value;
    if(setting==IP_PROOF_WTFM_WHEEL) wtfm_wheel_amount=value;
    int result=instrument_sound_proof_menu(0,0x123400,7,setting,value,prepare_menu,attach_menu,tx);
    assert(!masked && dk_ui_is_open(tone_menu));
    dk_ui_refresh(tone_menu);
    assert(wheel_amount==cur->wt[CP_WT_WHEEL-16]-100 &&
        fm_wheel_amount==cur->fm[CP_FM_WHEEL-15]-100 &&
        wtfm_wheel_amount==cur->wtfm[CP_WTFM_WHEEL-28]-100);
    return result;
}
static void matches(const MenuTransaction *tx)
{
    uint8_t live[SC_BYTES],stored[SC_BYTES];
    bytes_current(live);
    assert(!ss_read(&tx->pool,tx->handle,stored) && !memcmp(live,stored,sizeof(live)));
    assert(!masked && !unsafe_io);
}
static uint16_t freeze(MenuTransaction *tx,int reserve)
{
    uint32_t sr=dkrt_irq_mask();
    assert(!ss_share(&tx->pool,tx->handle,0x100));
    uint16_t copy=tx->handle;
    dkrt_irq_restore(sr);
    if(reserve) assert(!ss_reserve(&tx->pool,&tx->reserved));
    return copy;
}
typedef struct {
    IpInst live;
    MiLedger ledger;
    SsEntry entries[2];
    uint16_t handle,reserved;
    uint32_t count;
    int wheel,fm_wheel,wtfm_wheel;
} UnchangedMenu;
static void remember(const MenuTransaction *tx,UnchangedMenu *before)
{
    memcpy(&before->live,cur,sizeof(*cur));
    memcpy(&before->ledger,&mi_ledger,sizeof(mi_ledger));
    memcpy(before->entries,tx->entries,sizeof(tx->entries));
    before->handle=tx->handle; before->reserved=tx->reserved; before->count=tx->pool.count;
    before->wheel=wheel_amount; before->fm_wheel=fm_wheel_amount; before->wtfm_wheel=wtfm_wheel_amount;
}
static void unchanged(const MenuTransaction *tx,const UnchangedMenu *before)
{
    assert(!memcmp(&before->live,cur,sizeof(*cur)) && !memcmp(&before->ledger,&mi_ledger,sizeof(mi_ledger)));
    assert(!memcmp(before->entries,tx->entries,sizeof(tx->entries)) && before->count==tx->pool.count);
    assert(before->handle==tx->handle && before->reserved==tx->reserved && !tx->attaches);
    assert(before->wheel==wheel_amount && before->fm_wheel==fm_wheel_amount && before->wtfm_wheel==wtfm_wheel_amount);
    assert(!masked && !unsafe_io);
    matches(tx);
}
static void clock_pair(void)
{
    MenuTransaction tx; setup(&tx,2,2);
    uint8_t old[SC_BYTES],copied[SC_BYTES]; bytes_current(old);
    uint16_t copy=freeze(&tx,1),reservation=tx.reserved;
    assert(!edit(&tx,IP_PROOF_SCAN_CLOCK,1) && tx.prepares==1 && tx.attaches==1);
    assert(cur->wt[CP_WT_CLOCK-16]==1 && cur->wt[CP_WT_RATE-16]==3);
    assert(tx.handle==reservation && tx.handle!=copy && !tx.reserved);
    assert(!ss_read(&tx.pool,copy,copied) && !memcmp(old,copied,sizeof(old)));
    matches(&tx);
    assert(!edit(&tx,IP_PROOF_SCAN_CLOCK,0));
    assert(cur->wt[CP_WT_CLOCK-16]==0 && cur->wt[CP_WT_RATE-16]==10);
    matches(&tx);
    assert(!ss_read(&tx.pool,copy,copied) && !memcmp(old,copied,sizeof(old)));
}
static void inactive_banks(void)
{
    MenuTransaction tx; setup(&tx,3,1);
    CpPreset previous,actual;
    preset_snapshot(cur,&previous);
    /* WT clock/reset remain WT settings while the FM source is selected. */
    assert(!edit(&tx,IP_PROOF_SCAN_CLOCK,1));
    assert(!edit(&tx,IP_PROOF_SCAN_RESET,2));
    assert(!edit(&tx,IP_PROOF_WT_WHEEL,-100));
    assert(!edit(&tx,IP_PROOF_WTFM_WHEEL,100));
    preset_snapshot(cur,&actual);
    assert(actual.family==3 && !memcmp(actual.fm,previous.fm,sizeof(actual.fm)));
    assert(actual.control[CP_WT_CLOCK]==1 && actual.control[CP_WT_RATE]==3 &&
        actual.control[CP_WT_RESET]==2 && actual.control[CP_WT_WHEEL]==0 &&
        actual.control[CP_WTFM_WHEEL]==200);
    matches(&tx);
    /* After selecting a legacy source, the FM menu still addresses its
     * remembered FM bank, not the similarly numbered WT wire field. */
    uint32_t sr=dkrt_irq_mask();
    cur->pad_family=0;
    bytes_current(tx.staged);
    assert(!ss_edit(&tx.pool,&tx.handle,0x100,tx.handle,0,tx.staged));
    dkrt_irq_restore(sr);
    preset_snapshot(cur,&previous);
    assert(!edit(&tx,IP_PROOF_FM_WHEEL,-47));
    preset_snapshot(cur,&actual);
    assert(actual.family==0 && actual.fm[CP_FM_WHEEL-15]==53);
    assert(!memcmp(actual.control,previous.control,sizeof(actual.control)));
    matches(&tx);
}
static void all_refused(int exhausted)
{
    MenuTransaction tx; UnchangedMenu before; setup(&tx,3,exhausted?1:2);
    freeze(&tx,!exhausted);
    tx.fail_after_prepare=!exhausted;
    const int values[]={1,2,-100,100,-37};
    for(uint32_t setting=0;setting<=IP_PROOF_WTFM_WHEEL;++setting) {
        remember(&tx,&before);
        assert(edit(&tx,setting,values[setting])<0 && tx.prepares==1);
        unchanged(&tx,&before);
    }
    if(!exhausted) {
        tx.fail_after_prepare=0;
        assert(!edit(&tx,IP_PROOF_SCAN_CLOCK,1) && tx.attaches==1);
        matches(&tx);
    }
}
static void invalid_or_stale(void)
{
    MenuTransaction tx; UnchangedMenu before; setup(&tx,2,1);
    const struct { uint32_t setting; int value; } invalid[]={
        {IP_PROOF_SCAN_CLOCK,-1},{IP_PROOF_SCAN_CLOCK,2},
        {IP_PROOF_SCAN_RESET,3},{IP_PROOF_WT_WHEEL,-101},
        {IP_PROOF_FM_WHEEL,101},{IP_PROOF_WTFM_WHEEL,101},{99,0}
    };
    remember(&tx,&before);
    for(uint32_t n=0;n<sizeof(invalid)/sizeof(invalid[0]);++n) {
        assert(edit(&tx,invalid[n].setting,invalid[n].value)<0 && !tx.prepares);
        unchanged(&tx,&before);
    }
    ++ip_admission[4];
    assert(edit(&tx,IP_PROOF_SCAN_CLOCK,1)<0 && !tx.prepares);
    unchanged(&tx,&before);
    --ip_admission[4]; ip_admission[6]=1;
    assert(edit(&tx,IP_PROOF_FM_WHEEL,-10)<0 && !tx.prepares);
    unchanged(&tx,&before);
}
static void reentry(void)
{
    MenuTransaction tx; setup(&tx,2,1); tx.probe_reentry=1;
    int fm_before=cur->fm[CP_FM_WHEEL-15];
    assert(!edit(&tx,IP_PROOF_SCAN_CLOCK,1) && tx.reentries==1 && tx.prepares==1 && tx.attaches==1);
    assert(cur->fm[CP_FM_WHEEL-15]==fm_before);
    matches(&tx);
}
int main(int argc,char **argv)
{
    if(argc!=2) return 2;
    if(!strcmp(argv[1],"clock")) clock_pair();
    else if(!strcmp(argv[1],"inactive")) inactive_banks();
    else if(!strcmp(argv[1],"capacity")) all_refused(1);
    else if(!strcmp(argv[1],"prepared")) all_refused(0);
    else if(!strcmp(argv[1],"invalid")) invalid_or_stale();
    else if(!strcmp(argv[1],"reentry")) reentry();
    else return 2;
    return 0;
}
