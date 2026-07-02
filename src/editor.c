/* PHRASE editor — M4: grid, cursor, edit, insert, prelisten.
 *
 * Control frame (DESIGN.md §3, ported): A = item modifier (tap insert,
 * hold+dpad edit), B = project modifier (held+A = play/stop transport).
 * The button already held when the other arrives selects the action; the
 * A-tap action fires on *release* only if no chord happened while it was
 * down (same resolution rule as the siblings).
 */
#include <lynx.h>

#include "../build/notes.h"

/* --- engine interface (engine.c) --- */
struct step {
    unsigned char note, instr, cmd, param;
};
extern struct step phrase[16];
extern unsigned char eng_playing, eng_row;
void engine_play(void);
void engine_stop(void);
void engine_audition(unsigned char note);

/* --- render interface (main.c) --- */
void draw_text(unsigned char cx, unsigned char cy, const char *s,
               unsigned char fg, unsigned char bg);
void draw_hex8(unsigned char cx, unsigned char cy, unsigned char v,
               unsigned char fg, unsigned char bg);
#define PEN_BG      0
#define PEN_TEXT    1
#define PEN_DIM     2
#define PEN_ACCENT  3

/* debug mirrors for the headless harness */
#define MIRROR_ROW (*(volatile unsigned char *)0xC001)
#define MIRROR_COL (*(volatile unsigned char *)0xC002)

/* grid geometry: header row 0, grid rows 1..16 */
#define GRID_TOP    1
#define COL_ROWNUM  1
#define COL_NOTE    4
#define COL_INSTR   9
#define COL_CMD     13

#define NCOLS 3                 /* cursor columns: note, instr, cmd+param */

static unsigned char cur_row, cur_col;
static unsigned char last_note = 37;    /* C-4; insert repeats last entry */
static unsigned char pp_row = 0xFF;     /* drawn playhead row */

/* key repeat (DAS), ported behaviour */
#define DAS_DELAY  12
#define DAS_RATE    2
static unsigned char rep_dir, rep_timer;

/* A-chord tracking: did a chord/edit consume this A press? */
static unsigned char a_used;

static void draw_step(unsigned char row, unsigned char inverse)
{
    unsigned char fg = inverse ? PEN_BG : PEN_TEXT;
    unsigned char bg = inverse ? PEN_TEXT : PEN_BG;
    struct step *s = &phrase[row];
    unsigned char y = GRID_TOP + row;

    /* row number stays chrome-dim, playhead-accented */
    draw_hex8(COL_ROWNUM, y, row,
              row == eng_row && eng_playing ? PEN_ACCENT : PEN_DIM, PEN_BG);

    if (s->note) {
        const char *n = note_names[s->note - 1];
        char b[4];
        b[0] = n[0]; b[1] = n[1]; b[2] = n[2]; b[3] = 0;
        draw_text(COL_NOTE, y, b, cur_col == 0 && inverse ? fg : PEN_TEXT,
                  cur_col == 0 && inverse ? bg : PEN_BG);
    } else
        draw_text(COL_NOTE, y, "---", cur_col == 0 && inverse ? fg : PEN_DIM,
                  cur_col == 0 && inverse ? bg : PEN_BG);

    if (s->note)
        draw_hex8(COL_INSTR, y, s->instr,
                  cur_col == 1 && inverse ? fg : PEN_TEXT,
                  cur_col == 1 && inverse ? bg : PEN_BG);
    else
        draw_text(COL_INSTR, y, "--", cur_col == 1 && inverse ? fg : PEN_DIM,
                  cur_col == 1 && inverse ? bg : PEN_BG);

    /* command column: placeholder until the M6 executor */
    draw_text(COL_CMD, y, "---", cur_col == 2 && inverse ? fg : PEN_DIM,
              cur_col == 2 && inverse ? bg : PEN_BG);
}

static void redraw_grid(void)
{
    unsigned char r;
    for (r = 0; r < 16; ++r)
        draw_step(r, r == cur_row);
}

void editor_init(void)
{
    draw_text(1, 0, "PHRASE 00", PEN_ACCENT, PEN_BG);
    draw_text(30, 0, "STOP", PEN_DIM, PEN_BG);
    redraw_grid();
}

static void move_cursor(unsigned char dir)
{
    unsigned char old = cur_row;

    switch (dir) {
    case 0: if (cur_row) --cur_row; else cur_row = 15; break;   /* up */
    case 1: if (cur_row < 15) ++cur_row; else cur_row = 0; break; /* down */
    case 2: if (cur_col) --cur_col; break;                      /* left */
    case 3: if (cur_col < NCOLS - 1) ++cur_col; break;          /* right */
    }
    draw_step(old, old == cur_row);
    draw_step(cur_row, 1);
    MIRROR_ROW = cur_row;
    MIRROR_COL = cur_col;
}

static void edit_cell(unsigned char dir)
{
    struct step *s = &phrase[cur_row];

    if (cur_col == 0) {                     /* note: L/R semitone, U/D octave */
        unsigned char n = s->note ? s->note : last_note;
        switch (dir) {
        case 0: n = (n + 12 <= NOTE_MAX) ? n + 12 : n; break;
        case 1: n = (n > 12) ? n - 12 : n; break;
        case 2: n = (n > NOTE_MIN) ? n - 1 : n; break;
        case 3: n = (n < NOTE_MAX) ? n + 1 : n; break;
        }
        s->note = n;
        last_note = n;
        if (!eng_playing)
            engine_audition(n);
    } else if (cur_col == 1 && s->note) {   /* instr: L/R ±1, U/D ±16 */
        switch (dir) {
        case 0: s->instr += 16; break;
        case 1: s->instr -= 16; break;
        case 2: --s->instr; break;
        case 3: ++s->instr; break;
        }
    }
    draw_step(cur_row, 1);
}

static void insert_cell(void)
{
    struct step *s = &phrase[cur_row];

    if (cur_col == 0) {
        if (s->note) {                      /* delete-on-populated comes with
                                               the M9 cut/clipboard pass */
            return;
        }
        s->note = last_note;
        if (!eng_playing)
            engine_audition(s->note);
        draw_step(cur_row, 1);
    }
}

static void set_transport_label(void)
{
    draw_text(30, 0, eng_playing ? "PLAY" : "STOP",
              eng_playing ? PEN_ACCENT : PEN_DIM, PEN_BG);
}

/* one call per VBlank frame with the raw pad byte */
void editor_frame(unsigned char joy, unsigned char prev)
{
    unsigned char pressed = joy & ~prev;
    unsigned char dir = 0xFF;

    /* playhead repaint */
    if (eng_playing || pp_row != 0xFF) {
        unsigned char pr = eng_playing ? eng_row : 0xFF;
        if (pr != pp_row) {
            if (pp_row != 0xFF)
                draw_step(pp_row, pp_row == cur_row);
            if (pr != 0xFF)
                draw_step(pr, pr == cur_row);
            pp_row = pr;
        }
    }

    /* B held + A pressed = transport (B was already down => chord wins) */
    if ((joy & JOY_BTN_B_MASK) && (pressed & JOY_BTN_A_MASK)) {
        if (eng_playing)
            engine_stop();
        else
            engine_play();
        set_transport_label();
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
        if (joy & JOY_BTN_A_MASK) {         /* A already held: edit chord */
            a_used = 1;
            edit_cell(dir);
        } else
            move_cursor(dir);
    }

    if (pressed & JOY_BTN_A_MASK)
        a_used = 0;                         /* fresh press, nothing consumed */
    if ((prev & JOY_BTN_A_MASK) && !(joy & JOY_BTN_A_MASK)) {
        if (!a_used)
            insert_cell();                  /* clean tap = insert on release */
    }
}
