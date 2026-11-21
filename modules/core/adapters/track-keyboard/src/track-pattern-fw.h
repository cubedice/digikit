#ifndef DK_TRACK_PATTERN_FW_H
#define DK_TRACK_PATTERN_FW_H
#include "track-service.h"

TkPattern *tk_pattern_fw_get(uint32_t address);
uint32_t tk_pattern_fw_revision(void);
uint32_t tk_pattern_fw_selected(void);

#endif
