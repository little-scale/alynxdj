/* M2 exploration voice: one Mikey channel as an LFSR square wave.
 *
 * Square recipe: FEEDBACK = tap 0 only, shifter seeded 0 — Mikey shifts in
 * the inverted parity of the tapped bits, so from 0 the register alternates
 * 0/1/0/1: a square at 1 / (2 * (BACKUP+1) * clock).  Real driver (asm,
 * shadow+flush) lands at M3; this file exists to prove the sound path.
 */
#include <lynx.h>

/* control bits */
#define AUD_ENABLE_COUNT  0x10
#define AUD_ENABLE_RELOAD 0x08
#define AUD_INTEGRATE     0x20

void sound_init(void)
{
    /* crt0 muted everything: MSTEREO=$FF (all channel/side disables set),
       volumes 0. Open the gates; keep volumes per-note. */
    MIKEY.mstereo = 0x00;
    MIKEY.attena = 0xFF;
    MIKEY.attenb = 0xFF;
    MIKEY.attenc = 0xFF;
    MIKEY.attend = 0xFF;
    MIKEY.panning = 0x00;
}

/* clocksel: 0..6 = 1<<n microseconds per tick; bkup: timer reload */
void tone_on(unsigned char clocksel, unsigned char bkup)
{
    MIKEY.channel_a.control  = 0;            /* stop while reprogramming */
    MIKEY.channel_a.feedback = 0x01;         /* tap 0 = square */
    MIKEY.channel_a.dac      = 0;
    MIKEY.channel_a.shiftlo  = 0;
    MIKEY.channel_a.other    = 0;
    MIKEY.channel_a.count    = bkup;
    MIKEY.channel_a.reload   = bkup;
    MIKEY.channel_a.volume   = 0x7F;
    MIKEY.channel_a.control  = AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD | clocksel;
}

void tone_off(void)
{
    MIKEY.channel_a.control = 0;
    MIKEY.channel_a.volume  = 0;
    MIKEY.channel_a.dac     = 0;
}
