#include <stdbool.h>
#include <stdint.h>

#include "bsp/board_api.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "comlynx_tx.pio.h"

#ifndef COMLYNX_TX_PIN
#define COMLYNX_TX_PIN 2
#endif

#define COMLYNX_BAUD 62500u

/* The bridge output is faster than the USB-MIDI 1.0 source, but USB delivers
 * packets in bursts.  A byte ring lets TinyUSB return immediately while PIO
 * drains exact 11-bit ComLynx words. */
static uint8_t tx_queue[256];
static uint8_t tx_head;
static uint8_t tx_tail;
static uint8_t clock_phase;
static PIO tx_pio = pio0;
static uint tx_sm;

static bool queue_byte(uint8_t value)
{
    uint8_t next = (uint8_t)(tx_head + 1);
    if (next == tx_tail)
        return false;
    tx_queue[tx_head] = value;
    tx_head = next;
    return true;
}

static void queue_recover(void)
{
    /* A full queue implies lost protocol state.  Drop pending software bytes
     * and send System Reset so MIDI takeover cannot retain a stuck note. */
    tx_head = tx_tail = 0;
    clock_phase = 0;
    (void)queue_byte(0xFF);
}

static void queue_message(const uint8_t *bytes, uint8_t length)
{
    while (length--) {
        if (!queue_byte(*bytes++)) {
            queue_recover();
            return;
        }
    }
}

static void usb_midi_packet(const uint8_t packet[4])
{
    uint8_t status = packet[1];
    uint8_t kind;
    uint8_t length;

    /* USB-MIDI event packets carry a complete channel status.  Restrict live
     * traffic to the four Lynx tracks but preserve the ordinary MIDI bytes. */
    if (status >= 0x80 && status <= 0xEF) {
        if ((status & 0x0F) >= 4)
            return;
        kind = status & 0xF0;
        length = (kind == 0xC0 || kind == 0xD0) ? 2 : 3;
        queue_message(&packet[1], length);
        return;
    }

    /* Reduce USB MIDI's 24 clocks/quarter to four tracker rows/quarter.  The
     * Lynx therefore receives one F8 row grant for every six source clocks.
     * MIDI takeover ignores these real-time bytes, while IN24 ignores notes,
     * so no bridge-side mode switch or heartbeat is needed. */
    if (status == 0xF8) {
        if (++clock_phase >= 6) {
            clock_phase = 0;
            (void)queue_byte(status);
        }
    } else if (status == 0xFA || status == 0xFB || status == 0xFC) {
        clock_phase = 0;
        (void)queue_byte(status);
    } else if (status == 0xFF) {
        clock_phase = 0;
        (void)queue_byte(status);
    }
}

static void usb_midi_task(void)
{
    while (tud_midi_available()) {
        uint8_t packet[4];
        if (!tud_midi_packet_read(packet))
            break;
        usb_midi_packet(packet);
    }
}

static void comlynx_task(void)
{
    while (tx_tail != tx_head && !pio_sm_is_tx_fifo_full(tx_pio, tx_sm)) {
        uint8_t value = tx_queue[tx_tail++];
        /* PIO writes direction bits: inverted MIDI data makes a zero drive
         * low and a one release the open-drain line. */
        pio_sm_put(tx_pio, tx_sm, (uint32_t)(uint8_t)~value);
    }
}

int main(void)
{
    uint offset;
    const tusb_rhport_init_t usb = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL,
    };

    board_init();
    tx_sm = pio_claim_unused_sm(tx_pio, true);
    offset = pio_add_program(tx_pio, &comlynx_tx_program);
    comlynx_tx_program_init(tx_pio, tx_sm, offset,
                            COMLYNX_TX_PIN, COMLYNX_BAUD);

    TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &usb));
    board_init_after_tusb();

    while (true) {
        tud_task();
        usb_midi_task();
        comlynx_task();
    }
}
