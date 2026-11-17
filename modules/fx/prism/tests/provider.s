// Small, explicit synthetic leases for module-owned concrete-core tests.
// Firmware allocator ownership and routing are tested by their owning adapters.
.SECTION/PM seg_pmco;
    NOP;
.SECTION/DM seg_dmda;
.GLOBAL _phrase_storage_info;
.VAR _phrase_storage_info = 0x50484231;
.VAR _prism_test_version = 1;
.VAR _prism_test_bank0 = 0x9aa2b9c8;
.VAR _prism_test_bank1 = 0x9b22b9c8;
.VAR _prism_test_bytes = 8192;
.VAR _prism_test_frames = 512;
.PKDATA _phrase_control_info, 256;
.PKDATA _phrase_status, 128;
