#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "../pico-midi-comlynx/serial_midi_parser.h"

static serial_midi_result_t feed(serial_midi_parser_t *parser, uint8_t byte,
                                 serial_midi_message_t *message)
{
    return serial_midi_parser_feed(parser, byte, message);
}

int main(void)
{
    serial_midi_parser_t parser;
    serial_midi_message_t message;

    serial_midi_parser_reset(&parser);

    assert(feed(&parser, 0x90, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 60, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 0xF8, &message) == SERIAL_MIDI_REALTIME);
    assert(message.status == 0xF8);
    assert(feed(&parser, 100, &message) == SERIAL_MIDI_CHANNEL);
    assert(message.status == 0x90 && message.data1 == 60 &&
           message.data2 == 100);

    /* Running status must be expanded into another complete channel message. */
    assert(feed(&parser, 62, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 101, &message) == SERIAL_MIDI_CHANNEL);
    assert(message.status == 0x90 && message.data1 == 62 &&
           message.data2 == 101);

    /* One-data-byte channel messages retain running status too. */
    assert(feed(&parser, 0xC0, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 7, &message) == SERIAL_MIDI_CHANNEL);
    assert(message.status == 0xC0 && message.data1 == 7);
    assert(feed(&parser, 8, &message) == SERIAL_MIDI_CHANNEL);
    assert(message.status == 0xC0 && message.data1 == 8);

    /* System Common cancels running status and consumes its own data. */
    assert(feed(&parser, 0xF2, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 1, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 2, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 64, &message) == SERIAL_MIDI_NONE);

    /* SysEx is ignored, but real-time bytes may legally interrupt it. */
    assert(feed(&parser, 0xF0, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 1, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 0xFA, &message) == SERIAL_MIDI_REALTIME);
    assert(message.status == 0xFA);
    assert(feed(&parser, 2, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 0xF7, &message) == SERIAL_MIDI_NONE);

    /* Reset is emitted as panic and also clears any partial/running message. */
    assert(feed(&parser, 0x90, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 65, &message) == SERIAL_MIDI_NONE);
    assert(feed(&parser, 0xFF, &message) == SERIAL_MIDI_REALTIME);
    assert(message.status == 0xFF);
    assert(feed(&parser, 100, &message) == SERIAL_MIDI_NONE);

    puts("serial MIDI parser tests passed");
    return 0;
}
