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
#define CMD_H    5      /* H xx  table: loop to row x; phrase: end phrase */
#define CMD_K    6      /* K xx  kill note after xx ticks */
#define CMD_O    7      /* O xy  pan: ATTEN left x / right y */
#define CMD_P    8      /* P xx  pitch bend, signed 1/16-semi per tick */
#define CMD_V    9      /* V xy  vibrato speed x depth y (1/16 semis) */
#define CMD_W    10     /* W xx  shorten this row to xx ticks */
#define CMD_X    11     /* X xx  this note's volume (envelope peak) */
#define CMD_F    12     /* F xx  finetune, signed 1/16 semis (one-shot) */
#define CMD_L    13     /* L xx  slide from the previous pitch, xx/16 semi
                                 per tick (needs a note on the row) */
#define CMD_N    14     /* N xx  live LFSR taps override: bits 0-5 = taps
                                 0-5, 6 = tap 7, 7 = tap 10 (D11 morph) */
#define CMD_R    15     /* R xy  retrig every y ticks, peak -8x per fire */
#define CMD_S    16     /* S xx  live PCM rate: timer-7 reload (pitch the
                                 playing sample; smaller = faster) */
#define CMD_Z    17     /* Z xx  probability: note plays if rand8 < xx */
#define CMD_E    18     /* E xy  re-slope the envelope live: ATK x, DCY y
                                 nibbles (current stage + level untouched) */
#define CMD_T    19     /* T xx  tempo: set the active groove flat to the
                                 hex BPM (flattens swing by design) */
#define CMD_I    20     /* I xx  iteration: play this note only on phrase
                                 passes whose bit (count mod 8) is set */
#define CMD_J    21     /* J xy  variation: transpose x (signed nibble) on
                                 passes whose bit (count mod 4) is set in y */
#define NCMDS    22

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

/* 16-byte fixed record, union-by-type later (DESIGN.md §6).
 *
 * TAPS is the raw 12-bit-LFSR tap mask (DESIGN.md D11), user-contiguous:
 * bits 0-5 = taps 0-5, bit 6 = tap 7, bit 7 = tap 10, bit 8 = tap 11
 * (trigger remaps to the scattered FEEDBACK/control layout). SEED is the
 * 12-bit shifter start state — many tap sets have several disjoint state
 * cycles, so the seed picks the waveform; some (taps, seed) pairs hit the
 * LFSR lock state and go silent, exactly like the silicon. */
struct instr {
    unsigned char type;     /* IT_* */
    unsigned char vol;      /* envelope peak, $00-$7F */
    unsigned char env;      /* ATK<<4 | DCY: 4-bit TIMES, higher = longer
                               (0 = instant attack / sustain-forever decay);
                               mapped through env_rate[] in the engine */
    unsigned char hold;     /* low nibble: ticks at peak (0-15) */
    unsigned char wave;     /* the BANK byte: WAV = wavetable # ($FF =
                               hardware triangle); KIT = pool kit # */
    unsigned char taps_lo;  /* TAPS bits 7..0 */
    unsigned char table;    /* $FF = none */
    unsigned char pan;      /* L/R nibbles */
    signed char   fine;     /* (M9c) */
    unsigned char taps_hi;  /* TAPS bit 8 (tap 11) in bit 0 */
    unsigned char seed_lo;  /* SEED bits 7..0 */
    unsigned char seed_hi;  /* SEED bits 11..8 in bits 3..0 */
    unsigned char pad[4];
};

#define TAPS_SQUARE 0x001   /* tap 0 only: the proven square */
#define TAPS_NOISE  0x1BF   /* taps 0-5 + 10 + 11: long/max-length noise */

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
    unsigned char    waves[8][32];              /* signed 8-bit wavetables */
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

extern const unsigned char env_rate[16];
void engine_init(void);
void engine_tick(void);
void engine_stop(void);
void engine_play_song(unsigned char row);
void engine_play_chain(unsigned char track, unsigned char chain);
void engine_play_phrase(unsigned char track, unsigned char phrase);
void engine_audition(unsigned char note, unsigned char inum);
void __fastcall__ engine_audition_cmd(unsigned char cmd, unsigned char param);

void sound_init(void);
void pcm_stop(void);
void pcm_ring_start(void);

/* cart streaming (src/cart.s) + sample pool (src/pool.c) */
void __fastcall__ cart_seek(unsigned char block, unsigned off);
void __fastcall__ cart_read(unsigned char *dst, unsigned char n);
void pool_init(void);
void pool_pump(void);
void __fastcall__ pool_trigger(unsigned char kit, unsigned char slot);
unsigned char pool_kits(void);
extern unsigned char *pcm_head;
extern unsigned char pcm_done;

/* ComLynx sync (src/sync.c) */
#define SYNC_OFF 0
#define SYNC_OUT 1
#define SYNC_IN  2
#define NSYNC    3
#define SYNC_OP_ROW   0x01
#define SYNC_OP_START 0x02
#define SYNC_OP_STOP  0x03
extern unsigned char sync_mode;
extern unsigned char sync_row_pending;
void sync_init(void);
void __fastcall__ sync_tx(unsigned char b);
void sync_poll(void);

/* wavetable feed (irq.s): loops sd.waves[w] through channel C's DAC */
void __fastcall__ wave_start(unsigned char w);
void __fastcall__ wave_rate(unsigned char clock, unsigned char bkup,
                            unsigned char step);
void wave_stop(void);

/* 93C86 EEPROM (src/eeprom.s), full 16-bit words per cell */
unsigned __fastcall__ ee_read(unsigned cell);
void __fastcall__ ee_write(unsigned cell, unsigned val);

/* packed save (src/save.c); ST codes shared with the FILES screen.
 * Full 93C86 capacity (1020 payload words) — needs the repo-built Handy
 * core (stock truncates EEPROM file loads to 1024 bytes, eeprom.cpp:59
 * — fix applied in our build; PR upstream pending). */
#define SAVE_CAP_BYTES 2040
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
