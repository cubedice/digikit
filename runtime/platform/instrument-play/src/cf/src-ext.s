| The table `machines_ext_info` (an [[info]] of module.toml): the functions the machines service file's hooks call for the SRC pages
| (src-pages.c: an added type's page count and the page vector, `mf_srcpage`) and for a persistent instrument's modulation destinations
| (`instrument_mod_param`, `mf_typearg`). The service copies it into `DKRT_MF_EXT` (services/machines/src/ext_info.rs has the layout); an image
| without the table keeps the one-page rule.
    .section .rodata
    .balign 4
    .globl mf_ext_info
mf_ext_info:
    .long   0x4d465831, 1                   | magic MFX1, version
    .long   mf_src_page_count, mf_src_apply_pages
    .long   instrument_mod_param
