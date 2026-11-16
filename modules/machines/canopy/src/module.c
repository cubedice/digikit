/* CANOPY, a persistent V2 instrument (core/instrument-play), declared by builder calls in init: one machine `canopy` with no parameter
   page of its own (core/instrument-play draws its SRC pages). Its DSP entry points are _canopy_reset and _canopy_process and its
   descriptor _canopy_descriptor (`instrument = true` in module.toml): the runtime admits it on its V2 path, not as a V1 voice, and
   publishes this machine only after the DSP acknowledged the instance. */
#include <dk/core/module.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_module_ctx_add_machine(ctx, DK_MACHINE_ID_CANOPY, "CANOPY", "CANOPY");
    return DK_OK;
}

const dk_module_def dk_module = {.init = init};
