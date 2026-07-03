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

/* 12-bit GBR palettes, {green, blue<<4|red} x {bg, text, dim, accent} */
#define NPALETTES 6
static const unsigned char palettes[NPALETTES][4][2] = {
    { {0x2,0x32}, {0xF,0xEE}, {0x8,0x97}, {0xC,0x2F} },  /* 0 lynx night  */
    { {0x3,0x11}, {0xE,0x9E}, {0x8,0x46}, {0xF,0x4F} },  /* 1 game boy    */
    { {0x1,0x10}, {0xB,0x2F}, {0x5,0x14}, {0xF,0x6F} },  /* 2 amber crt   */
    { {0x0,0x00}, {0xF,0xFF}, {0x7,0x77}, {0x0,0xFF} },  /* 3 paperwhite  */
    { {0x0,0x31}, {0xD,0xFC}, {0x6,0x85}, {0x4,0xFF} },  /* 4 vapor       */
    { {0x2,0x10}, {0xF,0xCF}, {0x7,0x58}, {0xE,0xF2} },  /* 5 swamp       */
};
unsigned char opt_palette;

void palette_apply(void)
{
    unsigned char i;
    for (i = 0; i < 4; ++i) {
        MIKEY.palette[i]      = palettes[opt_palette][i][0];
        MIKEY.palette[i + 16] = palettes[opt_palette][i][1];
    }
}

static void palette_init(void)
{
    unsigned char i;
    for (i = 4; i < 16; ++i) {          /* unused pens: black */
        MIKEY.palette[i] = 0;
        MIKEY.palette[i + 16] = 0;
    }
    palette_apply();
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
void song_clear(void)
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
        sd.instrs[c].wave = EMPTY;      /* WAV default: hardware triangle */
        sd.instrs[c].taps_lo = TAPS_SQUARE;  /* no taps = frozen LFSR */
    }
    for (c = 0; c < 32; ++c) {          /* factory wavetables 0-3 */
        sd.waves[0][c] = (c < 16) ? (c * 15 - 120) : (120 - (c - 16) * 15);
        sd.waves[1][c] = c * 8 - 124;                    /* saw */
        sd.waves[2][c] = (c < 16) ? 100 : -100;          /* square */
        sd.waves[3][c] = (c < 8) ? 100 : -100;           /* pulse 25 */
    }
    sd.grooves[0][0] = 6;               /* default groove: straight 6/6 */
    sd.grooves[0][1] = 6;
}

/* The demo song: an A-minor swung groove touring the instrument.
 * T1 = arp / lead / seed-plucks, T2 = bass (Z ghosts, J variation),
 * T3 = wavetable pads / speech, T4 = 808 then 909 drums + retrig fill.
 * Verification rigs live from song row 16 (out of the demo's loop). */
static void song_demo(void)
{
    unsigned char i;

    song_clear();

    /* groove 0: 7/5 swing (~150 BPM) */
    sd.grooves[0][0] = 7;
    sd.grooves[0][1] = 5;

    /* --- instruments --- */
    sd.instrs[0].type = IT_TONE;                   /* plain square (rigs) */
    sd.instrs[0].vol = 0x7F; sd.instrs[0].hold = 2; sd.instrs[0].env = 0x04;
    sd.instrs[1].type = IT_NOISE;                  /* noise (rigs) */
    sd.instrs[1].vol = 0x60; sd.instrs[1].env = 0x05;
    sd.instrs[1].taps_lo = (unsigned char)TAPS_NOISE;
    sd.instrs[1].taps_hi = TAPS_NOISE >> 8;
    sd.instrs[2].type = IT_TONE;                   /* sustained (rigs) */
    sd.instrs[2].vol = 0x70; sd.instrs[2].hold = 8; sd.instrs[2].env = 0x08;
    sd.instrs[2].pan = 0xF4;
    sd.instrs[3].type = IT_KIT;                    /* kit (rigs) */
    sd.instrs[3].vol = 0x7F;
    sd.instrs[4].type = IT_WAV;                    /* triangle (rigs) */
    sd.instrs[4].vol = 10; sd.instrs[4].hold = 8; sd.instrs[4].env = 0x0E;
    sd.instrs[4].pan = 0x4F;
    sd.instrs[5].type = IT_TONE;                   /* sustain lead (rigs) */
    sd.instrs[5].vol = 0x7F; sd.instrs[5].hold = 8; sd.instrs[5].env = 0x0E;

    sd.instrs[9].type = IT_TONE;                   /* LEAD */
    sd.instrs[9].vol = 0x6E; sd.instrs[9].hold = 6; sd.instrs[9].env = 0x0A;
    sd.instrs[10].type = IT_TONE;                  /* BASS: buzzy taps */
    sd.instrs[10].vol = 0x74; sd.instrs[10].hold = 3; sd.instrs[10].env = 0x07;
    sd.instrs[10].taps_lo = 0x03; sd.instrs[10].pan = 0xFA;
    sd.instrs[11].type = IT_WAV;                   /* PAD: saw wavetable */
    sd.instrs[11].vol = 0x7F; sd.instrs[11].hold = 15; sd.instrs[11].env = 0x2C;
    sd.instrs[11].wave = 1; sd.instrs[11].pan = 0xAF;
    sd.instrs[12].type = IT_KIT;                   /* 808 */
    sd.instrs[12].vol = 0x7F;
    sd.instrs[12].wave = 0;
    sd.instrs[13].type = IT_KIT;                   /* 909 */
    sd.instrs[13].vol = 0x7F;
    sd.instrs[13].wave = 1;
    sd.instrs[14].type = IT_KIT;                   /* speech bank */
    sd.instrs[14].vol = 0x7F;
    sd.instrs[14].wave = 4;
    sd.instrs[15].type = IT_TONE;                  /* PLUCK: seed timbre */
    sd.instrs[15].vol = 0x78; sd.instrs[15].hold = 2; sd.instrs[15].env = 0x09;
    sd.instrs[15].taps_lo = 0xF1;

    /* --- phrases --- */
    /* p20 bass: Am root motion, Z ghost, J lift every 4th pass */
    sd.phrases[20][0].note = N(2,9);  sd.phrases[20][0].instr = 10;
    sd.phrases[20][3].note = N(2,9);  sd.phrases[20][3].instr = 10;
    sd.phrases[20][3].cmd = CMD_Z;    sd.phrases[20][3].param = 0x90;
    sd.phrases[20][6].note = N(2,4);  sd.phrases[20][6].instr = 10;
    sd.phrases[20][8].note = N(2,9);  sd.phrases[20][8].instr = 10;
    sd.phrases[20][8].cmd = CMD_J;    sd.phrases[20][8].param = 0x51;
    sd.phrases[20][11].note = N(2,7); sd.phrases[20][11].instr = 10;
    sd.phrases[20][14].note = N(2,9); sd.phrases[20][14].instr = 10;
    sd.phrases[20][14].cmd = CMD_Z;   sd.phrases[20][14].param = 0x70;

    /* p22 drums 808 */
    for (i = 0; i < 16; i += 2) {
        sd.phrases[22][i].note = N(4,6);           /* HH */
        sd.phrases[22][i].instr = 12;
    }
    sd.phrases[22][0].note = N(4,0);               /* BD */
    sd.phrases[22][4].note = N(4,2);               /* SD */
    sd.phrases[22][8].note = N(4,0);
    sd.phrases[22][10].note = N(4,0);
    sd.phrases[22][10].cmd = CMD_Z; sd.phrases[22][10].param = 0x60;
    sd.phrases[22][12].note = N(4,2);

    /* p23 drums 909 */
    for (i = 0; i < 16; i += 2) {
        sd.phrases[23][i].note = N(4,6);
        sd.phrases[23][i].instr = 13;
    }
    sd.phrases[23][0].note = N(4,0);
    sd.phrases[23][4].note = N(4,2);
    sd.phrases[23][7].note = N(4,3);               /* CL offbeat */
    sd.phrases[23][8].note = N(4,0);
    sd.phrases[23][12].note = N(4,2);

    /* p24 fill: retrig snare rolls into a speech hit */
    for (i = 0; i < 16; i += 2) {
        sd.phrases[24][i].note = N(4,6);
        sd.phrases[24][i].instr = 13;
    }
    sd.phrases[24][0].note = N(4,0);
    sd.phrases[24][4].note = N(4,2);
    sd.phrases[24][8].note = N(4,0);
    sd.phrases[24][12].note = N(4,2); sd.phrases[24][12].instr = 13;
    sd.phrases[24][12].cmd = CMD_R;  sd.phrases[24][12].param = 0x13;
    sd.phrases[24][15].note = N(4,0); sd.phrases[24][15].instr = 14;

    /* p25 arp: Am chord shimmer (C37), octave hops */
    for (i = 0; i < 16; i += 2) {
        sd.phrases[25][i].note = (i & 4) ? N(4,9) : N(3,9);
        sd.phrases[25][i].instr = 9;
        sd.phrases[25][i].cmd = CMD_C;
        sd.phrases[25][i].param = 0x37;
    }

    /* p26 lead: vibrato line with slides */
    sd.phrases[26][0].note = N(4,9);  sd.phrases[26][0].instr = 9;
    sd.phrases[26][0].cmd = CMD_V;    sd.phrases[26][0].param = 0x26;
    sd.phrases[26][4].note = N(5,0);  sd.phrases[26][4].instr = 9;
    sd.phrases[26][4].cmd = CMD_L;    sd.phrases[26][4].param = 0x06;
    sd.phrases[26][6].note = N(4,11); sd.phrases[26][6].instr = 9;
    sd.phrases[26][8].note = N(4,7);  sd.phrases[26][8].instr = 9;
    sd.phrases[26][8].cmd = CMD_V;    sd.phrases[26][8].param = 0x26;
    sd.phrases[26][12].note = N(4,4); sd.phrases[26][12].instr = 9;
    sd.phrases[26][12].cmd = CMD_L;   sd.phrases[26][12].param = 0x04;
    sd.phrases[26][14].note = N(4,9); sd.phrases[26][14].instr = 9;
    sd.phrases[26][14].cmd = CMD_L;   sd.phrases[26][14].param = 0x08;

    /* p27 pad: long saw waves */
    sd.phrases[27][0].note = N(3,9);  sd.phrases[27][0].instr = 11;
    sd.phrases[27][8].note = N(4,4);  sd.phrases[27][8].instr = 11;
    sd.phrases[27][8].cmd = CMD_F;    sd.phrases[27][8].param = 0x04;

    /* p28 speech: sparse vocal cuts */
    sd.phrases[28][0].note = N(4,0);  sd.phrases[28][0].instr = 14;
    sd.phrases[28][6].note = N(4,4);  sd.phrases[28][6].instr = 14;
    sd.phrases[28][12].note = N(4,7); sd.phrases[28][12].instr = 14;
    sd.phrases[28][12].cmd = CMD_S;   sd.phrases[28][12].param = 0x60;

    /* p29 plucks: the seed timbre, morphing taps live */
    for (i = 0; i < 16; i += 3) {
        sd.phrases[29][i].note = N(3,9);
        sd.phrases[29][i].instr = 15;
    }
    sd.phrases[29][6].cmd = CMD_N;  sd.phrases[29][6].param = 0x43;
    sd.phrases[29][12].cmd = CMD_N; sd.phrases[29][12].param = 0x1F;

    /* --- chains (2 steps each) --- */
    { static const unsigned char cp[10][4] = {
        /* chain, phrase, tsp0, tsp1 (signed) */
        {20, 20, 0, 0},        /* bass Am */
        {21, 20, 3, 0xFE},     /* bass C / G(-2) */
        {22, 22, 0, 0},        /* 808 */
        {23, 23, 0, 0},        /* 909 (fill chain below) */
        {24, 25, 0, 12},       /* arp, octave lift */
        {25, 25, 0, 0},        /* arp */
        {26, 26, 0, 0},        /* lead */
        {27, 27, 0, 3},        /* pad, lift */
        {28, 28, 0, 0},        /* speech */
        {29, 29, 0, 0},        /* plucks */
      };
      for (i = 0; i < 10; ++i) {
        sd.chains[cp[i][0]][0].phrase = cp[i][1];
        sd.chains[cp[i][0]][0].tsp = (signed char)cp[i][2];
        sd.chains[cp[i][0]][1].phrase = cp[i][1];
        sd.chains[cp[i][0]][1].tsp = (signed char)cp[i][3];
      }
    }
    sd.chains[23][1].phrase = 24;                  /* 909 pass 2 = fill */
    sd.chains[23][1].tsp = 0;

    /* --- the song (8 rows, loops) --- */
    {
      static const unsigned char grid[8][4] = {
        {0xFF, 20, 0xFF, 22},
        {25,   20, 0xFF, 22},
        {25,   21, 24,   22},
        {26,   21, 24,   23},
        {25,   20, 27,   23},
        {26,   20, 27,   23},
        {29,   20, 28,   22},
        {26,   21, 25,   23},
      };
      unsigned char r;
      for (r = 0; r < 8; ++r)
        for (i = 0; i < 4; ++i)
          sd.song[r][i] = grid[r][i];
    }

    /* LFSR exploration rigs (D11 verification + starting points):
     * instr 6 = tap 7 only (control-bit wiring proof), 7/8 = same taps,
     * different seed (disjoint state cycles = different waveforms) */
    for (i = 6; i <= 8; ++i) {
        sd.instrs[i].type = IT_TONE;
        sd.instrs[i].vol = 0x7F;
        sd.instrs[i].hold = 8;
        sd.instrs[i].env = 0x0E;
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
    sd.instrs[5].env = 0x0E;
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

    /* T/I/J rigs (song rows 26-28) */
    for (i = 16; i <= 18; ++i) {
        sd.chains[i][0].phrase = i;
        sd.chains[i][0].tsp = 0;
        sd.song[26 + (i - 16)][0] = i;
    }
    sd.phrases[16][0].cmd = CMD_T; sd.phrases[16][0].param = 0x78;
    for (i = 0; i < 16; i += 2)
        sd.phrases[16][i].note = N(4,0);
    sd.phrases[17][0].note = N(4,0); sd.phrases[17][0].instr = 0;
    sd.phrases[17][0].cmd = CMD_I;  sd.phrases[17][0].param = 0x55;
    sd.phrases[18][0].note = N(4,0); sd.phrases[18][0].instr = 5;
    sd.phrases[18][0].cmd = CMD_J;  sd.phrases[18][0].param = 0x71;

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

    config_load();
    palette_init();
    screen_clear();
    MIKEY.scrbase = SCREEN;
    sound_init();
    sync_init();
    engine_init();
    pool_init();
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

        joy = SUZY.joystick;
        JOY_MIRROR = joy;
        sync_poll();
        pool_pump();
        editor_frame(joy, prev_joy);
        prev_joy = joy;
    }
}
