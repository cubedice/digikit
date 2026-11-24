| Test fixture: the assembler source of the svc-test service object: the export dk_svctest_ping, which returns 42.

    .text
    .globl  dk_svctest_ping
dk_svctest_ping:
    moveq   #42,%d0
    rts
