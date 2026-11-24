#include "src-mod.h"
extern uint32_t dkrt_instrument_type(void);
extern uint32_t dkrt_instrument_generation(void);
#include "instances.h"
#include "seam.h"
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
#ifdef DK_SERVICE
/* The service build: the registry codes are words of the image table (seam.h); ip_seam_rows fills the rows once the
 * service's setup has copied it. */
#define CODE(n) 0
static uint32_t rows[3][6] = {
#else
#define CODE(n) ((uint32_t)(uintptr_t)&ip_mod_##n)
static const uint32_t rows[3][6] = {
#endif
    {CODE(color), CODE(drift), CODE(mix), CODE(strum), CODE(gate), CODE(swing)},
    {CODE(position), CODE(scan_depth), CODE(scan_rate), CODE(timbre_env), CODE(wtfm_depth),
     CODE(wtfm_env)},
    {CODE(fm_depth), CODE(balance), CODE(feedback), CODE(fm_env), CODE(fm_attack),
     CODE(fm_release)}};
#undef CODE
#ifdef DK_SERVICE
void ip_seam_rows(void)
{
    const uint32_t codes[18] = {
        IP_VALUE(ip_mod_color), IP_VALUE(ip_mod_drift), IP_VALUE(ip_mod_mix),
        IP_VALUE(ip_mod_strum), IP_VALUE(ip_mod_gate), IP_VALUE(ip_mod_swing),
        IP_VALUE(ip_mod_position), IP_VALUE(ip_mod_scan_depth), IP_VALUE(ip_mod_scan_rate),
        IP_VALUE(ip_mod_timbre_env), IP_VALUE(ip_mod_wtfm_depth), IP_VALUE(ip_mod_wtfm_env),
        IP_VALUE(ip_mod_fm_depth), IP_VALUE(ip_mod_balance), IP_VALUE(ip_mod_feedback),
        IP_VALUE(ip_mod_fm_env), IP_VALUE(ip_mod_fm_attack), IP_VALUE(ip_mod_fm_release)};
    for (uint32_t i = 0; i < 18; ++i)
        rows[i / 6][i % 6] = codes[i];
}
#endif
static const uint8_t mirrors[6] = {26, 27, 28, 31, 32, 33};
static uint32_t published_family;
extern uint32_t rtm_mod_param(uint32_t type, uint32_t mirror) __attribute__((weak));
extern int rtm_mod_carrier(uint32_t type) __attribute__((weak));

void instrument_mod_publish(uint32_t family)
{
    if (family < 4)
        published_family = family;
}

uint32_t instrument_mod_param(uint32_t type, uint32_t mirror)
{
    if (type < 16 || type != dkrt_instrument_type() || !dkrt_instrument_generation())
        return IP_HAVE(rtm_mod_param) && type >= 7 ? rtm_mod_param(type, mirror) : UINT32_MAX;
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
    if (track >= 16)
        return 0;
    uint32_t actual = *(volatile const uint8_t *)(uintptr_t)(ip_mod_track_type + 0x9au * track);
    return (actual == type && type >= 16 && instrument_sound_admitted(track) &&
            dkrt_instrument_generation()) ||
           (IP_HAVE(rtm_mod_carrier) && actual >= 7 && rtm_mod_carrier(actual));
}

void instrument_mod_defaults(uint32_t source)
{
    if (!source)
        return;
    for (uint32_t i = 0; i < 6; ++i)
        *(volatile uint16_t *)(uintptr_t)(source + 0x14 + 2u * mirrors[i]) = 0x4000;
}
