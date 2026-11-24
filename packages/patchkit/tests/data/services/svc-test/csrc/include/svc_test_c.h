/* Test fixture: the header of the C and assembler sources of the svc-test service object, found through the `include_dirs` of its [[object]]. */
#ifndef SVC_TEST_C_H
#define SVC_TEST_C_H

#include <stdint.h>

/* What one run of the hook adds to the count. */
#define SVC_TEST_STEP 1u

int dk_svctest_ping(void);
uint32_t dk_svctest_hits(void);
void svc_test_settings_tail(uint8_t *menu);
uint32_t svc_test_unused(void);

#endif
