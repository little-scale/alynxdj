/* Cart-streamed sample pool (DESIGN §10): kits live on the cart past the
 * loaded program; playback streams through a 1 KB RAM ring at $D000 that
 * the timer-7 IRQ drains and this pump refills from the main loop.
 *
 * Budget: the IRQ drains ~131 bytes/frame at 7812.5 Hz; the pump moves up
 * to 192, so the ring never starves after the 512-byte prefill. Cart
 * reads cost ~10 cycles/byte — ~2K cycles/frame, noise.
 */
#include <string.h>

#include "tracker.h"

#define POOL_BLOCK 40
#define POOL_BASE  ((unsigned long)POOL_BLOCK << 10)
#define RING       ((unsigned char *)0xD000)
#define RING_SIZE  1024u

extern unsigned char *pcm_ptr;          /* ring tail (IRQ-owned) */
#pragma zpsym("pcm_ptr")
extern unsigned char *pcm_head;         /* ring head (ours) */
extern unsigned char pcm_done;

/* 8 kits x 8 slots x {u24 rel offset, u16 len}, straight from the cart */
#pragma bss-name (push, "SONG")
static unsigned char pool_dir[8][8][5];
#pragma bss-name (pop)
static unsigned char pool_nkits;

static unsigned stream_left;
static unsigned char stream_block;      /* resync info for S/eeprom races */
static volatile unsigned char trig_kit = 0xFF, trig_slot;

unsigned char pool_kits(void)
{
    return pool_nkits;
}

void pool_init(void)
{
    unsigned char hdr[4];
    unsigned char k;

    cart_seek(POOL_BLOCK, 0);
    cart_read(hdr, 4);
    if (hdr[0] != 'P' || hdr[1] != 'L') {
        pool_nkits = 0;
        return;
    }
    pool_nkits = (hdr[2] <= 8) ? hdr[2] : 8;
    for (k = 0; k < pool_nkits; ++k)
        cart_read(pool_dir[k][0], 40);
}

static void pump_chunk(unsigned char max)
{
    unsigned char n = max;
    unsigned free_, to_end;

    if (!stream_left)
        return;
    /* free ring space, one byte reserved to keep head != tail when full */
    free_ = (unsigned)(pcm_ptr - pcm_head - 1) & (RING_SIZE - 1);
    if (!free_)
        return;
    if (n > free_)
        n = (unsigned char)free_;
    to_end = (unsigned)(RING + RING_SIZE - pcm_head);
    if (n > to_end)
        n = (unsigned char)to_end;
    if (n > stream_left)
        n = (unsigned char)stream_left;
    if (!n)
        return;
    cart_read(pcm_head, n);
    stream_left -= n;
    pcm_head += n;
    if (pcm_head >= RING + RING_SIZE)
        pcm_head = RING;
    if (!stream_left)
        pcm_done = 1;
}

static void do_trigger(unsigned char kit, unsigned char slot);

/* called every frame from the main loop */
void pool_pump(void)
{
    if (trig_kit != 0xFF) {
        unsigned char k = trig_kit, s = trig_slot;
        trig_kit = 0xFF;
        do_trigger(k, s);
    }
    pump_chunk(64);
    pump_chunk(64);
    pump_chunk(64);
}

/* Called from the engine tick (IRQ context): just latch the request —
 * the slow cart seek + prefill runs from the main-loop pump. Costs up to
 * one frame of drum-onset latency, buys a race-free cart bus. */
void __fastcall__ pool_trigger(unsigned char kit, unsigned char slot)
{
    trig_kit = kit;
    trig_slot = slot;
}

static void do_trigger(unsigned char kit, unsigned char slot)
{
    const unsigned char *e;
    unsigned long abs_;
    unsigned len;
    unsigned char i;

    if (kit >= pool_nkits)
        kit = 0;
    if (!pool_nkits)
        return;
    e = pool_dir[kit][slot & 7];
    len = e[3] | ((unsigned)e[4] << 8);
    if (!len)
        return;
    pcm_stop();                          /* silence while we re-aim */
    abs_ = POOL_BASE + ((unsigned long)e[0] | ((unsigned long)e[1] << 8)
                        | ((unsigned long)e[2] << 16));
    stream_block = (unsigned char)(abs_ >> 10);
    cart_seek(stream_block, (unsigned)(abs_ & 1023u));
    stream_left = len;
    pcm_done = 0;
    pcm_head = RING;
    /* prefill so the IRQ can't catch the pump on startup */
    for (i = 0; i < 8; ++i)
        pump_chunk(64);
    pcm_ring_start();
}
