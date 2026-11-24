/* Test fixture: the C source of the svc-test service object (packages/patchkit/tests/service_c_sources.rs). patchkit compiles it as an adapter
 * source, with -DDK_SERVICE=1, and links it with the crate: the hook handler (which hook.s calls) and dk_svctest_hits stay (the crate reaches them), svc_test_unused
 * goes.
 */
#include "svc_test_c.h"

#if !DK_SERVICE
#error "svc-test: hook.c is built into the service object only (patchkit defines DK_SERVICE there)"
#endif

static uint32_t hits;

/* The handler that the raw hook of hook.s (cf.menu.hook.settings_list.test) calls: `menu` is the settings menu (a2 at the site). It counts the call and changes
 * nothing. */
void svc_test_settings_tail(uint8_t *menu)
{
    (void)menu;
    hits += SVC_TEST_STEP;
}

uint32_t dk_svctest_hits(void)
{
    return hits;
}

/* Reached from no export of the crate: the link drops it. */
uint32_t svc_test_unused(void)
{
    return hits * 3u;
}
