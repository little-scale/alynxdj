/* ComLynx sync (DESIGN.md §11) — M11a: the UART driver + OUT/IN modes.
 *
 * ComLynx is Mikey's UART on an open-collector bus: every transmitted
 * byte is also received by the sender (loopback) — which is how the
 * headless harness verifies the whole path on a single unit. Protocol is
 * deliberately dumb (multi-drop, no addressing): one byte per event.
 *
 * OUT: master — sends START at play, one ROW byte per row boundary,
 *      STOP at stop. Its own loopback bytes are counted but not obeyed.
 * IN:  slave — rows advance only on received ROW bytes (the stored
 *      groove and W are ignored, ported semantics); START/STOP follow
 *      the master's transport.
 */
#include <lynx.h>

#include "tracker.h"

unsigned char sync_mode;            /* SYNC_*; mirrored at $C021 */
#define MIRROR_SYNCMODE (*(volatile unsigned char *)0xC021)
unsigned char sync_row_pending;     /* IN: clocks received, rows owed */
static unsigned char rx_count;

#define MIRROR_RXCOUNT (*(volatile unsigned char *)0xC020)

void sync_init(void)
{
    /* timer 4 is the UART bit clock: ~62.5 kbaud, the ComLynx standard */
    _UART_TIMER.reload = 1;
    _UART_TIMER.control = 0x18;     /* reload | count, 1 us clock */
    MIKEY.serctl = 0x08;            /* clear errors; polled, no IRQs */
}

void __fastcall__ sync_tx(unsigned char b)
{
    if (sync_mode != SYNC_OUT)
        return;
    while (!(MIKEY.serctl & 0x80))  /* TxDone */
        ;
    MIKEY.serdat = b;
}

/* one call per frame from the main loop */
void sync_poll(void)
{
    unsigned char b;

    MIRROR_SYNCMODE = sync_mode;
    while (MIKEY.serctl & 0x40) {   /* RxReady */
        b = MIKEY.serdat;
        MIRROR_RXCOUNT = ++rx_count;
        if (sync_mode != SYNC_IN)
            continue;               /* OUT: our own loopback — count only */
        switch (b) {
        case SYNC_OP_ROW:
            ++sync_row_pending;
            break;
        case SYNC_OP_START:
            engine_play_song(0);
            break;
        case SYNC_OP_STOP:
            engine_stop();
            break;
        }
    }
}
