#pragma once
#include <stdint.h>
enum { RF_SCAN = 1, RF_SAVE, RF_LOAD };
enum { RF_IDLE, RF_WORK, RF_SAVED, RF_LOADED, RF_FAILED, RF_SCANNED };
// Slot = zero-based track + 16 * lane (main, first take, second take).
struct RelayOverview {
    uint32_t frames, starts[64], count, slot;
    uint16_t peaks[120];
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
