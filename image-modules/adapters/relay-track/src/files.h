#pragma once
#include <stdint.h>
enum { RF_SCAN = 1, RF_SAVE, RF_LOAD };
enum { RF_IDLE, RF_WORK, RF_SAVED, RF_LOADED, RF_FAILED, RF_SCANNED };
// Biased signed bytes: the minimum of either channel is low, the maximum high.
#define RELAY_WAVE_EMPTY 0x00ffu
#define RELAY_WAVE_SILENT 0x8080u
#define RELAY_WAVE_BINS 120u
// The scans the SRC view keeps (of 48 slots): one entry per slot does not fit in cf.expansion
// beside the system package block since the upstream merge of 2026-11-24.
#define RELAY_WAVE_CACHED 16u
static inline uint16_t relay_wave_join(uint16_t a, uint16_t b)
{
    return (((a & 0x00ff) < (b & 0x00ff) ? a : b) & 0x00ff) |
           (((a & 0xff00) > (b & 0xff00) ? a : b) & 0xff00);
}
// Slot = zero-based track + 16 * lane (main, first take, second take).
struct RelayOverview {
    uint32_t frames, starts[64], count, slot;
    uint16_t peaks[RELAY_WAVE_BINS];
};
struct RelayFiles {
    unsigned state, action, slot, progress, total, number, source;
    struct RelayOverview overview;
    const char *error;
    unsigned revision, grid;
};
extern struct RelayFiles relay_files;
int relay_file_start(unsigned action, unsigned slot, unsigned source, unsigned take, unsigned hits,
                     unsigned sensitivity);
void relay_files_reply(const volatile uint16_t *reply);
void relay_files_tick(void);
void relay_stop_scan(void);
const struct RelayOverview *rtm_wave(unsigned slot, unsigned revision, unsigned frames,
                                     unsigned hits, unsigned sensitivity);
const struct RelayOverview *rtm_wave_pending(unsigned slot, unsigned revision, unsigned frames);
