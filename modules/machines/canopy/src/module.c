/* Persistent pad declaration. No V1 note-on clearing or stock type shadow. */
#include <dk/instrument.h>
DK_INSTRUMENT(canopy, .name = "CANOPY", .short_name = "CANOPY",
    .reset = "_canopy_reset", .process = "_canopy_process", .desc = "_canopy_descriptor");
