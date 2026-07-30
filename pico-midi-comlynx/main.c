#include <stdbool.h>
#include <stdint.h>

#include "bsp/board_api.h"
#include "hardware/pio.h"
#include "hardware/uart.h"
#include "pico/stdlib.h"
#include "tusb.h"

#include "comlynx_tx.pio.h"
#include "serial_midi_parser.h"

#ifndef COMLYNX_TX_PIN
#define COMLYNX_TX_PIN 2
#endif

#ifndef SERIAL_MIDI_RX_PIN
#define SERIAL_MIDI_RX_PIN 13
#endif

#ifndef STATUS_LED_PIN
#define STATUS_LED_PIN -1
#endif

#define COMLYNX_BAUD 62500u
#define SERIAL_MIDI_BAUD 31250u
#define SERIAL_MIDI_UART uart0

/* The bridge output is faster than either MIDI 1.0 source, but USB delivers
 * packets in bursts. A byte ring lets both inputs return immediately while
 * PIO drains exact 11-bit ComLynx words. */
static uint8_t tx_queue[256];
static uint8_t tx_head;
static uint8_t tx_tail;
static uint8_t clock_phase;
static PIO tx_pio = pio0;
static uint tx_sm;
#if SERIAL_MIDI_RX_PIN >= 0
static serial_midi_parser_t serial_parser;
#endif

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

static void midi_channel_message(uint8_t status, uint8_t data1, uint8_t data2)
{
    uint8_t kind;
    uint8_t length;
    uint8_t message[3];

    if (status < 0x80u || status > 0xEFu || (status & 0x0Fu) >= 4u)
        return;

    kind = status & 0xF0u;
    length = (kind == 0xC0u || kind == 0xD0u) ? 2u : 3u;
    message[0] = status;
    message[1] = data1;
    message[2] = data2;
    queue_message(message, length);
}

static void midi_realtime(uint8_t status)
{
    if (status == 0xF8) {
        if (++clock_phase >= 6) {
            clock_phase = 0;
            (void)queue_byte(status);
        }
    } else if (status == 0xFA || status == 0xFB) {
        /* Start/Continue is the downbeat.  Emit its first row grant now;
         * waiting for the divider to collect six later clocks adds exactly
         * one tracker row of startup latency.  The reset phase makes the
         * next divided F8 arrive one complete row after this one. */
        clock_phase = 0;
        if (!queue_byte(status) || !queue_byte(0xF8))
            queue_recover();
    } else if (status == 0xFC) {
        clock_phase = 0;
        (void)queue_byte(status);
    } else if (status == 0xFF) {
        clock_phase = 0;
        (void)queue_byte(status);
    }
}

static void usb_midi_packet(const uint8_t packet[4])
{
    uint8_t status = packet[1];

    /* USB-MIDI event packets already contain complete channel messages.
     * The serial parser below expands running status to this same form. */
    if (status >= 0x80u && status <= 0xEFu)
        midi_channel_message(status, packet[2], packet[3]);
    else
        midi_realtime(status);
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

#if SERIAL_MIDI_RX_PIN >= 0
static void serial_midi_init(void)
{
    serial_midi_parser_reset(&serial_parser);
    uart_init(SERIAL_MIDI_UART, SERIAL_MIDI_BAUD);
    uart_set_hw_flow(SERIAL_MIDI_UART, false, false);
    uart_set_format(SERIAL_MIDI_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(SERIAL_MIDI_UART, true);
    gpio_set_function(SERIAL_MIDI_RX_PIN, GPIO_FUNC_UART);
    gpio_pull_up(SERIAL_MIDI_RX_PIN);
}

static void serial_midi_task(void)
{
    while (uart_is_readable(SERIAL_MIDI_UART)) {
        serial_midi_message_t message;
        serial_midi_result_t result =
            serial_midi_parser_feed(&serial_parser,
                                    (uint8_t)uart_getc(SERIAL_MIDI_UART),
                                    &message);
        if (result == SERIAL_MIDI_CHANNEL)
            midi_channel_message(message.status, message.data1, message.data2);
        else if (result == SERIAL_MIDI_REALTIME)
            midi_realtime(message.status);
    }
}
#else
static void serial_midi_init(void) {}
static void serial_midi_task(void) {}
#endif

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
    serial_midi_init();

    TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &usb));
    board_init_after_tusb();

#if STATUS_LED_PIN >= 0
    gpio_init(STATUS_LED_PIN);
    gpio_set_dir(STATUS_LED_PIN, GPIO_OUT);
    gpio_put(STATUS_LED_PIN, 1);
#endif

    while (true) {
        tud_task();
        usb_midi_task();
        serial_midi_task();
        comlynx_task();
    }
}
