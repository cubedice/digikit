/* Experimental persistent instruments. Runtime packing requires the optional
 * DT2 1.17 single-instance V2 playback adapter. DK_PARAM/DK_PAGE are V1 records and
 * cannot be attached to this declaration yet. */
#ifndef DK_INSTRUMENT_H
#define DK_INSTRUMENT_H
#include <dk/gen/instrument.h>
#include <dk/page.h>

/* ID is a C identifier, unique among all instruments, machines and effects.
 * Required initializers: .name, .short_name, .reset, .process and .desc.
 * Callbacks and descriptor name SHARC+ symbols in this module, not CF pointers. */
#define DK_INSTRUMENT(ID, ...)                                                   \
    DK_DESC_OVERRIDE_BEGIN                                                      \
    static const dk_desc_instrument dk_desc_instrument_##ID DK_DESC = {           \
        .kind = DK_DESC_INSTRUMENT, .size = sizeof(dk_desc_instrument), .id = #ID,  \
        __VA_ARGS__};                                                           \
    DK_DESC_OVERRIDE_END
#endif
