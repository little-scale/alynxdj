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

/* blank song: $FF sentinels everywhere (ported convention). Empty chain
 * steps are FF FF — tsp is don't-care until a phrase lands, and solid FF
 * runs are what keeps the EEPROM pack small (the editor zeroes tsp on
 * insert). */
static void song_new(void)
{
    unsigned char c;

    memset(sd.song, EMPTY, sizeof(sd.song));
    memset(sd.chains, EMPTY, sizeof(sd.chains));
    memset(sd.phrases, 0, sizeof(sd.phrases));
    memset(sd.instrs, 0, sizeof(sd.instrs));
    memset(sd.tables, 0, sizeof(sd.tables));
    memset(sd.grooves, 0, sizeof(sd.grooves));
    for (c = 0; c < NINSTR; ++c) {
        sd.instrs[c].pan = 0xFF;        /* centre = full both sides */
        sd.instrs[c].table = EMPTY;
        sd.instrs[c].taps_lo = TAPS_SQUARE;  /* no taps = frozen LFSR */
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
    sd.instrs[1].taps_lo = (unsigned char)TAPS_NOISE;
    sd.instrs[1].taps_hi = TAPS_NOISE >> 8;
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

    for (i = 0; i < 4; ++i) {
        sd.chains[i][0].phrase = i;
        sd.chains[i][0].tsp = 0;
        sd.chains[i][1].phrase = i;
        sd.chains[i][1].tsp = 0;
    }
    sd.chains[0][1].tsp = 12;                      /* second pass +1 octave */

    for (i = 0; i < 2; ++i) {
        sd.song[i][0] = 0;
        sd.song[i][1] = 1;
        sd.song[i][2] = 2;
        sd.song[i][3] = 3;
    }

    /* LFSR exploration rigs (D11 verification + starting points):
     * instr 6 = tap 7 only (control-bit wiring proof), 7/8 = same taps,
     * different seed (disjoint state cycles = different waveforms) */
    for (i = 6; i <= 8; ++i) {
        sd.instrs[i].type = IT_TONE;
        sd.instrs[i].vol = 0x7F;
        sd.instrs[i].hold = 8;
        sd.instrs[i].dcy = 2;
        sd.phrases[i - 2][0].note = N(4,0);
        sd.phrases[i - 2][0].instr = i;
        sd.chains[i - 2][0].phrase = i - 2;
        sd.chains[i - 2][0].tsp = 0;
        sd.song[16 + (i - 6)][0] = i - 2;
    }
    sd.instrs[6].taps_lo = 0x40;                   /* tap 7 only */
    sd.instrs[7].taps_lo = 0xF1;    /* taps $0F1: strongly seed-dependent */
    sd.instrs[8].taps_lo = 0xF1;    /* same taps, seed $555 (new cycle) */
    sd.instrs[8].seed_lo = 0x55;
    sd.instrs[8].seed_hi = 0x05;

    /* M9c command rigs (song rows 19-25, chain-loop transport) */
    sd.instrs[5].type = IT_TONE;                   /* sustain lead */
    sd.instrs[5].vol = 0x7F;
    sd.instrs[5].hold = 8;
    sd.instrs[5].dcy = 2;
    for (i = 9; i <= 15; ++i) {
        sd.chains[i][0].phrase = i;
        sd.chains[i][0].tsp = 0;
        sd.song[19 + (i - 9)][0] = i;
    }
    /* ph9  L: C-4 then C-5 with L04 -> audible glide */
    sd.phrases[9][0].note = N(4,0);  sd.phrases[9][0].instr = 5;
    sd.phrases[9][8].note = N(5,0);  sd.phrases[9][8].instr = 5;
    sd.phrases[9][8].cmd = CMD_L;    sd.phrases[9][8].param = 4;
    /* ph10 R: one note retriggered every 3 ticks, fading */
    sd.phrases[10][0].note = N(4,0); sd.phrases[10][0].instr = 5;
    sd.phrases[10][0].cmd = CMD_R;   sd.phrases[10][0].param = 0x13;
    /* ph11 F: plain C-4 / +8 sixteenths C-4 alternating */
    for (i = 0; i < 16; i += 4) {
        sd.phrases[11][i].note = N(4,0); sd.phrases[11][i].instr = 0;
        if (i & 4) { sd.phrases[11][i].cmd = CMD_F; sd.phrases[11][i].param = 8; }
    }
    /* ph12 N: square then taps-$F1 morph */
    sd.phrases[12][0].note = N(4,0); sd.phrases[12][0].instr = 5;
    sd.phrases[12][8].note = N(4,0); sd.phrases[12][8].instr = 5;
    sd.phrases[12][8].cmd = CMD_N;   sd.phrases[12][8].param = 0xF1;
    /* ph13 S: kit BD normal / double-rate */
    sd.phrases[13][0].note = N(4,0); sd.phrases[13][0].instr = 3;
    sd.phrases[13][8].note = N(4,0); sd.phrases[13][8].instr = 3;
    sd.phrases[13][8].cmd = CMD_S;   sd.phrases[13][8].param = 63;
    /* ph14 Z: coin-flip note on every row */
    for (i = 0; i < 16; ++i) {
        sd.phrases[14][i].note = N(4,0);
        sd.phrases[14][i].cmd = CMD_Z;
        sd.phrases[14][i].param = 0x80;
    }
    /* ph15 H: note at row 0, phrase ends at row 4 -> 5-row loop */
    sd.phrases[15][0].note = N(4,0);
    sd.phrases[15][4].cmd = CMD_H;

    /* table 0: a demo arp macro (0/+4/+7 at tick rate, H-looped) */
    sd.tables[0][0].tsp = 0;
    sd.tables[0][1].tsp = 4;
    sd.tables[0][2].tsp = 7;
    sd.tables[0][3].cmd = CMD_H;
    sd.tables[0][3].param = 0;
    /* (the M9 command test rigs were retired once verified — git history
     * has them; a slim demo keeps the packed save well inside the meter) */
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

    /* autoload: a valid save replaces the demo song (ported slot-0
     * policy); mirrors let the harness verify the round trip */
    *(volatile unsigned char *)0xC01D = save_load();
    *(volatile unsigned int *)0xC018 = save_sum();

    vbl_install();
    editor_init();
    draw_text(18, 0, BUILDID, PEN_DIM, PEN_BG);

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
