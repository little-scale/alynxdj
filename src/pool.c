/* Two-voice cart-streamed sample pool (DESIGN D5/D6).
 *
 * DAC slot 0 owns $D000-$D1FF and slot 1 owns $D200-$D3FF.  Each timer
 * drains its 512-byte ring independently.  A lone stream retains the cart
 * cursor; two interleaved streams re-seek when ownership changes.  Reads are
 * published in 64-byte pieces so the IRQ never has to wait behind one long
 * cartridge transfer, while each pump can restore the full 511-byte cushion.
 */
#include <string.h>

#include "tracker.h"

#define POOL_BLOCK 45
#define POOL_BASE  ((unsigned long)POOL_BLOCK << 10)
#define RING0      ((unsigned char *)0xD000)
#define RING1      ((unsigned char *)0xD200)
#define RING_SIZE  512u
#define PUMP_CHUNK 64
#define PUMP_PASSES 8

extern unsigned char *pcm_ptr[NDAC];    /* ring tails (IRQ-owned) */
#pragma zpsym("pcm_ptr")

/* Which stream currently owns the physical cart cursor.  With one active
 * sample, preserving this lets every refill continue sequentially instead
 * of re-selecting the block and discarding up to 1023 bytes.  A second voice
 * still re-seeks when ownership alternates, preserving D5/D6 symmetry. */
#define cart_stream (*(volatile unsigned char *)0xC029)

/* Cold stream state occupies the unused end of the harness/debug RAM page. */
#pragma bss-name (push, "MIRRORRAM")
static unsigned char pool_nkits;
static unsigned stream_left[NDAC];
/* Externally named so cart.s can perform the tiny cursor-owner fast path
 * without cc65's comparatively large 16-bit indexed-array helper. */
unsigned stream_off[NDAC];
unsigned char stream_block[NDAC];
static volatile unsigned char stream_cancel[NDAC];
static volatile unsigned char trig_kit[NDAC];
static volatile unsigned char trig_member[NDAC];
#pragma bss-name (pop)

void __fastcall__ position_cart(unsigned char voice); /* cart.s */

#pragma code-name (push, "HICODE1")

static unsigned char *ring_base(unsigned char voice)
{
    return voice ? RING1 : RING0;
}

void pool_init(void)
{
    unsigned char hdr[4];

    cart_stream = EMPTY;
    trig_kit[0] = trig_kit[1] = EMPTY;
    stream_cancel[0] = stream_cancel[1] = 0;
    stream_left[0] = stream_left[1] = 0;
    dac_started[0] = dac_started[1] = 0;
    cart_seek(POOL_BLOCK, 0);
    cart_read(hdr, 4);
    if (hdr[0] != 'P' || hdr[1] != 'L') {
        pool_nkits = 0;
        return;
    }
    pool_nkits = (hdr[2] <= 8) ? hdr[2] : 8;
}

static void pump_voice(unsigned char voice)
{
    unsigned char *base = ring_base(voice);
    unsigned char *head = pcm_head[voice];
    unsigned char *tail;
    unsigned char passes = PUMP_PASSES;
    unsigned char n;
    unsigned free_, to_end;

    do {
        if (!stream_left[voice])
            return;
        /* The timer IRQ advances this 16-bit pointer.  Snapshot it atomically;
         * a torn read at $D1FF->$D000 made the pump overwrite unread audio on
         * real hardware while Handy's scheduling tended to hide the race. */
        __asm__("sei");
        tail = pcm_ptr[voice];
        __asm__("cli");
        free_ = (unsigned)(tail - head - 1) & (RING_SIZE - 1);
        if (!free_)
            return;
        /* Rendering now checks in at glyph granularity.  Wait for a useful
         * piece of room instead of turning those frequent service points
         * into one- or two-byte cart transactions. */
        if (free_ < PUMP_CHUNK)
            return;
        n = PUMP_CHUNK;
        if (n > free_)
            n = (unsigned char)free_;
        to_end = (unsigned)(base + RING_SIZE - head);
        if (n > to_end)
            n = (unsigned char)to_end;
        if (n > stream_left[voice])
            n = (unsigned char)stream_left[voice];
        if (!n)
            return;

        /* Retain the cursor while one voice streams.  Interleaved voices and
         * directory reads invalidate ownership and take the re-seek path. */
        position_cart(voice);
        cart_read(head, n);
        stream_left[voice] -= n;
        stream_off[voice] += n;
        while (stream_off[voice] >= 1024u) {
            stream_off[voice] -= 1024u;
            ++stream_block[voice];
        }
        head += n;
        if (head >= base + RING_SIZE)
            head = base;
        /* Publish each short piece immediately.  The IRQ can now only see
         * the old complete buffer or the newly extended one. */
        __asm__("sei");
        pcm_head[voice] = head;
        if (!stream_left[voice])
            pcm_done[voice] = 1;
        __asm__("cli");
    } while (--passes);
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
    cart_stream = EMPTY;                /* directory seek leaves sample data */
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
    pump_voice(voice);                   /* full 511-byte startup cushion */

    /* Cart reads are deliberately interruptible.  Recheck atomically: a
     * trigger that landed during prefill stays pending for the next frame,
     * while a cancellation must leave the timer stopped. */
    __asm__("sei");
    if (trig_kit[voice] == EMPTY && !stream_cancel[voice]) {
        pcm_ring_start(voice);
        ++dac_started[voice];
    } else if (trig_kit[voice] != EMPTY)
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
        pump_voice(voice);
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
