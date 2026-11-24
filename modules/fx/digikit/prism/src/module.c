/* The effect of PRISM, declared by builder calls in init: one effect `prism` with no knob of its own. Its DSP symbols are
   _prism_process and _prism_desc (`dsp = "prism"` in module.toml). It is a send effect and only that: each track has a
   PRS send amount, and the effect's output is added to the FX return. The phrase adapters supply its controls and its
   page (core/phrase-control, core/phrase-ui); MIX is one of those controls, and the effect row's own MIX stays at unity.
   Saved kits keep the effect by module id + effect id: never rename either. */
#include <dk/core/module.h>
#include <dk/gen/page_consts.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_fx *fx = dk_module_ctx_add_fx(ctx, DK_FX_ID_PRISM, "PRISM", "PRS");
    dk_fx_routes(fx, DK_ROUTE_SEND, DK_ROUTE_SEND);
    dk_fx_mix(fx, 127);
    dk_fx_add_page(fx, "PRISM", "PRS");
    return DK_OK;
}

const dk_module_def dk_module = {.init = init};
