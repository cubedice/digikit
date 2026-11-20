/* The pattern a boundary carries: the playing one, taken at master step edges (finding 365). The tick's state is given
 * as it is at each snapshot: START, the edge (substep 0), track 1's playing pattern, the scheduled pattern, the
 * scheduled length (override first) and the playing pattern's own length. */
#include <assert.h>
#define PP_QUEUE_TEST
#include "../src/cf/boundary.c"

#define P1 0x41000000u
#define P2 0x42000000u
#define P3 0x43000000u

static u32 shown[2];

static void snap(u32 start, u32 edge, u32 playing, u32 scheduled, u32 scheduled_length, u32 own)
{
    pp_playing(start, edge, playing, scheduled, scheduled_length, own, shown);
}

/* One snapshot and its boundary, committed as the tick's type-5 (or START's type-7) event of that deadline. */
static void boundary(u32 clock, u32 start, u32 edge, u32 playing, u32 scheduled,
                     u32 scheduled_length, u32 own, u32 step)
{
    snap(start, edge, playing, scheduled, scheduled_length, own);
    pp_observe_scaled(start, clock, shown[0], step, shown[1], 5400000);
    pp_commit(clock, start ? 7 : 5, clock / 100);
}

int main(void)
{
    /* START on P1 (16 steps): the playing pattern is the scheduled one, with its length */
    boundary(100, 1, 1, P1, P1, 16, 16, 0);
    assert(pp_position[6] == P1 && pp_position[7] == 0 && pp_position[8] == 16 && valid);
    for (u32 s = 1; s < 15; ++s)
        boundary(100 + 100 * s, 0, 1, P1, P1, 16, 16, s);
    assert(pp_position[3] == 15 && pp_position[7] == 14);

    /* A change at the end to P2 (8 steps): at the edge of P1's last step the scheduled pointer is P2 already, with its
     * length; the boundary is P1's step 15 of 16 */
    boundary(1600, 0, 1, P1, P2, 8, 16, 15);
    assert(pp_position[3] == 16 && pp_position[6] == P1 && pp_position[7] == 15 &&
           pp_position[8] == 16 && valid);
    /* the substeps of that step: no boundary, nothing moves */
    for (u32 k = 1; k < 6; ++k) {
        snap(0, 0, P1, P2, 8, 16);
        pp_observe_scaled(0, 1600 + 10 * k, shown[0], 15, shown[1], 5400000);
        assert(shown[0] == P1 && shown[1] == 16);
    }
    assert(count == 0);
    /* a track faster than the master grid takes P2 for its old step before the change is heard: not an edge */
    snap(0, 0, P2, P2, 8, 8);
    assert(shown[0] == P1 && shown[1] == 16);
    /* the next edge: P2 is heard from its first step */
    boundary(1700, 0, 1, P2, P2, 8, 8, 0);
    assert(pp_position[3] == 17 && pp_position[6] == P2 && pp_position[7] == 0 &&
           pp_position[8] == 8 && valid);

    /* A direct change: the scheduled and the playing pattern move in the same tick, at an edge */
    boundary(1800, 0, 1, P3, P3, 12, 12, 5);
    assert(pp_position[6] == P3 && pp_position[7] == 5 && pp_position[8] == 12 && valid);
    /* the same step again with another pattern (a direct start during the first step): a boundary as well */
    boundary(1900, 0, 1, P3, P3, 12, 12, 0);
    boundary(2000, 0, 1, P2, P2, 8, 8, 0);
    assert(pp_position[3] == 20 && pp_position[6] == P2 && pp_position[7] == 0);

    /* the master override is the scheduled length while the two are the same */
    boundary(2100, 0, 1, P2, P2, 64, 8, 1);
    assert(pp_position[8] == 64);
    /* while the scheduled pattern has moved on, the playing one keeps the length taken for it */
    boundary(2200, 0, 1, P2, P1, 16, 8, 2);
    assert(pp_position[6] == P2 && pp_position[8] == 64);
    /* a playing pattern that is neither the scheduled one nor the one before: its own length */
    boundary(2300, 0, 1, P3, P1, 16, 12, 3);
    assert(pp_position[6] == P3 && pp_position[8] == 12);
    return 0;
}
