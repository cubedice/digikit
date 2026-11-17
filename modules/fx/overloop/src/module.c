/* The effect of Overloop, declared by builder calls in init: one effect `loop` with four knobs. Its DSP symbols are _bl_process and
   _bl_desc (`dsp = "bl"` in module.toml). The DSP reads the parameter words in this order: REC, START, FADE, LENGTH; a knob sets the
   word of its position, so the knobs follow the words. Saved kits keep the effect by module id + effect id: never rename either
   (core/loop-controls finds the row by that id). */
#include <dk/core/module.h>
#include <dk/gen/page_consts.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_fx *fx = dk_module_ctx_add_fx(ctx, DK_FX_ID_LOOP, "Overloop", "OVL");
    dk_fx_routes(fx, DK_ROUTE_MASTER, DK_ROUTE_MASTER);
    dk_fx_mix(fx, 127);
    dk_page *p = dk_fx_add_page(fx, "Overloop", "OVL");
    dk_page_add_fx_param(p, "Capture once", "REC", DK_PERCENT, 0.0f, 1.0f, 0.0f);
    dk_page_add_fx_param(p, "Start step (zero based)", "START", DK_PERCENT, 0.0f, 127.0f, 0.0f);
    dk_page_add_fx_param(p, "Loop crossfade", "FADE", DK_PERCENT, 0.0f, 1.0f, 0.0f);
    dk_page_add_fx_param(p, "Steps (0 = pattern)", "LENGTH", DK_PERCENT, 0.0f, 127.0f, 0.0f);
    return DK_OK;
}

const dk_module_def dk_module = {.init = init};
