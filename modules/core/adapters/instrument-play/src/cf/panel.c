/* OS1.17 KeyboardView supplies physical key, resolved track and pitch here.
 * Intercept before its stock mono audition and held-note vector. The caller
 * still registers the native key-up closure, so scale/octave edits cannot
 * change the pitch released. No raw button-to-note translation lives here. */
#include <stdint.h>
#include "../../../midi-events/src/cf/bridge.h"
extern uint32_t dkrt_instrument_type(void);
extern volatile uint32_t ip_track;
extern uint8_t *ip_panel_track_object(uint32_t,uint32_t);
extern uint32_t ip_panel_track_type(uint8_t *);
extern uint32_t ip_panel_selected_track(uint32_t);
extern uint8_t ip_panel_active(uint32_t);
extern void ip_panel_stock_toggle(uint32_t);
extern void ip_panel_original_on(uint8_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern void ip_panel_original_off(uint8_t *,uint32_t);
extern uint32_t ip_panel_stock_key(uint8_t *,uint8_t *);
extern void ip_panel_stock_detach(uint8_t *,uint8_t *);
static uint8_t *panel_view;
volatile uint32_t ip_panel_debug[8];
static uint32_t word(const uint8_t *p,uint32_t n) { return *(const uint32_t *)(p+n); }
void ip_panel_on(uint8_t *view,uint32_t key,uint32_t track,uint32_t pitch,
    uint32_t velocity,uint32_t a,uint32_t b,uint32_t c)
{
    uint32_t type=dkrt_instrument_type();
    uint8_t *object=track<16 && track==ip_track && type>=7 && type<32 ? ip_panel_track_object(word(view,0xa4),track) : 0;
    if(object && ip_panel_track_type(object)==type && key<16 && ip_panel_active(word(view,0xa0))) {
        if(panel_view && panel_view!=view) mi_panel_release();
        panel_view=view;
        ++ip_panel_debug[0]; ip_panel_debug[2]=key; ip_panel_debug[3]=track;
        ip_panel_debug[4]=pitch; ip_panel_debug[5]=velocity;
        (void)mi_panel_key(key,pitch,velocity,1);
        return;
    }
    ip_panel_original_on(view,key,track,pitch,velocity,a,b,c);
}
void ip_panel_off(uint8_t *view,uint32_t key)
{
    /* Consume tombstones even after track/machine/mode changes. Stock owns
     * its own vector and still receives releases for non-CANOPY auditions. */
    if(key<16) { (void)mi_panel_key(key,0,0,0); ++ip_panel_debug[1]; }
    ip_panel_original_off(view,key);
}
void ip_panel_update(uint8_t *view)
{
    if(panel_view==view && (!ip_panel_active(word(view,0xa0)) ||
        ip_panel_selected_track(word(view,0xa0))!=ip_track)) {
        mi_panel_release(); panel_view=0; ++ip_panel_debug[6];
    }
    /* The pinned stock KeyboardView update is an empty return. Unlike a
     * key callback, this also sees selection handled by another view. */
}
void ip_panel_toggle(uint32_t manager)
{
    ip_panel_stock_toggle(manager);
    /* KeyboardView is retained when the main controller toggles KEYBOARD.
     * Its detach callback therefore cannot be the only release boundary. */
    if(panel_view && !ip_panel_active(manager)) {
        mi_panel_release(); panel_view=0; ++ip_panel_debug[6];
    }
}
uint32_t ip_panel_key_event(uint8_t *view,uint8_t *event)
{
    uint32_t result=ip_panel_stock_key(view,event);
    ip_panel_update(view);
    return result;
}
void ip_panel_detach(uint8_t *view,uint8_t *manager)
{
    if(panel_view==view) { mi_panel_release(); panel_view=0; ++ip_panel_debug[7]; }
    ip_panel_stock_detach(view,manager);
}
