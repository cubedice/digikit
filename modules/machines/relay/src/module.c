#include <dk/core/module.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_module_ctx_add_machine(ctx, DK_MACHINE_ID_RELAY, "RELAY", "RLY");
    return DK_OK;
}
const dk_module_def dk_module = {.init = init};
