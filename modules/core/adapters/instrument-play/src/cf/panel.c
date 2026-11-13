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
extern uint8_t ip_panel_step_record(uint32_t);
/* The active pattern (core/pattern-position); without it no chord is stored. */
extern uint32_t pp_current_pattern(void) __attribute__((weak));
/* control.c: the track's instance plays held keys as separate notes (PLAY). */
extern int instrument_track_plays_keys(uint32_t track);
/* src-native.c: chord-note locks on the given steps of the current track. */
extern int instrument_record_chord(const uint32_t steps[4],const int8_t *intervals,uint32_t count);
extern uint32_t instrument_src_now(void);
static uint8_t *panel_view;
static uint32_t panel_track=IP_NONE;
volatile uint32_t ip_panel_debug[8];
/* Key presses handed to the stock path for recording (proof counter). */
volatile uint32_t ip_panel_recorded;
/* Recording diagnostics: last chord step, chord notes stored, last result,
 * chords stored, watched step changes, last changed step and its note. */
volatile uint32_t ip_panel_chord[8];
/* Last 16 chord stores: step << 24 | note << 16 | group size << 8 | count. */
volatile uint32_t ip_panel_chord_log[16];
static uint32_t word(const uint8_t *p,uint32_t n) { return *(const uint32_t *)(p+n); }
/* KeyboardView +0x98 is the sequencer UI state (finding 123). While it live
 * records, step records, or a trig is held, the stock note action records the
 * key as a trig or a note lock. CANOPY still plays the press itself (all held
 * keys, as outside recording); the stock audition of the same press is muted
 * on the frame path, and the other held keys are stored on the recorded step
 * as chord-note locks so playback sounds the whole chord. */
static int recording(const uint8_t *view)
{
    uint32_t seq=word(view,0x98);
    return seq && (ip_panel_live_record(seq) || ip_panel_trig_held(seq) || ip_panel_step_record(seq));
}
/* Held recording keys, per physical key. A chord group is every key pressed
 * while another key of the group is still held; it closes when all are up. */
typedef struct { uint8_t used,pitch,track; } RecKey;
static RecKey rec_keys[16];
static uint8_t group_pitch[4],group_count;
static void group_append(uint32_t pitch)
{
    for(uint32_t i=0;i<group_count;++i) if(group_pitch[i]==pitch) {
        /* Move a repeated pitch to the newest position. */
        for(uint32_t j=i;j+1<group_count;++j) group_pitch[j]=group_pitch[j+1];
        --group_count; break;
    }
    if(group_count==4) { for(uint32_t j=0;j<3;++j) group_pitch[j]=group_pitch[j+1]; --group_count; }
    group_pitch[group_count++]=(uint8_t)pitch;
}
static void group_add(uint32_t track,uint32_t pitch)
{
    uint32_t held=0;
    for(uint32_t i=0;i<16;++i) held+=rec_keys[i].used && rec_keys[i].track==track;
    if(!held) group_count=0;
    group_append(pitch);
}
/* Step-record keys still waiting for their committed note (see below). */
enum { COMMITS=16, WAITING=4, WAIT_MS=500 };
typedef struct { uint8_t key,released; uint32_t group; } Waiting;
static Waiting waiting[WAITING];
static uint32_t waiting_count,waiting_track,commit_until,step_group,stored_group;
static volatile uint32_t commit_track=IP_NONE;
static int waiting_has(uint32_t key);
static void waiting_release(uint32_t key);
static void rec_release(uint32_t key)
{
    if(key>=16 || !rec_keys[key].used) return;
    uint32_t track=rec_keys[key].track; rec_keys[key].used=0;
    for(uint32_t i=0;i<16;++i) if(rec_keys[i].used && rec_keys[i].track==track) return;
    if(waiting_count && waiting_track==track) return;
    mi_seq_mute(track,0);
}
static void rec_release_all(void) { for(uint32_t i=0;i<16;++i) rec_release(i); }
/* DT2 1.17 pattern object (finding 125): each step's notes are a linked
 * list of 6-byte entries {track, step, note, velocity, ...} at +0x6a50, with
 * the head entry index of (track, step) at +0x12a50 (s16, negative = none),
 * next indices at +0x13a50, and the trig word of (track, step) at
 * +0x6a5 * track + 2 * step. */
enum { NL_ENTRIES=0x6a50, NL_HEAD=0x12a50, NL_NEXT=0x13a50, NL_COUNT=8192, TRACK_BYTES=0x6a5 };
static const uint8_t *pattern_object(void)
{ return pp_current_pattern ? (const uint8_t *)(uintptr_t)pp_current_pattern() : 0; }
static uint16_t trig_word(const uint8_t *p,uint32_t track,uint32_t step)
{ const uint8_t *b=p+TRACK_BYTES*track+2*step; return (uint16_t)((b[0]<<8)|b[1]); }
static int32_t note_head(const uint8_t *p,uint32_t track,uint32_t step)
{ const uint8_t *h=p+NL_HEAD+2*(128*track+step); return (int16_t)((h[0]<<8)|h[1]); }
/* Signature of the step's note list: head index and up to four notes of its
 * chain (next index of entry i at +0x13a50 + 2 * i), so that a note added to
 * an existing list is seen as a change too. */
static uint32_t note_signature(const uint8_t *p,uint32_t track,uint32_t step)
{
    int32_t entry=note_head(p,track,step);
    uint32_t sig=(uint32_t)(entry & 0xffff);
    for(uint32_t n=0;n<4 && entry>=0 && entry<NL_COUNT;++n) {
        const uint8_t *e=p+NL_ENTRIES+6*(uint32_t)entry;
        sig=(sig*31u)^((uint32_t)e[2]<<8)^e[3];
        const uint8_t *next=p+NL_NEXT+2*(uint32_t)entry;
        entry=(int16_t)((next[0]<<8)|next[1]);
    }
    return sig;
}
/* The step's first note, or -1. */
static int32_t step_note(const uint8_t *p,uint32_t track,uint32_t step)
{
    int32_t head=note_head(p,track,step);
    if(head<0 || head>=NL_COUNT) return -1;
    const uint8_t *e=p+NL_ENTRIES+6*(uint32_t)head;
    return e[0]==track && e[1]==step && e[2]<128 ? e[2] : -1;
}
/* The other notes of the chord group, newest first, as intervals from the
 * recorded note; at most three (CANOPY sounds four notes). */
static uint32_t chord_intervals(uint32_t track,uint32_t pitch,int8_t out[3])
{
    uint32_t count=0;
    if(!instrument_track_plays_keys(track)) return 0;
    for(uint32_t i=group_count;i-- && count<3;) {
        int32_t interval=(int32_t)group_pitch[i]-(int32_t)pitch;
        if(interval && interval>=-48 && interval<=48) out[count++]=(int8_t)interval;
    }
    return count;
}
static void store_chord(const uint32_t steps[4],uint32_t track,uint32_t pitch)
{
    int8_t intervals[3];
    uint32_t count=chord_intervals(track,pitch,intervals),first=0;
    for(uint32_t i=0;i<128;++i) if(steps[i>>5]>>(i&31)&1u) { first=i; break; }
    ip_panel_chord_log[ip_panel_chord[3]&15]=(first<<24)|((pitch&255)<<16)|((uint32_t)group_count<<8)|count;
    ip_panel_chord[1]=count;
    ip_panel_chord[2]=(uint32_t)instrument_record_chord(steps,intervals,count);
    ++ip_panel_chord[3];
}
/* Live and step recording commit the trig and its note list after the key
 * event, so the recorded steps are watched on the UI tick while a chord
 * group is recorded and briefly after its last release. A step whose note
 * list or trig changes and whose note belongs to the group gets the group's
 * other notes as chord-note locks. */
enum { WATCH_MS=1500 };
static uint32_t watch_active,watch_track,watch_until;
static uint32_t watch_list[128];
static uint16_t watch_trig[128];
static void watch_begin(uint32_t track)
{
    const uint8_t *p=pattern_object();
    if(!p || track>=16 || (watch_active && watch_track==track)) return;
    for(uint32_t s=0;s<128;++s) { watch_list[s]=note_signature(p,track,s); watch_trig[s]=trig_word(p,track,s); }
    watch_active=1; watch_track=track;
}
static int group_has(uint32_t pitch)
{
    for(uint32_t i=0;i<group_count;++i) if(group_pitch[i]==pitch) return 1;
    return 0;
}
static void panel_play(uint8_t *view,uint32_t key,uint32_t track,uint32_t pitch,uint32_t velocity)
{
    if(panel_view && (panel_view!=view || panel_track!=track)) { mi_panel_release(); rec_release_all(); }
    panel_view=view; panel_track=track;
    instrument_select_track(track);
    ++ip_panel_debug[0]; ip_panel_debug[2]=key; ip_panel_debug[3]=track;
    ip_panel_debug[4]=pitch; ip_panel_debug[5]=velocity;
    (void)mi_panel_key(key,pitch,velocity,1,track);
}
void ip_panel_on(uint8_t *view,uint32_t key,uint32_t track,uint32_t pitch,
    uint32_t velocity,uint32_t a,uint32_t b,uint32_t c)
{
    uint32_t type=dkrt_instrument_type();
    uint8_t *object=track<16 && instrument_track_owned(track) && type>=7 && type<32 ?
        ip_panel_track_object(word(view,0xa4),track) : 0;
    int canopy=object && ip_panel_track_type(object)==type && key<16 && ip_panel_active(word(view,0xa0));
    if(canopy && recording(view)) {
        uint32_t seq=word(view,0x98);
        ++ip_panel_recorded;
        group_add(track,pitch);
        rec_keys[key].used=1; rec_keys[key].pitch=(uint8_t)pitch; rec_keys[key].track=(uint8_t)track;
        mi_seq_mute(track,1);
        int held=ip_panel_trig_held(seq);
        if(!held) watch_begin(track);
        ip_panel_original_on(view,key,track,pitch,velocity,a,b,c);
        if(held) {
            /* A held trig takes the note at once: store the chord there. */
            uint32_t steps[4];
            for(uint32_t i=0;i<4;++i) steps[i]=word((const uint8_t *)(uintptr_t)seq,0x258+4*i);
            store_chord(steps,track,pitch);
        }
        panel_play(view,key,track,pitch,velocity);
        return;
    }
    if(canopy) { panel_play(view,key,track,pitch,velocity); return; }
    ip_panel_original_on(view,key,track,pitch,velocity,a,b,c);
}
void ip_panel_off(uint8_t *view,uint32_t key)
{
    /* Consume tombstones even after track/machine/mode changes. Stock owns
     * its own vector and still receives releases for non-CANOPY auditions. */
    if(key<16) { (void)mi_panel_key(key,0,0,0,panel_track); ++ip_panel_debug[1]; }
    if(key<16 && waiting_has(key)) {
        /* Released before its note was committed: the commit still counts. */
        waiting_release(key); commit_until=instrument_src_now()+WAIT_MS;
    }
    if(key<16 && rec_keys[key].used) {
        uint32_t track=rec_keys[key].track;
        rec_release(key);
        /* Only live recording commits later than its keys' releases. */
        if(track!=commit_track) watch_until=instrument_src_now()+WATCH_MS;
    }
    ip_panel_original_off(view,key);
}
static void step_commits(void);
/* UI task (instrument_play_tick). */
void ip_panel_tick(void)
{
    step_commits();
    if(!watch_active) return;
    /* Step-record entries are taken from their commits, not by this watch. */
    if(watch_track==commit_track) { watch_active=0; return; }
    const uint8_t *p=pattern_object();
    uint32_t track=watch_track,held=0;
    for(uint32_t i=0;i<16;++i) held+=rec_keys[i].used && rec_keys[i].track==track;
    /* Lock writes address the UI's current track: only while it is ours. */
    if(!p || !panel_view || ip_panel_selected_track(word(panel_view,0xa0))!=track) { watch_active=0; return; }
    if(!held && (int32_t)(instrument_src_now()-watch_until)>=0) { watch_active=0; return; }
    for(uint32_t s=0;s<128;++s) {
        uint32_t list=note_signature(p,track,s);
        uint16_t trig=trig_word(p,track,s);
        if(list==watch_list[s] && trig==watch_trig[s]) continue;
        watch_list[s]=list; watch_trig[s]=trig;
        int32_t note=step_note(p,track,s);
        ++ip_panel_chord[4]; ip_panel_chord[5]=s; ip_panel_chord[6]=(uint32_t)note;
        if(!(trig&1u) || note<0 || !group_has((uint32_t)note)) continue;
        uint32_t steps[4];
        steps[0]=steps[1]=steps[2]=steps[3]=0; /* no memset in this image */
        steps[s>>5]=1u<<(s&31);
        store_chord(steps,track,(uint32_t)note);
        ip_panel_chord[0]=s;
        watch_list[s]=note_signature(p,track,s); watch_trig[s]=trig_word(p,track,s);
    }
}
/* Step recording (finding 125): with KEYBOARD held, a trig key writes its
 * note on the cursor step at once, inside the stock KeyboardView key handler,
 * without the note action (ip_panel_on). Each further held key rewrites the
 * same step's note, and the cursor advances when the keys are released. */
static int step_recording(uint8_t *view,uint32_t *track_out)
{
    uint32_t seq=word(view,0x98),type=dkrt_instrument_type();
    if(!seq || !ip_panel_step_record(seq) || type<7 || type>=32) return 0;
    uint32_t track=ip_panel_selected_track(word(view,0xa0));
    if(track>=16 || !instrument_track_owned(track)) return 0;
    uint8_t *object=ip_panel_track_object(word(view,0xa4),track);
    if(!object || ip_panel_track_type(object)!=type) return 0;
    *track_out=track; return 1;
}
/* Every stock note commit goes through the note-list writer
 * 0x400392be(editor, track, step, records) (hooked below); records are 4-byte
 * {note, velocity, ...} items. While a step-recorded chord is being entered on
 * a CANOPY track, each commit is queued here, in order, and the UI tick takes
 * them: a commit's note belongs to the oldest key still waiting for its note.
 * The commit can follow the key event, and simultaneous presses commit one
 * after another, so neither key order nor timing can lose a note. */
extern void ip_note_list_original(uint8_t *,uint32_t,uint32_t,uint32_t *);
extern uint32_t dkrt_irq_mask(void);
extern void dkrt_irq_restore(uint32_t);
typedef struct { uint8_t track,step,note; } Commit;
static Commit commits[COMMITS];
static volatile uint32_t commit_head,commit_count;
static uint32_t waiting_at;
static uint8_t *waiting_view;
/* Step-entry diagnostics: trig-key events, entries started, commits queued,
 * commits used, commits dropped, last commit note, waits expired, last code. */
volatile uint32_t ip_panel_step[8];
void ip_note_list_write(uint8_t *editor,uint32_t track,uint32_t step,uint32_t *records)
{
    uint32_t note=128;
    if(records && track==commit_track && step<128) {
        const uint8_t *first=(const uint8_t *)(uintptr_t)records[0],*end=(const uint8_t *)(uintptr_t)records[1];
        if(first && first<end && first[0]<128) note=first[0];
    }
    /* Publish only after the stock writer has finished updating the pattern. */
    ip_note_list_original(editor,track,step,records);
    if(note<128) {
        uint32_t sr=dkrt_irq_mask();
        if(track==commit_track && commit_count<COMMITS) {
            Commit *c=&commits[(commit_head+commit_count)%COMMITS];
            c->track=(uint8_t)track; c->step=(uint8_t)step; c->note=(uint8_t)note;
            ++commit_count; ++ip_panel_step[2];
        } else if(track==commit_track) ++ip_panel_step[4];
        dkrt_irq_restore(sr);
    }
}
static int waiting_has(uint32_t key)
{
    for(uint32_t i=0;i<waiting_count;++i) if(waiting[i].key==key && !waiting[i].released) return 1;
    return 0;
}
static void waiting_release(uint32_t key)
{
    for(uint32_t i=0;i<waiting_count;++i) if(waiting[i].key==key && !waiting[i].released) { waiting[i].released=1; return; }
}
static uint32_t keys_held(uint32_t track)
{
    uint32_t held=0;
    for(uint32_t i=0;i<16;++i) held+=rec_keys[i].used && rec_keys[i].track==track;
    return held;
}
/* UI task: each queued commit, in order. Its note belongs to the oldest key
 * still waiting (that key now sounds it); a commit after its key's release
 * still joins the chord. The chord's other notes go on the committed step. */
static void step_commits(void)
{
    uint32_t now=instrument_src_now();
    /* Chord locks address the selected track. Discard pending entries after
     * changing tracks, machines, or leaving step-record mode. */
    uint32_t current_track=IP_NONE;
    if(commit_track<16 && waiting_view &&
       (!step_recording(waiting_view,&current_track) || current_track!=commit_track)) {
        uint32_t sr=dkrt_irq_mask(); commit_count=0; commit_head=0; dkrt_irq_restore(sr);
        waiting_count=0; mi_seq_mute(commit_track,0); commit_track=IP_NONE;
        return;
    }
    for(;;) {
        uint32_t sr=dkrt_irq_mask();
        if(!commit_count) { dkrt_irq_restore(sr); break; }
        Commit c=commits[commit_head];
        commit_head=(commit_head+1)%COMMITS; --commit_count;
        dkrt_irq_restore(sr);
        if(c.track!=commit_track || (int32_t)(now-commit_until)>0 || !waiting_count || waiting_track!=c.track) continue;
        Waiting w=waiting[0];
        for(uint32_t i=1;i<waiting_count;++i) waiting[i-1]=waiting[i];
        --waiting_count;
        if(stored_group!=w.group) { group_count=0; stored_group=w.group; }
        group_append(c.note);
        if(!w.released) { rec_keys[w.key].used=1; rec_keys[w.key].pitch=c.note; rec_keys[w.key].track=c.track; }
        ++ip_panel_recorded; ++ip_panel_step[3]; ip_panel_step[5]=c.note;
        uint32_t steps[4];
        steps[0]=steps[1]=steps[2]=steps[3]=0; /* no memset in this image */
        steps[c.step>>5]=1u<<(c.step&31);
        store_chord(steps,c.track,c.note);
        ip_panel_chord[0]=c.step;
        /* A live watch on this track must not take the same entry again. */
        const uint8_t *p=pattern_object();
        if(p && watch_active && watch_track==c.track) {
            watch_list[c.step]=note_signature(p,c.track,c.step); watch_trig[c.step]=trig_word(p,c.track,c.step);
        }
        if(!w.released && waiting_view) panel_play(waiting_view,w.key,c.track,c.note,100);
    }
    if(waiting_count && (int32_t)(now-commit_until)>=0) { ip_panel_step[6]+=waiting_count; waiting_count=0; }
    if(commit_track<16 && !waiting_count && !keys_held(commit_track)) {
        mi_seq_mute(commit_track,0); commit_track=IP_NONE;
    }
}
void ip_panel_update(uint8_t *view)
{
    uint32_t step_track;
    /* KEYBOARD is only held during step entry: keep the entered notes. */
    if(panel_view==view && step_recording(view,&step_track) && step_track==panel_track) return;
    if(panel_view==view && (!ip_panel_active(word(view,0xa0)) ||
        ip_panel_selected_track(word(view,0xa0))!=panel_track)) {
        mi_panel_release(); rec_release_all(); panel_view=0; ++ip_panel_debug[6];
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
        mi_panel_release(); rec_release_all(); panel_view=0; ++ip_panel_debug[6];
    }
}
uint32_t ip_panel_key_event(uint8_t *view,uint8_t *event)
{
    /* Trig keys are codes 0x19..0x28 (physical keys 0..15). */
    uint32_t key=event ? word(event,0xc)-0x19u : 16u,track=IP_NONE;
    step_commits();
    /* Firmware flags: bit 0 is press, bit 3 is auto-repeat, bit 4 is
     * release. The note-off callback may run before the release event. */
    uint32_t flags=event ? word(event,0x10) : 0;
    int entry=key<16 && (flags&1u) && !(flags&8u) && step_recording(view,&track);
    if(key<16) { ++ip_panel_step[0]; ip_panel_step[7]=word(event,0xc); }
    /* A held or waiting key's further events (its release) are not entries. */
    if(entry && (rec_keys[key].used || waiting_has(key) || waiting_count>=WAITING)) entry=0;
    if(entry) {
        if(waiting_count && waiting_track!=track) waiting_count=0;
        uint32_t held=keys_held(track);
        for(uint32_t i=0;i<waiting_count;++i) held+=!waiting[i].released && waiting_track==track;
        if(!held) ++step_group;
        waiting[waiting_count].key=(uint8_t)key; waiting[waiting_count].released=0;
        waiting[waiting_count++].group=step_group; waiting_track=track;
        waiting_view=view; waiting_at=instrument_src_now();
        commit_track=track; commit_until=waiting_at+WAIT_MS;
        /* The stock audition of the entry would sound it a second time. */
        mi_seq_mute(track,1);
        ++ip_panel_step[1];
    }
    uint32_t result=ip_panel_stock_key(view,event);
    step_commits();
    ip_panel_update(view);
    return result;
}
void ip_panel_detach(uint8_t *view,uint8_t *manager)
{
    if(panel_view==view) { mi_panel_release(); rec_release_all(); panel_view=0; ++ip_panel_debug[7]; }
    ip_panel_stock_detach(view,manager);
}
