#pragma once
#include <stdint.h>
enum { RF_SCAN = 1, RF_SAVE, RF_LOAD };
enum { RF_IDLE, RF_WORK, RF_SAVED, RF_LOADED, RF_FAILED, RF_SCANNED };
// Biased signed nibbles, low to high: left min/max, right min/max.
#define RELAY_WAVE_EMPTY 0x0f0fu
#define RELAY_WAVE_SILENT 0x8888u
#define RELAY_WAVE_BINS 120u
static inline uint16_t relay_wave_join(uint16_t a, uint16_t b)
{
    return (((a & 0x000f) < (b & 0x000f) ? a : b) & 0x000f) |
           (((a & 0x00f0) > (b & 0x00f0) ? a : b) & 0x00f0) |
           (((a & 0x0f00) < (b & 0x0f00) ? a : b) & 0x0f00) |
           (((a & 0xf000) > (b & 0xf000) ? a : b) & 0xf000);
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
