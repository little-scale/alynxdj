/* Shared data model + module interfaces (single source of truth for the
 * flat song block layout — SAVEFORMAT.md will mirror this at M10). */
#ifndef TRACKER_H
#define TRACKER_H

#define NCH         4
#define PHRASE_ROWS 16
#define SONG_ROWS   128
#define NCHAINS     32
#define NPHRASES    64
#define NINSTR      32
#define NTABLES     16
#define NGROOVES    16
#define EMPTY       0xFF

/* command ids (cmd byte in steps/tables; 0 = none). Letters follow the
 * SMSGGDJ set where the meaning survives (DESIGN.md §8). */
#define CMD_NONE 0
#define CMD_A    1      /* A xx  run table xx (one-shot; 10 = off) */
#define CMD_C    2      /* C xy  chord: loop +0,+x,+y semitones per tick */
#define CMD_D    3      /* D xx  delay trigger xx ticks */
#define CMD_G    4      /* G xx  switch groove */
#define CMD_H    5      /* H xx  table: loop to row x (phrase H is M9b) */
#define CMD_K    6      /* K xx  kill note after xx ticks */
#define CMD_O    7      /* O xy  pan: ATTEN left x / right y */
#define CMD_P    8      /* P xx  pitch bend, signed 1/16-semi per tick */
#define CMD_V    9      /* V xy  vibrato speed x depth y (1/16 semis) */
#define CMD_W    10     /* W xx  shorten this row to xx ticks */
#define CMD_X    11     /* X xx  this note's volume (envelope peak) */
#define NCMDS    12

#define IT_TONE  0
#define IT_NOISE 1
#define IT_WAV   2      /* M8: falls back to TONE until then */
#define IT_KIT   3
#define NTYPES   4

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

/* 16-byte fixed record, union-by-type later (DESIGN.md §6) */
struct instr {
    unsigned char type;     /* IT_* */
    unsigned char vol;      /* envelope peak, $00-$7F */
    unsigned char atk;      /* level += atk/tick; 0 = instant */
    unsigned char hold;     /* ticks at peak */
    unsigned char dcy;      /* level -= dcy/tick; 0 = sustain */
    unsigned char timbre;   /* LFSR tap preset 0-7 within the type's bank */
    unsigned char table;    /* $FF = none (M6b) */
    unsigned char pan;      /* L/R nibbles (M8) */
    signed char   fine;     /* (M9) */
    unsigned char pad[7];
};

struct tablerow {
    unsigned char vol;      /* 0 = no change, else set envelope level */
    signed char   tsp;      /* semitone offset while this row is active */
    unsigned char cmd;
    unsigned char param;
};

struct songdata {
    unsigned char    song[SONG_ROWS][NCH];
    struct chainstep chains[NCHAINS][PHRASE_ROWS];
    struct step      phrases[NPHRASES][PHRASE_ROWS];
    struct instr     instrs[NINSTR];
    struct tablerow  tables[NTABLES][PHRASE_ROWS];
    unsigned char    grooves[NGROOVES][16];     /* ticks/row, 0 = end */
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
extern unsigned char eng_mute;
extern unsigned char eng_gpos, eng_groove;
void __fastcall__ engine_set_mute(unsigned char mask);

void engine_tick(void);
void engine_stop(void);
void engine_play_song(unsigned char row);
void engine_play_chain(unsigned char track, unsigned char chain);
void engine_play_phrase(unsigned char track, unsigned char phrase);
void engine_audition(unsigned char note, unsigned char inum);

void sound_init(void);
void __fastcall__ pcm_play(unsigned char slot);
void pcm_stop(void);

/* 93C86 EEPROM (src/eeprom.s), full 16-bit words per cell */
unsigned __fastcall__ ee_read(unsigned cell);
void __fastcall__ ee_write(unsigned cell, unsigned val);

/* packed save (src/save.c); ST codes shared with the FILES screen.
 * Capacity capped at 508 words while stock Handy truncates EEPROM file
 * loads to 1024 bytes (eeprom.cpp:59) — lift to 1020 words with the core
 * fix / hardware verification. */
#define SAVE_CAP_BYTES 1016
#define ST_NONE   0
#define ST_OK     1
#define ST_TOOBIG 2
#define ST_NODATA 3
#define ST_BADSUM 4
unsigned save_pack(void);       /* dry run: packed length, 0 = too big */
unsigned char save_do(void);
unsigned char save_load(void);
unsigned save_sum(void);

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
