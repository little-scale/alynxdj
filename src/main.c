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
#include "../build/logo.h"
#include "../build/buildid.h"

#define SCREEN      ((unsigned char *)0xA000)
#define LINE_BYTES  80

extern volatile unsigned int frames;
void vbl_install(void);

#define N(oct, semi) ((unsigned char)(((oct) - 1) * 12 + (semi) + 1))

/* raw pad byte mirrored here for the harness's RAM-dump probes */
#define JOY_MIRROR (*(volatile unsigned char *)0xC000)
#define TEST_HOOK  (*(volatile unsigned char *)0xC02D)

/* 12-bit palettes, {green, blue<<4|red} x {bg, text, dim, accent}.
 * The eight schemes port SMSGGDJ's COLR presets (WHT/WB/AMBR/CYAN/PINK/
 * NEON/KIDD/MINT): its bg/fg anchor bg/text here, with a muted dim and a
 * brighter accent (cursor/playhead/meters) synthesized per scheme. */
#define NPALETTES 8
static const unsigned char palettes[NPALETTES][4][2] = {
    { {0x0,0x00}, {0xD,0xDD}, {0x6,0x66}, {0xF,0xFF} },  /* 0 WHT  mono   */
    { {0xF,0xFF}, {0x1,0x11}, {0x8,0x88}, {0x0,0x00} },  /* 1 WB   invert */
    { {0x0,0x00}, {0x8,0x0D}, {0x4,0x06}, {0xB,0x3F} },  /* 2 AMBR crt    */
    { {0x1,0x50}, {0xD,0xE2}, {0x6,0x81}, {0xF,0xF7} },  /* 3 CYAN navy   */
    { {0x0,0x55}, {0x2,0xDD}, {0x1,0x88}, {0x7,0xFF} },  /* 4 PINK purple */
    { {0x9,0xE4}, {0x1,0x9F}, {0x4,0x98}, {0x5,0xCF} },  /* 5 NEON        */
    { {0x5,0xE1}, {0xE,0x2F}, {0x9,0x77}, {0xF,0x7F} },  /* 6 KIDD sky    */
    { {0x4,0x40}, {0xE,0xA6}, {0x8,0x62}, {0xF,0xDA} },  /* 7 MINT teal   */
};
#pragma code-name (push, "MIDICODE")
void palette_apply(void)
{
    unsigned char i;
    for (i = 0; i < 4; ++i) {
        MIKEY.palette[i]      = palettes[opt_palette][i][0];
        MIKEY.palette[i + 16] = palettes[opt_palette][i][1];
    }
}
#pragma code-name (pop)

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

static const struct instr blank_instr = {
    IT_TONE, 0x7F, 0x05, 5, EMPTY, TAPS_SQUARE, EMPTY, 0xFF,
    0, 0, 0, 0, 0, 0, 0, 0,
};

/* Blank song: $FF sentinels everywhere (ported convention). Empty chain
 * steps stay FF FF internally for compact saves, but the editor presents
 * their don't-care transpose as 00. */
void song_clear(void)
{
    unsigned char c;

    memset(sd.song, EMPTY, sizeof(sd.song));
    memset(sd.chains, EMPTY, sizeof(sd.chains));
    memset(sd.phrases, 0, sizeof(sd.phrases));
    memset(sd.tables, 0, sizeof(sd.tables));
    memset(sd.grooves, 0, sizeof(sd.grooves));
    for (c = 0; c < NINSTR; ++c)
        memcpy(&sd.instrs[c], &blank_instr, sizeof(blank_instr));
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
void song_demo(void)
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

    /* Symmetric-DAC regression rig (song row 29).  Tracks 1/2 start two
     * simultaneous KIT streams; tracks 3/4 then force oldest-first steals.
     * R proves envelope-less KIT retrigger, and same-row S must survive the
     * deferred cart start.  tools/test_dac_symmetry.py drives this row. */
    for (i = 0; i < NCH; ++i) {
        sd.chains[i][0].phrase = 30 + i;
        sd.chains[i][0].tsp = 0;
        sd.song[29][i] = i;
    }
    sd.phrases[30][0].note = N(4,0); sd.phrases[30][0].instr = 12;
    sd.phrases[30][0].cmd = CMD_R;   sd.phrases[30][0].param = 0x02;
    sd.phrases[31][0].note = N(4,2); sd.phrases[31][0].instr = 12;
    sd.phrases[31][0].cmd = CMD_S;   sd.phrases[31][0].param = 63;
    sd.phrases[32][1].note = N(4,4); sd.phrases[32][1].instr = 12;
    sd.phrases[33][2].note = N(4,6); sd.phrases[33][2].instr = 12;

    /* table 0: a demo arp macro (0/+4/+7 at tick rate, H-looped) */
    sd.tables[0][0].tsp = 0;
    sd.tables[0][1].tsp = 4;
    sd.tables[0][2].tsp = 7;
    sd.tables[0][3].cmd = CMD_H;
    sd.tables[0][3].param = 0;
    /* (the M9 command test rigs were retired once verified — git history
     * has them; a slim demo keeps the packed save well inside the meter) */
}

/* draw a string horizontally centred on the 40-char grid */
static void draw_centered(unsigned char row, const char *s, unsigned char fg)
{
    unsigned char len = 0;
    while (s[len])
        ++len;
    draw_text((40 - len) / 2, row, s, fg, PEN_BG);
}

/* boot splash: ALYNXDJ / version / build hash, centred; held ~100 VBlanks
 * (~1.7 s, matching SMSGGDJ). Art goes here later. */
#define LOGO_X   ((160 - LOGO_W) / 2)   /* even pixel x -> byte-aligned blit */
#define LOGO_TOP 14                      /* top pixel row of the logo */

/* full-width inverted bar with s centred on it (SMSGGDJ version bar) */
static void draw_bar(unsigned char row, const char *s)
{
    static const char blank[] = "                                        ";
    unsigned char len = 0;
    while (s[len])
        ++len;
    draw_text(0, row, blank, PEN_BG, PEN_TEXT);            /* solid ink bar */
    draw_text((40 - len) / 2, row, s, PEN_BG, PEN_TEXT);   /* inverse text */
}

static void splash(void)
{
    unsigned int t = frames;
    unsigned char row;

    screen_clear();
    /* expand the 1-bit logo: each set bit -> pen PEN_ACCENT (0x3) in the
     * 4bpp framebuffer, so the logo shows in the palette's highlight colour
     * and follows the selection. One src byte (8 px) -> 4 dst bytes. */
    for (row = 0; row < LOGO_H; ++row) {
        unsigned char *dst = SCREEN + (unsigned int)(LOGO_TOP + row) * LINE_BYTES
                             + LOGO_X / 2;
        const unsigned char *src = logo_bits[row];
        unsigned char col;
        for (col = 0; col < LOGO_W / 8; ++col) {
            unsigned char b = src[col];
            dst[0] = ((b & 0x80) ? 0x30 : 0) | ((b & 0x40) ? 0x03 : 0);
            dst[1] = ((b & 0x20) ? 0x30 : 0) | ((b & 0x10) ? 0x03 : 0);
            dst[2] = ((b & 0x08) ? 0x30 : 0) | ((b & 0x04) ? 0x03 : 0);
            dst[3] = ((b & 0x02) ? 0x30 : 0) | ((b & 0x01) ? 0x03 : 0);
            dst += 4;
        }
    }
    draw_bar(10, VERSION);                 /* version on an inverted bar */
    draw_centered(12, BUILDID, PEN_TEXT);  /* build hash below (dev aid) */
    while ((unsigned int)(frames - t) < 100)
        ;
}

/* MAIN is deliberately kept below the framebuffer.  Cold-path routines
 * are stored in cart blocks 40/42/44 and copied into three otherwise-unused
 * RAM bands before any can be called.  Uniform 32-byte chunks cover all
 * three bands, including HICODE3's expanded 736-byte helper tail. */
static void overlay_load(unsigned char block, unsigned char *dst,
                         unsigned char chunks)
{
    cart_seek(block, 0);
    do {
        cart_read(dst, 32);
        dst += 32;
    } while (--chunks);
}

#pragma code-name (push, "HICODE2")
void midi_overlay_load(void)
{
    unsigned char *dst = (unsigned char *)0xC100;
    unsigned char chunks = 48;          /* MIDICODE: $0600 bytes */

    cart_seek(254, 0);                  /* final two blocks, after sample bank */
    do {
        cart_read(dst, 32);
        dst += 32;
    } while (--chunks);
}

static void test_hook_run(void)
{
    unsigned char hook = TEST_HOOK;

    TEST_HOOK = 0;
    if (hook == 0xA5) {
        song_demo();                     /* install the private DAC rig */
        engine_play_song(29);            /* symmetric-DAC test rig */
    } else if (hook == 0xA6) {
        *(volatile unsigned int *)0xC01A = save_pack();
        *(volatile unsigned char *)0xC01D = save_do();
        *(volatile unsigned int *)0xC018 = save_sum();
        midi_overlay_load();
    } else if (hook == 0xA8) {
        /* Reuse the same four-track rig as table-WAV voices. */
        song_demo();
        sd.instrs[12].type = IT_WAV;
        sd.instrs[12].env = 0x0E;
        sd.instrs[12].hold = 15;
        sd.instrs[12].wave = 0;
        engine_play_song(29);
    } else if (hook == 0xA9)
        engine_audition(37, 0);          /* stopped TONE modulation rig */
}
#pragma code-name (pop)

void main(void)
{
    unsigned char last = 0xFF;
    unsigned char joy, prev_joy = 0;

    TEST_HOOK = 0;                       /* real hardware RAM is not zeroed */
    overlay_load(40, (unsigned char *)0xC900, 56); /* HICODE1: $0700 */
    overlay_load(42, (unsigned char *)0xF600, 48); /* HICODE2: $0600 */
    overlay_load(44, (unsigned char *)0xF320, 23); /* HICODE3: $02E0 */
    midi_overlay_load();
    sync_init();
    config_load();
    palette_init();
    screen_clear();
    MIKEY.scrbase = SCREEN;
    sound_init();
    engine_init();
    pool_init();
    song_clear();

    /* Autoload a valid slot-0 save over the clean working song.  The factory
     * demo is an explicit FILES action, not the empty-EEPROM fallback. */
    *(volatile unsigned char *)0xC01D = save_load();
    *(volatile unsigned int *)0xC018 = save_sum();
    midi_overlay_load();

    vbl_install();
    sync_irq_enable();
    splash();
    editor_init();
    draw_text(18, 0, BUILDID, PEN_DIM, PEN_BG);
    test_hook_run();                      /* no-op outside headless tests */

    for (;;) {
        unsigned char f = (unsigned char)frames;
        sync_poll();                    /* MIDI is serviced between frames */
        if (f == last)
            continue;
        last = f;

        joy = SUZY.joystick;
        /* swap A (bit0) and B (bit1) for hardware button ergonomics — all
         * downstream gestures read JOY_BTN_A/B_MASK, so this flips them all */
        joy = (unsigned char)((joy & 0xFC)
                              | ((joy & 0x01) << 1) | ((joy & 0x02) >> 1));
        JOY_MIRROR = joy;
        pool_pump();
        editor_frame(joy, prev_joy);
        prev_joy = joy;
    }
}
