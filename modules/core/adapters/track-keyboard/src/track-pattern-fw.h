#ifndef DK_TRACK_PATTERN_FW_H
#define DK_TRACK_PATTERN_FW_H
#include "track-service.h"

TkPattern *tk_pattern_fw_get(uint32_t address);
const TkPattern *tk_pattern_fw_existing(uint32_t address);
TkPattern *tk_pattern_fw_peek(uint32_t address);
uint32_t tk_pattern_fw_track_base(uint32_t address, uint8_t *track);
void tk_pattern_fw_changed(void);
uint32_t tk_pattern_fw_revision(void);
uint32_t tk_pattern_fw_selected(void);

#endif
