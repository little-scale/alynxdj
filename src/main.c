/* ALYNXDJ — M1: boot, display init, 4x6 font, splash + build stamp.
 *
 * crt0 already ran the Mikey init table (display timers, DISPCTL=$0D 4bpp
 * color DMA, audio muted); what it does not set is the screen address,
 * the palette, and interrupts — that is done here.
 */
#include <lynx.h>

#include "../build/font.h"
#include "../build/buildid.h"

#define SCREEN      ((unsigned char *)0xA000)
#define COLS        40
#define ROWS        17
#define LINE_BYTES  80

#define PEN_BG      0
#define PEN_TEXT    1
#define PEN_DIM     2
#define PEN_ACCENT  3

extern volatile unsigned int frames;
void vbl_install(void);
void sound_init(void);
void tone_on(unsigned char clocksel, unsigned char bkup);
void tone_off(void);

struct step {
    unsigned char note, instr, cmd, param;
};
extern struct step phrase[16];
extern unsigned char eng_playing, eng_row;
void engine_play(void);
void engine_stop(void);
void engine_tick(void);

#define N(oct, semi) ((unsigned char)(((oct) - 1) * 12 + (semi) + 1))
#define NC(o) N(o, 0)
#define NE(o) N(o, 4)
#define NG(o) N(o, 7)

/* raw pad byte mirrored here for the harness's RAM-dump mapping probe */
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
        MIKEY.palette[i]     = palette[i][0];  /* $FDA0+i green */
        MIKEY.palette[i + 16] = palette[i][1]; /* $FDB0+i blue|red */
    }
}

static void screen_clear(void)
{
    unsigned char *p = SCREEN;
    unsigned int n;
    for (n = 0; n < (unsigned int)LINE_BYTES * 102; ++n)
        p[n] = PEN_BG | (PEN_BG << 4);
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

static void draw_text(unsigned char cx, unsigned char cy, const char *s,
                      unsigned char fg, unsigned char bg)
{
    while (*s)
        draw_char(cx++, cy, *s++, fg, bg);
}

static void draw_hex8(unsigned char cx, unsigned char cy, unsigned char v,
                      unsigned char fg, unsigned char bg)
{
    static const char hexd[] = "0123456789ABCDEF";
    draw_char(cx, cy, hexd[v >> 4], fg, bg);
    draw_char(cx + 1, cy, hexd[v & 0x0F], fg, bg);
}

void main(void)
{
    unsigned char last = 0xFF;
    unsigned char joy, prev_joy = 0;

    palette_init();
    screen_clear();
    MIKEY.scrbase = SCREEN;
    sound_init();

    draw_text(14, 5, "ALYNXDJ", PEN_ACCENT, PEN_BG);
    draw_text(14, 7, "V0.1", PEN_TEXT, PEN_BG);
    draw_text(14, 8, BUILDID, PEN_DIM, PEN_BG);
    draw_text(14, 10, "FRAME", PEN_DIM, PEN_BG);
    draw_text(14, 11, "JOY", PEN_DIM, PEN_BG);
    draw_text(14, 12, "ROW", PEN_DIM, PEN_BG);

    /* M3 demo phrase: C-major arp up and back, one note per row */
    {
        static const unsigned char arp[16] = {
            NC(4), NE(4), NG(4), NC(5), NE(5), NC(5), NG(4), NE(4),
            NC(4), NG(4), NC(5), NE(5), NG(5), NE(5), NC(5), NG(4),
        };
        unsigned char i;
        for (i = 0; i < 16; ++i)
            phrase[i].note = arp[i];
    }

    vbl_install();
    engine_play();

    for (;;) {
        unsigned char f = (unsigned char)frames;
        if (f == last)
            continue;
        last = f;
        engine_tick();

        draw_hex8(20, 10, f, PEN_TEXT, PEN_BG);

        joy = SUZY.joystick;
        JOY_MIRROR = joy;
        draw_hex8(20, 11, joy, PEN_TEXT, PEN_BG);
        draw_hex8(20, 12, eng_row, PEN_TEXT, PEN_BG);

        /* A toggles play/stop (edge) */
        if ((joy & JOY_BTN_A_MASK) && !(prev_joy & JOY_BTN_A_MASK)) {
            if (eng_playing)
                engine_stop();
            else
                engine_play();
        }
        prev_joy = joy;
    }
}
