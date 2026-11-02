#ifndef DK_INSTRUMENT_SRC_CONTROL_H
#define DK_INSTRUMENT_SRC_CONTROL_H
#include "src-display.h"
/* UI-task bridge. Track zero is the only currently admitted carrier. Context
 * must be refreshed on selection/mode/page boundaries before dispatching input.
 * Neither this bridge nor the model retains borrowed firmware view pointers. */
uint32_t instrument_src_context(uint32_t track,uint32_t page,int visible);
int instrument_src_encoder(uint32_t token,uint32_t encoder,int32_t detents,uint32_t now_ms);
void instrument_src_display(CvDisplay *,uint32_t now_ms,const char *tune,const char *level);
/* Drain telemetry on the UI task independently of the paint cadence. */
void instrument_src_poll(uint32_t now_ms);
/* Reserved for the authenticated status demultiplexer, not registry ACKs. */
int instrument_src_receive(const uint32_t words[IS_WORDS],uint32_t now_ms);
#endif
