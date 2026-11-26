#include "track-midi.h"

extern const uint8_t tk_fw_midi_selected_lit[];
extern const uint8_t tk_fw_midi_routes_lit[];
extern const uint8_t tk_fw_midi_rx_notes_lit[];
extern const uint8_t tk_fw_midi_ports_lit[];

static uintptr_t literal(const uint8_t *operand)
{
    /* ColdFire absolute operands may only be halfword aligned. */
    return (uintptr_t)operand[0] << 24 | (uintptr_t)operand[1] << 16 | (uintptr_t)operand[2] << 8 |
           operand[3];
}

uint8_t tk_midi_selected_track(void)
{
    const volatile uint16_t *track = (const volatile uint16_t *)literal(tk_fw_midi_selected_lit);
    return !track[0] && track[1] < 16u ? (uint8_t)track[1] : UINT8_MAX;
}

int tk_midi_notes_enabled(void)
{
    const volatile uint16_t *notes = (const volatile uint16_t *)literal(tk_fw_midi_rx_notes_lit);
    return notes[0] || notes[1];
}

uint8_t tk_midi_receive_ports(void)
{
    const volatile uint16_t *flags = (const volatile uint16_t *)literal(tk_fw_midi_ports_lit);
    uint16_t bits = flags[1];
    return (uint8_t)(((bits >> 1u) & 1u) | ((bits << 1u) & 2u)); /* USB, DIN. */
}

uint32_t tk_midi_route(uint8_t channel, int auto_channel)
{
    if (auto_channel) {
        uint8_t track = tk_midi_selected_track();
        return track < 16u ? 1u << track : 0;
    }
    const volatile int32_t *routes = (const volatile int32_t *)literal(tk_fw_midi_routes_lit);
    uint32_t mask = 0;
    for (unsigned track = 0; track <= 16u; ++track) {
        /* The stock signed channel words also sit on halfword boundaries. */
        const volatile uint16_t *words = (const volatile uint16_t *)(routes + track);
        if (!words[0] && words[1] == channel)
            mask |= 1u << track;
    }
    return mask;
}
