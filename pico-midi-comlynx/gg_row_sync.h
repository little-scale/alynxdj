#ifndef GG_ROW_SYNC_H
#define GG_ROW_SYNC_H

#include <stdint.h>

/* The unchanged Chipbridge Game Gear adapter presents DATA1 on the Type-A
 * MIDI ring and DATA0 on its tip. The 6N138 therefore turns on only for GG
 * counter state 2 (DATA1=1, DATA0=0): GP13 is low for one row and high for
 * the other three. Falling/rising edges give two exact row boundaries; the
 * two hidden boundaries are reconstructed from the measured row period. */
#define GG_SYNC_DEBOUNCE_US      250u
#define GG_SYNC_MIN_ROW_US      8000u
#define GG_SYNC_MAX_ROW_US      300000u
#define GG_SYNC_CATCHUP_US      25000u
#define GG_SYNC_STOP_SILENCE_US 750000u

enum gg_sync_event {
    GG_SYNC_EVENT_NONE  = 0,
    GG_SYNC_EVENT_START = 1,
    GG_SYNC_EVENT_ROW   = 2,
    GG_SYNC_EVENT_STOP  = 4,
};

typedef struct gg_row_sync {
    uint64_t candidate_since;
    uint64_t last_edge;
    uint64_t last_fall;
    uint64_t row_period;
    uint64_t catchup_due;
    uint64_t next_hidden;
    uint8_t stable_level;
    uint8_t candidate_level;
    uint8_t running;
    uint8_t catchup_pending;
    uint8_t hidden_remaining;
} gg_row_sync_t;

void gg_row_sync_init(gg_row_sync_t *sync, uint8_t input_level,
                      uint64_t now_us);
uint8_t gg_row_sync_update(gg_row_sync_t *sync, uint8_t input_level,
                           uint64_t now_us);

#endif
