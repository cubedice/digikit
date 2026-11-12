#include <assert.h>
#include <stdio.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/panel.c"
static uint8_t view[0x200],other[0x200],object[4];
static uint32_t machine=9,selected,downs,ups,cancels,original_on,original_off,detaches;
static uint8_t active=1;
volatile uint32_t ip_track;
uint32_t dkrt_instrument_type(void) { return 9; }
uint8_t *ip_panel_track_object(uint32_t manager,uint32_t track)
{ (void)manager;assert(track==ip_track);return object; }
uint32_t ip_panel_track_type(uint8_t *p) { assert(p==object);return machine; }
uint32_t ip_panel_selected_track(uint32_t manager) { (void)manager;return selected; }
uint8_t ip_panel_active(uint32_t manager) { (void)manager;return active; }
void ip_panel_stock_toggle(uint32_t manager) { (void)manager;active^=1; }
void ip_panel_original_on(uint8_t *v,uint32_t key,uint32_t track,uint32_t pitch,uint32_t velocity,
    uint32_t a,uint32_t b,uint32_t c)
{ assert(v==view && key==3 && (track<=1 || track==10) && pitch==65 && velocity==88 && a==5 && b==6 && c==7);++original_on; }
void ip_panel_original_off(uint8_t *v,uint32_t key) { assert(v==view && key==3);++original_off; }
uint32_t ip_panel_stock_key(uint8_t *v,uint8_t *e) { (void)e;assert(v==view);return 17; }
void ip_panel_stock_detach(uint8_t *v,uint8_t *m) { (void)m;assert(v==view || v==other);++detaches; }
int mi_panel_key(uint32_t key,uint32_t note,uint32_t velocity,int down)
{ assert(key==3);if(down) { assert(note==65 && velocity==88);++downs; } else ++ups;return 1; }
void mi_panel_release(void) { ++cancels; }
int main(void)
{
    ip_panel_on(view,3,0,65,88,5,6,7);
    assert(downs==1 && !original_on);
    /* Ordinary pitches and velocity have already been resolved by firmware. */
    ip_panel_off(view,3);assert(ups==1 && original_off==1);
    machine=0;ip_panel_on(view,3,0,65,88,5,6,7);
    machine=9;ip_panel_on(view,3,1,65,88,5,6,7);
    assert(original_on==2 && downs==1);
    assert(ip_panel_key_event(view,0)==17 && !cancels);
    selected=1;assert(ip_panel_key_event(view,0)==17 && cancels==1);
    selected=0;ip_panel_on(view,3,0,65,88,5,6,7);
    selected=1;ip_panel_update(view);assert(cancels==2);
    /* Foreign views cannot release the current local phrase. */
    selected=0;ip_panel_on(view,3,0,65,88,5,6,7);
    ip_panel_detach(other,0);assert(cancels==2 && detaches==1);
    ip_panel_detach(view,0);assert(cancels==3 && detaches==2);
    ip_panel_off(view,3);assert(ups==2 && original_off==2);
    ip_panel_on(view,3,0,65,88,5,6,7);
    ip_panel_toggle(0);assert(!active && cancels==4);
    ip_panel_on(view,3,0,65,88,5,6,7);assert(original_on==3);
    ip_panel_toggle(0);assert(active && cancels==4);
    ip_panel_on(view,3,0,65,88,5,6,7);
    active=0;ip_panel_update(view);assert(cancels==5);
    /* The instrument on track 11: only that resolved track is consumed. */
    active=1;ip_track=10;selected=10;
    ip_panel_on(view,3,10,65,88,5,6,7);assert(downs==6 && original_on==3);
    ip_panel_on(view,3,0,65,88,5,6,7);assert(downs==6 && original_on==4);
    ip_panel_update(view);assert(cancels==5);
    selected=0;ip_panel_update(view);assert(cancels==6);
    ip_track=0xffffffffu;ip_panel_on(view,3,10,65,88,5,6,7);assert(downs==6 && original_on==5);
    puts("ok");return 0;
}
