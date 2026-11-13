#include <assert.h>
#include <stdio.h>
#include "../../../modules/core/adapters/instrument-play/src/cf/panel.c"
static uint8_t view[0x200],other[0x200],object[4];
static uint32_t machine=9,selected,downs,ups,cancels,original_on,original_off,detaches,last_down_key,last_down_note;
static uint8_t active=1,live,held,step_record;
static uint32_t seq_object[0x100];
#define SEQ ((uint32_t)(uintptr_t)seq_object)
uint8_t ip_panel_live_record(uint32_t seq) { assert(seq==SEQ);return live; }
uint8_t ip_panel_trig_held(uint32_t seq) { assert(seq==SEQ);return held; }
uint8_t ip_panel_step_record(uint32_t seq) { assert(seq==SEQ);return step_record; }
/* A fake pattern object (finding 125): trig words per track, the note-list
 * entries at +0x6a50 and the head index per (track, step) at +0x12a50. The
 * recorder commits after the key event; commit() models that write. */
static uint8_t pattern[0x1dc00];
static uint32_t entries,clock_ms=1000;
uint32_t pp_current_pattern(void) { return (uint32_t)(uintptr_t)pattern; }
uint32_t instrument_src_now(void) { return clock_ms; }
static void commit(uint32_t track,uint32_t step,uint32_t note)
{
    uint8_t *trig=pattern+0x6a5*track+2*step; trig[1]|=1;
    uint8_t *e=pattern+0x6a50+6*entries; e[0]=(uint8_t)track; e[1]=(uint8_t)step; e[2]=(uint8_t)note;
    uint8_t *h=pattern+0x12a50+2*(128*track+step); h[0]=(uint8_t)(entries>>8); h[1]=(uint8_t)entries;
    uint8_t *n=pattern+0x13a50+2*entries; n[0]=n[1]=0xff;
    ++entries;
}
/* The stock recorder adding a note to a step's existing list (same head). */
static void append(uint32_t track,uint32_t step,uint32_t note)
{
    const uint8_t *h=pattern+0x12a50+2*(128*track+step);
    uint32_t at=(uint32_t)((h[0]<<8)|h[1]);
    for(;;) { uint8_t *n=pattern+0x13a50+2*at; if(n[0]==0xff) { n[0]=(uint8_t)(entries>>8); n[1]=(uint8_t)entries; break; } at=(uint32_t)((n[0]<<8)|n[1]); }
    uint8_t *e=pattern+0x6a50+6*entries; e[0]=(uint8_t)track; e[1]=(uint8_t)step; e[2]=(uint8_t)note;
    uint8_t *n=pattern+0x13a50+2*entries; n[0]=n[1]=0xff;
    ++entries;
}
static void heads_empty(void) { for(unsigned i=0;i<16*128;++i) pattern[0x12a50+2*i]=pattern[0x12a51+2*i]=0xff; }
static int plays_keys=1;
int instrument_track_plays_keys(uint32_t t) { (void)t; return plays_keys; }
static uint32_t chord_steps[4],chord_count,chord_calls,mutes;
static int8_t chord[3];
int instrument_record_chord(const uint32_t steps[4],const int8_t *intervals,uint32_t count)
{
    for(int i=0;i<4;++i) chord_steps[i]=steps[i];
    for(uint32_t i=0;i<count;++i) chord[i]=intervals[i];
    chord_count=count; ++chord_calls; return 1;
}
void mi_seq_mute(uint32_t track,int on) { assert(track<16); mutes=on ? mutes|(1u<<track) : mutes&~(1u<<track); }
volatile uint32_t ip_track;
int instrument_track_owned(uint32_t t) { return t==ip_track; }
void instrument_select_track(uint32_t t) { (void)t; }
uint32_t dkrt_instrument_type(void) { return 9; }
uint8_t *ip_panel_track_object(uint32_t manager,uint32_t track)
{ (void)manager;assert(track==ip_track);return object; }
uint32_t ip_panel_track_type(uint8_t *p) { assert(p==object);return machine; }
uint32_t ip_panel_selected_track(uint32_t manager) { (void)manager;return selected; }
uint8_t ip_panel_active(uint32_t manager) { (void)manager;return active; }
void ip_panel_stock_toggle(uint32_t manager) { (void)manager;active^=1; }
void ip_panel_original_on(uint8_t *v,uint32_t key,uint32_t track,uint32_t pitch,uint32_t velocity,
    uint32_t a,uint32_t b,uint32_t c)
{
    assert(v==view && key<16 && (track<=1 || track==10) && velocity==88 && a==5 && b==6 && c==7);
    assert(key!=3 || pitch==65);
    ++original_on;
}
void ip_panel_original_off(uint8_t *v,uint32_t key) { assert(v==view && key<16); ++original_off; }
/* Step recording: the stock key handler writes the entry itself. */
static int stock_step=-1; static uint8_t stock_note;
static void commit(uint32_t track,uint32_t step,uint32_t note);
/* The stock note-list writer behind the hook: it updates the fake pattern. */
void ip_note_list_original(uint8_t *editor,uint32_t track,uint32_t step,uint32_t *records)
{ (void)editor; commit(track,step,((const uint8_t *)(uintptr_t)records[0])[0]); }
static uint32_t irq_masked;
uint32_t dkrt_irq_mask(void) { uint32_t old=irq_masked; irq_masked=1; return old; }
void dkrt_irq_restore(uint32_t old) { irq_masked=old; }
/* A stock commit through the hooked writer: one record {note, velocity}. */
static uint8_t record_bytes[4];
static uint32_t record_vector[2];
static void note_commit(uint32_t track,uint32_t step,uint32_t note)
{
    record_bytes[0]=(uint8_t)note; record_bytes[1]=100;
    record_vector[0]=(uint32_t)(uintptr_t)record_bytes; record_vector[1]=(uint32_t)(uintptr_t)(record_bytes+4);
    ip_note_list_write(0,track,step,record_vector);
}
uint32_t ip_panel_stock_key(uint8_t *v,uint8_t *e)
{ (void)e;assert(v==view); if(stock_step>=0) note_commit(0,(uint32_t)stock_step,stock_note); return 17; }
void ip_panel_stock_detach(uint8_t *v,uint8_t *m) { (void)m;assert(v==view || v==other);++detaches; }
int mi_panel_key(uint32_t key,uint32_t note,uint32_t velocity,int down,uint32_t track)
{ (void)track;assert(key<16);if(down) { assert((key!=3 || note==65) && (velocity==88 || velocity==100));last_down_key=key;last_down_note=note;++downs; } else ++ups;return 1; }
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
    /* Live recording, step recording or a held trig: the stock note action
     * records the press and CANOPY still plays it, with the stock audition
     * muted on the frame path while the key is held. */
    heads_empty();
    ip_track=0;selected=0;*(uint32_t *)(view+0x98)=SEQ;
    live=1;ip_panel_on(view,3,0,65,88,5,6,7);
    assert(original_on==6 && downs==7 && ip_panel_recorded==1 && mutes==1);
    ip_panel_off(view,3);assert(!mutes);
    live=0;step_record=1;ip_panel_on(view,3,0,65,88,5,6,7);
    assert(original_on==7 && downs==8 && ip_panel_recorded==2);
    ip_panel_off(view,3);
    /* A held trig takes the note at once: a lone key clears chord notes. */
    step_record=0;held=1;seq_object[0x258/4]=1u<<4;ip_panel_on(view,3,0,65,88,5,6,7);
    assert(original_on==8 && downs==9 && ip_panel_recorded==3);
    assert(chord_calls==1 && !chord_count && chord_steps[0]==(1u<<4));
    ip_panel_off(view,3);
    /* Held C, E, G on a held trig: each press stores the earlier keys. */
    ip_panel_on(view,9,0,60,88,5,6,7);ip_panel_on(view,11,0,64,88,5,6,7);
    assert(chord_calls==3 && chord_count==1 && chord[0]==-4);
    ip_panel_on(view,13,0,67,88,5,6,7);
    assert(chord_calls==4 && chord_count==2 && chord[0]==-3 && chord[1]==-7);
    ip_panel_off(view,9);ip_panel_off(view,11);ip_panel_off(view,13);
    seq_object[0x258/4]=0;held=0;
    ip_panel_on(view,3,0,65,88,5,6,7);assert(original_on==11 && downs==13 && ip_panel_recorded==6);
    ip_panel_off(view,3);
    clock_ms+=2000;ip_panel_tick();
    /* Live recording commits each trig and its note list after the key
     * event. A committed step whose note belongs to the held group gets the
     * group's other notes, relative to that note, on the next UI tick. */
    live=1;chord_calls=0;
    ip_panel_on(view,9,0,60,88,5,6,7);ip_panel_on(view,11,0,64,88,5,6,7);ip_panel_on(view,13,0,67,88,5,6,7);
    ip_panel_tick();assert(!chord_calls);
    commit(0,6,67);ip_panel_tick();
    assert(chord_calls==1 && chord_steps[0]==(1u<<6) && chord_count==2 && chord[0]==-3 && chord[1]==-7);
    ip_panel_tick();assert(chord_calls==1); /* unchanged steps are not rewritten */
    commit(0,7,60);ip_panel_tick();
    assert(chord_calls==2 && chord_steps[0]==(1u<<7) && chord_count==2 && chord[0]==7 && chord[1]==4);
    commit(0,8,50);ip_panel_tick();assert(chord_calls==2); /* not a held note */
    /* A note added to step 7's list (same head) is seen as a change. */
    append(0,7,64);ip_panel_tick();
    assert(chord_calls==3 && chord_steps[0]==(1u<<7) && chord[0]==7 && chord[1]==4);
    /* The watch outlives the release briefly, then stops. */
    ip_panel_off(view,9);ip_panel_off(view,11);ip_panel_off(view,13);assert(!mutes);
    clock_ms+=1000;commit(0,9,64);ip_panel_tick();
    assert(chord_calls==4 && chord_steps[0]==(1u<<9) && chord[0]==3 && chord[1]==-4);
    clock_ms+=1000;ip_panel_tick();commit(0,10,64);ip_panel_tick();assert(chord_calls==4);
    /* A ROOT chord sound records its root alone (clears chord notes). */
    plays_keys=0;
    ip_panel_on(view,9,0,60,88,5,6,7);ip_panel_on(view,11,0,64,88,5,6,7);
    commit(0,11,64);ip_panel_tick();
    assert(chord_calls==5 && !chord_count && chord_steps[0]==(1u<<11));
    ip_panel_off(view,9);ip_panel_off(view,11);
    clock_ms+=2000;ip_panel_tick();plays_keys=1;
    /* Another selected track stops the watch: locks address the UI track. */
    ip_panel_on(view,9,0,60,88,5,6,7);ip_panel_on(view,11,0,64,88,5,6,7);
    selected=1;commit(0,12,64);ip_panel_tick();assert(chord_calls==5);
    selected=0;ip_panel_off(view,9);ip_panel_off(view,11);
    /* Leaving keyboard mode releases recorded keys and their mute. */
    ip_panel_on(view,9,0,60,88,5,6,7);assert(mutes==1);
    ip_panel_toggle(0);assert(!active && !mutes);
    ip_panel_toggle(0);live=0;
    /* Step recording with KEYBOARD held: each trig key's note is committed
     * through the hooked note-list writer, during or after the key event,
     * without the note action. CANOPY takes the commits in order: each note
     * sounds for the oldest waiting key, the stock audition is muted and the
     * held group's other notes are stored on the committed step. */
    {
        uint32_t ev[8]={0};ev[4]=1;
        step_record=1;live=0;chord_calls=0;
        unsigned on=original_on,d=downs;
        ev[3]=0x19+8;stock_step=20;stock_note=60;
        assert(ip_panel_key_event(view,(uint8_t *)ev)==17);
        assert(chord_calls==1 && chord_steps[0]==(1u<<20) && !chord_count && downs==d+1 && mutes==1);
        ev[3]=0x19+10;stock_note=64;ip_panel_key_event(view,(uint8_t *)ev);
        assert(chord_calls==2 && chord_count==1 && chord[0]==-4 && downs==d+2);
        ev[3]=0x19+12;stock_note=67;ip_panel_key_event(view,(uint8_t *)ev);
        assert(chord_calls==3 && chord_count==2 && chord[0]==-3 && chord[1]==-7 && downs==d+3);
        /* Releases commit nothing; the mute ends with the last held key. */
        stock_step=-1;ev[3]=0x19+8;ip_panel_key_event(view,(uint8_t *)ev);ip_panel_off(view,8);
        assert(chord_calls==3 && mutes==1);
        ip_panel_off(view,10);ip_panel_off(view,12);assert(!mutes && original_on==on);
        /* The firmware can call note-off before delivering the key release.
         * Neither that event nor auto-repeat starts another entry. */
        ev[3]=0x19+12;ev[4]=0x10;ip_panel_key_event(view,(uint8_t *)ev);
        ev[4]=9;ip_panel_key_event(view,(uint8_t *)ev);
        assert(!waiting_count && !mutes);ev[4]=1;
        /* The next entry starts a new group: a lone note clears chord notes. */
        ev[3]=0x19+9;stock_step=21;stock_note=62;ip_panel_key_event(view,(uint8_t *)ev);
        assert(chord_calls==4 && chord_steps[0]==(1u<<21) && !chord_count);
        stock_step=-1;ip_panel_off(view,9);assert(!mutes);
        /* A press that commits nothing (e.g. KEYBOARD itself) changes nothing. */
        ev[3]=0x12;ip_panel_key_event(view,(uint8_t *)ev);assert(chord_calls==4 && !mutes);
        /* Simultaneous presses: all three key events come before any commit;
         * the commits then arrive together, in press order, and every note
         * joins the chord. */
        d=downs;
        ev[3]=0x19+8;ip_panel_key_event(view,(uint8_t *)ev);
        ev[3]=0x19+10;ip_panel_key_event(view,(uint8_t *)ev);
        ev[3]=0x19+12;ip_panel_key_event(view,(uint8_t *)ev);
        assert(chord_calls==4 && mutes==1 && downs==d);
        note_commit(0,22,60);note_commit(0,22,64);note_commit(0,22,67);ip_panel_tick();
        assert(chord_calls==7 && chord_steps[0]==(1u<<22) && chord_count==2 && chord[0]==-3 && chord[1]==-7);
        assert(downs==d+3);
        ip_panel_off(view,8);ip_panel_off(view,10);ip_panel_off(view,12);assert(!mutes);
        /* A key released before its commit still records its note. */
        d=downs;
        ev[3]=0x19+9;ip_panel_key_event(view,(uint8_t *)ev);ip_panel_off(view,9);
        note_commit(0,23,62);ip_panel_tick();
        assert(chord_calls==8 && chord_steps[0]==(1u<<23) && !chord_count && downs==d);
        /* After the entry window, commits on this track are not taken: a
         * later copy or paste cannot touch chord-note locks. */
        clock_ms+=600;ip_panel_tick();note_commit(0,24,62);ip_panel_tick();assert(chord_calls==8 && !mutes);
        /* A waiting key whose note never comes expires and ends the mute. */
        ev[3]=0x19+11;ip_panel_key_event(view,(uint8_t *)ev);assert(mutes==1);
        clock_ms+=600;ip_panel_tick();assert(!mutes && chord_calls==8);
        /* The first key may be released while both commits are still in
         * flight. Its tombstone must not steal the second key's note. */
        d=downs;
        ev[3]=0x19+8;ip_panel_key_event(view,(uint8_t *)ev);
        ev[3]=0x19+10;ip_panel_key_event(view,(uint8_t *)ev);
        ip_panel_off(view,8);
        note_commit(0,25,60);note_commit(0,25,64);ip_panel_tick();
        assert(chord_calls==10 && chord_count==1 && chord[0]==-4);
        assert(downs==d+1 && last_down_key==10 && last_down_note==64);
        /* A different note-list write with no pending entry is not a chord. */
        note_commit(0,26,67);ip_panel_tick();assert(chord_calls==10);
        ip_panel_off(view,10);ip_panel_tick();assert(!mutes);
        /* A new group can reuse a released key before its prior commit. */
        ev[3]=0x19+9;ip_panel_key_event(view,(uint8_t *)ev);ip_panel_off(view,9);
        ev[3]=0x19+9;ip_panel_key_event(view,(uint8_t *)ev);
        note_commit(0,27,62);note_commit(0,28,65);ip_panel_tick();
        assert(chord_calls==12 && !chord_count && last_down_key==9 && last_down_note==65);
        ip_panel_off(view,9);ip_panel_tick();assert(!mutes);
        /* The release gives a late commit its full grace period. */
        ev[3]=0x19+9;ip_panel_key_event(view,(uint8_t *)ev);
        clock_ms+=450;ip_panel_off(view,9);clock_ms+=100;ip_panel_tick();
        note_commit(0,31,62);ip_panel_tick();assert(chord_calls==13 && !mutes);
        /* A delayed commit must never write locks to a newly selected track. */
        ev[3]=0x19+8;ip_panel_key_event(view,(uint8_t *)ev);
        note_commit(0,29,60);selected=1;ip_panel_tick();
        assert(chord_calls==13 && !mutes);
        selected=0;ip_panel_off(view,8);
        ev[3]=0x19+8;ip_panel_key_event(view,(uint8_t *)ev);
        note_commit(0,30,60);step_record=0;ip_panel_tick();
        assert(chord_calls==13 && !mutes);ip_panel_off(view,8);
        step_record=0;
    }
    /* Foreign tracks never ask the sequencer. */
    live=1;machine=0;unsigned before=original_on,recorded=ip_panel_recorded;
    ip_panel_on(view,3,0,65,88,5,6,7);assert(original_on==before+1 && ip_panel_recorded==recorded);
    puts("ok");return 0;
}
