#include <stdbool.h>
#include <stdint.h>

#include "hardware/pio.h"
#include "pico/stdlib.h"

#include "comlynx_tx.pio.h"
#include "gg_row_sync.h"

#ifndef COMLYNX_TX_PIN
#define COMLYNX_TX_PIN 1
#endif

#ifndef GG_SYNC_INPUT_PIN
#define GG_SYNC_INPUT_PIN 13
#endif

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN 7
#endif

#define COMLYNX_BAUD 62500u
#define SYNC_OP_ROW   0x01u
#define SYNC_OP_START 0x02u
#define SYNC_OP_STOP  0x03u

static uint8_t tx_queue[32];
static uint8_t tx_head;
static uint8_t tx_tail;
static PIO tx_pio = pio0;
static uint tx_sm;

static bool queue_byte(uint8_t value)
{
    uint8_t next = (uint8_t)((tx_head + 1u) & 31u);
    if (next == tx_tail)
        return false;
    tx_queue[tx_head] = value;
    tx_head = next;
    return true;
}

static void queue_event(uint8_t event)
{
    /* At ten rows per second this queue has enormous margin. If anything
     * exceptional fills it, STOP is safer than silently shifting position. */
    if ((event & GG_SYNC_EVENT_STOP) && !queue_byte(SYNC_OP_STOP)) {
        tx_head = tx_tail = 0u;
        (void)queue_byte(SYNC_OP_STOP);
        return;
    }
    if ((event & GG_SYNC_EVENT_START) && !queue_byte(SYNC_OP_START)) {
        tx_head = tx_tail = 0u;
        (void)queue_byte(SYNC_OP_STOP);
        return;
    }
    if ((event & GG_SYNC_EVENT_ROW) && !queue_byte(SYNC_OP_ROW)) {
        tx_head = tx_tail = 0u;
        (void)queue_byte(SYNC_OP_STOP);
    }
}

static void comlynx_task(void)
{
    while (tx_tail != tx_head && !pio_sm_is_tx_fifo_full(tx_pio, tx_sm)) {
        uint8_t value = tx_queue[tx_tail];
        tx_tail = (uint8_t)((tx_tail + 1u) & 31u);
        /* PIO consumes inverted data as pin directions: a zero drives the
         * open-drain output low and a one releases it. */
        pio_sm_put(tx_pio, tx_sm, (uint32_t)(uint8_t)~value);
    }
}

int main(void)
{
    gg_row_sync_t sync;
    uint offset;

    gpio_init(GG_SYNC_INPUT_PIN);
    gpio_set_dir(GG_SYNC_INPUT_PIN, GPIO_IN);
    gpio_disable_pulls(GG_SYNC_INPUT_PIN); /* PCB R4 supplies the pull-up */

#if STATUS_LED_PIN >= 0
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, 0);
#endif

    tx_sm = pio_claim_unused_sm(tx_pio, true);
    offset = pio_add_program(tx_pio, &comlynx_tx_program);
    comlynx_tx_program_init(tx_pio, tx_sm, offset,
                            COMLYNX_TX_PIN, COMLYNX_BAUD);

    sleep_ms(20);
    gg_row_sync_init(&sync, (uint8_t)gpio_get(GG_SYNC_INPUT_PIN),
                     time_us_64());

#if STATUS_LED_PIN >= 0
    gpio_put(STATUS_LED_PIN, 1); /* firmware ready */
#endif

    while (true) {
        uint8_t event = gg_row_sync_update(
            &sync, (uint8_t)gpio_get(GG_SYNC_INPUT_PIN), time_us_64());
        if (event != GG_SYNC_EVENT_NONE)
            queue_event(event);
        comlynx_task();
        tight_loop_contents();
    }
}
