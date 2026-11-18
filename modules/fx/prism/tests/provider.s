// Stand-ins for the providers the engine imports, for the module's own rig tests: the storage descriptor (a test sets
// the frames and the bytes of its bank), the control mailbox and the status mailbox.
.SECTION/PM seg_pmco;
    NOP;
.SECTION/DM seg_dmda;
.GLOBAL _phrase_storage_info;
.VAR _phrase_storage_info = 0x50484231;
.VAR _prism_test_version = 1;
.VAR _prism_test_bank0 = 0x9aa2b9c8;
.VAR _prism_test_bank1 = 0x9b22b9c8;
.VAR _prism_test_bytes = 131072;
.VAR _prism_test_frames = 16384;
.PKDATA _phrase_control_info, 256;
.PKDATA _phrase_status, 128;
