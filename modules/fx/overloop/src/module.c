/* The effect of Overloop, declared by builder calls in init: one effect `loop` with four parameter words. Its DSP symbols are
   _bl_process and _bl_desc (`dsp = "bl"` in module.toml). The words are the loop's controls in the order of dk_loop.inc: the
   crossfader, the counters of ARM and DROP, and the loop count the control home has seen. Their values are not the kit's:
   core/loop-control holds them and the fx service sends them (an image without it would read the kit, but this module does
   not load there: its DSP imports that adapter's mailbox). The page is core/loop-display's, so these knobs are never seen.
   Saved kits keep the effect by module id + effect id: never rename either (the adapters find the row by that id). */
#include <dk/core/module.h>
#include <dk/gen/page_consts.h>
#include "module_ids.h"

static int32_t init(dk_module_ctx *ctx)
{
    dk_fx *fx = dk_module_ctx_add_fx(ctx, DK_FX_ID_LOOP, "Overloop", "OVL");
    dk_fx_routes(fx, DK_ROUTE_MASTER, DK_ROUTE_MASTER);
    dk_fx_mix(fx, 127);
    dk_page *p = dk_fx_add_page(fx, "Overloop", "OVL");
    dk_page_add_fx_param(p, "Loop crossfade", "XFAD", DK_PERCENT, 0.0f, 1.0f, 0.0f);
    dk_page_add_fx_param(p, "Arm (a counter)", "ARM", DK_PERCENT, 0.0f, 127.0f, 0.0f);
    dk_page_add_fx_param(p, "Drop (a counter)", "DROP", DK_PERCENT, 0.0f, 127.0f, 0.0f);
    dk_page_add_fx_param(p, "Loops seen", "SEEN", DK_PERCENT, 0.0f, 127.0f, 0.0f);
    return DK_OK;
}

const dk_module_def dk_module = {.init = init};
