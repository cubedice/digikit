#include <dk/instrument.h>
DK_INSTRUMENT(canopy, .name = "CANOPY", .short_name = "CANOPY",
              .reset = "_canopy_reset", .process = "_canopy_process",
              .desc = "_canopy_desc");
