/* ComLynx transport + MIDI takeover (DESIGN.md §11).
 *
 * OFF/OUT/IN retain the deliberately small one-byte row-sync protocol.  A
 * legacy START byte is also the first row grant: sending START then ROW as
 * two adjacent bytes would overrun the polled one-byte Mikey receiver before
 * the slave's next VBlank.  Later rows still use one ROW byte each.
 * IN24 consumes row-rate Timing Clock plus Start/Continue/Stop bytes.  The
 * Pico emits an immediate downbeat grant after Start/Continue, then divides
 * USB MIDI's 24-PPQN clock by six before each following row, leaving the Lynx
 * with the same deliberately small one-pulse-per-row job as IN.
 * MIDI is an exclusive, receive-only live mode: normalized USB-MIDI channel
 * messages from a bridge drive tracks A-D through the ordinary instrument
 * trigger path.  The bridge always emits a complete status byte (no running
 * status), so the next status naturally resynchronizes a damaged stream.
 *
 * MIDI traffic can contain a chord inside one video frame, whereas Mikey has
 * only one receive holding register.  In MIDI mode the timer-4 UART interrupt
 * therefore captures bytes in a 64-byte ring.  engine_tick() drains the ring
 * before voice work while the IRQ remains nestable, so redraw never owns MIDI
 * latency and later chord bytes are received during earlier tracker triggers.
 * The ring overlays play_cnt at $C048: takeover stops the sequencer, so the
 * two uses are mutually exclusive.
 */
#include <lynx.h>
#include <string.h>

#include "tracker.h"

#pragma code-name (push, "HICODE2")

#define sync_rx_head     (*(volatile unsigned char *)0xC004)
#define sync_rx_tail     (*(volatile unsigned char *)0xC005)
#define sync_rx_overrun  (*(volatile unsigned char *)0xC006)
#define midi_status      (*(volatile unsigned char *)0xC007)
#define midi_data        (*(volatile unsigned char *)0xC008)
#define midi_have        (*(volatile unsigned char *)0xC009)
#define midi_held        ((volatile unsigned char *)0xC00A)
#define irq_ready        (*(volatile unsigned char *)0xC00E)
#define clock_rx_head    (*(volatile unsigned char *)0xC0F9)
#define clock_rx_tail    (*(volatile unsigned char *)0xC0FA)
#define clock_rx_overrun (*(volatile unsigned char *)0xC0FB)
#define CLOCK_RX_BUF     ((volatile unsigned char *)0xC088)
#define CLOCK_RX_MASK    0x3F
#define MIRROR_RXCOUNT  (*(volatile unsigned char *)0xC020)
#define MIRROR_SYNCMODE (*(volatile unsigned char *)0xC021)
#define MIDI_RX_BUF     ((volatile unsigned char *)0xC048)
#define MIDI_RX_MASK    0x3F

/* cc65 2.18's _UART_TIMER points at timer 5, not the real UART timer 4. */
#define TIM4BKUP (*(volatile unsigned char *)0xFD10)
#define TIM4CTLA (*(volatile unsigned char *)0xFD11)

/* The ninth serial bit is fixed Space (PAREN=0/PAREVEN=0).  TXOPEN must be
 * set even in receive-only modes: Mikey powers up driving TTL high, which is
 * unsafe on the shared open-collector ComLynx data line. */
#define SER_BASE 0x04               /* TXOPEN */
#define SER_RESET (SER_BASE | 0x08) /* RESETERR strobe */
#define SER_RXIRQ (SER_BASE | 0x40) /* RXINTEN */

#pragma code-name (pop)
#pragma code-name (push, "MIDICODE")

/* The complete-message parser and panic reset are assembly-sized because
 * this live overlay shares the EEPROM pack window byte-for-byte. */
void __fastcall__ midi_byte(unsigned char b);
void midi_panic(void);

static void sync_clock_byte(unsigned char b)
{
    switch (b) {
    case 0xF8:                       /* Pico emits one pulse per row */
        if (eng_mode != MODE_STOP && sync_row_pending != 0xFF)
            ++sync_row_pending;
        break;
    case 0xFA:                       /* Start */
    case 0xFB:                       /* Continue: no SPP, restart at row 0 */
        sync_row_pending = 0;
        if (!eng_waiting)            /* a local cue takes precedence */
            engine_play_song(0);
        break;
    case 0xFC:                       /* Stop */
        sync_row_pending = 0;
        engine_stop();
        break;
    }
}

#pragma code-name (pop)
#pragma code-name (push, "HICODE2")

void sync_init(void)
{
    /* Fixed live/debug state is outside the C BSS because MAIN is full.
     * Clear its contiguous band explicitly, then mark all held notes empty. */
    memset((void *)0xC004, 0, 18);
    memset((void *)0xC00A, EMPTY, NCH);
    clock_rx_head = clock_rx_tail = clock_rx_overrun = 0;
    TIM4BKUP = 1;                    /* 62.5 kbaud */
    TIM4CTLA = 0x18;                 /* reload | count, 1 us clock */
    MIKEY.serctl = SER_RESET;
}

#pragma code-name (pop)
#pragma code-name (push, "MIDICODE")

/* Called only after the project IRQ vector has been installed. */
void sync_irq_enable(void)
{
    irq_ready = 1;
    if (sync_mode == SYNC_MIDI || sync_mode == SYNC_IN24)
        MIKEY.serctl = SER_RXIRQ;
}

void __fastcall__ sync_set_mode(unsigned char mode)
{
    volatile unsigned char discard = 0;

    if (mode >= NSYNC)
        mode = SYNC_OFF;
    if (mode == sync_mode)
        return;

    MIKEY.serctl = SER_RESET;        /* disable RX IRQ before reusing ring */
    while (MIKEY.serctl & 0x40)
        discard = MIKEY.serdat;
    (void)discard;
    MIKEY.intrst = 0x10;
    sync_rx_head = sync_rx_tail = sync_rx_overrun = 0;
    clock_rx_head = clock_rx_tail = clock_rx_overrun = 0;
    sync_row_pending = 0;
    midi_panic();                    /* mode changes never leave live notes */
    sync_mode = mode;
    if (mode == SYNC_IN24)
        editor_clipboard_clear();    /* its 64 bytes become the clock RX ring */
    if ((mode == SYNC_MIDI || mode == SYNC_IN24) && irq_ready)
        MIKEY.serctl = SER_RXIRQ;
}

#pragma code-name (pop)
#pragma code-name (push, "MIDICODE")

void __fastcall__ sync_tx(unsigned char b)
{
    if (sync_mode != SYNC_OUT)
        return;
    while (!(MIKEY.serctl & 0x80))
        ;
    MIKEY.serdat = b;
}

#pragma code-name (pop)
#pragma code-name (push, "MIDICODE")

/* Shared by every local transport entry point. Keeping the WAIT setup and
 * OUT's one-byte downbeat together avoids mode-specific start omissions. */
void sync_play_start(void)
{
    sync_row_pending = 0;
    eng_waiting = (sync_mode == SYNC_IN || sync_mode == SYNC_IN24);
    sync_tx(SYNC_OP_START);
}

void sync_poll(void)
{
    unsigned char b;

    MIRROR_SYNCMODE = sync_mode;
    if (sync_mode == SYNC_MIDI) {
        if (sync_rx_overrun) {
            sync_rx_tail = sync_rx_head;
            sync_rx_overrun = 0;
            midi_panic();             /* a lost Note Off must not stick */
        }
        while (sync_rx_tail != sync_rx_head) {
            b = MIDI_RX_BUF[sync_rx_tail];
            sync_rx_tail = (sync_rx_tail + 1) & MIDI_RX_MASK;
            ++MIRROR_RXCOUNT;
            midi_byte(b);
        }
        return;
    }

    if (sync_mode == SYNC_IN24) {
        if (clock_rx_overrun) {
            clock_rx_tail = clock_rx_head;
            clock_rx_overrun = 0;
            sync_row_pending = 0;
            engine_stop();             /* never continue on a shifted clock */
        }
        while (clock_rx_tail != clock_rx_head) {
            b = CLOCK_RX_BUF[clock_rx_tail];
            clock_rx_tail = (clock_rx_tail + 1) & CLOCK_RX_MASK;
            ++MIRROR_RXCOUNT;
            sync_clock_byte(b);
        }
        return;
    }

    /* Row sync remains sparse enough to poll, and does not touch the MIDI
     * ring because $C048 is the running sequencer's phrase-pass state. */
    while (MIKEY.serctl & 0x40) {
        b = MIKEY.serdat;
        ++MIRROR_RXCOUNT;
        if (sync_mode != SYNC_IN)
            continue;
        switch (b) {
        case SYNC_OP_ROW:
            if (eng_mode != MODE_STOP && sync_row_pending != 0xFF)
                ++sync_row_pending;
            break;
        case SYNC_OP_START:
            /* START is the downbeat as well as transport.  Preserve a local
             * cue, otherwise load song row 0, then grant that selected row in
             * this same engine tick.  This avoids the historical full-row
             * wait for OUT's first ordinary ROW byte. */
            sync_row_pending = 0;
            if (!eng_waiting)        /* preserve an explicitly cued row */
                engine_play_song(0);
            if (eng_mode != MODE_STOP)
                sync_row_pending = 1;
            break;
        case SYNC_OP_STOP:  engine_stop(); break;
        }
    }
}

/* Deterministic parser entry for the headless protocol regression. */
void __fastcall__ sync_test_byte(unsigned char b)
{
    midi_byte(b);
}

#pragma code-name (pop)
