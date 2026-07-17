/* Two-voice cart-streamed sample pool (DESIGN D5/D6).
 *
 * DAC slot 0 owns $D000-$D1FF and slot 1 owns $D200-$D3FF.  Each timer
 * drains its 512-byte ring independently.  The main-loop pump re-seeks the
 * cart before every chunk; this costs a little throughput but makes two
 * interleaved streams possible and repairs the cart position after EEPROM
 * config traffic.  At 7.8 kHz each voice drains ~131 bytes/frame and gets
 * up to 192 bytes/frame of refill bandwidth.
 */
#include <string.h>

#include "tracker.h"

#define POOL_BLOCK 45
#define POOL_BASE  ((unsigned long)POOL_BLOCK << 10)
#define RING0      ((unsigned char *)0xD000)
#define RING1      ((unsigned char *)0xD200)
#define RING_SIZE  512u
#define PUMP_BYTES 192

extern unsigned char *pcm_ptr[NDAC];    /* ring tails (IRQ-owned) */
#pragma zpsym("pcm_ptr")

#pragma code-name (push, "HICODE1")

/* Cold stream state occupies the unused end of the harness/debug RAM page. */
#pragma bss-name (push, "MIRRORRAM")
static unsigned char pool_nkits;
static unsigned stream_left[NDAC];
static unsigned stream_off[NDAC];
static unsigned char stream_block[NDAC];
static volatile unsigned char stream_cancel[NDAC];
static volatile unsigned char trig_kit[NDAC];
static volatile unsigned char trig_member[NDAC];
#pragma bss-name (pop)

static unsigned char *ring_base(unsigned char voice)
{
    return voice ? RING1 : RING0;
}

void pool_init(void)
{
    unsigned char hdr[4];

    trig_kit[0] = trig_kit[1] = EMPTY;
    stream_cancel[0] = stream_cancel[1] = 0;
    stream_left[0] = stream_left[1] = 0;
    cart_seek(POOL_BLOCK, 0);
    cart_read(hdr, 4);
    if (hdr[0] != 'P' || hdr[1] != 'L') {
        pool_nkits = 0;
        return;
    }
    pool_nkits = (hdr[2] <= 8) ? hdr[2] : 8;
}

static void pump_voice(unsigned char voice, unsigned char max)
{
    unsigned char *base = ring_base(voice);
    unsigned char n = max;
    unsigned free_, to_end;

    if (!stream_left[voice])
        return;
    free_ = (unsigned)(pcm_ptr[voice] - pcm_head[voice] - 1)
            & (RING_SIZE - 1);
    if (!free_)
        return;
    if (n > free_)
        n = (unsigned char)free_;
    to_end = (unsigned)(base + RING_SIZE - pcm_head[voice]);
    if (n > to_end)
        n = (unsigned char)to_end;
    if (n > stream_left[voice])
        n = (unsigned char)stream_left[voice];
    if (!n)
        return;

    /* Every chunk restores its own serial-cart cursor.  Besides enabling
     * two streams, this makes OPTIONS EEPROM writes harmless to playback. */
    cart_seek(stream_block[voice], stream_off[voice]);
    cart_read(pcm_head[voice], n);
    stream_left[voice] -= n;
    stream_off[voice] += n;
    while (stream_off[voice] >= 1024u) {
        stream_off[voice] -= 1024u;
        ++stream_block[voice];
    }
    pcm_head[voice] += n;
    if (pcm_head[voice] >= base + RING_SIZE)
        pcm_head[voice] = base;
    if (!stream_left[voice])
        pcm_done[voice] = 1;
}

static void do_trigger(unsigned char voice, unsigned char kit,
                       unsigned char member)
{
    unsigned char e[5];
    unsigned long abs_;
    unsigned len;
    unsigned char *base = ring_base(voice);

    /* A newer IRQ trigger may have arrived after pool_pump snapshotted this
     * one.  Never stop or start a slot on behalf of a stale request. */
    __asm__("sei");
    if (trig_kit[voice] != EMPTY || stream_cancel[voice]) {
        __asm__("cli");
        return;
    }
    __asm__("cli");

    if (kit >= pool_nkits)
        kit = 0;
    if (!pool_nkits) {
        dac_stop(voice);
        return;
    }
    /* The five-byte directory entry is cold trigger data, so read only the
     * selected one instead of pinning all 320 bytes in scarce RAM. */
    cart_seek(POOL_BLOCK, 4 + kit * 40 + (member & 7) * 5);
    cart_read(e, 5);
    len = e[3] | ((unsigned)e[4] << 8);
    if (!len) {
        dac_stop(voice);
        return;
    }

    dac_stop(voice);
    abs_ = POOL_BASE + ((unsigned long)e[0] | ((unsigned long)e[1] << 8)
                        | ((unsigned long)e[2] << 16));
    stream_block[voice] = (unsigned char)(abs_ >> 10);
    stream_off[voice] = (unsigned)(abs_ & 1023u);
    stream_left[voice] = len;
    pcm_done[voice] = 0;
    pcm_ptr[voice] = base;
    pcm_head[voice] = base;
    pump_voice(voice, PUMP_BYTES);
    pump_voice(voice, PUMP_BYTES);       /* 384-byte startup cushion */

    /* Cart reads are deliberately interruptible.  Recheck atomically: a
     * trigger that landed during prefill stays pending for the next frame,
     * while a cancellation must leave the timer stopped. */
    __asm__("sei");
    if (trig_kit[voice] == EMPTY && !stream_cancel[voice])
        pcm_ring_start(voice);
    else if (trig_kit[voice] != EMPTY)
        dac_mode[voice] = DAC_SAMPLE;
    __asm__("cli");
}

/* Called once per display frame from the main loop. */
void pool_pump(void)
{
    unsigned char voice;

    for (voice = 0; voice < NDAC; ++voice) {
        unsigned char kit, member;

        if (stream_cancel[voice]) {
            stream_left[voice] = 0;
            pcm_done[voice] = 1;
            stream_cancel[voice] = 0;
        }
        /* Snapshot/clear atomically against the VBlank engine trigger. */
        __asm__("sei");
        kit = trig_kit[voice];
        member = trig_member[voice];
        trig_kit[voice] = EMPTY;
        __asm__("cli");
        if (kit != EMPTY)
            do_trigger(voice, kit, member);
        pump_voice(voice, PUMP_BYTES);
    }
}

/* IRQ-context trigger: latch only; slow cart work stays in the main loop. */
void __fastcall__ pool_trigger(unsigned char voice, unsigned char kit,
                               unsigned char member)
{
    if (voice >= NDAC)
        return;
    stream_cancel[voice] = 0;
    pcm_done[voice] = 0;
    dac_mode[voice] = DAC_SAMPLE;       /* reserve until cart pump starts */
    ++(*(volatile unsigned char *)(0xC02B + voice));
    trig_member[voice] = member;
    trig_kit[voice] = kit;              /* publish last */
}

/* Stop future cart work for a stolen/released slot.  The timer itself is
 * stopped synchronously by dac_stop(); this flag is pump-side cleanup. */
void __fastcall__ pool_cancel(unsigned char voice)
{
    if (voice < NDAC) {
        trig_kit[voice] = EMPTY;
        stream_cancel[voice] = 1;
    }
}

#pragma code-name (pop)
