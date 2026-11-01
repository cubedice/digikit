/* Execute real UI callbacks with a failing/in-memory card and minimal UI host. */
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/control.c"
struct dk_ui_menu { const dk_ui_menu_desc *desc; int open; };
MiLedger mi_ledger;
MiTransport mi_transport;
volatile uint32_t ip_view;
static struct dk_ui_menu menus[2];
static unsigned menu_count, masked, unsafe_io, token=1;
static int exists, staged, put_error, flush_error, open_error;
static uint8_t card[KB_RECORD_BYTES], pending_card[KB_RECORD_BYTES];
uint32_t dkrt_instrument_type(void) { return 1; }
uint32_t dkrt_instrument_generation(void) { return 1; }
uint32_t dkrt_irq_mask(void) { unsigned before=masked; ++masked; return before; }
void dkrt_irq_restore(uint32_t sr) { masked=sr; }
int instrument_sound_prepare(int refresh) { (void)refresh; return 1; }
void ip_view_build(uint32_t view) { (void)view; }
void mi_instance_reset(uint32_t track) { ++mi_ledger.generations[track]; }
int mi_registry_reply(uint32_t *out,uint32_t words) { (void)out; (void)words; return 0; }
int mi_config_submit(uint32_t a,uint32_t b,uint32_t c,const uint32_t *d,uint32_t e)
{ (void)a;(void)b;(void)c;(void)d;(void)e;return -2; }
int mi_route_enable(uint32_t p,uint32_t c,uint32_t t,int s)
{ (void)p;(void)c;(void)t;(void)s;return -2; }
int mi_keyboard_recover(void) { return -1; }
int dkrt_settings_get(const char *key,uint8_t *out,uint32_t capacity)
{
    if(masked) ++unsafe_io;
    if(strcmp(key,"canopy/shapes/v1") || capacity<sizeof(card)) return -69;
    if(!exists) return -68;
    memcpy(out,card,sizeof(card)); return sizeof(card);
}
int dkrt_settings_put(const char *key,const uint8_t *in,uint32_t size)
{
    if(masked) ++unsafe_io;
    if(strcmp(key,"canopy/shapes/v1") || size!=sizeof(card)) return -69;
    if(put_error) return put_error;
    memcpy(pending_card,in,size); staged=1; return 0;
}
int dkrt_settings_flush(void)
{
    if(masked) ++unsafe_io;
    if(flush_error) return flush_error;
    if(staged) { memcpy(card,pending_card,sizeof(card)); exists=1; staged=0; }
    return 0;
}
dk_ui_menu *dk_ui_menu_new(const dk_ui_menu_desc *d)
{ if(menu_count==2) return 0; menus[menu_count].desc=d; return &menus[menu_count++]; }
int dk_ui_open(dk_ui_menu *m) { if(open_error) return -1; m->open=1; m->desc->fill(m,0); return 0; }
void dk_ui_close(dk_ui_menu *m) { m->open=0; }
int dk_ui_is_open(const dk_ui_menu *m) { return m->open; }
void dk_ui_refresh(dk_ui_menu *m) { m->desc->fill(m,0); }
void dk_ui_text(dk_ui_menu *m,const char *s) { (void)m;(void)s; }
void dk_ui_action(dk_ui_menu *m,const char *s,dk_ui_act_fn *f,void *c,int a)
{ (void)m;(void)s;(void)f;(void)c;(void)a; }
void dk_ui_toggle(dk_ui_menu *m,const char *s,int *v,dk_ui_act_fn *f,void *c)
{ (void)m;(void)s;(void)v;(void)f;(void)c; }
void dk_ui_value(dk_ui_menu *m,const char *s,int *v,int a,int b,dk_ui_act_fn *f,void *c)
{ (void)m;(void)s;(void)v;(void)a;(void)b;(void)f;(void)c; }
void dk_ui_submenu(dk_ui_menu *m,const char *s,dk_ui_menu *child) { (void)m;(void)s;(void)child; }
int dk_ui_choice(const char *t,const char *const *i,int n,void (*done)(void *,int),void *c)
{ (void)t;(void)i;(void)n;(void)done;(void)c;return 0; }
int dk_ui_confirm(const char *t,const char *v,void (*done)(void *,int),void *c)
{ (void)t;(void)v;(void)done;(void)c;return 0; }
int dk_ui_toast(const char *text) { (void)text; return 0; }
int main(void)
{
    char line[64],cmd[24]; int value;
    mi_ledger.epoch=1; mi_ledger.generations[0]=1; instrument_play_tick();
    while(fgets(line,sizeof(line),stdin)) {
        value=0; if(sscanf(line,"%23s %d",cmd,&value)<1) continue;
        if(!strcmp(cmd,"learn")) learn_begin(0,0);
        else if(!strcmp(cmd,"key")) {
            MiEvent in={0},out[8]; in.kind=MI_NOTE_ON;in.note=(uint8_t)value;in.value=90;
            in.epoch=mi_ledger.epoch;in.generation=mi_ledger.generations[0];in.token=token++;
            instrument_keyboard_events(0,in.epoch,in.generation,&in,1,out);
        } else if(!strcmp(cmd,"tick")) instrument_play_tick();
        else if(!strcmp(cmd,"save")) learn_close(0,1);
        else if(!strcmp(cmd,"suspend")) instrument_sound_suspend();
        else if(!strcmp(cmd,"back")) learn_back(0);
        else if(!strcmp(cmd,"puterror")) put_error=value;
        else if(!strcmp(cmd,"flusherror")) flush_error=value;
        else if(!strcmp(cmd,"openerror")) open_error=value;
        else if(!strcmp(cmd,"retry")) retry_save(0,0);
        else if(!strcmp(cmd,"restart")) { keyboard_initialized=0; instrument_play_tick(); }
        else if(!strcmp(cmd,"snapshot")) {
            printf("{\"mode\":%u,\"dirty\":%u,\"status\":%u,\"storage\":%d,\"pending\":%d,\"exists\":%d,\"unsafe\":%u,\"shape\":[",
                keyboard.mode,keyboard.dirty,keyboard.status,storage_result,save_pending,exists,unsafe_io);
            for(unsigned i=0;i<keyboard.shapes[0].count;++i) printf("%s%u",i ? ",":"",keyboard.shapes[0].intervals[i]);
            puts("]}");
        } else return 2;
    }
}
