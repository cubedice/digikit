#include "dk_fx.inc"
#include "dk_phrase.inc"
.SECTION/PM seg_pmco;
.GLOBAL _test_phrase;
.GLOBAL _phr_pre_back;
.GLOBAL _phr_first_back;
.GLOBAL _phr_tracks_back;
.GLOBAL _phr_master_back;
.EXTERN _fx_back;
_phr_pre_back:
_phr_first_back:
_phr_tracks_back:
_phr_master_back:
    JUMP _fx_back;

// A controllable wet-only engine. Status ownership and heartbeat can be made
// invalid without changing the router or bypassing its production FX guard.
_test_phrase:
    I0 = R8;
    I1 = DM(DK_FX_IN_W_OUT,I0);
    R0 = DM(_test_mode);
    R1 = 1;
    COMP(R0,R1);
    IF EQ JUMP _test_fail;
    R1 = 0x3f800000;
    R2 = 3;
    COMP(R0,R2);
    IF NE JUMP _test_silent;
    R1 = 0x7fc00001;
    JUMP _test_output;
_test_silent:
    R2 = 5;
    COMP(R0,R2);
    IF NE JUMP _test_output;
    R1 = 0;
_test_output:
    LCNTR = 64, DO _test_fill UNTIL LCE;
_test_fill: DM(I1,M6) = R1;
    R1 = PH_STATUS_MAGIC;
    DM(_phrase_status + PH_S_MAGIC) = R1;
    R1 = PH_VERSION;
    DM(_phrase_status + PH_S_VERSION) = R1;
    R1 = DM(_phrase_control_info + PH_C_OWNER_GENERATION);
    R2 = 4;
    COMP(R0,R2);
    IF NE JUMP _test_generation;
    R1 = R1 + 1;
_test_generation:
    DM(_phrase_status + PH_S_OWNER_GENERATION) = R1;
    R2 = 2;
    COMP(R0,R2);
    IF EQ JUMP _test_done;
    R1 = DM(_phrase_status + PH_S_HEARTBEAT);
    R1 = R1 + 1;
    DM(_phrase_status + PH_S_HEARTBEAT) = R1;
_test_done:
    R0 = 0;
    JUMP (M13,I13);
_test_fail:
    R0 = -1;
    JUMP (M13,I13);

.SECTION/DM seg_dmda;
.PKDATA _phrase_control_info, 256;
.PKDATA _phrase_status, 128;
.PKDATA _phr_main_exclude, 4;
.PKDATA _test_mode, 4;
.PKDATA _test_ext_pointers, 8;
.PKDATA _test_ext_l, 128;
.PKDATA _test_ext_r, 128;
