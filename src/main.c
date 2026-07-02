/* ALYNXDJ — boot, display init, text render, main loop.
 *
 * crt0 already ran the Mikey init table (display timers, DISPCTL=$0D 4bpp
 * color DMA, audio muted); what it does not set is the screen address,
 * the palette, and interrupts — that is done here.
 */
#include <lynx.h>
#include <string.h>

#include "tracker.h"
#include "../build/font.h"
#include "../build/buildid.h"

#define SCREEN      ((unsigned char *)0xA000)
#define LINE_BYTES  80

extern volatile unsigned int frames;
void vbl_install(void);

#define N(oct, semi) ((unsigned char)(((oct) - 1) * 12 + (semi) + 1))

/* raw pad byte mirrored here for the harness's RAM-dump probes */
#define JOY_MIRROR (*(volatile unsigned char *)0xC000)

/* 12-bit GBR palette entries, {green, blue<<4|red} per pen */
static const unsigned char palette[16][2] = {
    {0x2, 0x32}, /* 0 bg: deep blue-grey */
    {0xF, 0xEE}, /* 1 text: near-white */
    {0x8, 0x97}, /* 2 dim chrome */
    {0xC, 0x2F}, /* 3 accent: warm orange */
    /* pens 4-15 settle with the real UI */
};

static void palette_init(void)
{
    unsigned char i;
    for (i = 0; i < 16; ++i) {
        MIKEY.palette[i]      = palette[i][0];  /* $FDA0+i green */
        MIKEY.palette[i + 16] = palette[i][1];  /* $FDB0+i blue|red */
    }
}

static void screen_clear(void)
{
    memset(SCREEN, PEN_BG | (PEN_BG << 4), (unsigned int)LINE_BYTES * 102);
}

/* clear the 16 grid rows (char rows 1..16 = pixel rows 6..101) */
void clear_grid(void)
{
    memset(SCREEN + 6 * LINE_BYTES, PEN_BG | (PEN_BG << 4),
           (unsigned int)LINE_BYTES * 96);
}

/* Draw one glyph at char cell (cx, cy); fg/bg are pen numbers. */
static void draw_char(unsigned char cx, unsigned char cy, char ch,
                      unsigned char fg, unsigned char bg)
{
    const unsigned char *g;
    unsigned char *p = SCREEN + (unsigned int)cy * 6 * LINE_BYTES + cx * 2;
    unsigned char r, bits, l, rr;

    if (ch >= 'a' && ch <= 'z')
        ch -= 'a' - 'A';           /* all-caps UI, sibling style */
    if (ch < FONT_FIRST || ch > FONT_LAST)
        ch = '?';
    g = font[ch - FONT_FIRST];
    for (r = 0; r < 6; ++r) {
        bits = g[r];
        l  = (bits & 0x08) ? fg : bg;
        rr = (bits & 0x04) ? fg : bg;
        p[0] = (l << 4) | rr;
        l  = (bits & 0x02) ? fg : bg;
        rr = (bits & 0x01) ? fg : bg;
        p[1] = (l << 4) | rr;
        p += LINE_BYTES;
    }
}

void draw_text(unsigned char cx, unsigned char cy, const char *s,
               unsigned char fg, unsigned char bg)
{
    while (*s)
        draw_char(cx++, cy, *s++, fg, bg);
}

void draw_hex8(unsigned char cx, unsigned char cy, unsigned char v,
               unsigned char fg, unsigned char bg)
{
    static const char hexd[] = "0123456789ABCDEF";
    draw_char(cx, cy, hexd[v >> 4], fg, bg);
    draw_char(cx + 1, cy, hexd[v & 0x0F], fg, bg);
}

/* blank song: $FF sentinels everywhere (ported convention) */
static void song_new(void)
{
    memset(sd.song, EMPTY, sizeof(sd.song));
    memset(sd.chains, 0, sizeof(sd.chains));
    memset(sd.phrases, 0, sizeof(sd.phrases));
    {
        unsigned char c, s;
        for (c = 0; c < NCHAINS; ++c)
            for (s = 0; s < PHRASE_ROWS; ++s)
                sd.chains[c][s].phrase = EMPTY;
        for (c = 0; c < NINSTR; ++c) {
            sd.instrs[c].pan = 0xFF;    /* centre = full both sides */
            sd.instrs[c].table = EMPTY;
        }
    }
    sd.grooves[0][0] = 6;               /* default groove: straight 6/6 */
    sd.grooves[0][1] = 6;
}

/* demo song until the sample pool ships a real one:
 * T1 = C-major arp (chain 0: phrase 0, then phrase 0 transposed +12)
 * T2 = bass roots   (chain 1: phrase 1)
 * T3 = offbeat blips (chain 2: phrase 2)
 * T4 = noise hats   (chain 3: phrase 3, instrument 1) */
static void song_demo(void)
{
    static const unsigned char arp[16] = {
        N(4,0), N(4,4), N(4,7), N(5,0), N(5,4), N(5,0), N(4,7), N(4,4),
        N(4,0), N(4,7), N(5,0), N(5,4), N(5,7), N(5,4), N(5,0), N(4,7),
    };
    unsigned char i;

    song_new();

    /* instrument 0: default square lead; 1: noise hat; 2: sustained bass */
    sd.instrs[0].type = IT_TONE;
    sd.instrs[0].vol = 0x7F;
    sd.instrs[0].hold = 2;
    sd.instrs[0].dcy = 32;
    sd.instrs[1].type = IT_NOISE;
    sd.instrs[1].vol = 0x60;
    sd.instrs[1].timbre = 7;
    sd.instrs[1].dcy = 24;
    sd.instrs[2].type = IT_TONE;
    sd.instrs[2].vol = 0x70;
    sd.instrs[2].hold = 8;
    sd.instrs[2].dcy = 8;
    sd.instrs[2].pan = 0xF4;                       /* bass: hard left */
    sd.instrs[3].type = IT_KIT;
    sd.instrs[3].vol = 0x7F;
    sd.instrs[4].type = IT_WAV;                    /* integrate triangle */
    sd.instrs[4].vol = 10;
    sd.instrs[4].hold = 8;
    sd.instrs[4].dcy = 2;
    sd.instrs[4].pan = 0x4F;                       /* blips: hard right */

    for (i = 0; i < 16; ++i)
        sd.phrases[0][i].note = arp[i];
    for (i = 0; i < 16; i += 4) {
        sd.phrases[1][i].note = N(2,0);            /* C-2 bass */
        sd.phrases[1][i].instr = 2;
    }
    for (i = 2; i < 16; i += 4) {
        sd.phrases[2][i].note = N(5,7);            /* G-5 blips, WAV voice */
        sd.phrases[2][i].instr = 4;
    }
    /* 808 kit: C=BD, D=SD, F#=HH (note semitone picks the slot) */
    for (i = 0; i < 16; i += 2) {
        sd.phrases[3][i].note = (i & 4) ? ((i & 2) ? N(4,6) : N(4,2))
                                        : ((i & 2) ? N(4,6) : N(4,0));
        sd.phrases[3][i].instr = 3;
    }

    sd.chains[0][0].phrase = 0;
    sd.chains[0][1].phrase = 0;
    sd.chains[0][1].tsp = 12;                      /* second pass +1 octave */
    sd.chains[1][0].phrase = 1;
    sd.chains[1][1].phrase = 1;
    sd.chains[2][0].phrase = 2;
    sd.chains[2][1].phrase = 2;
    sd.chains[3][0].phrase = 3;
    sd.chains[3][1].phrase = 3;

    for (i = 0; i < 2; ++i) {
        sd.song[i][0] = 0;
        sd.song[i][1] = 1;
        sd.song[i][2] = 2;
        sd.song[i][3] = 3;
    }

    /* --- M9 command test rigs (song rows 16.., unreachable from the demo
     * playback, driven by chain-loop transport in the harness) --- */
    sd.instrs[5].type = IT_TONE;                   /* sustain lead */
    sd.instrs[5].vol = 0x7F;
    sd.instrs[5].hold = 4;
    sd.instrs[5].dcy = 2;
    sd.grooves[1][0] = 3;                          /* G-test groove */
    sd.grooves[1][1] = 3;

    for (i = 4; i <= 10; ++i) {
        sd.chains[i][0].phrase = i;
        sd.song[16 + (i - 4)][0] = i;
    }
    /* ph4: K kill — short/long burst alternation */
    for (i = 0; i < 16; i += 4) {
        sd.phrases[4][i].note = N(4,0);
        if (!(i & 4)) { sd.phrases[4][i].cmd = CMD_K; sd.phrases[4][i].param = 2; }
    }
    /* ph5: P bend up */
    sd.phrases[5][0].note = N(4,0); sd.phrases[5][0].instr = 5;
    sd.phrases[5][0].cmd = CMD_P;  sd.phrases[5][0].param = 2;
    /* ph6: V vibrato */
    sd.phrases[6][0].note = N(4,0); sd.phrases[6][0].instr = 5;
    sd.phrases[6][0].cmd = CMD_V;  sd.phrases[6][0].param = 0x28;
    /* ph7: C chord arp */
    sd.phrases[7][0].note = N(4,0); sd.phrases[7][0].instr = 5;
    sd.phrases[7][0].cmd = CMD_C;  sd.phrases[7][0].param = 0x47;
    /* ph8: X volume accent — quiet/loud alternation */
    for (i = 0; i < 16; i += 4) {
        sd.phrases[8][i].note = N(4,0);
        if (!(i & 4)) { sd.phrases[8][i].cmd = CMD_X; sd.phrases[8][i].param = 0x20; }
    }
    /* ph9: A table — table 0 arpeggiates 0/+4/+7 at tick rate */
    sd.phrases[9][0].note = N(4,0); sd.phrases[9][0].instr = 5;
    sd.phrases[9][0].cmd = CMD_A;  sd.phrases[9][0].param = 0;
    sd.tables[0][0].tsp = 0;
    sd.tables[0][1].tsp = 4;
    sd.tables[0][2].tsp = 7;
    sd.tables[0][3].cmd = CMD_H;   sd.tables[0][3].param = 0;
    /* ph10: G groove switch — notes every 2 rows, groove 1 doubles the rate */
    sd.phrases[10][0].cmd = CMD_G; sd.phrases[10][0].param = 1;
    for (i = 0; i < 16; i += 2)
        sd.phrases[10][i].note = N(4,0);
}

void main(void)
{
    unsigned char last = 0xFF;
    unsigned char joy, prev_joy = 0;

    palette_init();
    screen_clear();
    MIKEY.scrbase = SCREEN;
    sound_init();
    song_demo();

    vbl_install();
    editor_init();
    draw_text(18, 0, BUILDID, PEN_DIM, PEN_BG);

    /* M10a EEPROM plumbing probe: mirror what cell 0 held at boot, then
     * stamp the magic — a reload of the emulator .sav proves the round
     * trip across power cycles. Becomes the real save header at M10b. */
    *(volatile unsigned int *)0xC010 = lynx_eeprom_read(0);
    lynx_eeprom_write(0, 0x414C);       /* 'AL' */
    *(volatile unsigned int *)0xC012 = lynx_eeprom_read(0);
    lynx_eeprom_write(1, 0xAA55);
    lynx_eeprom_write(2, 0x0180);
    *(volatile unsigned int *)0xC014 = lynx_eeprom_read(1);
    *(volatile unsigned int *)0xC016 = lynx_eeprom_read(2);

    for (;;) {
        unsigned char f = (unsigned char)frames;
        if (f == last)
            continue;
        last = f;

        engine_tick();

        joy = SUZY.joystick;
        JOY_MIRROR = joy;
        editor_frame(joy, prev_joy);
        prev_joy = joy;
    }
}
