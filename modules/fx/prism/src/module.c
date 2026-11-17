/* The effect of PRISM, declared by builder calls in init: one effect `prism` with no knob of its own. Its DSP symbols are
   _prism_process and _prism_desc (`dsp = "prism"` in module.toml). The phrase adapters supply the selected capture bus, the
   extended controls and the selective live/wet blend (core/phrase-control, core/phrase-ui, core/phrase-route); the ordinary
   effect row stays at unity. Saved kits keep the effect by module id + effect id: never rename either. */
#include <dk/core/module.h>
#include <dk/gen/page_consts.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_fx *fx = dk_module_ctx_add_fx(ctx, DK_FX_ID_PRISM, "PRISM", "PRS");
    dk_fx_routes(fx, DK_ROUTE_MASTER, DK_ROUTE_MASTER);
    dk_fx_mix(fx, 127);
    dk_fx_add_page(fx, "PRISM", "PRS");
    return DK_OK;
}

const dk_module_def dk_module = {.init = init};
