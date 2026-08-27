#include "gg_row_sync.h"

#include <string.h>

static uint8_t valid_period(uint64_t period)
{
    return period >= GG_SYNC_MIN_ROW_US && period <= GG_SYNC_MAX_ROW_US;
}

static void update_period(gg_row_sync_t *sync, uint64_t sample)
{
    if (!valid_period(sample))
        return;

    if (!sync->row_period) {
        sync->row_period = sample;
    } else {
        /* A light PLL removes optocoupler/loop edge jitter without making
         * tempo changes take more than one four-row marker cycle to settle. */
        sync->row_period = (sync->row_period * 3u + sample) / 4u;
    }
}

static void stop_sync(gg_row_sync_t *sync)
{
    sync->running = 0u;
    sync->catchup_pending = 0u;
    sync->hidden_remaining = 0u;
    sync->last_edge = 0u;
    sync->last_fall = 0u;
    sync->row_period = 0u;
}

void gg_row_sync_init(gg_row_sync_t *sync, uint8_t input_level,
                      uint64_t now_us)
{
    memset(sync, 0, sizeof(*sync));
    sync->stable_level = input_level ? 1u : 0u;
    sync->candidate_level = sync->stable_level;
    sync->candidate_since = now_us;
}

uint8_t gg_row_sync_update(gg_row_sync_t *sync, uint8_t input_level,
                           uint64_t now_us)
{
    uint8_t event = GG_SYNC_EVENT_NONE;
    uint8_t level = input_level ? 1u : 0u;

    if (level != sync->candidate_level) {
        sync->candidate_level = level;
        sync->candidate_since = now_us;
    }

    if (sync->candidate_level != sync->stable_level
        && now_us - sync->candidate_since >= GG_SYNC_DEBOUNCE_US) {
        sync->stable_level = sync->candidate_level;

        if (!sync->stable_level) {
            /* The first visible falling edge is GG row 1: row 0 is counter
             * state 1, reverse-biasing the optocoupler and thus invisible.
             * START grants Lynx row 0 now, then a separately timed ROW safely
             * catches it up to row 1 without adjacent UART bytes. */
            if (!sync->running) {
                sync->running = 1u;
                sync->last_edge = now_us;
                sync->last_fall = now_us;
                sync->catchup_due = now_us + GG_SYNC_CATCHUP_US;
                sync->catchup_pending = 1u;
                event = GG_SYNC_EVENT_START;
            } else {
                if (sync->last_fall) {
                    uint64_t cycle = now_us - sync->last_fall;
                    update_period(sync, cycle / 4u);
                }
                sync->last_fall = now_us;
                sync->last_edge = now_us;
                /* A real falling edge is the boundary following the two
                 * predicted high-state rows. It wins over late predictions. */
                sync->hidden_remaining = 0u;
                event = GG_SYNC_EVENT_ROW;
            }
        } else if (sync->running) {
            /* The rising edge is the next exact boundary. Its distance from
             * the fall is one source row and seeds the initial tempo. */
            if (sync->last_fall)
                update_period(sync, now_us - sync->last_fall);
            sync->last_edge = now_us;
            if (sync->row_period) {
                sync->next_hidden = now_us + sync->row_period;
                sync->hidden_remaining = 2u;
            }
            event = GG_SYNC_EVENT_ROW;
        }
    }

    if (sync->running && sync->last_edge) {
        uint64_t stop_silence = GG_SYNC_STOP_SILENCE_US;
        if (sync->row_period && sync->row_period * 5u > stop_silence)
            stop_silence = sync->row_period * 5u;
        if (now_us - sync->last_edge >= stop_silence) {
            /* SMSGGDJ OUT has no separate stop conductor through this
             * one-bit optocoupler path, so silence is the only indication. */
            stop_sync(sync);
            return GG_SYNC_EVENT_STOP;
        }
    }

    if (event == GG_SYNC_EVENT_NONE && sync->catchup_pending
        && now_us >= sync->catchup_due) {
        sync->catchup_pending = 0u;
        event = GG_SYNC_EVENT_ROW;
    }

    if (event == GG_SYNC_EVENT_NONE && !sync->catchup_pending
        && sync->hidden_remaining && now_us >= sync->next_hidden) {
        --sync->hidden_remaining;
        sync->next_hidden += sync->row_period;
        event = GG_SYNC_EVENT_ROW;
    }

    return event;
}
