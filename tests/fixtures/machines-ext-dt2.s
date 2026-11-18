| Host test fixture: the table `machines_ext_info` of core/machines (as `MF_EXT` of the service file) for the DT2 hooks `mf_srcpage` and `mf_typearg`:
| the SRC page functions of `src-pages.c` (in the test unit) and `instrument_mod_param` (a symbol the unit gives; 0: no core/instrument-play).
    .data
    .balign 4
    .globl MF_EXT
MF_EXT:
    .long   0x4d465831, 1
    .long   mf_src_page_count, mf_src_apply_pages
    .long   instrument_mod_param
