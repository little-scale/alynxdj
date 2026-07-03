/* Editor — SONG / CHAIN / PHRASE screens (M5).
 *
 * Control frame (DESIGN.md §3, ported): A = item modifier (tap insert on
 * release, hold+dpad edit), B = project modifier (tap = back, held+dpad =
 * screen nav with drill-down, held+A = transport). The button already held
 * when the other arrives selects the action; a chord marks the held
 * button "used" so its tap action doesn't also fire on release.
 */
#include <lynx.h>
#include <string.h>

#include "tracker.h"
#include "../build/notes.h"

/* debug mirrors for the headless harness */
#define MIRROR_ROW    (*(volatile unsigned char *)0xC001)
#define MIRROR_COL    (*(volatile unsigned char *)0xC002)
#define MIRROR_SCREEN (*(volatile unsigned char *)0xC003)

#define SCR_SONG   0
#define SCR_CHAIN  1
#define SCR_PHRASE 2
#define SCR_INSTR  3
#define SCR_TABLE  4
#define SCR_FILES  5
#define SCR_GROOVE 6
#define SCR_WAVE   7
#define SCR_PROJ   8
#define SCR_OPTS   9

/* command letters, indexed by CMD_* id */
static const char cmd_chars[NCMDS] = {
    '-', 'A', 'C', 'D', 'G', 'H', 'K', 'O', 'P', 'V', 'W', 'X',
    'F', 'L', 'N', 'R', 'S', 'Z', 'E', 'T', 'I', 'J',
};

/* cmd cycling skips CMD_G: with a single groove (D13) there is nothing to
 * switch, so it never appears in the menu (its id is kept to preserve the
 * save encoding of every other command) */
static unsigned char cmd_next(unsigned char c)
{
    c = (c + 1 < NCMDS) ? c + 1 : 0;
    if (c == CMD_G) c = CMD_H;
    return c;
}
static unsigned char cmd_prev(unsigned char c)
{
    c = c ? c - 1 : NCMDS - 1;
    if (c == CMD_G) c = CMD_D;
    return c;
}

#define GRID_TOP 1

static unsigned char screen;
static unsigned char cur_track;         /* set by the SONG cursor column */
static unsigned char edit_chain;        /* what CHAIN shows */
static unsigned char edit_phrase;       /* what PHRASE shows */
static unsigned char edit_instr;        /* what INSTR shows */
static unsigned char edit_table;        /* what TABLE shows */

/* per-screen cursors */
static unsigned char s_row, s_col;      /* SONG: 0-127 x 0-3 */
static unsigned char c_row, c_col;      /* CHAIN: 0-15 x {phrase,tsp} */
static unsigned char p_row, p_col;      /* PHRASE: note,instr,cmd,param */
static unsigned char i_row;             /* INSTR: field index */
static unsigned char t_row, t_col;      /* TABLE: 0-15 x vol,tsp,cmd,param */
static unsigned char f_row;             /* FILES: SAVE/LOAD/NEW/PURGE */
static unsigned char f_status;          /* last save/load ST_* result */
static unsigned char f_confirm;         /* NEW: armed, awaiting 2nd tap */
static void purge(void);
static unsigned char g_row;             /* GROOVE: 0-15 */
static unsigned char w_col;             /* WAVE: 0-31 */
static unsigned char edit_wave;         /* what WAVE shows */
static unsigned char live_mode;         /* SONG screen is a clip launcher */
static unsigned char pj_row;            /* PROJECT: field index */
static unsigned char o_row;             /* OPTIONS: field index */
unsigned char opt_prelisten = 1;        /* audition on edit */
unsigned char opt_repeat = 12;          /* DAS delay, frames */
static unsigned char edit_groove;       /* what GROOVE shows */

/* clipboard: single fields and row ranges share one kind tag */
#define CLIP_NONE  0
#define CLIP_STEP  1                    /* phrase step */
#define CLIP_CHST  2                    /* chain step */
#define CLIP_CELL  3                    /* song cell (chain #) */
#define CLIP_RPH   4                    /* phrase row range */
#define CLIP_RCH   5                    /* chain row range */
#define CLIP_RSNG  6                    /* song row range */
static unsigned char clip_kind;
static struct step clip_step;
static struct chainstep clip_chst;
static unsigned char clip_cell;
#pragma bss-name (push, "SONG")
static struct step blk_steps[16];
static struct chainstep blk_chst[16];
static unsigned char blk_song[16][NCH];
#pragma bss-name (pop)
static unsigned char blk_n;

/* block select (A-held + B long-hold, ported gesture) */
#define SEL_HOLD 20
static unsigned char sel_active;
static unsigned char sel_anchor;
static unsigned char ab_pending, ab_timer;

/* A double-tap window (paste / mint / clone), ~0.3 s */
#define TAP_WINDOW 18
static unsigned char a_release_age = 0xFF;

#define MIRROR_PACKLEN (*(volatile unsigned int *)0xC01A)
#define MIRROR_STATUS  (*(volatile unsigned char *)0xC01C)
#define MIRROR_SDSUM   (*(volatile unsigned int *)0xC018)

/* last-entered memory (insert repeats it, ported) */
static unsigned char last_note = 37;    /* C-4 */
static unsigned char last_chain = 0;
static unsigned char last_phrase = 0;

/* channel activity meters (right column): cached bar heights */
static unsigned char meter_h[NCH] = {0xFF, 0xFF, 0xFF, 0xFF};

/* playhead bookkeeping: what's currently accented, per screen */
static unsigned char ph_song[NCH];      /* drawn song row per track, $FF none */
static unsigned char ph_row = 0xFF;     /* CHAIN/PHRASE drawn row */

/* key repeat */
#define DAS_DELAY 12
#define DAS_RATE   2
static unsigned char rep_dir, rep_timer;
static unsigned char a_used, b_used;

/* ---------------- drawing ---------------- */

static void top_bar(void)
{
    draw_text(1, 0, "          ", PEN_DIM, PEN_BG);
    switch (screen) {
    case SCR_SONG:
        draw_text(1, 0, live_mode ? "LIVE" : "SONG", PEN_ACCENT, PEN_BG);
        break;
    case SCR_CHAIN:
        draw_text(1, 0, "CHAIN", PEN_ACCENT, PEN_BG);
        draw_hex8(7, 0, edit_chain, PEN_TEXT, PEN_BG);
        break;
    case SCR_PHRASE:
        draw_text(1, 0, "PHRASE", PEN_ACCENT, PEN_BG);
        draw_hex8(8, 0, edit_phrase, PEN_TEXT, PEN_BG);
        break;
    case SCR_INSTR:
        draw_text(1, 0, "INSTR", PEN_ACCENT, PEN_BG);
        draw_hex8(7, 0, edit_instr, PEN_TEXT, PEN_BG);
        break;
    case SCR_TABLE:
        draw_text(1, 0, "TABLE", PEN_ACCENT, PEN_BG);
        draw_hex8(7, 0, edit_table, PEN_TEXT, PEN_BG);
        break;
    case SCR_FILES:
        draw_text(1, 0, "FILES", PEN_ACCENT, PEN_BG);
        break;
    case SCR_GROOVE:
        draw_text(1, 0, "GROOVE", PEN_ACCENT, PEN_BG);
        break;
    case SCR_WAVE:
        draw_text(1, 0, "WAVE", PEN_ACCENT, PEN_BG);
        draw_hex8(6, 0, edit_wave, PEN_TEXT, PEN_BG);
        break;
    case SCR_PROJ:
        draw_text(1, 0, "PROJECT", PEN_ACCENT, PEN_BG);
        break;
    case SCR_OPTS:
        draw_text(1, 0, "OPTIONS", PEN_ACCENT, PEN_BG);
        break;
    }
    /* mute flags: track digits, muted = inverted */
    {
        unsigned char t;
        char b[2];
        b[1] = 0;
        for (t = 0; t < NCH; ++t) {
            b[0] = '1' + t;
            if (eng_mute & (1 << t))
                draw_text(25 + t, 0, b, PEN_BG, PEN_ACCENT);
            else
                draw_text(25 + t, 0, b, PEN_DIM, PEN_BG);
        }
    }
    draw_text(12, 0, "T", PEN_DIM, PEN_BG);
    draw_hex8(13, 0, cur_track + 1, PEN_DIM, PEN_BG);
}

static void transport_label(void)
{
    draw_text(30, 0, eng_mode ? "PLAY" : "STOP",
              eng_mode ? PEN_ACCENT : PEN_DIM, PEN_BG);
}

/* Screen-map indicator (right column, ported from SMSGGDJ): the middle row
 * is the shipped SONG-CHAIN-PHRASE-INSTR-TABLE strip (current = inverted);
 * the rows above/below are the planned screens, drawn dim until they
 * exist — OPTIONS/PROJECT above SONG/CHAIN, WAVE above INSTR (SMSGGDJ
 * layout), GROOVE below. Column positions follow the 2D map (DESIGN.md
 * §4) so vertical B-nav can land here later. */
#define MAP_X 30
#define MAP_Y (GRID_TOP + 1)
#define METER_H     8               /* channel meter height in cells */
#define METER_BOT   (MAP_Y + 14)    /* baseline row (bars grow upward) */
#define METER_X(t)  (MAP_X + 1 + (t))       /* 4 adjacent bars */
static void draw_map(void)
{
    static const char rows[3][6] = { "OP W ", "SCPIT", "FG   " };
    unsigned char r, c;
    char b[2];

    b[1] = 0;
    for (r = 0; r < 3; ++r)
        for (c = 0; c < 5; ++c) {
            unsigned char cur = (r == 1 && c == screen)
                || (r == 2 && c == 0 && screen == SCR_FILES)
                || (r == 2 && c == 1 && screen == SCR_GROOVE)
                || (r == 0 && c == 3 && screen == SCR_WAVE)
                || (r == 0 && c == 0 && screen == SCR_OPTS)
                || (r == 0 && c == 1 && screen == SCR_PROJ);
            unsigned char shipped = (r == 1) || (r == 2 && c <= 1)
                || (r == 0 && c <= 1) || (r == 0 && c == 3);
            b[0] = rows[r][c];
            draw_text(MAP_X + c, MAP_Y + r, b,
                      cur ? PEN_BG : (shipped ? PEN_TEXT : PEN_DIM),
                      cur ? PEN_ACCENT : PEN_BG);
        }
}

/* --- SONG screen: 16-row page of the 128-row song, 4 track columns --- */

#define SONG_COLX(c) (4 + (c) * 3)

static unsigned char song_page(void)  { return s_row & 0xF0; }

static void draw_song_row(unsigned char vr, unsigned char cursor_here)
{
    unsigned char row = song_page() + vr;
    unsigned char y = GRID_TOP + vr;
    unsigned char c, cn;
    unsigned char is_ph;

    is_ph = 0;
    for (c = 0; c < NCH; ++c)
        if (ph_song[c] == row)
            is_ph = 1;
    draw_hex8(1, y, row, is_ph ? PEN_ACCENT : PEN_DIM, PEN_BG);

    for (c = 0; c < NCH; ++c) {
        unsigned char inv = cursor_here && c == s_col;
        unsigned char fg = inv ? PEN_BG : PEN_TEXT;
        unsigned char bg = inv ? PEN_TEXT : PEN_BG;
        cn = sd.song[row][c];
        if (ph_song[c] == row && !inv) {
            fg = PEN_ACCENT;
        }
        if (live_mode && eng_mode == MODE_LIVE && live_q[c] != EMPTY
            && sd.song[row][c] == live_q[c] && !inv) {
            fg = PEN_BG;                /* queued clip: inverted accent */
            bg = PEN_ACCENT;
        }
        if (cn == EMPTY)
            draw_text(SONG_COLX(c), y, "--", inv ? fg : PEN_DIM, bg);
        else
            draw_hex8(SONG_COLX(c), y, cn, fg, bg);
    }
}

static void draw_song_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_song_row(r, r == (s_row & 0x0F));
}

/* --- CHAIN screen: 16 steps of {phrase, transpose} --- */

static void draw_chain_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char y = GRID_TOP + r;
    struct chainstep *cs = &sd.chains[edit_chain][r];
    unsigned char inv0 = cursor_here && c_col == 0;
    unsigned char inv1 = cursor_here && c_col == 1;

    draw_hex8(1, y, r, r == ph_row ? PEN_ACCENT : PEN_DIM, PEN_BG);
    if (cs->phrase == EMPTY)
        draw_text(4, y, "--", inv0 ? PEN_BG : PEN_DIM,
                  inv0 ? PEN_TEXT : PEN_BG);
    else
        draw_hex8(4, y, cs->phrase, inv0 ? PEN_BG : PEN_TEXT,
                  inv0 ? PEN_TEXT : PEN_BG);
    draw_hex8(8, y, (unsigned char)cs->tsp, inv1 ? PEN_BG : PEN_TEXT,
              inv1 ? PEN_TEXT : PEN_BG);
}

static void draw_chain_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_chain_row(r, r == c_row);
}

/* --- PHRASE screen (M4, retargeted at the phrase pool) --- */

static void draw_phrase_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char y = GRID_TOP + r;
    struct step *s = &sd.phrases[edit_phrase][r];
    unsigned char inv0 = cursor_here && p_col == 0;
    unsigned char inv1 = cursor_here && p_col == 1;
    unsigned char inv2 = cursor_here && p_col == 2;
    unsigned char inv3 = cursor_here && p_col == 3;
    char b[4];

    draw_hex8(1, y, r, r == ph_row ? PEN_ACCENT : PEN_DIM, PEN_BG);

    if (s->note) {
        const char *n = note_names[s->note - 1];
        b[0] = n[0]; b[1] = n[1]; b[2] = n[2]; b[3] = 0;
        draw_text(4, y, b, inv0 ? PEN_BG : PEN_TEXT,
                  inv0 ? PEN_TEXT : PEN_BG);
        draw_hex8(9, y, s->instr, inv1 ? PEN_BG : PEN_TEXT,
                  inv1 ? PEN_TEXT : PEN_BG);
    } else {
        draw_text(4, y, "---", inv0 ? PEN_BG : PEN_DIM,
                  inv0 ? PEN_TEXT : PEN_BG);
        draw_text(9, y, "--", inv1 ? PEN_BG : PEN_DIM,
                  inv1 ? PEN_TEXT : PEN_BG);
    }
    b[0] = cmd_chars[s->cmd < NCMDS ? s->cmd : 0];
    b[1] = 0;
    draw_text(12, y, b, inv2 ? PEN_BG : (s->cmd ? PEN_TEXT : PEN_DIM),
              inv2 ? PEN_TEXT : PEN_BG);
    if (s->cmd)
        draw_hex8(14, y, s->param, inv3 ? PEN_BG : PEN_TEXT,
                  inv3 ? PEN_TEXT : PEN_BG);
    else
        draw_text(14, y, "--", inv3 ? PEN_BG : PEN_DIM,
                  inv3 ? PEN_TEXT : PEN_BG);
}

static void draw_phrase_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_phrase_row(r, r == p_row);
}

/* --- INSTR screen: field rows --- */

#define NIFIELDS 9
#define IF_TAPS  5
#define IF_SEED  6
#define IF_WAVE  7
#define IF_TABLE 8
static const char *const ifield_name[NIFIELDS] = {
    "TYPE", "VOL", "ATK", "HOLD", "DCY", "TAPS", "SEED", "BANK", "TABLE",
};
/* display grid row per field (absolute): blank rows group TYPE / envelope /
 * LFSR / routing, and one blank below the title */
static const unsigned char ifield_y[NIFIELDS] = {
    2, 4, 5, 6, 7, 9, 10, 12, 13,
};
static const char *const itype_name[4] = { "TONE", "NOISE", "WAV", "KIT" };

static unsigned char ifield_nib(unsigned char f)
{
    struct instr *in = &sd.instrs[edit_instr];
    switch (f) {
    case 2:  return in->env >> 4;           /* ATK */
    case 3:  return in->hold & 0x0F;        /* HOLD */
    default: return in->env & 0x0F;         /* DCY */
    }
}

static void ifield_nib_set(unsigned char f, unsigned char v)
{
    struct instr *in = &sd.instrs[edit_instr];
    switch (f) {
    case 2: in->env = (in->env & 0x0F) | (v << 4); break;
    case 3: in->hold = v; break;
    default: in->env = (in->env & 0xF0) | v; break;
    }
}

static unsigned instr_taps(void)
{
    struct instr *in = &sd.instrs[edit_instr];
    return in->taps_lo | ((in->taps_hi & 1) << 8);
}

static unsigned instr_seed(void)
{
    struct instr *in = &sd.instrs[edit_instr];
    return in->seed_lo | ((in->seed_hi & 0x0F) << 8);
}

/* which taps the mask lights, in user-bit order (D11) */
static const char tap_glyph[9] = { '0','1','2','3','4','5','7','A','B' };

static void draw_instr_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char y;
    unsigned char fg = cursor_here ? PEN_BG : PEN_TEXT;
    unsigned char bg = cursor_here ? PEN_TEXT : PEN_BG;
    unsigned v;

    if (r >= NIFIELDS)
        return;
    y = ifield_y[r];
    draw_text(1, y, ifield_name[r], PEN_DIM, PEN_BG);
    switch (r) {
    case 0:
        draw_text(8, y, "     ", PEN_BG, PEN_BG);
        draw_text(8, y, itype_name[sd.instrs[edit_instr].type & 3], fg, bg);
        break;
    case IF_TAPS: {
        unsigned char i;
        char b[2];
        v = instr_taps();
        draw_hex8(8, y, (unsigned char)(v >> 8), fg, bg);
        draw_hex8(10, y, (unsigned char)v, fg, bg);
        /* lit tap glyphs: 0-5 7 A B */
        b[1] = 0;
        for (i = 0; i < 9; ++i) {
            b[0] = tap_glyph[i];
            draw_text(14 + i, y, b, (v & (1 << i)) ? PEN_ACCENT : PEN_DIM,
                      PEN_BG);
        }
        break;
    }
    case IF_SEED:
        v = instr_seed();
        draw_hex8(8, y, (unsigned char)(v >> 8), fg, bg);
        draw_hex8(10, y, (unsigned char)v, fg, bg);
        break;
    case IF_TABLE:
        if (sd.instrs[edit_instr].table >= NTABLES)
            draw_text(8, y, "--", fg, bg);
        else
            draw_hex8(8, y, sd.instrs[edit_instr].table, fg, bg);
        break;
    case IF_WAVE:
        if (sd.instrs[edit_instr].wave >= 8)
            draw_text(8, y, "--", fg, bg);
        else
            draw_hex8(8, y, sd.instrs[edit_instr].wave, fg, bg);
        break;
    case 1:
        draw_hex8(8, y, sd.instrs[edit_instr].vol, fg, bg);
        break;
    default: {                          /* ATK / HOLD / DCY nibbles */
        char b[2];
        static const char hexd[] = "0123456789ABCDEF";
        b[0] = hexd[ifield_nib(r)];
        b[1] = 0;
        draw_text(8, y, b, fg, bg);
        break;
    }
    }
}

static void draw_instr_screen(void)
{
    unsigned char r;
    for (r = 0; r < NIFIELDS; ++r)
        draw_instr_row(r, r == i_row);
}

/* --- TABLE screen: 16 rows of {vol, tsp, cmd, param} --- */

static void draw_table_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char y = GRID_TOP + r;
    struct tablerow *tr = &sd.tables[edit_table][r];
    unsigned char i;
    char b[2];

    draw_hex8(1, y, r, PEN_DIM, PEN_BG);
    for (i = 0; i < 4; ++i) {
        unsigned char inv = cursor_here && t_col == i;
        unsigned char fg = inv ? PEN_BG : PEN_TEXT;
        unsigned char bg = inv ? PEN_TEXT : PEN_BG;
        switch (i) {
        case 0:
            if (tr->vol)
                draw_hex8(4, y, tr->vol, fg, bg);
            else
                draw_text(4, y, "--", inv ? fg : PEN_DIM, bg);
            break;
        case 1:
            draw_hex8(7, y, (unsigned char)tr->tsp, fg, bg);
            break;
        case 2:
            b[0] = cmd_chars[tr->cmd < NCMDS ? tr->cmd : 0];
            b[1] = 0;
            draw_text(10, y, b, inv ? fg : (tr->cmd ? PEN_TEXT : PEN_DIM),
                      bg);
            break;
        case 3:
            if (tr->cmd)
                draw_hex8(12, y, tr->param, fg, bg);
            else
                draw_text(12, y, "--", inv ? fg : PEN_DIM, bg);
            break;
        }
    }
}

static void draw_table_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_table_row(r, r == t_row);
}

/* --- FILES screen: SAVE / LOAD + packed-size meter (D10) --- */

static void draw_files_status(void)
{
    static const char *const st[5] = {
        "     ", "OK   ", "FULL!", "EMPTY", "BAD! ",
    };
    unsigned len = save_pack();

    draw_text(1, 10, "PACK $", PEN_DIM, PEN_BG);
    draw_hex8(7, 10, (unsigned char)(len >> 8), len ? PEN_TEXT : PEN_ACCENT,
              PEN_BG);
    draw_hex8(9, 10, (unsigned char)len, len ? PEN_TEXT : PEN_ACCENT, PEN_BG);
    draw_text(12, 10, "OF $", PEN_DIM, PEN_BG);
    draw_hex8(16, 10, (unsigned char)(SAVE_CAP_BYTES >> 8), PEN_DIM, PEN_BG);
    draw_hex8(18, 10, (unsigned char)SAVE_CAP_BYTES, PEN_DIM, PEN_BG);
    draw_text(1, 12, st[f_status], f_status == ST_OK ? PEN_ACCENT : PEN_TEXT,
              PEN_BG);
    MIRROR_PACKLEN = len;
    MIRROR_STATUS = f_status;
}

/* PURGE: drop chains no song row references, then phrases no remaining
 * chain references (rig rows included in the scan, so they survive) */
static void purge(void)
{
    unsigned char used[NPHRASES];
    unsigned char r, c, s, n;

    __asm__("sei");
    memset(used, 0, NCHAINS);
    for (r = 0; r < SONG_ROWS; ++r)
        for (c = 0; c < NCH; ++c) {
            n = sd.song[r][c];
            if (n < NCHAINS)
                used[n] = 1;
        }
    for (c = 0; c < NCHAINS; ++c)
        if (!used[c])
            memset(sd.chains[c], EMPTY, sizeof(sd.chains[0]));
    memset(used, 0, NPHRASES);
    for (c = 0; c < NCHAINS; ++c)
        for (s = 0; s < PHRASE_ROWS; ++s) {
            n = sd.chains[c][s].phrase;
            if (n < NPHRASES)
                used[n] = 1;
        }
    for (c = 0; c < NPHRASES; ++c)
        if (!used[c])
            memset(sd.phrases[c], 0, sizeof(sd.phrases[0]));
    __asm__("cli");
}

static void draw_files_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char fg = cursor_here ? PEN_BG : PEN_TEXT;
    unsigned char bg = cursor_here ? PEN_TEXT : PEN_BG;

    if (r == 0)
        draw_text(1, GRID_TOP + 1, "SAVE", fg, bg);
    else if (r == 1)
        draw_text(1, GRID_TOP + 3, "LOAD", fg, bg);
    else if (r == 2)
        draw_text(1, GRID_TOP + 5, f_confirm ? "SURE" : "NEW ", fg, bg);
    else if (r == 3)
        draw_text(1, GRID_TOP + 7, "PURGE", fg, bg);
}

static void draw_files_screen(void)
{
    unsigned char r;
    for (r = 0; r < 4; ++r)
        draw_files_row(r, r == f_row);
    draw_files_status();
}

/* --- GROOVE screen: 16 tick-count rows (0 = end marker) --- */

static unsigned char groove_ph = 0xFF;

static void draw_groove_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char y = GRID_TOP + r;
    unsigned char v = sd.grooves[edit_groove][r];

    draw_hex8(1, y, r, r == groove_ph ? PEN_ACCENT : PEN_DIM, PEN_BG);
    if (v)
        draw_hex8(4, y, v, cursor_here ? PEN_BG : PEN_TEXT,
                  cursor_here ? PEN_TEXT : PEN_BG);
    else
        draw_text(4, y, "--", cursor_here ? PEN_BG : PEN_DIM,
                  cursor_here ? PEN_TEXT : PEN_BG);
}

static void draw_groove_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_groove_row(r, r == g_row);
}

/* --- WAVE screen: 32-column bar graph of the 8-bit wavetable --- */

static void draw_wave_col(unsigned char c, unsigned char cursor_here)
{
    unsigned char h = (unsigned char)(sd.waves[edit_wave][c] + 128) >> 4;
    unsigned char r;
    char b[2];

    b[1] = 0;
    for (r = 0; r < 16; ++r) {
        unsigned char filled = (15 - r) < h || ((15 - r) == 0 && h == 0);
        b[0] = filled ? '#' : ' ';
        draw_text(1 + c, GRID_TOP + r, b,
                  cursor_here ? PEN_TEXT : PEN_ACCENT, PEN_BG);
    }
}

static void draw_wave_screen(void)
{
    unsigned char c;
    for (c = 0; c < 32; ++c)
        draw_wave_col(c, c == w_col);
}

/* --- PROJECT: tempo readout (tempo IS the groove) + free-block counts --- */

static void draw_dec(unsigned char cx, unsigned char cy, unsigned v,
                     unsigned char fg, unsigned char bg)
{
    char b[4];
    b[0] = '0' + (v / 100) % 10;
    b[1] = '0' + (v / 10) % 10;
    b[2] = '0' + v % 10;
    b[3] = 0;
    draw_text(cx, cy, b, fg, bg);
}

/* BPM = ticks/min / (avg ticks/row * 4 rows/beat) at the 59.9 Hz tick */
static unsigned proj_bpm(void)
{
    unsigned sum = 0;
    unsigned char n = 0;
    while (n < 16 && sd.grooves[eng_groove][n]) {
        sum += sd.grooves[eng_groove][n];
        ++n;
    }
    if (!sum)
        return 0;
    return (unsigned)(899u * n / sum);
}

static unsigned char count_free_chains(void)
{
    unsigned char c, s, n = 0;
    for (c = 0; c < NCHAINS; ++c) {
        for (s = 0; s < PHRASE_ROWS; ++s)
            if (sd.chains[c][s].phrase != EMPTY)
                break;
        if (s == PHRASE_ROWS)
            ++n;
    }
    return n;
}

static unsigned char count_free_phrases(void)
{
    unsigned char c, s, n = 0;
    for (c = 0; c < NPHRASES; ++c) {
        for (s = 0; s < PHRASE_ROWS; ++s)
            if (sd.phrases[c][s].note || sd.phrases[c][s].cmd)
                break;
        if (s == PHRASE_ROWS)
            ++n;
    }
    return n;
}

static void draw_proj_row(unsigned char r, unsigned char cursor_here)
{
    unsigned char fg = cursor_here ? PEN_BG : PEN_TEXT;
    unsigned char bg = cursor_here ? PEN_TEXT : PEN_BG;

    switch (r) {
    case 0:
        draw_text(1, GRID_TOP + 1, "TMPO", PEN_DIM, PEN_BG);
        draw_dec(6, GRID_TOP + 1, proj_bpm(), fg, bg);
        draw_text(10, GRID_TOP + 1, "BPM", PEN_DIM, PEN_BG);
        break;
    case 1:
        draw_text(1, GRID_TOP + 3, "FREE CHAINS", PEN_DIM, PEN_BG);
        draw_hex8(13, GRID_TOP + 3, count_free_chains(), PEN_TEXT, PEN_BG);
        break;
    case 2:
        draw_text(1, GRID_TOP + 5, "FREE PHRASES", PEN_DIM, PEN_BG);
        draw_hex8(14, GRID_TOP + 5, count_free_phrases(), PEN_TEXT, PEN_BG);
        break;
    }
}

static void draw_proj_screen(void)
{
    draw_proj_row(0, pj_row == 0);
    draw_proj_row(1, 0);
    draw_proj_row(2, 0);
}

/* --- OPTIONS: machine settings (runtime-only for now) --- */

static void draw_opts_row(unsigned char r, unsigned char cursor_here)
{
    static const char *const sync_name[NSYNC] = { "OFF", "OUT", "IN " };
    unsigned char fg = cursor_here ? PEN_BG : PEN_TEXT;
    unsigned char bg = cursor_here ? PEN_TEXT : PEN_BG;

    switch (r) {
    case 0:
        draw_text(1, GRID_TOP + 1, "SYNC", PEN_DIM, PEN_BG);
        draw_text(9, GRID_TOP + 1, sync_name[sync_mode], fg, bg);
        break;
    case 1:
        draw_text(1, GRID_TOP + 3, "PRELIS", PEN_DIM, PEN_BG);
        draw_text(9, GRID_TOP + 3, opt_prelisten ? "ON " : "OFF", fg, bg);
        break;
    case 2:
        draw_text(1, GRID_TOP + 5, "REPEAT", PEN_DIM, PEN_BG);
        draw_hex8(9, GRID_TOP + 5, opt_repeat, fg, bg);
        break;
    case 3:
        draw_text(1, GRID_TOP + 7, "PALETTE", PEN_DIM, PEN_BG);
        draw_text(9, GRID_TOP + 7, palette_label(opt_palette), fg, bg);
        break;
    }
}

static void draw_opts_screen(void)
{
    unsigned char r;
    for (r = 0; r < 4; ++r)
        draw_opts_row(r, r == o_row);
}

/* --- dispatch --- */

static void draw_row(unsigned char r, unsigned char cursor_here)
{
    switch (screen) {
    case SCR_SONG:   draw_song_row(r, cursor_here); break;
    case SCR_CHAIN:  draw_chain_row(r, cursor_here); break;
    case SCR_PHRASE: draw_phrase_row(r, cursor_here); break;
    case SCR_INSTR:  draw_instr_row(r, cursor_here); break;
    case SCR_TABLE:  draw_table_row(r, cursor_here); break;
    case SCR_FILES:  draw_files_row(r, cursor_here); break;
    case SCR_GROOVE: draw_groove_row(r, cursor_here); break;
    case SCR_PROJ:   draw_proj_row(r, cursor_here); break;
    case SCR_OPTS:   draw_opts_row(r, cursor_here); break;
    }
}

static void draw_screen(void)
{
    clear_grid();
    top_bar();
    transport_label();
    switch (screen) {
    case SCR_SONG:   draw_song_screen(); break;
    case SCR_CHAIN:  draw_chain_screen(); break;
    case SCR_PHRASE: draw_phrase_screen(); break;
    case SCR_INSTR:  draw_instr_screen(); break;
    case SCR_TABLE:  draw_table_screen(); break;
    case SCR_FILES:  draw_files_screen(); break;
    case SCR_GROOVE: draw_groove_screen(); break;
    case SCR_WAVE:   draw_wave_screen(); break;
    case SCR_PROJ:   draw_proj_screen(); break;
    case SCR_OPTS:   draw_opts_screen(); break;
    }
    draw_map();
    meter_h[0] = meter_h[1] = meter_h[2] = meter_h[3] = 0xFF;
    MIRROR_SCREEN = screen;
}

static unsigned char cursor_vrow(void)
{
    switch (screen) {
    case SCR_SONG:  return s_row & 0x0F;
    case SCR_CHAIN: return c_row;
    case SCR_INSTR: return i_row;
    case SCR_TABLE: return t_row;
    case SCR_FILES: return f_row;
    case SCR_GROOVE: return g_row;
    case SCR_WAVE:  return 0;
    case SCR_PROJ:  return pj_row;
    case SCR_OPTS:  return o_row;
    default:        return p_row;
    }
}

static void mirror_cursor(void)
{
    switch (screen) {
    case SCR_SONG:  MIRROR_ROW = s_row; MIRROR_COL = s_col; break;
    case SCR_CHAIN: MIRROR_ROW = c_row; MIRROR_COL = c_col; break;
    case SCR_INSTR: MIRROR_ROW = i_row; MIRROR_COL = 0; break;
    case SCR_TABLE: MIRROR_ROW = t_row; MIRROR_COL = t_col; break;
    case SCR_GROOVE: MIRROR_ROW = g_row; MIRROR_COL = 0; break;
    case SCR_WAVE:  MIRROR_ROW = edit_wave; MIRROR_COL = w_col; break;
    case SCR_PROJ:  MIRROR_ROW = pj_row; MIRROR_COL = 0; break;
    case SCR_OPTS:  MIRROR_ROW = o_row; MIRROR_COL = 0; break;
    default:        MIRROR_ROW = p_row; MIRROR_COL = p_col; break;
    }
}

/* ---------------- cursor + editing ---------------- */

static void move_cursor(unsigned char dir)
{
    unsigned char old = cursor_vrow();
    unsigned char repage = 0;

    switch (screen) {
    case SCR_SONG:
        switch (dir) {
        case 0: if (s_row) { --s_row; repage = (s_row & 0xF0) != song_page(); } break;
        case 1: if (s_row < SONG_ROWS - 1) { ++s_row; } break;
        case 2: if (s_col) --s_col; break;
        case 3: if (s_col < NCH - 1) ++s_col; break;
        }
        /* paging: crossing a 16-row boundary repaints the grid */
        if (dir == 0 || dir == 1) {
            static unsigned char last_page = 0;
            if (song_page() != last_page) {
                last_page = song_page();
                repage = 1;
            }
        }
        cur_track = s_col;
        top_bar();
        break;
    case SCR_CHAIN:
        switch (dir) {
        case 0: if (c_row) --c_row; else c_row = 15; break;
        case 1: if (c_row < 15) ++c_row; else c_row = 0; break;
        case 2: if (c_col) --c_col; break;
        case 3: if (c_col < 1) ++c_col; break;
        }
        break;
    case SCR_INSTR:
        switch (dir) {
        case 0: if (i_row) --i_row; else i_row = NIFIELDS - 1; break;
        case 1: if (i_row < NIFIELDS - 1) ++i_row; else i_row = 0; break;
        }
        break;
    case SCR_TABLE:
        switch (dir) {
        case 0: if (t_row) --t_row; else t_row = 15; break;
        case 1: if (t_row < 15) ++t_row; else t_row = 0; break;
        case 2: if (t_col) --t_col; break;
        case 3: if (t_col < 3) ++t_col; break;
        }
        break;
    case SCR_FILES:
        if (dir == 0 || dir == 1) {
            f_confirm = 0;
            draw_files_row(2, f_row == 2);
            f_row = (dir == 1) ? (f_row < 3 ? f_row + 1 : 0)
                               : (f_row ? f_row - 1 : 3);
        }
        break;
    case SCR_GROOVE:                        /* single groove (D13): no pool */
        switch (dir) {
        case 0: if (g_row) --g_row; else g_row = 15; break;
        case 1: if (g_row < 15) ++g_row; else g_row = 0; break;
        }
        break;
    case SCR_PROJ:
        break;                          /* TMPO is the only cursor field */
    case SCR_OPTS:
        if (dir == 0 || dir == 1) {
            unsigned char old = o_row;
            o_row = (dir == 1) ? (o_row < 3 ? o_row + 1 : 0)
                               : (o_row ? o_row - 1 : 3);
            draw_opts_row(old, 0);
            draw_opts_row(o_row, 1);
        }
        mirror_cursor();
        return;
    case SCR_WAVE: {
        unsigned char oc = w_col;
        switch (dir) {
        case 0: if (edit_wave < 7) { ++edit_wave; draw_screen(); } break;
        case 1: if (edit_wave) { --edit_wave; draw_screen(); } break;
        case 2: if (w_col) --w_col; else w_col = 31; break;
        case 3: if (w_col < 31) ++w_col; else w_col = 0; break;
        }
        if (w_col != oc) {
            draw_wave_col(oc, 0);
            draw_wave_col(w_col, 1);
        }
        mirror_cursor();
        return;
    }
    default:
        switch (dir) {
        case 0: if (p_row) --p_row; else p_row = 15; break;
        case 1: if (p_row < 15) ++p_row; else p_row = 0; break;
        case 2: if (p_col) --p_col; break;
        case 3: if (p_col < 3) ++p_col; break;
        }
        break;
    }

    if (repage)
        draw_screen();
    else {
        draw_row(old, 0);
        draw_row(cursor_vrow(), 1);
    }
    mirror_cursor();
}

static void edit_song_cell(unsigned char dir)
{
    unsigned char *cell = &sd.song[s_row][s_col];
    unsigned char v = (*cell == EMPTY) ? last_chain : *cell;

    switch (dir) {
    case 0: v = (v + 16 < NCHAINS) ? v + 16 : NCHAINS - 1; break;
    case 1: v = (v >= 16) ? v - 16 : 0; break;
    case 2: v = v ? v - 1 : 0; break;
    case 3: v = (v + 1 < NCHAINS) ? v + 1 : NCHAINS - 1; break;
    }
    *cell = v;
    last_chain = v;
}

static void edit_chain_cell(unsigned char dir)
{
    struct chainstep *cs = &sd.chains[edit_chain][c_row];

    if (c_col == 0) {
        unsigned char v = (cs->phrase == EMPTY) ? last_phrase : cs->phrase;
        switch (dir) {
        case 0: v = (v + 16 < NPHRASES) ? v + 16 : NPHRASES - 1; break;
        case 1: v = (v >= 16) ? v - 16 : 0; break;
        case 2: v = v ? v - 1 : 0; break;
        case 3: v = (v + 1 < NPHRASES) ? v + 1 : NPHRASES - 1; break;
        }
        if (cs->phrase == EMPTY)
            cs->tsp = 0;        /* was the FF-FF empty sentinel */
        cs->phrase = v;
        last_phrase = v;
    } else {
        switch (dir) {
        case 0: cs->tsp += 12; break;
        case 1: cs->tsp -= 12; break;
        case 2: --cs->tsp; break;
        case 3: ++cs->tsp; break;
        }
    }
}

static void edit_phrase_cell(unsigned char dir)
{
    struct step *s = &sd.phrases[edit_phrase][p_row];

    if (p_col == 0) {
        unsigned char n = s->note ? s->note : last_note;
        switch (dir) {
        case 0: n = (n + 12 <= NOTE_MAX) ? n + 12 : n; break;
        case 1: n = (n > 12) ? n - 12 : n; break;
        case 2: n = (n > NOTE_MIN) ? n - 1 : n; break;
        case 3: n = (n < NOTE_MAX) ? n + 1 : n; break;
        }
        s->note = n;
        last_note = n;
        if (!eng_mode && opt_prelisten)
            engine_audition(n, s->instr);
    } else if (p_col == 1 && s->note) {
        switch (dir) {
        case 0: s->instr += 16; break;
        case 1: s->instr -= 16; break;
        case 2: --s->instr; break;
        case 3: ++s->instr; break;
        }
    } else if (p_col == 2) {
        switch (dir) {
        case 0: case 3: s->cmd = cmd_next(s->cmd); break;
        case 1: case 2: s->cmd = cmd_prev(s->cmd); break;
        }
        goto audition_row;
    } else if (p_col == 3 && s->cmd) {
        switch (dir) {
        case 0: s->param += 16; break;
        case 1: s->param -= 16; break;
        case 2: --s->param; break;
        case 3: ++s->param; break;
        }
audition_row:
        /* prelisten the row as it will play: note + command */
        if (s->note && !eng_mode && opt_prelisten) {
            engine_audition(s->note, s->instr);
            engine_audition_cmd(s->cmd, s->param);
        }
    }
}

static void edit_table_cell(unsigned char dir)
{
    struct tablerow *tr = &sd.tables[edit_table][t_row];

    switch (t_col) {
    case 0:
        switch (dir) {
        case 0: tr->vol = (tr->vol + 16 <= 0x7F) ? tr->vol + 16 : 0x7F; break;
        case 1: tr->vol = (tr->vol >= 16) ? tr->vol - 16 : 0; break;
        case 2: if (tr->vol) --tr->vol; break;
        case 3: if (tr->vol < 0x7F) ++tr->vol; break;
        }
        break;
    case 1:
        switch (dir) {
        case 0: tr->tsp += 12; break;
        case 1: tr->tsp -= 12; break;
        case 2: --tr->tsp; break;
        case 3: ++tr->tsp; break;
        }
        break;
    case 2:
        switch (dir) {
        case 0: case 3: tr->cmd = cmd_next(tr->cmd); break;
        case 1: case 2: tr->cmd = cmd_prev(tr->cmd); break;
        }
        break;
    case 3:
        if (!tr->cmd)
            break;
        switch (dir) {
        case 0: tr->param += 16; break;
        case 1: tr->param -= 16; break;
        case 2: --tr->param; break;
        case 3: ++tr->param; break;
        }
        break;
    }
}

/* clamp table per field: {max, small step, big step} */
static const unsigned char ifield_lim[5][3] = {
    {NTYPES - 1, 1, 1},     /* TYPE cycles the shipped types */
    {0x7F, 1, 16},          /* VOL */
    {0x7F, 1, 16},          /* ATK */
    {0x7F, 1, 16},          /* HOLD */
    {0x7F, 1, 16},          /* DCY */
};

/* edit a 16-bit field masked to `max`; L/R +-1, U/D +-16, wrapping —
 * sweeping the whole tap/seed space is the point (D11) */
static unsigned edit_u16(unsigned v, unsigned char dir, unsigned max)
{
    switch (dir) {
    case 0: v += 16; break;
    case 1: v -= 16; break;
    case 2: --v; break;
    case 3: ++v; break;
    }
    return v & max;
}

static unsigned char *ifield_ptr(unsigned char f)
{
    struct instr *in = &sd.instrs[edit_instr];
    return (f == 0) ? &in->type : &in->vol;
}

static void edit_instr_cell(unsigned char dir)
{
    struct instr *in = &sd.instrs[edit_instr];
    unsigned char *p;
    unsigned char max, stp, v;
    unsigned t;

    switch (i_row) {
    case IF_TAPS:
        t = edit_u16(instr_taps(), dir, 0x1FF);
        in->taps_lo = (unsigned char)t;
        in->taps_hi = (unsigned char)(t >> 8);
        break;
    case IF_SEED:
        t = edit_u16(instr_seed(), dir, 0xFFF);
        in->seed_lo = (unsigned char)t;
        in->seed_hi = (unsigned char)(t >> 8);
        break;
    case IF_TABLE:
        v = in->table;
        if (dir == 0 || dir == 3)
            in->table = (v >= NTABLES) ? 0 : (v < NTABLES - 1 ? v + 1 : v);
        else
            in->table = (v == 0 || v >= NTABLES) ? EMPTY : v - 1;
        return;
    case IF_WAVE:
        v = in->wave;
        if (dir == 0 || dir == 3)
            in->wave = (v >= 8) ? 0 : (v < 7 ? v + 1 : v);
        else
            in->wave = (v == 0 || v >= 8) ? EMPTY : v - 1;
        break;
    case 2:                             /* ATK / HOLD / DCY: 0-F times */
    case 3:
    case 4:
        v = ifield_nib(i_row);
        stp = (dir < 2) ? 4 : 1;
        if (dir == 0 || dir == 3)
            v = (v + stp <= 15) ? v + stp : 15;
        else
            v = (v >= stp) ? v - stp : 0;
        ifield_nib_set(i_row, v);
        break;
    default:
        p = ifield_ptr(i_row);
        v = *p;
        max = ifield_lim[i_row][0];
        stp = (dir < 2) ? ifield_lim[i_row][2] : ifield_lim[i_row][1];
        if (dir == 0 || dir == 3)
            v = (v + stp <= max) ? v + stp : max;
        else
            v = (v >= stp) ? v - stp : 0;
        *p = v;
        break;
    }
    if (!eng_mode)
        engine_audition(last_note, edit_instr);
}

static void edit_groove_cell(unsigned char dir)
{
    unsigned char *v = &sd.grooves[edit_groove][g_row];

    switch (dir) {
    case 0: *v = (*v + 4 <= 15) ? *v + 4 : 15; break;
    case 1: *v = (*v >= 4) ? *v - 4 : 0; break;
    case 2: if (*v) --*v; break;
    case 3: if (*v < 15) ++*v; break;
    }
}

static void edit_cell(unsigned char dir)
{
    switch (screen) {
    case SCR_SONG:   edit_song_cell(dir); break;
    case SCR_CHAIN:  edit_chain_cell(dir); break;
    case SCR_PHRASE: edit_phrase_cell(dir); break;
    case SCR_INSTR:  edit_instr_cell(dir); break;
    case SCR_TABLE:  edit_table_cell(dir); break;
    case SCR_GROOVE: edit_groove_cell(dir); break;
    case SCR_PROJ: {
        /* TMPO: step every entry of the active groove together (walks
         * the achievable BPM rungs without flattening swing) */
        unsigned char i, up = (dir == 0 || dir == 3);
        for (i = 0; i < 16 && sd.grooves[eng_groove][i]; ++i) {
            unsigned char *g = &sd.grooves[eng_groove][i];
            /* more ticks/row = slower: up means fewer ticks */
            if (up && *g > 1) --*g;
            if (!up && *g < 15) ++*g;
        }
        draw_proj_row(0, 1);
        return;
    }
    case SCR_OPTS:
        switch (o_row) {
        case 0:
            sync_mode = (dir == 0 || dir == 3)
                        ? (sync_mode + 1) % NSYNC
                        : (sync_mode + NSYNC - 1) % NSYNC;
            break;
        case 1:
            opt_prelisten ^= 1;
            break;
        case 2:
            if ((dir == 0 || dir == 3) && opt_repeat < 30)
                ++opt_repeat;
            else if ((dir == 1 || dir == 2) && opt_repeat > 4)
                --opt_repeat;
            break;
        case 3:
            opt_palette = (dir == 0 || dir == 3)
                          ? (opt_palette + 1) % NPALETTES
                          : (opt_palette + NPALETTES - 1) % NPALETTES;
            palette_apply();
            break;
        }
        config_save();
        draw_opts_row(o_row, 1);
        return;
    case SCR_WAVE: {
        signed char *v = (signed char *)&sd.waves[edit_wave][w_col];
        int nv = *v;
        switch (dir) {
        case 0: nv += 16; break;
        case 1: nv -= 16; break;
        case 2: nv -= 4; break;
        case 3: nv += 4; break;
        }
        if (nv > 127) nv = 127;
        if (nv < -128) nv = -128;
        *v = (signed char)nv;
        draw_wave_col(w_col, 1);
        return;
    }
    }
    draw_row(cursor_vrow(), 1);
}

static void insert_cell(void)
{
    switch (screen) {
    case SCR_SONG:
        if (sd.song[s_row][s_col] == EMPTY)
            sd.song[s_row][s_col] = last_chain;
        break;
    case SCR_CHAIN:
        if (c_col == 0 && sd.chains[edit_chain][c_row].phrase == EMPTY) {
            sd.chains[edit_chain][c_row].phrase = last_phrase;
            sd.chains[edit_chain][c_row].tsp = 0;
        }
        break;
    case SCR_PHRASE:
        if (p_col == 0 && !sd.phrases[edit_phrase][p_row].note) {
            sd.phrases[edit_phrase][p_row].note = last_note;
            if (!eng_mode)
                engine_audition(last_note,
                                sd.phrases[edit_phrase][p_row].instr);
        }
        break;
    case SCR_FILES:
        engine_stop();
        transport_label();
        switch (f_row) {
        case 0: f_status = save_do(); break;
        case 1: f_status = save_load(); break;
        case 2:
            if (!f_confirm) {           /* two taps to wipe the song */
                f_confirm = 1;
                draw_files_row(2, 1);
                return;
            }
            f_confirm = 0;
            __asm__("sei");
            song_clear();
            __asm__("cli");
            f_status = ST_OK;
            draw_files_row(2, 1);
            break;
        case 3:
            purge();
            f_status = ST_OK;
            break;
        }
        MIRROR_SDSUM = save_sum();
        draw_files_status();
        break;
    case SCR_GROOVE:
        if (!sd.grooves[edit_groove][g_row])
            sd.grooves[edit_groove][g_row] = 6;
        break;
    }
    draw_row(cursor_vrow(), 1);
}

/* ---------------- screen navigation ---------------- */

/* B-held + Right descends (drill: load the thing under the cursor);
 * B-held + Left / B-tap ascends. */
static void nav(unsigned char to_right)
{
    if (to_right) {
        if (screen == SCR_FILES) {
            screen = SCR_GROOVE;
            goto moved;
        }
        if (screen == SCR_OPTS) {
            screen = SCR_PROJ;
            goto moved;
        }
        if (screen == SCR_PROJ) {
            screen = SCR_WAVE;
            goto moved;
        }
        if (screen == SCR_SONG) {
            unsigned char cn = sd.song[s_row][s_col];
            cur_track = s_col;
            if (cn != EMPTY)
                edit_chain = cn;
            screen = SCR_CHAIN;
        } else if (screen == SCR_CHAIN) {
            unsigned char pn = sd.chains[edit_chain][c_row].phrase;
            if (pn != EMPTY)
                edit_phrase = pn;
            screen = SCR_PHRASE;
        } else if (screen == SCR_PHRASE) {
            struct step *s = &sd.phrases[edit_phrase][p_row];
            if (s->note)
                edit_instr = s->instr < NINSTR ? s->instr : 0;
            screen = SCR_INSTR;
        } else if (screen == SCR_INSTR) {
            unsigned char t = sd.instrs[edit_instr].table;
            if (t < NTABLES)
                edit_table = t;
            screen = SCR_TABLE;
        } else
            return;
    } else {
        if (screen == SCR_GROOVE) {
            screen = SCR_FILES;
            goto moved;
        }
        if (screen == SCR_PROJ) {
            screen = SCR_OPTS;
            goto moved;
        }
        if (screen == SCR_WAVE) {
            screen = SCR_PROJ;
            goto moved;
        }
        if (screen == SCR_OPTS)
            screen = SCR_SONG;
        else if (screen == SCR_TABLE)
            screen = SCR_INSTR;
        else if (screen == SCR_INSTR)
            screen = SCR_PHRASE;
        else if (screen == SCR_PHRASE)
            screen = SCR_CHAIN;
        else if (screen == SCR_CHAIN || screen == SCR_FILES)
            screen = SCR_SONG;
        else
            return;
    }
moved:
    ph_row = 0xFF;
    groove_ph = 0xFF;
    draw_screen();
    mirror_cursor();
}

/* B-held + Up/Down: vertical hops on the 2D map
 * (SONG <-> FILES, CHAIN <-> GROOVE) */
static void nav_v(unsigned char down)
{
    if (down && screen == SCR_SONG)
        screen = SCR_FILES;
    else if (!down && screen == SCR_FILES)
        screen = SCR_SONG;
    else if (!down && screen == SCR_SONG)
        screen = SCR_OPTS;
    else if (down && screen == SCR_OPTS)
        screen = SCR_SONG;
    else if (!down && screen == SCR_CHAIN)
        screen = SCR_PROJ;
    else if (down && screen == SCR_PROJ)
        screen = SCR_CHAIN;
    else if (down && screen == SCR_CHAIN)
        screen = SCR_GROOVE;
    else if (!down && screen == SCR_GROOVE)
        screen = SCR_CHAIN;
    else if (!down && screen == SCR_INSTR) {
        unsigned char w = sd.instrs[edit_instr].wave;
        if (w < 8)
            edit_wave = w;
        screen = SCR_WAVE;
    } else if (down && screen == SCR_WAVE)
        screen = SCR_INSTR;
    else
        return;
    ph_row = 0xFF;
    groove_ph = 0xFF;
    draw_screen();
    mirror_cursor();
}

/* --- clipboard: cut (A-held + B), paste / mint / clone (A double-tap) --- */

static void cut_field(void)
{
    switch (screen) {
    case SCR_SONG:
        clip_kind = CLIP_CELL;
        clip_cell = sd.song[s_row][s_col];
        sd.song[s_row][s_col] = EMPTY;
        break;
    case SCR_CHAIN:
        clip_kind = CLIP_CHST;
        clip_chst = sd.chains[edit_chain][c_row];
        sd.chains[edit_chain][c_row].phrase = EMPTY;
        sd.chains[edit_chain][c_row].tsp = -1;
        break;
    case SCR_PHRASE:
        clip_kind = CLIP_STEP;
        clip_step = sd.phrases[edit_phrase][p_row];
        memset(&sd.phrases[edit_phrase][p_row], 0, sizeof(struct step));
        break;
    default:
        return;
    }
    draw_row(cursor_vrow(), 1);
}

/* --- block select: row ranges on SONG/CHAIN/PHRASE --- */

static unsigned char cursor_row_abs(void)
{
    switch (screen) {
    case SCR_SONG:  return s_row;
    case SCR_CHAIN: return c_row;
    default:        return p_row;
    }
}

static void sel_bounds(unsigned char *a, unsigned char *b)
{
    unsigned char c = cursor_row_abs();
    *a = (sel_anchor < c) ? sel_anchor : c;
    *b = (sel_anchor < c) ? c : sel_anchor;
}

/* selected rows get inverted row numbers */
static void sel_paint(void)
{
    unsigned char a, b, r, vr;
    sel_bounds(&a, &b);
    for (r = 0; r < 16; ++r) {
        unsigned char abs_r = (screen == SCR_SONG) ? song_page() + r : r;
        unsigned char inside = sel_active && abs_r >= a && abs_r <= b;
        vr = GRID_TOP + r;
        if (inside)
            draw_hex8(1, vr, abs_r, PEN_BG, PEN_ACCENT);
        else
            draw_hex8(1, vr, abs_r, PEN_DIM, PEN_BG);
    }
}

static void blk_copy(void)
{
    unsigned char a, b, i;
    sel_bounds(&a, &b);
    blk_n = b - a + 1;
    switch (screen) {
    case SCR_PHRASE:
        clip_kind = CLIP_RPH;
        memcpy(blk_steps, &sd.phrases[edit_phrase][a],
               blk_n * sizeof(struct step));
        break;
    case SCR_CHAIN:
        clip_kind = CLIP_RCH;
        memcpy(blk_chst, &sd.chains[edit_chain][a],
               blk_n * sizeof(struct chainstep));
        break;
    case SCR_SONG:
        clip_kind = CLIP_RSNG;
        for (i = 0; i < blk_n; ++i)
            memcpy(blk_song[i], sd.song[a + i], NCH);
        break;
    }
}

static void blk_clear(void)
{
    unsigned char a, b, i;
    sel_bounds(&a, &b);
    switch (screen) {
    case SCR_PHRASE:
        memset(&sd.phrases[edit_phrase][a], 0,
               (b - a + 1) * sizeof(struct step));
        break;
    case SCR_CHAIN:
        memset(&sd.chains[edit_chain][a], EMPTY,
               (b - a + 1) * sizeof(struct chainstep));
        break;
    case SCR_SONG:
        for (i = a; i <= b; ++i)
            memset(sd.song[i], EMPTY, NCH);
        break;
    }
}

static void sel_exit(void)
{
    sel_active = 0;
    draw_screen();
    mirror_cursor();
}

static void blk_paste(void)
{
    unsigned char at = cursor_row_abs();
    unsigned char n = blk_n, i;

    switch (screen) {
    case SCR_PHRASE:
        if (clip_kind != CLIP_RPH)
            return;
        if (at + n > PHRASE_ROWS)
            n = PHRASE_ROWS - at;
        memcpy(&sd.phrases[edit_phrase][at], blk_steps,
               n * sizeof(struct step));
        break;
    case SCR_CHAIN:
        if (clip_kind != CLIP_RCH)
            return;
        if (at + n > PHRASE_ROWS)
            n = PHRASE_ROWS - at;
        memcpy(&sd.chains[edit_chain][at], blk_chst,
               n * sizeof(struct chainstep));
        break;
    case SCR_SONG:
        if (clip_kind != CLIP_RSNG)
            return;
        if (at + n > SONG_ROWS)
            n = SONG_ROWS - at;
        for (i = 0; i < n; ++i)
            memcpy(sd.song[at + i], blk_song[i], NCH);
        break;
    default:
        return;
    }
    draw_screen();
}

/* first all-empty chain/phrase, or $FF if the pool is full */
static unsigned char find_free_chain(void)
{
    unsigned char c, s;
    for (c = 0; c < NCHAINS; ++c) {
        for (s = 0; s < PHRASE_ROWS; ++s)
            if (sd.chains[c][s].phrase != EMPTY)
                break;
        if (s == PHRASE_ROWS)
            return c;
    }
    return EMPTY;
}

static unsigned char find_free_phrase(void)
{
    unsigned char p, s;
    for (p = 0; p < NPHRASES; ++p) {
        for (s = 0; s < PHRASE_ROWS; ++s)
            if (sd.phrases[p][s].note || sd.phrases[p][s].cmd)
                break;
        if (s == PHRASE_ROWS)
            return p;
    }
    return EMPTY;
}

/* A double-tap: paste a matching clip, else mint (empty cell) or clone
 * (populated cell) into the next free slot — ported semantics */
static void double_tap(void)
{
    if (clip_kind >= CLIP_RPH) {
        blk_paste();
        return;
    }
    switch (screen) {
    case SCR_SONG:
        if (clip_kind == CLIP_CELL) {
            sd.song[s_row][s_col] = clip_cell;
        } else {
            unsigned char cur = sd.song[s_row][s_col];
            unsigned char nc = find_free_chain();
            if (nc == EMPTY)
                return;                       /* pool full: no-op */
            if (cur != EMPTY && cur < NCHAINS) /* clone */
                memcpy(sd.chains[nc], sd.chains[cur], sizeof(sd.chains[0]));
            sd.song[s_row][s_col] = nc;
            last_chain = nc;
        }
        break;
    case SCR_CHAIN:
        if (clip_kind == CLIP_CHST) {
            sd.chains[edit_chain][c_row] = clip_chst;
        } else if (c_col == 0) {
            struct chainstep *cs = &sd.chains[edit_chain][c_row];
            unsigned char np = find_free_phrase();
            if (np == EMPTY)
                return;
            if (cs->phrase != EMPTY && cs->phrase < NPHRASES)
                memcpy(sd.phrases[np], sd.phrases[cs->phrase],
                       sizeof(sd.phrases[0]));
            else
                cs->tsp = 0;
            cs->phrase = np;
            last_phrase = np;
        }
        break;
    case SCR_PHRASE:
        if (clip_kind == CLIP_STEP)
            sd.phrases[edit_phrase][p_row] = clip_step;
        break;
    default:
        return;
    }
    draw_row(cursor_vrow(), 1);
}

/* ---------------- playhead ---------------- */

static void playhead_update(void)
{
    unsigned char c, r;

    if (screen == SCR_SONG) {
        static unsigned char had_q;
        unsigned char q_now = (live_q[0] != EMPTY) || (live_q[1] != EMPTY)
                            || (live_q[2] != EMPTY) || (live_q[3] != EMPTY);
        if (had_q && !q_now)
            draw_song_screen();          /* a queued clip launched */
        had_q = q_now;
        for (c = 0; c < NCH; ++c) {
            unsigned char pr = (eng_mode == MODE_SONG && eng_walk[c].active)
                               ? eng_walk[c].song_row : 0xFF;
            if (pr != ph_song[c]) {
                unsigned char old = ph_song[c];
                ph_song[c] = pr;
                if (old != 0xFF && (old & 0xF0) == song_page())
                    draw_song_row(old & 0x0F, (old & 0x0F) == (s_row & 0x0F)
                                  && (old & 0xF0) == song_page());
                if (pr != 0xFF && (pr & 0xF0) == song_page())
                    draw_song_row(pr & 0x0F, (pr & 0x0F) == (s_row & 0x0F)
                                  && (pr & 0xF0) == song_page());
            }
        }
        return;
    }

    if (screen == SCR_GROOVE) {
        r = (eng_mode && eng_groove == edit_groove) ? eng_gpos : 0xFF;
        if (r != groove_ph) {
            unsigned char old = groove_ph;
            groove_ph = r;
            if (old != 0xFF)
                draw_groove_row(old, old == g_row);
            if (r != 0xFF)
                draw_groove_row(r, r == g_row);
        }
        return;
    }
    if (screen >= SCR_INSTR)
        return;
    r = 0xFF;
    if (screen == SCR_CHAIN) {
        if (eng_mode && eng_walk[cur_track].active
            && eng_walk[cur_track].chain == edit_chain)
            r = eng_walk[cur_track].cpos;
    } else {
        if (eng_mode && eng_walk[cur_track].active
            && eng_walk[cur_track].phrase == edit_phrase)
            r = eng_walk[cur_track].prow;
    }
    if (r != ph_row) {
        unsigned char old = ph_row;
        ph_row = r;
        if (old != 0xFF)
            draw_row(old, old == cursor_vrow());
        if (r != 0xFF)
            draw_row(r, r == cursor_vrow());
    }
}

static unsigned char dir_of(unsigned char pressed)
{
    if (pressed & JOYPAD_UP) return 0;
    if (pressed & JOYPAD_DOWN) return 1;
    if (pressed & JOYPAD_LEFT) return 2;
    if (pressed & JOYPAD_RIGHT) return 3;
    return 0xFF;
}

/* ---------------- public ---------------- */

void editor_init(void)
{
    unsigned char c;
    for (c = 0; c < NCH; ++c)
        ph_song[c] = 0xFF;
    screen = SCR_SONG;
    draw_screen();
    mirror_cursor();
}

static void meters_update(void)
{
    unsigned char t, h, i;
    char b[2];

    b[1] = 0;
    for (t = 0; t < NCH; ++t) {
        /* 0-127 envelope -> 0-METER_H cells (rounded so full VOL fills) */
        h = (unsigned char)((eng_level[t] + 8) >> 4);
        if (h > METER_H)
            h = METER_H;
        if (h == meter_h[t])
            continue;
        meter_h[t] = h;
        for (i = 0; i < METER_H; ++i) {
            b[0] = '`';               /* solid block; lit above, dim track below */
            draw_text(METER_X(t), METER_BOT - i, b,
                      (i < h) ? PEN_ACCENT : PEN_DIM, PEN_BG);
        }
    }
}

void editor_frame(unsigned char joy, unsigned char prev)
{
    unsigned char pressed = joy & ~prev;
    unsigned char dir = 0xFF;

    playhead_update();
    meters_update();
    if (a_release_age != 0xFF)
        ++a_release_age;

    /* Option 1 layer: track select / mute / solo (DESIGN.md §3) */
    if (joy & BUTTON_OPTION1) {
        if ((pressed & JOYPAD_LEFT) && cur_track) {
            --cur_track;
            top_bar();
        }
        if ((pressed & JOYPAD_RIGHT) && cur_track < NCH - 1) {
            ++cur_track;
            top_bar();
        }
        if (pressed & JOY_BTN_A_MASK) {          /* mute toggle */
            engine_set_mute(eng_mute ^ (1 << cur_track));
            a_used = 1;
            top_bar();
        }
        if (pressed & JOY_BTN_B_MASK) {          /* solo / unsolo */
            unsigned char solo = (unsigned char)(~(1 << cur_track) & 0x0F);
            engine_set_mute(eng_mute == solo ? 0 : solo);
            b_used = 1;
            top_bar();
        }
        return;
    }

    /* block-select mode swallows everything (ported gesture set) */
    if (sel_active) {
        if (dir_of(pressed) == 0 || dir_of(pressed) == 1) {
            move_cursor(dir_of(pressed));
            sel_paint();
            return;
        }
        if ((joy & JOY_BTN_A_MASK) && (pressed & JOY_BTN_B_MASK)) {
            blk_copy();                  /* cut = copy + clear */
            blk_clear();
            a_used = 1;
            b_used = 1;
            sel_exit();
            return;
        }
        if (pressed & JOY_BTN_A_MASK) {  /* copy */
            blk_copy();
            a_used = 1;
            sel_exit();
            return;
        }
        if (pressed & JOY_BTN_B_MASK) {  /* cancel */
            b_used = 1;
            sel_exit();
            return;
        }
        return;
    }

    /* A held + B: quick release = single-field cut, long hold = SELECT */
    if ((joy & JOY_BTN_A_MASK) && (pressed & JOY_BTN_B_MASK)
        && (screen == SCR_SONG || screen == SCR_CHAIN
            || screen == SCR_PHRASE)) {
        a_used = 1;
        b_used = 1;
        ab_pending = 1;
        ab_timer = 0;
        return;
    }
    if ((joy & JOY_BTN_A_MASK) && (pressed & JOY_BTN_B_MASK)) {
        a_used = 1;                      /* other screens: immediate cut */
        b_used = 1;
        cut_field();
        return;
    }
    if (ab_pending) {
        if ((joy & (JOY_BTN_A_MASK | JOY_BTN_B_MASK))
            == (JOY_BTN_A_MASK | JOY_BTN_B_MASK)) {
            if (++ab_timer >= SEL_HOLD) {
                ab_pending = 0;
                sel_active = 1;
                sel_anchor = cursor_row_abs();
                sel_paint();
            }
            return;
        }
        ab_pending = 0;
        if (!(joy & JOY_BTN_B_MASK))     /* B released early: field cut */
            cut_field();
        return;
    }

    /* Option 2: LIVE mode toggle (SONG screen is the launcher) */
    if ((pressed & BUTTON_OPTION2) && screen == SCR_SONG) {
        live_mode ^= 1;
        top_bar();
        draw_song_screen();
        return;
    }

    /* B held + A pressed = transport, contextual per screen */
    if ((joy & JOY_BTN_B_MASK) && (pressed & JOY_BTN_A_MASK)) {
        b_used = 1;
        a_used = 1;
        if (live_mode && screen == SCR_SONG) {
            unsigned char cn = sd.song[s_row][s_col];
            engine_live_queue(s_col, cn == EMPTY ? 0xFE : cn);
            transport_label();
            draw_song_screen();
            return;
        }
        if (eng_mode)
            engine_stop();
        else switch (screen) {
        case SCR_SONG:   engine_play_song(s_row); break;
        case SCR_CHAIN:  engine_play_chain(cur_track, edit_chain); break;
        case SCR_PHRASE: engine_play_phrase(cur_track, edit_phrase); break;
        }
        transport_label();
        return;
    }

    /* d-pad edge + DAS repeat */
    if (pressed & JOYPAD_UP) dir = 0;
    else if (pressed & JOYPAD_DOWN) dir = 1;
    else if (pressed & JOYPAD_LEFT) dir = 2;
    else if (pressed & JOYPAD_RIGHT) dir = 3;

    if (dir != 0xFF) {
        rep_dir = dir;
        rep_timer = opt_repeat;
    } else if (joy & (JOYPAD_UP | JOYPAD_DOWN | JOYPAD_LEFT | JOYPAD_RIGHT)) {
        if (--rep_timer == 0) {
            rep_timer = DAS_RATE;
            dir = rep_dir;
        }
    }

    if (dir != 0xFF) {
        if (joy & JOY_BTN_B_MASK) {          /* B chord: screen nav */
            b_used = 1;
            if (dir == 2)
                nav(0);
            else if (dir == 3)
                nav(1);
            else
                nav_v(dir == 1);
        } else if (joy & JOY_BTN_A_MASK) {   /* A chord: edit */
            a_used = 1;
            edit_cell(dir);
        } else
            move_cursor(dir);
    }

    if (pressed & JOY_BTN_A_MASK) {
        a_used = 0;
        if (a_release_age < TAP_WINDOW) {    /* double-tap: paste/mint/clone */
            a_used = 1;
            a_release_age = 0xFF;
            double_tap();
        }
    }
    if (pressed & JOY_BTN_B_MASK)
        b_used = 0;
    if ((prev & JOY_BTN_A_MASK) && !(joy & JOY_BTN_A_MASK)) {
        if (!a_used) {
            insert_cell();                   /* clean A tap */
            a_release_age = 0;               /* arm the double-tap window */
        }
    }
    if ((prev & JOY_BTN_B_MASK) && !(joy & JOY_BTN_B_MASK) && !b_used)
        nav(0);                              /* clean B tap = back */
}
