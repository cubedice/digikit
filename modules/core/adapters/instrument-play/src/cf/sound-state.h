/* Explicit instance/Sound handoff for the unlinked persistence proof.
 * These functions do not create, load, save, or enable persistence. */
#ifndef DK_CANOPY_SOUND_STATE_H
#define DK_CANOPY_SOUND_STATE_H
#include <stdint.h>
#include "preset.h"
#include "../../../midi-events/src/cf/ownership.h"
/* Snapshot requires an admitted Sound and no active load or learn session.
 * On success, generation captures that admission. Failure changes neither
 * output. Internally the mapper lock protects the complete snapshot. */
int instrument_sound_snapshot(uint32_t instance, uint32_t sound, CpPreset *out,
                              uint32_t *generation);
/* Test-only accepted-state snapshot for registered transaction callbacks.
 * Unlike snapshot above, this permits a temporary LEARN session and captures
 * its previous mode; capture notes do not alter the accepted Sound. The
 * supplied Sound/generation must still be admitted. Mapper exclusion covers
 * validation and copying; refusal leaves out untouched. */
int instrument_sound_proof_snapshot(uint32_t instance, uint32_t sound, uint32_t generation,
                                    CpPreset *out);
/* The caller supplies an immutable, validated snapshot and the generation
 * returned by snapshot (or independently observed for a newly admitted Sound).
 * The mapper lock makes validation and the reset/apply boundary atomic. A
 * stale generation, changed Sound, active load or learn session fails closed. */
int instrument_sound_restore(uint32_t instance, uint32_t sound, uint32_t generation,
                             const CpPreset *preset);
/* Test-only preset application with pre-reserved capacity. Preparation is
 * non-mutating and attach is infallible while the mapper lock is held. */
int instrument_sound_proof_apply(uint32_t instance, uint32_t sound, uint32_t generation,
                                 const CpPreset *preset, int (*prepare)(void *, const CpPreset *),
                                 void (*attach)(void *), void *context);
/* Test-only shared LEARN transaction. The source may be in LEARN; session and
 * revision identify the displayed capture. All affected admitted instances
 * prepare before the real save changes anything, including peers whose chord
 * mode does not currently use USER. A preset override shields a peer, but the
 * source's save clears its own override. Each prepare must validate that
 * owner's Sound/generation against its expected identity, copy the ephemeral
 * preset into caller-owned staging, and verify distinct pre-reserved detach
 * entries without changing live state. Attach must be infallible and
 * publish only its staged owner. Both callbacks execute under one mapper lock;
 * no callback may allocate, wait, perform I/O, or change keyboard/Sound state.
 * Return 0 after all attachments, -1 on refusal without a live edit. UI close,
 * refresh and toasts belong to the caller after return. No production caller
 * or Sound lifecycle, storage, or critical-section latency proof is supplied. */
int instrument_sound_proof_learn(uint32_t source, uint32_t sound, uint32_t generation,
                                 uint32_t session, uint32_t revision,
                                 int (*prepare)(void *, uint32_t, uint32_t, uint32_t,
                                                const CpPreset *),
                                 void (*attach)(void *, uint32_t), void *context);
enum {
    IP_PROOF_SCAN_CLOCK,
    IP_PROOF_SCAN_RESET,
    IP_PROOF_WT_WHEEL,
    IP_PROOF_FM_WHEEL,
    IP_PROOF_WTFM_WHEEL
};
/* Test-only transaction for the modulation menu's explicit bank setters.
 * Clock is 0/1, reset is 0..2, and wheel amounts use the UI's signed -100..100.
 * Selecting a clock also resets its WT rate to the ordinary menu default;
 * both values prepare and commit together, even with another family active.
 * Callbacks follow the same prepare/attach contract as proof_apply: prepare
 * only stages caller-owned bytes in pre-reserved storage, then the actual
 * setter and infallible attach run under the same mapper lock. No I/O or
 * allocation is allowed. Return 0 on commit, -1 without an edit on refusal.
 * UI refresh, including restoring a refused dk_ui_value staging variable,
 * belongs to the caller outside the lock. No production hook is installed. */
int instrument_sound_proof_menu(uint32_t instance, uint32_t sound, uint32_t generation,
                                uint32_t setting, int value,
                                int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                void *context);
/* Test-only SHAPE SLOT callback transaction. The caller has staged the signed
 * 1..8 value into the instance's UI backing, as dk_ui_value does. A matching
 * staged request is first restored to the accepted keyboard slot; refusal
 * leaves it there. A mismatched (newer) staging value is never overwritten.
 * Prepare sees the actual changed() result: mapper chord retained, UI mode
 * and voicing reapplied, override cleared only when the slot changes. The
 * real changed() and infallible attach execute under one mapper lock after
 * preparation. Failure runs none of its routing/reset side effects. The
 * prepare/attach constraints are identical to proof_apply; UI work is left
 * outside this helper. No production hook is installed. */
int instrument_sound_proof_shape_slot(uint32_t instance, uint32_t sound, uint32_t generation,
                                      int slot, int (*prepare)(void *, const CpPreset *),
                                      void (*attach)(void *), void *context);
/* Emulator-only default admission. claim must atomically validate and consume
 * the caller's pending-default marker, and cannot allocate or wait; it is
 * called only after ownership is validated, under the mapper lock. */
int instrument_sound_proof_defaults(uint32_t instance, uint32_t sound, uint32_t generation,
                                    int (*claim)(void *), void *context);
/* Emulator-only fixture registration. No production module installs a tick. */
void instrument_sound_proof_set(void (*tick)(void));
/* Disposable emulator callback before the UI scan drops a tracked Sound.
 * This does not prove ownership or safe failure handling. */
void instrument_sound_proof_set_leave(void (*callback)(uint32_t));
typedef int (*IpSoundProofPrepare)(void *context, uint32_t instance, uint32_t sound,
                                   uint32_t generation, const CpPreset *preset);
typedef void (*IpSoundProofAttach)(void *context, uint32_t instance, uint32_t sound,
                                   uint32_t generation);
/* Emulator-only opt-in for actual SRC encoder, MIDI selector, preset LOAD,
 * modulation-menu, SHAPE SLOT and shared LEARN SAVE entry points.
 * A separately linked fixture supplies pre-reserved immutable snapshot storage
 * and validates Sound identity/generation/ownership in prepare; it must copy
 * the ephemeral preset and stage without changing any live state. Attach is
 * infallible and publishes only the staged entry for that same owner. Both
 * callbacks execute under uninterrupted mapper exclusion and may not allocate,
 * wait, perform I/O, re-enter control events, or change registration. Storage
 * replenishment belongs to a task context outside the callbacks/IRQ lock.
 * Shared LEARN prepares every affected owner before any edit or attach. Each
 * instance needs distinct staging and reserved capacity: prepare cannot
 * consume either reservation or live handle. A later owner's refusal leaves
 * all accepted state unchanged and calls no attach. The accepted-state
 * proof_snapshot above permits prior-byte validation while LEARN is open.
 * UI backing is restored on refusal; refresh/toasts run after mapper unlock.
 * Ordinary notes/releases and LEARN capture need no snapshot. Failed selector
 * preparation consumes only selectors, preserving other MIDI events. Missing
 * admission also filters selectors; custom SRC edits fail closed until ready.
 * Register both callbacks, or pass both null to disable before fixture unload.
 * Returns -1 for a partial callback pair or an active/re-entrant transaction.
 * Unregistered callers retain their ordinary behavior. This does not connect
 * firmware Sound copy/load/eviction or prove Sound lifetime, resource recovery,
 * callback timing, or on-card persistence. */
int instrument_sound_proof_set_transactions(IpSoundProofPrepare prepare, IpSoundProofAttach attach,
                                            void *context);
/* Test-only SRC transaction: caller reserves capacity beforehand. prepare
 * must not allocate, wait, or attach the reserved handle to a Sound; attach
 * is infallible and publishes its handle only after the real edit succeeds.
 * Both callbacks run under the mapper lock. CS_EDIT means a Sound snapshot was
 * attached; CS_READONLY preserves transient list motion without consuming the
 * reservation; CS_NATIVE delegates a stock control. No latency claim is made. */
int instrument_sound_proof_edit(uint32_t instance, uint32_t sound, uint32_t generation,
                                uint32_t token, uint32_t encoder, int32_t detents, uint32_t now,
                                int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                void *context);
/* Test-only selector transaction. Caller serializes mapper access and has
 * pre-reserved an entry. A failed prepare refuses selector note-ons but still
 * routes the other notes and releases; refused is set to 1 in that case. */
int instrument_sound_proof_midi(uint32_t instance, uint32_t sound, uint32_t sound_generation,
                                uint32_t epoch, uint32_t midi_generation, const MiEvent *input,
                                uint32_t count, MiEvent *output,
                                int (*prepare)(void *, const CpPreset *), void (*attach)(void *),
                                void *context, int *refused);
#endif
