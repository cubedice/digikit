#ifndef DK_INSTRUMENT_SRC_CONTROL_H
#define DK_INSTRUMENT_SRC_CONTROL_H
#include "src-display.h"
#include "src-lock-event.h"
/* UI-task bridge. The admitted carrier is the track in ip_track. Context
 * must be refreshed on selection/mode/page boundaries before dispatching input.
 * Neither this bridge nor the model retains borrowed firmware view pointers. */
uint32_t instrument_src_context(uint32_t track, uint32_t page, int visible);
/* UI task only. Call on admission/revocation by the shared track keyboard,
 * after it has taken ownership of the old KEYS and ARP controls. */
void instrument_src_track_keyboard(uint32_t track, int active);
/* UI/task boundary. Claim returns ready only after the carrier reset/config
 * completes. Tokens stay unique during a claim. On handoff or transport
 * faults the carrier resets them. */
/* Bit 0 enables the pad lane, bit 1 enables the timed lane. */
int instrument_track_keyboard_lanes(uint32_t track, uint32_t lanes);
int instrument_track_keyboard_claim(uint32_t track, int active);
int instrument_track_keyboard_note(uint32_t track, uint32_t token, uint32_t pitch,
                                   uint32_t velocity, uint32_t on, uint32_t lane);
struct IsnSnapshot;
int instrument_track_keyboard_locked_note(uint32_t track, uint32_t token, uint32_t pitch,
                                          uint32_t velocity, uint32_t on, uint32_t lane,
                                          const struct IsnSnapshot *snapshot);
int instrument_lock_source(uint32_t track, uint32_t wave, uint16_t *out);
/* Return a valid CANOPY Sound from the stock project pool. A probe takes
 * expected=0 and out=tune=NULL; a hit takes its exact native Sound pointer. */
int instrument_locked_sound(uint32_t index, uintptr_t expected, CpPreset *out, uint16_t *tune);
/* Read the current Sound without changing its origin or ownership. */
int instrument_track_keyboard_legacy(uint32_t track, CpPreset *out);
int instrument_src_encoder(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now_ms);
void instrument_src_display(CvDisplay *, uint32_t now_ms, const char *tune, const char *level);
void instrument_src_feedback(char out[32], uint32_t encoder);
/* Packed trig locks. The ID and base of the control under `encoder`
 * in the UI instance: CS_EDIT when it can be locked, CS_NATIVE for TUNE/LEV
 * (stock locks), CS_STALE before initialization, else CS_READONLY. */
int instrument_src_lock_target(uint32_t encoder, const IlpSnapshot *snapshot, uint32_t *control,
                               uint32_t *id, uint32_t *base);
/* cs_lock_step on the UI instance. */
int instrument_src_lock_step(uint32_t token, uint32_t encoder, int32_t detents, uint32_t now_ms,
                             const IlpSnapshot *snapshot, uint32_t current, uint32_t *next);
void instrument_src_lock_feedback(char out[32], uint32_t encoder, const IlpSnapshot *snapshot,
                                  uint32_t control, uint32_t value);
/* instrument_src_display showing the held trig's absolute packed locks. */
void instrument_src_display_locked(CvDisplay *, uint32_t now_ms, const char *tune,
                                   const char *level, const IlpSnapshot *snapshot);
/* Drain telemetry on the UI task independently of the paint cadence. */
void instrument_src_poll(uint32_t now_ms);
/* Reserved for the authenticated status demultiplexer, not registry ACKs. */
int instrument_src_receive(const uint32_t words[IS_WORDS], uint32_t now_ms);
#endif
