#if defined(__m68k__) && defined(__GNUC__)
/* No implicit memcpy/memset calls: this object links without libc. */
#pragma GCC optimize ("no-tree-loop-distribute-patterns")
#endif
#include "sound-assoc.h"

static void copy40(uint8_t *d,const uint8_t *s) { for(uint32_t i=0;i<SR_BYTES;++i) d[i]=s[i]; }
static uint32_t tick(SaStore *s) { if(!++s->clock) ++s->clock; return s->clock; }
static uintptr_t end_of(uintptr_t a,uint32_t len)
{
    uintptr_t e=a+len;
    return e<a ? (uintptr_t)-1 : e;
}

uint32_t sa_init(SaStore *s,uintptr_t kits,uintptr_t pool,void *memory,uint32_t bytes)
{
    uint8_t *m=(uint8_t *)memory;
    for(uint32_t i=0;i<sizeof(*s);++i) ((uint8_t *)s)[i]=0;
    s->kits=kits; s->pool=pool; s->window=kits&~(uintptr_t)(SA_WINDOW-1);
    if(!m || bytes<SA_SLOT_BYTES+sizeof(SaTemp)) return 0;
    for(uint32_t i=0;i<bytes;++i) m[i]=0;
    s->slot=(SaSlot *)(void *)m;
    s->temp=(SaTemp *)(void *)(m+SA_SLOT_BYTES);
    s->temps=(bytes-SA_SLOT_BYTES)/(uint32_t)sizeof(SaTemp);
    return s->temps;
}

int sa_slot(const SaStore *s,uintptr_t sound)
{
    if(!s->slot) return -1;
    if(sound>=s->kits && sound<s->kits+(uintptr_t)SA_KITS*SA_KIT) {
        uintptr_t off=sound-s->kits;
        uint32_t k=(uint32_t)(off/SA_KIT), r=(uint32_t)(off%SA_KIT);
        if(r<SA_KIT_FIRST || (r-SA_KIT_FIRST)%SA_SOUND) return -1;
        uint32_t t=(r-SA_KIT_FIRST)/SA_SOUND;
        return t<SA_TRACKS ? (int)(k*SA_TRACKS+t) : -1;
    }
    if(sound>=s->pool && sound<s->pool+(uintptr_t)SA_POOL*SA_SOUND) {
        uintptr_t off=sound-s->pool;
        return off%SA_SOUND ? -1 : (int)(SA_KITS*SA_TRACKS+off/SA_SOUND);
    }
    return -1;
}

/* Calls fn for every fixed slot whose whole Sound lies in [a, a+len). */
typedef void (*SaVisit)(SaStore *,uint32_t index,uintptr_t start,void *context);
static void each_slot(SaStore *s,uintptr_t a,uint32_t len,SaVisit fn,void *context)
{
    uintptr_t e=end_of(a,len);
    uintptr_t k0=s->kits, k1=s->kits+(uintptr_t)SA_KITS*SA_KIT;
    if(e>k0 && a<k1) {
        uint32_t first=a>k0 ? (uint32_t)((a-k0)/SA_KIT) : 0;
        uint32_t last=(uint32_t)(((e<k1 ? e : k1)-1-k0)/SA_KIT);
        for(uint32_t k=first;k<=last && k<SA_KITS;++k)
            for(uint32_t t=0;t<SA_TRACKS;++t) {
                uintptr_t start=k0+(uintptr_t)k*SA_KIT+SA_KIT_FIRST+(uintptr_t)t*SA_SOUND;
                if(start>=a && start+SA_SOUND<=e) fn(s,k*SA_TRACKS+t,start,context);
            }
    }
    uintptr_t p0=s->pool, p1=s->pool+(uintptr_t)SA_POOL*SA_SOUND;
    if(e>p0 && a<p1) {
        uint32_t i=a>p0 ? (uint32_t)((a-p0+SA_SOUND-1)/SA_SOUND) : 0;
        for(;i<SA_POOL;++i) {
            uintptr_t start=p0+(uintptr_t)i*SA_SOUND;
            if(start+SA_SOUND>e) break;
            fn(s,SA_KITS*SA_TRACKS+i,start,context);
        }
    }
}

/* Header, parameters, machine fields and name; not the slice table. Sounds
 * are only 2-byte aligned (the pool), so read halfwords. */
uint32_t sa_seal(uintptr_t sound)
{
    const volatile uint16_t *p=(const volatile uint16_t *)sound;
    uint32_t h=2166136261u;
    for(uint32_t i=0;i<SA_SEAL_BYTES/2;++i) h=(h^p[i])*16777619u;
    return h;
}

/* Temporary entries are packed in temp[0, used); a dead entry (addr 0) is
 * removed by compact() at the end of every public operation. pages[] marks
 * the pages holding a temporary, so a copy away from every Sound returns at
 * once; marks are recomputed by compact() after any removal. */
static int in_window(const SaStore *s,uintptr_t a) { return a>=s->window && a-s->window<SA_WINDOW; }
static void mark(SaStore *s,uintptr_t addr)
{
    if(!in_window(s,addr) || !in_window(s,addr+SA_SOUND-1)) { ++s->far; return; }
    uintptr_t last=(addr+SA_SOUND-1-s->window)>>SA_PAGE_SHIFT;
    for(uintptr_t p=(addr-s->window)>>SA_PAGE_SHIFT;p<=last;++p) s->pages[p>>5]|=1u<<(p&31);
}
static void compact(SaStore *s)
{
    if(!s->holes) return;
    uint32_t n=0;
    for(uint32_t i=0;i<s->used;++i)
        if(s->temp[i].addr) {
            if(n!=i) {
                SaTemp *d=&s->temp[n], *t=&s->temp[i];
                d->addr=t->addr; d->seal=t->seal; d->stamp=t->stamp; copy40(d->payload,t->payload);
            }
            ++n;
        }
    s->used=n; s->holes=0; s->far=0;
    for(uint32_t i=0;i<SA_PAGE_WORDS;++i) s->pages[i]=0;
    for(uint32_t i=0;i<n;++i) mark(s,s->temp[i].addr);
}
static int temps_near(const SaStore *s,uintptr_t a,uint32_t len)
{
    if(!s->used) return 0;
    if(s->far) return 1;
    uintptr_t e=end_of(a,len), w1=s->window+SA_WINDOW;
    if(e<=s->window || a>=w1) return 0;
    uintptr_t lo=a>s->window ? a-s->window : 0, hi=(e<w1 ? e : w1)-1-s->window;
    for(uintptr_t p=lo>>SA_PAGE_SHIFT;p<=(hi>>SA_PAGE_SHIFT);++p)
        if(s->pages[p>>5] & (1u<<(p&31))) return 1;
    return 0;
}
static int canon_near(const SaStore *s,uintptr_t a,uint32_t len)
{
    uintptr_t e=end_of(a,len);
    return (e>s->kits && a<s->kits+(uintptr_t)SA_KITS*SA_KIT) ||
        (e>s->pool && a<s->pool+(uintptr_t)SA_POOL*SA_SOUND);
}

static SaTemp *temp_at(SaStore *s,uintptr_t addr)
{
    for(uint32_t i=0;i<s->used;++i) if(s->temp[i].addr==addr) return &s->temp[i];
    return 0;
}
static void kill(SaStore *s,SaTemp *t) { t->addr=0; s->holes=1; }
static void temp_drop(SaStore *s,uintptr_t addr)
{
    SaTemp *t=temp_at(s,addr);
    if(t) kill(s,t);
}
/* When full, evicts the oldest entry outside [keep, keep_end), the range
 * being copied from. */
static void temp_set(SaStore *s,uintptr_t addr,const uint8_t *payload,uint32_t seal,uint32_t stamp,
    uintptr_t keep,uintptr_t keep_end)
{
    if(!s->temps || sa_slot(s,addr)>=0) return;
    SaTemp *t=temp_at(s,addr);
    if(!t && s->used<s->temps) { t=&s->temp[s->used++]; mark(s,addr); }
    if(!t) {
        for(uint32_t i=0;i<s->used;++i) {
            SaTemp *c=&s->temp[i];
            if(!c->addr || (c->addr>=keep && c->addr<keep_end)) continue;
            if(!t || c->stamp<t->stamp) t=c;
        }
        if(!t) return;
        ++s->stats[SA_STAT_EVICTIONS];
        s->holes=1;   /* recompute the page marks */
        mark(s,addr);
    }
    uint8_t copy[SR_BYTES]; copy40(copy,payload);
    t->addr=addr; t->seal=seal; t->stamp=stamp; copy40(t->payload,copy);
}

/* A Sound's current extension. Temporary entries count only while sealed. */
static int lookup(SaStore *s,uintptr_t sound,uint8_t out[SR_BYTES])
{
    int index=sa_slot(s,sound);
    if(index>=0) {
        if(!s->slot[index].present) return 0;
        copy40(out,s->slot[index].payload);
        return 1;
    }
    SaTemp *t=temp_at(s,sound);
    if(!t) return 0;
    if(t->seal!=sa_seal(sound)) { kill(s,t); ++s->stats[SA_STAT_TEMP_STALE]; return 0; }
    copy40(out,t->payload);
    ++s->stats[SA_STAT_TEMP_HITS];
    return 1;
}

typedef struct { uintptr_t dst, src, src_end; uint32_t stamp; } CopyCtx;
static void copy_into_slot(SaStore *s,uint32_t index,uintptr_t start,void *context)
{
    const CopyCtx *c=(const CopyCtx *)context;
    uint8_t payload[SR_BYTES];
    int present=lookup(s,c->src+(start-c->dst),payload);
    SaSlot *d=&s->slot[index];
    d->present=(uint8_t)present;
    if(present) copy40(d->payload,payload);
    d->seq=tick(s);
}
static void copy_from_slot(SaStore *s,uint32_t index,uintptr_t start,void *context)
{
    const CopyCtx *c=(const CopyCtx *)context;
    uintptr_t dst=c->dst+(start-c->src);
    if(sa_slot(s,dst)>=0) return;
    if(s->slot[index].present) temp_set(s,dst,s->slot[index].payload,sa_seal(dst),c->stamp,c->src,c->src_end);
    else temp_drop(s,dst);
}

void sa_copied(SaStore *s,uintptr_t dst,uintptr_t src,uint32_t len)
{
    if(!s->slot || len<SA_SOUND || dst==src) return;
    if(!canon_near(s,dst,len) && !canon_near(s,src,len) && !temps_near(s,src,len) && !temps_near(s,dst,len)) {
        ++s->stats[SA_STAT_PASSED];
        return;
    }
    ++s->stats[SA_STAT_COPIES];
    uintptr_t src_end=end_of(src,len), dst_end=end_of(dst,len);
    CopyCtx c={dst,src,src_end,tick(s)};
    each_slot(s,dst,len,copy_into_slot,&c);
    each_slot(s,src,len,copy_from_slot,&c);
    uint32_t n=s->used;   /* entries appended below are destinations, not sources */
    for(uint32_t i=0;i<n;++i) {
        SaTemp *t=&s->temp[i];
        if(!t->addr || t->stamp==c.stamp || t->addr<src || t->addr+SA_SOUND>src_end) continue;
        uintptr_t to=dst+(t->addr-src);
        if(sa_slot(s,to)>=0) continue;
        if(t->seal==sa_seal(t->addr)) {
            ++s->stats[SA_STAT_TEMP_HITS];
            uint8_t payload[SR_BYTES]; copy40(payload,t->payload);
            temp_set(s,to,payload,t->seal,c.stamp,src,src_end);
        } else {
            ++s->stats[SA_STAT_TEMP_STALE];
            kill(s,t);
        }
    }
    /* Whatever else lay under the destination was overwritten. */
    for(uint32_t i=0;i<s->used;++i) {
        SaTemp *t=&s->temp[i];
        if(t->addr && t->stamp!=c.stamp && t->addr<dst_end && t->addr+SA_SOUND>dst) kill(s,t);
    }
    compact(s);
}

static void clear_slot(SaStore *s,uint32_t index,uintptr_t start,void *context)
{
    (void)start; (void)context;
    s->slot[index].present=0;
    s->slot[index].seq=tick(s);
}
void sa_cleared(SaStore *s,uintptr_t dst,uint32_t len)
{
    if(!s->slot || len<SA_SOUND) return;
    if(!canon_near(s,dst,len) && !temps_near(s,dst,len)) { ++s->stats[SA_STAT_PASSED]; return; }
    ++s->stats[SA_STAT_CLEARS];
    each_slot(s,dst,len,clear_slot,0);
    uintptr_t e=end_of(dst,len);
    for(uint32_t i=0;i<s->used;++i) {
        SaTemp *t=&s->temp[i];
        if(t->addr && t->addr<e && t->addr+SA_SOUND>dst) kill(s,t);
    }
    compact(s);
}

void sa_replaced(SaStore *s,uintptr_t sound,const uint8_t *payload)
{
    if(!s->slot) return;
    int present=payload && sr_present(payload);
    ++s->stats[payload ? SA_STAT_LOADS : SA_STAT_INITS];
    int index=sa_slot(s,sound);
    if(index>=0) {
        SaSlot *d=&s->slot[index];
        d->present=(uint8_t)present;
        if(present) copy40(d->payload,payload);
        d->seq=tick(s);
        return;
    }
    if(present) temp_set(s,sound,payload,sa_seal(sound),tick(s),0,0);
    else temp_drop(s,sound);
    compact(s);
}

int sa_get(SaStore *s,uintptr_t sound,uint8_t out[SR_BYTES],uint32_t *seq)
{
    if(seq) *seq=sa_seq(s,sound);
    if(!s->slot) return 0;
    int present=lookup(s,sound,out);
    compact(s);
    return present;
}

uint32_t sa_put(SaStore *s,uintptr_t sound,const uint8_t payload[SR_BYTES])
{
    int index=s->slot ? sa_slot(s,sound) : -1;
    if(index<0 || !payload) return 0;
    SaSlot *d=&s->slot[index];
    d->present=1;
    copy40(d->payload,payload);
    ++s->stats[SA_STAT_PUTS];
    return d->seq=tick(s);
}

uint32_t sa_seq(const SaStore *s,uintptr_t sound)
{
    int index=sa_slot(s,sound);
    return index>=0 ? s->slot[index].seq : 0;
}
