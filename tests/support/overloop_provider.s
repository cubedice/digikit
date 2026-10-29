// Conformance fixture only: ownership in real firmware is proved separately.
.SECTION/PM seg_pmco;
    NOP;
.SECTION/DM seg_dmda;
.GLOBAL _loop_buffer_info;
.VAR _loop_buffer_info = 0x4c425546;
.VAR _test_version = 1;
.VAR _test_base = 0x9ca2b9c8;
.VAR _test_bytes = 0x00800000;

.PKDATA _pattern_position_info, 80;
.GLOBAL _loop_controls_info;
.VAR _loop_controls_info = 0x4c435431;
