# CANOPY tracks dropping during playback on DT2 1.17

**[D] Hardware reproduction.** With CANOPY, Overloop, PRISM and Remote active,
two tracks in the same playing pattern lost their custom SRC controls together.
The picker retained the CANOPY assignment. The SRC page showed dotted boxes
where CHRD and VOIC normally appear. The trace changed from two admitted
tracks to zero, and the instrument file changed from phase 3 to phase 9.

At CF block 292226, the first session fault was reason 5 (backward heartbeat).
The last accepted DSP counters included 300388, 300389 and 300392. The next
valid PLAY reply carried 300391 in the same bound session. The old filter
recognized exact copies only, so this late arrival revoked the instrument for
the rest of that boot. A restart cleared the fault. The saved project still
contained both assignments. The coupled emulator played the same project
without producing that ordering.

The frame adapter now discards an unseen counter strictly inside a gap
bounded by accepted replies at most eight blocks apart. It checks the frame
integrity and session first. A discarded reply cannot acknowledge work,
publish status or refresh either watchdog. A conflicting copy at a known
counter, a backward jump outside the bounded history, a session change and
an explicit hardware reset retain their existing behavior. The dispatch
regression replays the measured order, then verifies that the next fresh
reply reaches the registry. The transport tests also cover counter wrap.

**[D] Hardware check after installation.** Build `36843d1f43cac302` played
the same pattern for four minutes with the original four modules and the
diagnostic disabled. Across 228 screen samples, alternating the selected
track every 30 seconds, the CHRD and VOIC labels remained present. The old
fault capture at those positions showed the empty stock controls.

**[D] Separate mixer defect.** The input tap in this firmware runs even with
the RELAY module disabled. Entering it with both SHARC lanes enabled stored
the second lane's S0 in the next word, which the hook then reused to save L0.
The audio rig reproduced S0 changing from 654321 to zero. The hook now
reserves two words for each save made before disabling the second lane and
waits for the original mode to take effect before reloading the register.
The rig checks both entry modes and compares all 64 copied channel samples.
A listening comparison on the device is still needed to attribute any
remaining audible noise.
