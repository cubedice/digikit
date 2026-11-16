/* Registers a callable handler, then fails initialization. The loader must
 * remove this handler before freeing the module image. */
#include <dk/core/log.h>
#include <dk/core/module.h>
#include <dk/platform/host.h>

static int32_t handler(uint32_t cmd, uint32_t seq, const void *body, uint32_t len, void *out)
{
    (void)cmd;
    (void)seq;
    (void)body;
    (void)len;
    (void)out;
    return -88;
}

static int32_t init(dk_module_ctx *ctx)
{
    (void)ctx;
    int32_t result = dk_host_register(0x3e, 1, handler);
    dk_log(DK_LOG_INFO, "init-fail registration", (uint32_t)result);
    /* E_INIT in the boot log proves the registration actually succeeded. */
    return result == 0 ? -1 : DK_OK;
}

const dk_module_def dk_module = {.init = init};
