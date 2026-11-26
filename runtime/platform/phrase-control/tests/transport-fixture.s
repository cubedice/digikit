.SECTION/PM seg_pmco;
.GLOBAL _test_receive;
.GLOBAL _test_reply;
.EXTERN _phrase_receive;
.EXTERN _phrase_reply;
.EXTERN _fx_back;
_test_receive:
    CALL _phrase_receive;
    JUMP _fx_back;
_test_reply:
    CALL _phrase_reply;
    JUMP _fx_back;
