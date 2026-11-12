/* OS1.17 KeyboardView supplies physical key, resolved track and pitch here.
 * Intercept before its stock mono audition and held-note vector. The caller
 * still registers the native key-up closure, so scale/octave edits cannot
 * change the pitch released. No raw button-to-note translation lives here. */
#include <stdint.h>
#include "../../../midi-events/src/cf/bridge.h"
#include "instances.h"
extern uint32_t dkrt_instrument_type(void);
extern uint8_t *ip_panel_track_object(uint32_t,uint32_t);
extern uint32_t ip_panel_track_type(uint8_t *);
extern uint32_t ip_panel_selected_track(uint32_t);
extern uint8_t ip_panel_active(uint32_t);
extern void ip_panel_stock_toggle(uint32_t);
extern void ip_panel_original_on(uint8_t *,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
extern void ip_panel_original_off(uint8_t *,uint32_t);
extern uint32_t ip_panel_stock_key(uint8_t *,uint8_t *);
extern void ip_panel_stock_detach(uint8_t *,uint8_t *);
extern uint8_t ip_panel_live_record(uint32_t);
extern uint8_t ip_panel_trig_held(uint32_t);
static uint8_t *panel_view;
static uint32_t panel_track=IP_NONE;
volatile uint32_t ip_panel_debug[8];
/* Key presses handed to the stock path for recording (proof counter). */
volatile uint32_t ip_panel_recorded;
static uint32_t word(const uint8_t *p,uint32_t n) { return *(const uint32_t *)(p+n); }
/* KeyboardView +0x98 is the sequencer UI state (finding 123). While it live
 * records, or a trig is held, the stock note action records the key as a
 * trig or a note lock and auditions it as a sequencer trig, which the frame
 * path then plays on CANOPY (one note per track, chord modes from the root). */
static int recording(const uint8_t *view)
{
    uint32_t seq=word(view,0x98);
    return seq && (ip_panel_live_record(seq) || ip_panel_trig_held(seq));
}
void ip_panel_on(uint8_t *view,uint32_t key,uint32_t track,uint32_t pitch,
    uint32_t velocity,uint32_t a,uint32_t b,uint32_t c)
{
    uint32_t type=dkrt_instrument_type();
    uint8_t *object=track<16 && instrument_track_owned(track) && type>=7 && type<32 ?
        ip_panel_track_object(word(view,0xa4),track) : 0;
    int canopy=object && ip_panel_track_type(object)==type && key<16 && ip_panel_active(word(view,0xa0));
    if(canopy && recording(view)) ++ip_panel_recorded;
    else if(canopy) {
        if(panel_view && (panel_view!=view || panel_track!=track)) mi_panel_release();
        panel_view=view; panel_track=track;
        instrument_select_track(track);
        ++ip_panel_debug[0]; ip_panel_debug[2]=key; ip_panel_debug[3]=track;
        ip_panel_debug[4]=pitch; ip_panel_debug[5]=velocity;
        (void)mi_panel_key(key,pitch,velocity,1,track);
        return;
    }
    ip_panel_original_on(view,key,track,pitch,velocity,a,b,c);
}
void ip_panel_off(uint8_t *view,uint32_t key)
{
    /* Consume tombstones even after track/machine/mode changes. Stock owns
     * its own vector and still receives releases for non-CANOPY auditions. */
    if(key<16) { (void)mi_panel_key(key,0,0,0,panel_track); ++ip_panel_debug[1]; }
    ip_panel_original_off(view,key);
}
void ip_panel_update(uint8_t *view)
{
    if(panel_view==view && (!ip_panel_active(word(view,0xa0)) ||
        ip_panel_selected_track(word(view,0xa0))!=panel_track)) {
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
