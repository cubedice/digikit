// Required import prevents loading the new module on a firmware without
// destination-kit control handoff support.
.SECTION/PM seg_pmco;
    NOP;
.SECTION/DM seg_dmda;
.GLOBAL _loop_controls_info;
.VAR _loop_controls_info = 0x4c435431;
