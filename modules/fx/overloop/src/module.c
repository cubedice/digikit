#include <dk/fx.h>

DK_FX(loop, .process = "_bl_process", .desc = "_bl_desc",
      .mix = 127, .route = DK_ROUTE_MASTER, .routes = DK_ROUTE_MASTER);
DK_FX_PARAM(loop, 0, "Capture once", "REC", DK_PERCENT, 0.0, 1.0, 0.0);
DK_FX_PARAM(loop, 1, "Start step (zero based)", "START", DK_PERCENT, 0, 127, 0);
DK_FX_PARAM(loop, 2, "Loop crossfade", "FADE", DK_PERCENT, 0.0, 1.0, 0.0);
DK_FX_PARAM(loop, 3, "Steps (0 = pattern)", "LENGTH", DK_PERCENT, 0, 127, 0);
DK_PAGE(loop, "OVL", "Overloop", DK_KNOB_AT(0, 0), DK_KNOB_AT(1, 1), DK_KNOB_AT(2, 2), DK_KNOB_AT(3, 3));
