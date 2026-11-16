#include "src-lock-event.h"

static int carrier(uint16_t index)
{
    return index == 26 || index == 27 || index == 31 || index == 32 || index == 33;
}

int ilp_filter_event(IlpEvent *event, int owned, IlpSnapshot *snapshot)
{
    if (!event || !snapshot)
        return -1;
    if (!owned)
        return 0;
    if (event->count > 71)
        return -1;
    IlpSnapshot next;
    next.count = 0;
    /* Validate first so that malformed input can never partially erase stock
     * entries or overwrite the prior snapshot. */
    for (uint32_t i = 0; i < event->count; ++i) {
        uint32_t id, value;
        if (!carrier(event->entry[i].index) || !ilp_unpack(event->entry[i].value, &id, &value))
            continue;
        if (next.count == ILP_LANES)
            return -1;
        for (uint32_t j = 0; j < next.count; ++j)
            if (next.pair[j].id == id)
                return -2;
        next.pair[next.count].id = (uint16_t)id;
        next.pair[next.count].value = (uint16_t)value;
        ++next.count;
    }
    uint32_t out = 0;
    for (uint32_t i = 0; i < event->count; ++i) {
        uint32_t id, value;
        if (carrier(event->entry[i].index) && ilp_unpack(event->entry[i].value, &id, &value))
            continue;
        if (out != i) {
            event->entry[out].index = event->entry[i].index;
            event->entry[out].value = event->entry[i].value;
            event->entry[out].rest = event->entry[i].rest;
        }
        ++out;
    }
    event->count = out;
    snapshot->count = next.count;
    for (uint32_t i = 0; i < next.count; ++i) {
        snapshot->pair[i].id = next.pair[i].id;
        snapshot->pair[i].value = next.pair[i].value;
    }
    return 1;
}
