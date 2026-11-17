// DT2 1.17 dispatcher +0x60: capture the envelope bank derived by stock code.
// Replace only the 48-bit MODIFY at 0x1c648a, resume at 0x1c648d.
// I1 is the live workspace pointer; preserve its exact stock update semantics.
// Storing a DAG register avoids changing R/S registers or arithmetic status.
// Not installed by the normal manifest until carrier configuration is proved.
.SECTION/PM seg_pmco;
.GLOBAL _mi_workspace_entry;
.EXTERN _mi_workspace_next;
.EXTERN _mi_envelope_records;
_mi_workspace_entry:
    MODIFY(I1, 0xd604);
    DM(_mi_envelope_records) = I1;
    JUMP _mi_workspace_next;
