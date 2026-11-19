// Stand-ins for the providers the engine imports, for the module's own rig tests: the two storage descriptors of
// core/loop-storage (a test sets their bytes and frames) with its tap and wet mailbox, the position record of
// core/pattern-position, the status mailbox of core/loop-control, and the master stage's volume ramp (a test sets it).
.SECTION/PM seg_pmco;
    NOP;
.SECTION/DM seg_dmda;
.GLOBAL _loop_buffer_info;
.VAR _loop_buffer_info = 0x4c425546;
.VAR _ol_test_version = 1;
.VAR _ol_test_base = 0x9aa2b9c8;
.VAR _ol_test_bytes = 131072;
.GLOBAL _phrase_storage_info;
.VAR _phrase_storage_info = 0x50484231;
.VAR _ol_test_phrase_version = 1;
.VAR _ol_test_bank0 = 0x99a2b9c8;
.VAR _ol_test_bank1 = 0x9b22b9c8;
.VAR _ol_test_bank_bytes = 131072;
.VAR _ol_test_frames = 16384;
.GLOBAL _fw_master_gain;
.VAR _fw_master_gain = 0x3f800000;
.GLOBAL _fw_master_step;
.VAR _fw_master_step = 0;
.PKDATA _pattern_position_info, 80;
.PKDATA _loop_ui_status, 64;
.PKDATA _loop_tap, 272;
.PKDATA _loop_wet, 272;
