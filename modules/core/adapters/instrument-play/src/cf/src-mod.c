#include "src-mod.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
#include "instances.h"
/* The verified LEA operand holds the SRAM row address of track 0. Extern
 * mappings name anchors; load the operand before reading the current
 * track-type byte. DT2 1.17 outgoing machine rows are 0x9a bytes per track
 * (the stock refresh copies 0x9a bytes from Sound+0xa2 to row+0x9a*track). */
extern const uint32_t ip_mod_track_type;
#define ROW(n) extern const uint8_t ip_mod_##n
ROW(color);
ROW(drift);
ROW(mix);
ROW(strum);
ROW(gate);
ROW(swing);
ROW(position);
ROW(scan_depth);
ROW(scan_rate);
ROW(timbre_env);
ROW(wtfm_depth);
ROW(wtfm_env);
ROW(fm_depth);
ROW(balance);
ROW(feedback);
ROW(fm_env);
ROW(fm_attack);
ROW(fm_release);
#undef ROW
#define CODE(n) ((uint32_t)(uintptr_t)&ip_mod_##n)
static const uint32_t rows[3][6] = {
    {CODE(color), CODE(drift), CODE(mix), CODE(strum), CODE(gate), CODE(swing)},
    {CODE(position), CODE(scan_depth), CODE(scan_rate), CODE(timbre_env), CODE(wtfm_depth),
     CODE(wtfm_env)},
    {CODE(fm_depth), CODE(balance), CODE(feedback), CODE(fm_env), CODE(fm_attack),
     CODE(fm_release)}};
#undef CODE
static const uint8_t mirrors[6] = {26, 27, 28, 31, 32, 33};
static uint32_t published_family;

void instrument_mod_publish(uint32_t family)
{
    if (family < 4)
        published_family = family;
}

uint32_t instrument_mod_param(uint32_t type, uint32_t mirror)
{
    if (type < 16 || type != dkrt_instrument_type() || !dkrt_instrument_generation())
        return UINT32_MAX;
    /* Native sampler bank/slot aliases have no meaning for this instrument. */
    if (mirror == 29 || mirror == 30)
        return 0;
    for (uint32_t i = 0; i < 6; ++i)
        if (mirror == mirrors[i])
            return rows[published_family < 2 ? 0 : published_family - 1][i];
    return UINT32_MAX;
}

int instrument_mod_carrier(uint32_t track)
{
    uint32_t type = dkrt_instrument_type();
    return track < 16 && type >= 16 && instrument_sound_admitted(track) &&
           dkrt_instrument_generation() &&
           *(volatile const uint8_t *)(uintptr_t)(ip_mod_track_type + 0x9au * track) == type;
}

void instrument_mod_defaults(uint32_t source)
{
    if (!source)
        return;
    for (uint32_t i = 0; i < 6; ++i)
        *(volatile uint16_t *)(uintptr_t)(source + 0x14 + 2u * mirrors[i]) = 0x4000;
}
