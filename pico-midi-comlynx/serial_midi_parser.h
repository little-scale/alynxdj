#ifndef ALYNXDJ_SERIAL_MIDI_PARSER_H
#define ALYNXDJ_SERIAL_MIDI_PARSER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    SERIAL_MIDI_NONE = 0,
    SERIAL_MIDI_CHANNEL,
    SERIAL_MIDI_REALTIME
} serial_midi_result_t;

typedef struct {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
} serial_midi_message_t;

typedef struct {
    uint8_t running_status;
    uint8_t pending_status;
    uint8_t data[2];
    uint8_t data_count;
    uint8_t data_needed;
    uint8_t system_remaining;
    bool in_sysex;
} serial_midi_parser_t;

static inline uint8_t serial_midi_channel_data_length(uint8_t status)
{
    uint8_t kind = status & 0xF0u;
    return (kind == 0xC0u || kind == 0xD0u) ? 1u : 2u;
}

static inline uint8_t serial_midi_system_data_length(uint8_t status)
{
    switch (status) {
    case 0xF1:
    case 0xF3:
        return 1;
    case 0xF2:
        return 2;
    default:
        return 0;
    }
}

static inline void serial_midi_parser_reset(serial_midi_parser_t *parser)
{
    parser->running_status = 0;
    parser->pending_status = 0;
    parser->data_count = 0;
    parser->data_needed = 0;
    parser->system_remaining = 0;
    parser->in_sysex = false;
}

static inline serial_midi_result_t
serial_midi_parser_feed(serial_midi_parser_t *parser, uint8_t byte,
                        serial_midi_message_t *message)
{
    if (byte >= 0xF8u) {
        message->status = byte;
        message->data1 = 0;
        message->data2 = 0;
        if (byte == 0xFFu)
            serial_midi_parser_reset(parser);
        return SERIAL_MIDI_REALTIME;
    }

    if (parser->in_sysex) {
        if (byte == 0xF7u)
            parser->in_sysex = false;
        return SERIAL_MIDI_NONE;
    }

    if (byte & 0x80u) {
        parser->data_count = 0;
        parser->data_needed = 0;
        parser->system_remaining = 0;

        if (byte <= 0xEFu) {
            parser->running_status = byte;
            parser->pending_status = byte;
            parser->data_needed = serial_midi_channel_data_length(byte);
        } else {
            parser->running_status = 0;
            parser->pending_status = 0;
            if (byte == 0xF0u)
                parser->in_sysex = true;
            else
                parser->system_remaining =
                    serial_midi_system_data_length(byte);
        }
        return SERIAL_MIDI_NONE;
    }

    if (parser->system_remaining) {
        --parser->system_remaining;
        return SERIAL_MIDI_NONE;
    }

    if (!parser->pending_status) {
        if (!parser->running_status)
            return SERIAL_MIDI_NONE;
        parser->pending_status = parser->running_status;
        parser->data_count = 0;
        parser->data_needed =
            serial_midi_channel_data_length(parser->running_status);
    }

    parser->data[parser->data_count++] = byte;
    if (parser->data_count < parser->data_needed)
        return SERIAL_MIDI_NONE;

    message->status = parser->pending_status;
    message->data1 = parser->data[0];
    message->data2 =
        (parser->data_needed == 2u) ? parser->data[1] : 0u;
    parser->pending_status = 0;
    parser->data_count = 0;
    parser->data_needed = 0;
    return SERIAL_MIDI_CHANNEL;
}

#endif
