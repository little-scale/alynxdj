#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "gg_row_sync.h"

static uint8_t settle(gg_row_sync_t *sync, uint8_t level, uint64_t at)
{
    assert(gg_row_sync_update(sync, level, at) == GG_SYNC_EVENT_NONE);
    return gg_row_sync_update(sync, level, at + GG_SYNC_DEBOUNCE_US);
}

int main(void)
{
    gg_row_sync_t sync;
    uint64_t t;

    /* Flat 100 ms GG rows. Counter states at row boundaries are 1,2,3,0;
     * the differential optocoupler output is high,low,high,high. Row 0 is
     * invisible, row 1 starts the bridge, and the catch-up is not adjacent. */
    gg_row_sync_init(&sync, 1u, 0u);
    assert(settle(&sync, 0u, 100000u) == GG_SYNC_EVENT_START);
    assert(gg_row_sync_update(&sync, 0u, 124999u) == GG_SYNC_EVENT_NONE);
    assert(gg_row_sync_update(&sync, 0u, 125250u) == GG_SYNC_EVENT_ROW);

    assert(settle(&sync, 1u, 200000u) == GG_SYNC_EVENT_ROW);
    assert(sync.row_period == 100000u);
    assert(gg_row_sync_update(&sync, 1u, 299999u) == GG_SYNC_EVENT_NONE);
    assert(gg_row_sync_update(&sync, 1u, 300250u) == GG_SYNC_EVENT_ROW);
    assert(gg_row_sync_update(&sync, 1u, 400250u) == GG_SYNC_EVENT_ROW);

    assert(settle(&sync, 0u, 500000u) == GG_SYNC_EVENT_ROW);
    assert(sync.row_period == 100000u);
    assert(settle(&sync, 1u, 600000u) == GG_SYNC_EVENT_ROW);
    assert(gg_row_sync_update(&sync, 1u, 700250u) == GG_SYNC_EVENT_ROW);
    assert(gg_row_sync_update(&sync, 1u, 800250u) == GG_SYNC_EVENT_ROW);

    /* Sub-debounce chatter must not create a row. */
    assert(gg_row_sync_update(&sync, 0u, 900000u) == GG_SYNC_EVENT_NONE);
    assert(gg_row_sync_update(&sync, 1u, 900100u) == GG_SYNC_EVENT_NONE);
    assert(gg_row_sync_update(&sync, 1u, 900500u) == GG_SYNC_EVENT_NONE);

    /* The one-bit path has no explicit Stop; a missing marker is converted
     * to STOP after the bounded silence window. */
    t = 600000u + GG_SYNC_DEBOUNCE_US + GG_SYNC_STOP_SILENCE_US;
    assert(gg_row_sync_update(&sync, 1u, t) == GG_SYNC_EVENT_STOP);
    assert(!sync.running);

    /* If powered while the optocoupler is already active, wait for a complete
     * high->low marker rather than inventing a transport on the release edge. */
    gg_row_sync_init(&sync, 0u, 0u);
    assert(settle(&sync, 1u, 100000u) == GG_SYNC_EVENT_NONE);
    assert(settle(&sync, 0u, 500000u) == GG_SYNC_EVENT_START);

    puts("GG differential row-sync decoder tests passed");
    return 0;
}
