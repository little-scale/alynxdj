/* Editor — SONG / CHAIN / PHRASE screens (M5).
 *
 * Control frame (DESIGN.md §3, ported): A = item modifier (tap insert on
 * release, hold+dpad edit), B = project modifier (tap = back, held+dpad =
 * screen nav with drill-down, held+A = transport). The button already held
 * when the other arrives selects the action; a chord marks the held
 * button "used" so its tap action doesn't also fire on release.
 */
#include <lynx.h>

#include "tracker.h"
#include "../build/notes.h"

/* debug mirrors for the headless harness */
#define MIRROR_ROW    (*(volatile unsigned char *)0xC001)
#define MIRROR_COL    (*(volatile unsigned char *)0xC002)
#define MIRROR_SCREEN (*(volatile unsigned char *)0xC003)

#define SCR_SONG   0
#define SCR_CHAIN  1
#define SCR_PHRASE 2

#define GRID_TOP 1

static unsigned char screen;
static unsigned char cur_track;         /* set by the SONG cursor column */
static unsigned char edit_chain;        /* what CHAIN shows */
static unsigned char edit_phrase;       /* what PHRASE shows */

/* per-screen cursors */
static unsigned char s_row, s_col;      /* SONG: 0-127 x 0-3 */
static unsigned char c_row, c_col;      /* CHAIN: 0-15 x {phrase,tsp} */
static unsigned char p_row, p_col;      /* PHRASE: 0-15 x {note,instr,cmd} */

/* last-entered memory (insert repeats it, ported) */
static unsigned char last_note = 37;    /* C-4 */
static unsigned char last_chain = 0;
static unsigned char last_phrase = 0;

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
        draw_text(1, 0, "SONG", PEN_ACCENT, PEN_BG);
        break;
    case SCR_CHAIN:
        draw_text(1, 0, "CHAIN", PEN_ACCENT, PEN_BG);
        draw_hex8(7, 0, edit_chain, PEN_TEXT, PEN_BG);
        break;
    case SCR_PHRASE:
        draw_text(1, 0, "PHRASE", PEN_ACCENT, PEN_BG);
        draw_hex8(8, 0, edit_phrase, PEN_TEXT, PEN_BG);
        break;
    }
    draw_text(12, 0, "T", PEN_DIM, PEN_BG);
    draw_hex8(13, 0, cur_track + 1, PEN_DIM, PEN_BG);
}

static void transport_label(void)
{
    draw_text(30, 0, eng_mode ? "PLAY" : "STOP",
              eng_mode ? PEN_ACCENT : PEN_DIM, PEN_BG);
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

    draw_hex8(1, y, r, r == ph_row ? PEN_ACCENT : PEN_DIM, PEN_BG);

    if (s->note) {
        char b[4];
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
    draw_text(13, y, "---", inv2 ? PEN_BG : PEN_DIM,
              inv2 ? PEN_TEXT : PEN_BG);
}

static void draw_phrase_screen(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_phrase_row(r, r == p_row);
}

/* --- dispatch --- */

static void draw_row(unsigned char r, unsigned char cursor_here)
{
    switch (screen) {
    case SCR_SONG:   draw_song_row(r, cursor_here); break;
    case SCR_CHAIN:  draw_chain_row(r, cursor_here); break;
    case SCR_PHRASE: draw_phrase_row(r, cursor_here); break;
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
    }
    MIRROR_SCREEN = screen;
}

static unsigned char cursor_vrow(void)
{
    switch (screen) {
    case SCR_SONG:  return s_row & 0x0F;
    case SCR_CHAIN: return c_row;
    default:        return p_row;
    }
}

static void mirror_cursor(void)
{
    switch (screen) {
    case SCR_SONG:  MIRROR_ROW = s_row; MIRROR_COL = s_col; break;
    case SCR_CHAIN: MIRROR_ROW = c_row; MIRROR_COL = c_col; break;
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
    default:
        switch (dir) {
        case 0: if (p_row) --p_row; else p_row = 15; break;
        case 1: if (p_row < 15) ++p_row; else p_row = 0; break;
        case 2: if (p_col) --p_col; break;
        case 3: if (p_col < 2) ++p_col; break;
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
        if (!eng_mode)
            engine_audition(n);
    } else if (p_col == 1 && s->note) {
        switch (dir) {
        case 0: s->instr += 16; break;
        case 1: s->instr -= 16; break;
        case 2: --s->instr; break;
        case 3: ++s->instr; break;
        }
    }
}

static void edit_cell(unsigned char dir)
{
    switch (screen) {
    case SCR_SONG:   edit_song_cell(dir); break;
    case SCR_CHAIN:  edit_chain_cell(dir); break;
    case SCR_PHRASE: edit_phrase_cell(dir); break;
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
        if (c_col == 0 && sd.chains[edit_chain][c_row].phrase == EMPTY)
            sd.chains[edit_chain][c_row].phrase = last_phrase;
        break;
    case SCR_PHRASE:
        if (p_col == 0 && !sd.phrases[edit_phrase][p_row].note) {
            sd.phrases[edit_phrase][p_row].note = last_note;
            if (!eng_mode)
                engine_audition(last_note);
        }
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
        } else
            return;
    } else {
        if (screen == SCR_PHRASE)
            screen = SCR_CHAIN;
        else if (screen == SCR_CHAIN)
            screen = SCR_SONG;
        else
            return;
    }
    ph_row = 0xFF;
    draw_screen();
    mirror_cursor();
}

/* ---------------- playhead ---------------- */

static void playhead_update(void)
{
    unsigned char c, r;

    if (screen == SCR_SONG) {
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

void editor_frame(unsigned char joy, unsigned char prev)
{
    unsigned char pressed = joy & ~prev;
    unsigned char dir = 0xFF;

    playhead_update();

    /* B held + A pressed = transport, contextual per screen */
    if ((joy & JOY_BTN_B_MASK) && (pressed & JOY_BTN_A_MASK)) {
        b_used = 1;
        a_used = 1;
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
        rep_timer = DAS_DELAY;
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
        } else if (joy & JOY_BTN_A_MASK) {   /* A chord: edit */
            a_used = 1;
            edit_cell(dir);
        } else
            move_cursor(dir);
    }

    if (pressed & JOY_BTN_A_MASK)
        a_used = 0;
    if (pressed & JOY_BTN_B_MASK)
        b_used = 0;
    if ((prev & JOY_BTN_A_MASK) && !(joy & JOY_BTN_A_MASK) && !a_used)
        insert_cell();                       /* clean A tap */
    if ((prev & JOY_BTN_B_MASK) && !(joy & JOY_BTN_B_MASK) && !b_used)
        nav(0);                              /* clean B tap = back */
}
