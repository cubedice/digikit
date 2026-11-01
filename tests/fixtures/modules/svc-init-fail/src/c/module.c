/* Registers a callable handler, then fails initialization. The loader must
 * remove this handler before freeing the module image. */
#include <dk/api.h>

static int32_t handler(uint32_t cmd, uint32_t seq, const uint8_t *body,
                       uint32_t len, uint8_t *out)
{
    (void)cmd; (void)seq; (void)body; (void)len; (void)out;
    return -88;
}

int32_t dkm_init(const dk_api *api)
{
    int32_t result = api->register_cmds(0x3e, 1, handler);
    api->log((const uint8_t *)"init-fail registration", (uint32_t)result);
    /* E_INIT in the boot log proves the registration actually succeeded. */
    return result == 0 ? -1 : 0;
}
