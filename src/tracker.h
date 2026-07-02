/* Shared data model + module interfaces (single source of truth for the
 * flat song block layout — SAVEFORMAT.md will mirror this at M10). */
#ifndef TRACKER_H
#define TRACKER_H

#define NCH         4
#define PHRASE_ROWS 16
#define SONG_ROWS   128
#define NCHAINS     32
#define NPHRASES    64
#define EMPTY       0xFF

struct step {
    unsigned char note;     /* 0 = empty, 1..96 = C-1..B-8 */
    unsigned char instr;
    unsigned char cmd;
    unsigned char param;
};

struct chainstep {
    unsigned char phrase;   /* $FF = end/empty */
    signed char   tsp;      /* semitone transpose for the whole phrase */
};

struct songdata {
    unsigned char    song[SONG_ROWS][NCH];
    struct chainstep chains[NCHAINS][PHRASE_ROWS];
    struct step      phrases[NPHRASES][PHRASE_ROWS];
};
extern struct songdata sd;

struct walk {
    unsigned char active;
    unsigned char song_row;
    unsigned char chain;
    unsigned char cpos;
    unsigned char phrase;
    signed char   tsp;
    unsigned char prow;
};
extern struct walk eng_walk[NCH];

#define MODE_STOP   0
#define MODE_SONG   1
#define MODE_CHAIN  2
#define MODE_PHRASE 3
extern unsigned char eng_mode;

void engine_tick(void);
void engine_stop(void);
void engine_play_song(unsigned char row);
void engine_play_chain(unsigned char track, unsigned char chain);
void engine_play_phrase(unsigned char track, unsigned char phrase);
void engine_audition(unsigned char note);

void sound_init(void);

/* render (main.c) */
void draw_text(unsigned char cx, unsigned char cy, const char *s,
               unsigned char fg, unsigned char bg);
void draw_hex8(unsigned char cx, unsigned char cy, unsigned char v,
               unsigned char fg, unsigned char bg);
void clear_grid(void);

#define PEN_BG      0
#define PEN_TEXT    1
#define PEN_DIM     2
#define PEN_ACCENT  3

/* editor */
void editor_init(void);
void editor_frame(unsigned char joy, unsigned char prev);

#endif
