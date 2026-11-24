// The DSP imports of the RELAY machine voice (DTII OS 1.17): exports of the image adapters core/relay-track and core/relay-storage, which
// the voice reaches through the import table of core/dyndsp (its `optional` list). Include this in the voice's entry source; dk_relay.inc and
// dk_relay_track.inc hold only the numeric layout, which the adapters that define these symbols include too. An image without the adapters
// does not have them: a module that imports them is refused there.

// The 16-word track records (RT_*, dk_relay_track.inc) and the tape records of core/relay-track: the recorder writes them, the voice reads them.
.EXTERN _rtm_tracks;
.EXTERN _rtm_tapes;

// The owner of a track's read head, from the pool of the track keyboard (core/relay-track; R0 = -1 without the track keyboard).
.EXTERN _rtm_read_head_owner;

// The native AMP envelope state of a track, folded into the voice record at I3 (core/relay-track): the head retires when the release has ended.
.EXTERN _rtm_group_amp_done;

// The page pool of core/relay-storage: R4 command (RLY_*, dk_relay.inc), R8 argument, R12 frame index; R0 = -1 on refusal.
.EXTERN _relay_pool;
