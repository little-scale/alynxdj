/* Sequencer engine — per-tick pipeline ported from SMSGGDJ engine.asm:
 *
 *   groove -> row advance -> trigger peek (D) -> trigger + commands ->
 *   table step -> continuous effects (bend/vibrato/chord) -> envelope ->
 *   kill -> shadow -> flush
 *
 * Each of the 4 tracks walks the song independently; row timing is global
 * (one groove clocks all tracks). One executor runs both the phrase and
 * table command columns (ported discipline).
 *
 * Pitch model: 1/16-semitone fixed point. base note (chain transpose
 * applied) + chord/table semitone offsets + bend/vibrato sixteenths ->
 * table lookup with linear BACKUP interpolation; per-tick pitch updates
 * write the reload register only, so the oscillator phase never restarts.
 */
#include <lynx.h>

#include "tracker.h"
#include "../build/notes.h"

#define AUD_ENABLE_COUNT  0x10
#define AUD_ENABLE_RELOAD 0x08
#define AUD_INTEGRATE     0x20
#define AUD_TAP7          0x80          /* control bit 7 enables LFSR tap 7 */

/* the flat song block lives in HIRAM ($C900, SONG segment) — MAIN is full */
#pragma bss-name (push, "SONG")
struct songdata sd;
#pragma bss-name (pop)
struct walk eng_walk[NCH];

extern volatile unsigned int frames;    /* VBL counter (irq.s) */

struct voice {
    unsigned char base_note;    /* chain-transposed trigger note */
    unsigned char type;         /* IT_* latched at trigger */
    unsigned char env_phase;    /* 0=off 1=attack 2=hold 3=decay */
    unsigned char env_level;
    unsigned char env_peak;
    unsigned char hold_left;
    unsigned char e_atk, e_dcy;
    /* effects */
    int           bend;         /* accumulated 1/16 semis */
    signed char   bend_rate;    /* P: per tick */
    int           slide_off;    /* L: offset from previous pitch -> 0 */
    unsigned char slide_rate;
    unsigned char vib_speed, vib_depth, vib_phase;
    unsigned char chord[3], chord_len, chord_pos;
    unsigned char kill_in;      /* $FF = none */
    unsigned char table, tpos;  /* $FF = none */
    signed char   tbl_tsp;
    unsigned char inum;         /* instrument (retrig refires it) */
    unsigned char rt_rate, rt_fade, rt_cnt;
    /* delayed trigger (D) */
    unsigned char dly_in;       /* $FF = none */
    unsigned char dly_note, dly_instr;
    /* Mikey shadow */
    unsigned char sh_feedback, sh_bkup, sh_ctl, sh_pan;
    unsigned char sh_shiftlo, sh_shifthi;   /* LFSR seed (OTHER hi nibble) */
    unsigned char dirty, retime;
};

static struct voice voices[NCH];

unsigned char eng_mode;
unsigned char eng_mute;         /* per-track mute bitmask (editor-owned) */
#define eng_playing (eng_mode)

static unsigned char eng_tick;
unsigned char eng_gpos;                 /* groove row (GROOVE playhead) */
unsigned char eng_groove;               /* active groove number */
static unsigned char row_ticks;         /* this row's length (W overrides) */

static volatile struct _mikey_audio *const CHAN[NCH] = {
    &MIKEY.channel_a, &MIKEY.channel_b, &MIKEY.channel_c, &MIKEY.channel_d,
};

/* --- pitch --- */

/* effective pitch in 1/16 semis -> program sh_bkup (+ctl if clock moved) */
static void pitch_update(unsigned char ch)
{
    struct voice *v = &voices[ch];
    int p;
    unsigned char n, frac, b0, b1, clk, ctl;
    signed char vib = 0;

    if (v->type == IT_KIT || v->env_phase == 0)
        return;
    if (v->vib_depth) {
        unsigned char ph = v->vib_phase;
        /* triangle LFO, period 64 phase units, +/-16 peak */
        vib = (ph & 0x20) ? (signed char)(0x10 - (ph & 0x1F))
                          : (signed char)((ph & 0x1F) - 0x10);
        vib = (signed char)((vib * v->vib_depth) >> 4);
    }
    p = (int)v->base_note * 16 + v->bend + v->slide_off + vib
        + ((int)v->chord[v->chord_pos] + v->tbl_tsp) * 16;
    if (v->type == IT_WAV)
        p += 43 * 16;
    if (p < 16) p = 16;
    if (p > (v->type == IT_WAV ? 139 * 16 : 96 * 16))
        p = (v->type == IT_WAV ? 139 * 16 : 96 * 16);
    n = (unsigned char)(p >> 4);
    frac = (unsigned char)(p & 15);
    b0 = note_bkup[n - 1];
    clk = note_clock[n - 1];
    if (frac && n < 139 && note_clock[n] == clk) {
        b1 = note_bkup[n];
        b0 = b0 - (unsigned char)((((unsigned int)(b0 - b1)) * frac) >> 4);
    }
    ctl = (v->sh_ctl & (AUD_INTEGRATE | AUD_TAP7))
          | AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD | clk;
    if (b0 != v->sh_bkup || ctl != v->sh_ctl) {
        v->sh_bkup = b0;
        if (ctl != v->sh_ctl) {
            v->sh_ctl = ctl;
            v->retime = 1;      /* clock prescale moved: full reprogram */
        }
        v->dirty = 1;
    }
}

/* --- executor: one command, phrase or table column --- */

static void exec_cmd(unsigned char ch, unsigned char cmd, unsigned char param)
{
    struct voice *v = &voices[ch];

    switch (cmd) {
    case CMD_A:
        v->table = (param < NTABLES) ? param : EMPTY;
        v->tpos = 0;
        v->tbl_tsp = 0;
        break;
    case CMD_C:
        if (param) {
            v->chord[0] = 0;
            v->chord[1] = param >> 4;
            v->chord[2] = param & 0x0F;
            v->chord_len = 3;
        } else
            v->chord_len = 1;
        v->chord_pos = 0;
        break;
    case CMD_G:
        if (param < NGROOVES && sd.grooves[param][0])
            eng_groove = param;
        break;
    case CMD_K:
        v->kill_in = param;
        break;
    case CMD_O:
        v->sh_pan = param;
        (&MIKEY.attena)[ch] = param;
        break;
    case CMD_P:
        v->bend_rate = (signed char)param;
        break;
    case CMD_V:
        v->vib_speed = param >> 4;
        v->vib_depth = param & 0x0F;
        break;
    case CMD_W:
        if (param && param < row_ticks)
            row_ticks = param;
        break;
    case CMD_X:
        v->env_peak = (param <= 0x7F) ? param : 0x7F;
        if (v->env_phase == 2 || (v->env_phase == 1 && !v->e_atk))
            v->env_level = v->env_peak;
        v->dirty = 1;
        break;
    case CMD_F:
        v->bend += (signed char)param;
        break;
    case CMD_N: {
        /* live taps morph (D11): bits 0-5 = taps 0-5, 6 = tap 7,
         * 7 = tap 10; full reprogram reseeds the shifter */
        v->sh_feedback = (param & 0x3F) | ((param & 0x80) >> 1);
        if (param & 0x40)
            v->sh_ctl |= AUD_TAP7;
        else
            v->sh_ctl &= ~AUD_TAP7;
        v->retime = 1;
        v->dirty = 1;
        break;
    }
    case CMD_R:
        v->rt_rate = param & 0x0F;
        v->rt_fade = param >> 4;
        v->rt_cnt = 0;
        break;
    case CMD_S:
        *(volatile unsigned char *)0xFD1C = param;  /* TIM7BKUP live */
        break;
    /* CMD_H (table loop / phrase end), CMD_D and CMD_Z live in the peek */
    }
}

/* --- table macro sequencer: one row per tick --- */

static void table_step(unsigned char ch)
{
    struct voice *v = &voices[ch];
    struct tablerow *tr;

    if (v->table == EMPTY || v->env_phase == 0)
        return;
    tr = &sd.tables[v->table][v->tpos];
    if (tr->vol) {
        v->env_level = (tr->vol <= 0x7F) ? tr->vol : 0x7F;
        v->dirty = 1;
    }
    v->tbl_tsp = tr->tsp;
    if (tr->cmd == CMD_H) {
        v->tpos = tr->param & 0x0F;
        tr = &sd.tables[v->table][v->tpos];
        if (tr->vol) {
            v->env_level = (tr->vol <= 0x7F) ? tr->vol : 0x7F;
            v->dirty = 1;
        }
        v->tbl_tsp = tr->tsp;
    }
    if (tr->cmd && tr->cmd != CMD_H)
        exec_cmd(ch, tr->cmd, tr->param);
    if (v->tpos < PHRASE_ROWS - 1)
        ++v->tpos;                       /* stick at the last row */
}

/* --- trigger --- */

static void trigger(unsigned char ch, unsigned char note, unsigned char inum)
{
    struct voice *v = &voices[ch];
    struct instr *in = &sd.instrs[inum < NINSTR ? inum : 0];

    if (in->type == IT_KIT) {
        /* PCM: note semitone picks the kit slot; plays on channel D's DAC
         * regardless of track (mono sample bus, SMSGGDJ T3 policy). */
        pcm_play(((note - 1) % 12) & 7);
        v->type = IT_KIT;
        v->env_phase = 0;
        v->env_level = 0;
        v->dirty = 0;
        return;
    }
    v->base_note = note;
    v->inum = inum;
    v->type = in->type;
    v->env_peak = in->vol;
    v->e_atk = in->atk;
    v->e_dcy = in->dcy;
    v->hold_left = in->hold;
    if (in->atk) {
        v->env_phase = 1;
        v->env_level = 0;
    } else {
        v->env_phase = 2;
        v->env_level = in->vol;
    }
    /* one-shot effect state resets on every note (ported semantics) */
    v->bend = 0;
    v->bend_rate = 0;
    v->slide_off = 0;
    v->slide_rate = 0;
    v->vib_speed = v->vib_depth = v->vib_phase = 0;
    v->chord[0] = 0;
    v->chord_len = 1;
    v->chord_pos = 0;
    v->kill_in = EMPTY;
    v->rt_rate = 0;
    v->table = (in->table < NTABLES) ? in->table : EMPTY;
    v->tpos = 0;
    v->tbl_tsp = 0;

    if (in->type == IT_WAV) {
        v->sh_feedback = 0x80;          /* tap-11 triangle carrier */
        v->sh_ctl = AUD_INTEGRATE;
        v->sh_shiftlo = 0;
        v->sh_shifthi = 0;
    } else {
        /* raw tap mask (D11): user bits 0-5 = taps 0-5 -> FEEDBACK 0-5;
         * user bit 6 = tap 7 -> control bit 7; user bit 7 = tap 10 ->
         * FEEDBACK 6; user bit 8 = tap 11 -> FEEDBACK 7 */
        unsigned char lo = in->taps_lo;
        v->sh_feedback = (lo & 0x3F) | ((lo & 0x80) >> 1)
                         | ((in->taps_hi & 0x01) << 7);
        v->sh_ctl = (lo & 0x40) ? AUD_TAP7 : 0;
        v->sh_shiftlo = in->seed_lo;
        v->sh_shifthi = (in->seed_hi & 0x0F) << 4;  /* OTHER bits 7-4 */
    }
    v->sh_pan = in->pan;
    v->sh_bkup = 0;                     /* pitch_update programs it */
    pitch_update(ch);
    v->sh_ctl |= AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD;
    v->retime = 1;
    v->dirty = 1;
}

/* --- envelope / flush (unchanged discipline) --- */

static void envelope(unsigned char ch)
{
    struct voice *v = &voices[ch];
    unsigned char l = v->env_level;

    switch (v->env_phase) {
    case 1:
        if (l >= v->env_peak - v->e_atk) {
            l = v->env_peak;
            v->env_phase = 2;
        } else
            l += v->e_atk;
        break;
    case 2:
        if (v->hold_left == 0)
            v->env_phase = 3;
        else
            --v->hold_left;
        return;
    case 3:
        if (v->e_dcy == 0)
            return;
        if (l <= v->e_dcy) {
            l = 0;
            v->env_phase = 0;
        } else
            l -= v->e_dcy;
        break;
    default:
        return;
    }
    if (l != v->env_level) {
        v->env_level = l;
        v->dirty = 1;
    }
}

static void flush(unsigned char ch)
{
    volatile struct _mikey_audio *h = CHAN[ch];
    struct voice *v = &voices[ch];

    if (!v->dirty)
        return;
    v->dirty = 0;
    if (v->env_phase == 0 && v->env_level == 0) {
        h->control = 0;
        h->volume = 0;
        return;
    }
    if (v->retime) {
        v->retime = 0;
        h->control = 0;                 /* full reprogram (trigger/clock) */
        h->feedback = v->sh_feedback;
        h->dac = 0;
        h->shiftlo = v->sh_shiftlo;     /* LFSR seed, bits 7-0 */
        h->other = v->sh_shifthi;       /* seed bits 11-8 in bits 7-4 */
        h->count = v->sh_bkup;
        h->reload = v->sh_bkup;
        h->volume = (eng_mute & (1 << ch)) ? 0 : v->env_level;
        (&MIKEY.attena)[ch] = v->sh_pan;
        h->control = v->sh_ctl;
        return;
    }
    /* running update: reload + volume only — never restart the phase */
    h->reload = v->sh_bkup;
    h->volume = (eng_mute & (1 << ch)) ? 0 : v->env_level;
}

/* editor mute/solo: apply immediately, even mid-note */
void __fastcall__ engine_set_mute(unsigned char mask)
{
    unsigned char ch;
    eng_mute = mask;
    for (ch = 0; ch < NCH; ++ch)
        CHAN[ch]->volume = (mask & (1 << ch)) ? 0 : voices[ch].env_level;
}

/* --- walkers (M5, unchanged) --- */

static void walk_load_song(unsigned char ch, unsigned char row)
{
    struct walk *w = &eng_walk[ch];
    unsigned char tries = 0;

    w->active = 0;
    while (tries++ < 2) {
        for (; row < SONG_ROWS; ++row) {
            unsigned char cn = sd.song[row][ch];
            if (cn != EMPTY && cn < NCHAINS
                && sd.chains[cn][0].phrase != EMPTY) {
                w->song_row = row;
                w->chain = cn;
                w->cpos = 0;
                w->phrase = sd.chains[cn][0].phrase;
                w->tsp = sd.chains[cn][0].tsp;
                w->prow = 0;
                w->active = 1;
                return;
            }
            if (cn == EMPTY)
                break;
        }
        row = 0;
    }
}

static void walk_advance(unsigned char ch)
{
    struct walk *w = &eng_walk[ch];

    if (!w->active)
        return;
    if (++w->prow < PHRASE_ROWS)
        return;
    w->prow = 0;
    if (eng_mode == MODE_PHRASE)
        return;
    ++w->cpos;
    if (w->cpos < PHRASE_ROWS && sd.chains[w->chain][w->cpos].phrase != EMPTY) {
        w->phrase = sd.chains[w->chain][w->cpos].phrase;
        w->tsp = sd.chains[w->chain][w->cpos].tsp;
        return;
    }
    if (eng_mode == MODE_CHAIN) {
        w->cpos = 0;
        w->phrase = sd.chains[w->chain][0].phrase;
        w->tsp = sd.chains[w->chain][0].tsp;
        return;
    }
    walk_load_song(ch, w->song_row + 1);
}

/* 16-bit Galois LFSR, taps $B400 (the sibling's Z roll) */
static unsigned prng;
static unsigned char rand8(void)
{
    prng = (prng >> 1) ^ ((prng & 1) ? 0xB400 : 0);
    return (unsigned char)prng;
}

/* row start for one track: peek (D delay, Z roll, L capture, phrase H),
 * trigger, phrase command */
static void row_start(unsigned char ch)
{
    struct walk *w = &eng_walk[ch];
    struct voice *v = &voices[ch];
    struct step *s;
    unsigned char n;
    int prev_pitch;

    if (!w->active)
        return;
    s = &sd.phrases[w->phrase][w->prow];
    if (!s->note && !s->cmd)
        return;
    n = 0;
    if (s->note) {
        n = s->note + w->tsp;
        if (n < NOTE_MIN || n > NOTE_MAX)
            n = s->note;
    }
    if (s->cmd == CMD_Z && n && rand8() >= s->param)
        n = 0;                                  /* the roll failed */
    if (s->cmd == CMD_D && s->param) {          /* delayed trigger */
        if (n) {
            v->dly_in = s->param;
            v->dly_note = n;
            v->dly_instr = s->instr;
        }
        return;
    }
    prev_pitch = (int)v->base_note * 16 + v->bend + v->slide_off;
    if (n)
        trigger(ch, n, s->instr);
    if (s->cmd == CMD_L && n) {
        /* glide in from wherever the voice just was */
        v->slide_off = prev_pitch - (int)n * 16;
        v->slide_rate = s->param ? s->param : 1;
    } else if (s->cmd == CMD_H) {
        w->prow = PHRASE_ROWS - 1;              /* phrase ends after this row */
    } else if (s->cmd && s->cmd != CMD_D && s->cmd != CMD_Z)
        exec_cmd(ch, s->cmd, s->param);
}

void engine_stop(void)
{
    unsigned char ch;
    eng_mode = MODE_STOP;
    pcm_stop();
    for (ch = 0; ch < NCH; ++ch) {
        voices[ch].env_phase = 0;
        voices[ch].env_level = 0;
        voices[ch].dirty = 0;
        voices[ch].dly_in = EMPTY;
        eng_walk[ch].active = 0;
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
    }
}

static void play_common(void)
{
    eng_tick = 0;
    eng_gpos = 0;
    eng_groove = 0;
    row_ticks = sd.grooves[0][0] ? sd.grooves[0][0] : 6;
    prng ^= frames;                     /* Z roll: seed from play-start time */
    prng |= 1;                          /* never the LFSR zero state */
}

void engine_play_song(unsigned char row)
{
    unsigned char ch;
    engine_stop();
    for (ch = 0; ch < NCH; ++ch)
        walk_load_song(ch, row);
    play_common();
    eng_mode = MODE_SONG;
}

void engine_play_chain(unsigned char track, unsigned char chain)
{
    struct walk *w = &eng_walk[track];
    engine_stop();
    if (sd.chains[chain][0].phrase == EMPTY)
        return;
    w->chain = chain;
    w->cpos = 0;
    w->phrase = sd.chains[chain][0].phrase;
    w->tsp = sd.chains[chain][0].tsp;
    w->prow = 0;
    w->song_row = 0;
    w->active = 1;
    play_common();
    eng_mode = MODE_CHAIN;
}

void engine_play_phrase(unsigned char track, unsigned char phrase)
{
    struct walk *w = &eng_walk[track];
    engine_stop();
    w->chain = 0;
    w->cpos = 0;
    w->phrase = phrase;
    w->tsp = 0;
    w->prow = 0;
    w->song_row = 0;
    w->active = 1;
    play_common();
    eng_mode = MODE_PHRASE;
}

void engine_audition(unsigned char note, unsigned char inum)
{
    trigger(0, note, inum);
    flush(0);
}

void engine_tick(void)
{
    unsigned char ch;

    if (eng_playing && eng_tick == 0) {
        unsigned char g = sd.grooves[eng_groove][eng_gpos];
        row_ticks = g ? g : 6;
        for (ch = 0; ch < NCH; ++ch)
            row_start(ch);
    }

    for (ch = 0; ch < NCH; ++ch) {
        struct voice *v = &voices[ch];

        /* delayed trigger countdown (dly_note guards the boot BSS state) */
        if (v->dly_in != EMPTY && v->dly_note && v->dly_in-- == 0) {
            trigger(ch, v->dly_note, v->dly_instr);
            v->dly_in = EMPTY;
        }
        if (v->env_phase) {
            table_step(ch);
            if (v->bend_rate)
                v->bend += v->bend_rate;
            if (v->slide_off) {                  /* L: home in on the note */
                int r = (int)v->slide_rate;      /* explicit: cc65 compares
                                                    int vs uchar unsigned */
                if (v->slide_off > r)
                    v->slide_off -= r;
                else if (v->slide_off < -r)
                    v->slide_off += r;
                else
                    v->slide_off = 0;
            }
            if (v->vib_depth)
                v->vib_phase += v->vib_speed + 1;
            if (v->chord_len > 1
                && ++v->chord_pos >= v->chord_len)
                v->chord_pos = 0;
            if (v->rt_rate && ++v->rt_cnt >= v->rt_rate) {
                v->rt_cnt = 0;                   /* R: re-fire the note */
                if (v->type == IT_KIT)
                    pcm_play(((v->base_note - 1) % 12) & 7);
                else {
                    unsigned char fade = v->rt_fade << 3;
                    v->env_peak = (v->env_peak > fade)
                                  ? v->env_peak - fade : 0;
                    if (v->e_atk) {
                        v->env_phase = 1;
                        v->env_level = 0;
                    } else {
                        v->env_phase = 2;
                        v->env_level = v->env_peak;
                    }
                    v->hold_left = sd.instrs[v->inum < NINSTR
                                             ? v->inum : 0].hold;
                    v->dirty = 1;
                }
            }
            pitch_update(ch);
            envelope(ch);
            if (v->kill_in != EMPTY) {
                if (v->kill_in == 0) {
                    v->env_phase = 0;
                    v->env_level = 0;
                    v->kill_in = EMPTY;
                    v->dirty = 1;
                } else
                    --v->kill_in;
            }
        }
        flush(ch);
    }

    if (!eng_playing)
        return;

    if (++eng_tick >= row_ticks) {
        eng_tick = 0;
        if (++eng_gpos >= 16 || sd.grooves[eng_groove][eng_gpos] == 0)
            eng_gpos = 0;
        for (ch = 0; ch < NCH; ++ch)
            walk_advance(ch);
    }
}
