/* Execute real UI callbacks with a failing/in-memory card and minimal UI host. */
#include <stdio.h>
#include <string.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/control.c"
struct dk_ui_menu {
    const dk_ui_menu_desc *desc; int open, count;
    struct { dk_ui_act_fn *fn; void *ctx; int arg; } rows[32];
};
MiLedger mi_ledger;
MiTransport mi_transport;
volatile uint32_t ip_view;
static struct dk_ui_menu menus[3];
static unsigned menu_count, masked, unsafe_io, token=1;
static int exists, staged, put_error, flush_error, open_error;
static uint8_t card[KB_RECORD_BYTES], pending_card[KB_RECORD_BYTES];
static uint8_t preset_card[CP_SLOTS][CP_RECORD_BYTES], preset_staged[CP_SLOTS][CP_RECORD_BYTES];
static unsigned preset_exists, preset_staged_mask;
static uint8_t route_card[CR_BYTES],route_staged[CR_BYTES];
static int route_exists,route_staged_valid;
static uint32_t admitted=1,route_calls;
static uint32_t config_words[32],config_revision,config_schema,config_count;
static int preset_index(const char *key)
{
    for(int i=0;i<CP_SLOTS;++i) if(!strcmp(key,preset_keys[i])) return i;
    return -1;
}
uint32_t dkrt_instrument_type(void) { return admitted; }
uint32_t dkrt_instrument_generation(void) { return admitted; }
uint32_t dkrt_irq_mask(void) { unsigned before=masked; ++masked; return before; }
void dkrt_irq_restore(uint32_t sr) { masked=sr; }
int instrument_sound_prepare(int refresh) { (void)refresh; return 1; }
uint32_t instrument_sound_tune(void) { return 0x4000; }
void ip_view_build(uint32_t view) { (void)view; }
void mi_instance_reset(uint32_t track) { ++mi_ledger.generations[track]; }
int mi_registry_reply(uint32_t *out,uint32_t words) { (void)out; (void)words; return 0; }
int mi_status_reply(uint32_t *out,uint32_t words,uint32_t *age)
{ (void)out; (void)words; (void)age; return 0; }
int mi_config_submit(uint32_t a,uint32_t b,uint32_t c,const uint32_t *d,uint32_t e)
{
    (void)a; config_revision=b; config_schema=c; config_count=e;
    for(uint32_t i=0;i<e && i<32;++i) config_words[i]=d[i];
    return -2;
}
int mi_route_enable(uint32_t p,uint32_t c,uint32_t t,int s)
{ (void)p;(void)c;(void)t;(void)s;++route_calls;return -2; }
int mi_keyboard_recover(void) { return -1; }
int dkrt_settings_get(const char *key,uint8_t *out,uint32_t capacity)
{
    if(masked) ++unsafe_io;
    if(!strcmp(key,route_key)) {
        if(capacity<CR_BYTES) return -69;
        if(!route_exists) return -68;
        memcpy(out,route_card,CR_BYTES); return CR_BYTES;
    }
    int index=preset_index(key);
    if(index>=0) {
        if(capacity<CP_RECORD_BYTES) return -69;
        if(!(preset_exists & (1u<<index))) return -68;
        memcpy(out,preset_card[index],CP_RECORD_BYTES); return CP_RECORD_BYTES;
    }
    if(strcmp(key,"canopy/shapes/v1") || capacity<sizeof(card)) return -69;
    if(!exists) return -68;
    memcpy(out,card,sizeof(card)); return sizeof(card);
}
int dkrt_settings_put(const char *key,const uint8_t *in,uint32_t size)
{
    if(masked) ++unsafe_io;
    if(!strcmp(key,route_key)) {
        if(size!=CR_BYTES) return -69;
        if(put_error) return put_error;
        memcpy(route_staged,in,CR_BYTES); route_staged_valid=1; return 0;
    }
    int index=preset_index(key);
    if(index>=0) {
        if(size!=CP_RECORD_BYTES) return -69;
        if(put_error) return put_error;
        memcpy(preset_staged[index],in,size); preset_staged_mask|=1u<<index; return 0;
    }
    if(strcmp(key,"canopy/shapes/v1") || size!=sizeof(card)) return -69;
    if(put_error) return put_error;
    memcpy(pending_card,in,size); staged=1; return 0;
}
int dkrt_settings_flush(void)
{
    if(masked) ++unsafe_io;
    if(flush_error) return flush_error;
    if(route_staged_valid) { memcpy(route_card,route_staged,CR_BYTES); route_exists=1; route_staged_valid=0; }
    for(unsigned i=0;i<CP_SLOTS;++i) if(preset_staged_mask & (1u<<i)) {
        memcpy(preset_card[i],preset_staged[i],CP_RECORD_BYTES); preset_exists|=1u<<i;
    }
    preset_staged_mask=0;
    if(staged) { memcpy(card,pending_card,sizeof(card)); exists=1; staged=0; }
    return 0;
}
dk_ui_menu *dk_ui_menu_new(const dk_ui_menu_desc *d)
{ if(menu_count==3) return 0; menus[menu_count].desc=d; return &menus[menu_count++]; }
int dk_ui_open(dk_ui_menu *m) { if(open_error) return -1; m->open=1; m->count=0; m->desc->fill(m,0); return 0; }
void dk_ui_close(dk_ui_menu *m) { m->open=0; }
int dk_ui_is_open(const dk_ui_menu *m) { return m->open; }
void dk_ui_refresh(dk_ui_menu *m) { m->count=0; m->desc->fill(m,0); }
void dk_ui_text(dk_ui_menu *m,const char *s) { dk_ui_action(m,s,0,0,0); }
void dk_ui_action(dk_ui_menu *m,const char *s,dk_ui_act_fn *f,void *c,int a)
{ (void)s; if(m->count<32) { int i=m->count++; m->rows[i].fn=f; m->rows[i].ctx=c; m->rows[i].arg=a; } }
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
    uint32_t src_token=0; int src_result=0;
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
        else if(!strcmp(cmd,"yes")) {
            if(learn_menu && learn_menu->open && value>=0 && value<learn_menu->count && learn_menu->rows[value].fn)
                learn_menu->rows[value].fn(learn_menu->rows[value].ctx,learn_menu->rows[value].arg);
        }
        else if(!strcmp(cmd,"suspend")) instrument_sound_suspend();
        else if(!strcmp(cmd,"back")) learn_back(0);
        else if(!strcmp(cmd,"puterror")) put_error=value;
        else if(!strcmp(cmd,"flusherror")) flush_error=value;
        else if(!strcmp(cmd,"openerror")) open_error=value;
        else if(!strcmp(cmd,"retry")) retry_save(0,0);
        else if(!strcmp(cmd,"routesave")) route_save(0,0);
        else if(!strcmp(cmd,"enable")) { enabled=value; changed(0,0); }
        else if(!strcmp(cmd,"channel")) { channel=value; changed(0,0); }
        else if(!strcmp(cmd,"usb")) { usb=value; changed(0,0); }
        else if(!strcmp(cmd,"din")) { din=value; changed(0,0); }
        else if(!strcmp(cmd,"selectorchannel")) { selector_channel=value; changed(0,0); }
        else if(!strcmp(cmd,"selectoroctave")) { selector_octave=value; changed(0,0); }
        else if(!strcmp(cmd,"admitted")) admitted=(uint32_t)value;
        else if(!strcmp(cmd,"corruptroute")) route_card[value]^=1;
        else if(!strcmp(cmd,"resignroute")) {
            route_card[14]=route_card[15]=0;
            for(unsigned i=0;i<14;++i) { route_card[14]+=route_card[i]; route_card[15]+=route_card[14]; }
        }
        else if(!strcmp(cmd,"routestate"))
            printf("{\"enabled\":%d,\"usb\":%d,\"din\":%d,\"channel\":%d,\"octave\":%d,\"selector\":%d,\"result\":%d,\"pending\":%d,\"restore\":%d,\"calls\":%u,\"unsafe\":%u}\n",
                enabled,usb,din,channel,selector_octave,selector_channel,route_result,route_pending,route_restore,route_calls,unsafe_io);
        else if(!strcmp(cmd,"presetsave")) preset_save(0,0);
        else if(!strcmp(cmd,"presetload")) preset_load(0,0);
        else if(!strcmp(cmd,"presetslot")) preset_slot=value;
        else if(!strcmp(cmd,"color")) { pad_color=value; pad_changed(0,value); }
        else if(!strcmp(cmd,"bpm")) { arp_bpm=value; pad_changed(0,value); }
        else if(!strcmp(cmd,"config")) {
            healthy=enabled=prepared=1; phase=1; instrument_play_tick();
            printf("{\"revision\":%u,\"srcRevision\":%u,\"schema\":%u,\"count\":%u,\"pending\":%u,\"values\":[",
                config_revision,src_state.revision,config_schema,config_count,src_state.pending);
            for(uint32_t i=0;i<config_count;++i) printf("%s%u",i?",":"",config_words[i]);
            puts("]}");
        }
        else if(!strcmp(cmd,"mode")) choose_mode(0,value);
        else if(!strcmp(cmd,"srcpage")) src_token=instrument_src_context(0,(uint32_t)value,1);
        else if(!strcmp(cmd,"srcturn")) {
            unsigned knob; int delta;
            if(sscanf(line,"%*s %u %d",&knob,&delta)!=2) return 2;
            src_result=instrument_src_encoder(src_token,knob,delta,100);
        }
        else if(!strcmp(cmd,"srcstate")) {
            CvDisplay display; instrument_src_display(&display,100,"0","100");
            printf("{\"result\":%d,\"color\":%d,\"mix\":%d,\"rate\":%d,\"type\":%u,\"latch\":%u,\"dirty\":%u,\"value\":\"%s\",\"heading\":\"%s\"}\n",
                src_result,pad_color,arp_mix,arp_rate,keyboard.type,keyboard.latch,pad_dirty,display.value[3],display.heading);
        }
        else if(!strcmp(cmd,"type")) choose_type(0,value);
        else if(!strcmp(cmd,"shapeslot")) { slot=value; changed(0,0); }
        else if(!strcmp(cmd,"corruptpreset")) preset_card[preset_slot-1][value]^=1;
        else if(!strcmp(cmd,"presetstate")) {
            printf("{\"color\":%d,\"dirty\":%d,\"result\":%d,\"present\":%u,\"durable\":%u,\"pending\":%d,\"override\":%u,\"unsafe\":%u,\"shape\":[",
                pad_color,preset_dirty(),preset_result,preset_present,preset_exists,preset_pending,keyboard.preset_shape_active,unsafe_io);
            const KbShape *s=keyboard.preset_shape_active ? &keyboard.preset_shape : &keyboard.shapes[keyboard.slot];
            for(unsigned i=0;i<s->count;++i) printf("%s%u",i ? ",":"",s->intervals[i]);
            puts("]}");
        }
        else if(!strcmp(cmd,"restart")) {
            enabled=0; usb=din=channel=1; selector_channel=0; selector_octave=1;
            keyboard_initialized=0; instrument_play_tick();
        }
        else if(!strcmp(cmd,"snapshot")) {
            printf("{\"mode\":%u,\"dirty\":%u,\"status\":%u,\"storage\":%d,\"pending\":%d,\"exists\":%d,\"unsafe\":%u,\"shape\":[",
                keyboard.mode,keyboard.dirty,keyboard.status,storage_result,save_pending,exists,unsafe_io);
            for(unsigned i=0;i<keyboard.shapes[0].count;++i) printf("%s%u",i ? ",":"",keyboard.shapes[0].intervals[i]);
            puts("]}");
        } else return 2;
    }
}
