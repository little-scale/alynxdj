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
 * Pitch model: 1/16-semitone fixed point. base note (chain + instrument
 * transpose applied) + chord/table semitone offsets + bend/vibrato ->
 * table lookup with linear BACKUP interpolation; per-tick pitch updates
 * write the reload register only, so the oscillator phase never restarts.
 */
#include <lynx.h>
#include <string.h>

#include "tracker.h"
#include "../build/notes.h"

#define AUD_ENABLE_COUNT  0x10
#define AUD_ENABLE_RELOAD 0x08
#define AUD_INTEGRATE     0x20
#define AUD_TAP7          0x80          /* control bit 7 enables LFSR tap 7 */

/* the flat song block lives in HIRAM ($C900, SONG segment) — MAIN is full */
#pragma bss-name (push, "SONG")
struct songdata sd;
struct walk eng_walk[NCH];
#pragma bss-name (pop)

extern volatile unsigned int frames;    /* VBL counter (irq.s) */

struct voice {
    unsigned char base_note;    /* chain/instrument-transposed trigger note */
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
    unsigned char trm_speed, trm_depth, trm_phase;
    unsigned int  tap_cur;      /* G: current 9-bit tap value being swept */
    signed char   tap_rate;     /* G: signed per-tick tap step (0 = off) */
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
    unsigned char dac_slot;     /* 0/1 for KIT or table-WAV, $FF otherwise */
    unsigned char dirty, retime;
    unsigned char fb_dirty;     /* G: feedback changed live (no reseed) */
};

#pragma bss-name (push, "SONG")
static struct voice voices[NCH];
#pragma bss-name (pop)
/* Per-phrase pass counts use the otherwise-free upper debug/mirror page.
 * This releases 64 bytes of HIRAM for the sine-LFO cold overlay. */
#pragma bss-name (push, "MIRRORRAM")
static unsigned char play_cnt[NPHRASES];
#pragma bss-name (pop)
/* The 16-byte EEPROM-buffer tail is also free (the payload ends at $C8EF). */
#pragma bss-name (push, "TAILRAM")
static unsigned char dac_owner[NDAC];      /* slot -> logical/physical track */
static unsigned dac_stamp[NDAC];           /* oldest-voice stealing (D6) */
static unsigned dac_clock;
#pragma bss-name (pop)

/* ATK/DCY nibble -> per-tick level step. Time-semantic: higher nibble =
 * longer stage (~ticks: -,1,2,3,4,5,6,8,11,16,21,26,32,43,64,127 =
 * 17 ms .. 2.1 s at the 60 Hz tick). Index 0 = instant attack / sustain. */
const unsigned char env_rate[16] = {
    0, 127, 64, 43, 32, 26, 22, 16, 12, 8, 6, 5, 4, 3, 2, 1,
};

unsigned char eng_mode;
unsigned char eng_mute;         /* per-track mute bitmask (editor-owned) */
unsigned char eng_level[NCH];   /* live per-track levels (meters) */
unsigned char live_q[NCH];      /* LIVE: queued chain, $FF none, $FE stop */
static unsigned char live_bar;  /* global 16-row bar counter (LIVE grid) */
#define eng_playing (eng_mode)

/* Stable harness mirrors for the symmetric-DAC regression test. */
#define MIRROR_DAC_COUNT  (*(volatile unsigned char *)0xC022)
#define MIRROR_DAC_OWNER0 (*(volatile unsigned char *)0xC023)
#define MIRROR_DAC_OWNER1 (*(volatile unsigned char *)0xC024)
#define MIRROR_DAC_S_SLOT (*(volatile unsigned char *)0xC025)
#define MIRROR_DAC_S_RATE (*(volatile unsigned char *)0xC026)
#define MIRROR_TRIG0      (*(volatile unsigned char *)0xC02B)
#define MIRROR_TRIG1      (*(volatile unsigned char *)0xC02C)
#define MIRROR_DAC_TRACE  ((volatile unsigned char *)0xC030)

static unsigned char eng_tick;
unsigned char eng_gpos;                 /* groove row (GROOVE playhead) */
unsigned char eng_groove;               /* active groove number */
static unsigned char row_ticks;         /* this row's length (W overrides) */

static volatile struct _mikey_audio *const CHAN[NCH] = {
    &MIKEY.channel_a, &MIKEY.channel_b, &MIKEY.channel_c, &MIKEY.channel_d,
};

/* --- symmetric DAC ownership (D1/D5/D6) --- */

#pragma code-name (push, "HICODE2")

static void dac_release(unsigned char ch)
{
    struct voice *v = &voices[ch];
    unsigned char slot = v->dac_slot;

    if (slot >= NDAC)
        return;
    dac_stop(slot);
    pool_cancel(slot);
    if (dac_owner[slot] == ch)
        dac_owner[slot] = EMPTY;
    v->dac_slot = EMPTY;
    MIRROR_DAC_OWNER0 = dac_owner[0];
    MIRROR_DAC_OWNER1 = dac_owner[1];
}

/* Reuse this track's slot, take a free one, or steal the oldest. */
static unsigned char dac_acquire(unsigned char ch)
{
    unsigned char slot, old;

    /* A completed one-shot no longer consumes the two-voice budget.  A
     * deferred trigger reserves its slot by setting DAC_SAMPLE, so it is
     * not mistaken for a free voice before the main-loop cart pump runs. */
    for (slot = 0; slot < NDAC; ++slot) {
        old = dac_owner[slot];
        if (old < NCH && dac_mode[slot] == DAC_NONE) {
            voices[old].dac_slot = EMPTY;
            dac_owner[slot] = EMPTY;
        }
    }

    if (voices[ch].dac_slot < NDAC)
        slot = voices[ch].dac_slot;
    else if (dac_owner[0] == EMPTY)
        slot = 0;
    else if (dac_owner[1] == EMPTY)
        slot = 1;
    else
        slot = (dac_stamp[0] <= dac_stamp[1]) ? 0 : 1;

    old = dac_owner[slot];
    if (old < NCH && old != ch) {
        voices[old].dac_slot = EMPTY;
        voices[old].env_phase = 0;
        voices[old].env_level = 0;
        voices[old].dirty = 0;
    }
    dac_stop(slot);
    pool_cancel(slot);
    dac_owner[slot] = ch;
    voices[ch].dac_slot = slot;
    if (++dac_clock == 0) {
        /* The newly acquired slot is newest; renormalize at the 16-bit
         * wrap so a long live set cannot immediately steal it back. */
        dac_clock = 1;
        dac_stamp[slot ^ 1] = 0;
    }
    dac_stamp[slot] = dac_clock;
    dac_off[slot] = ch << 3;
    dac_muted[slot] = (eng_mute >> ch) & 1;
    {
        unsigned char i = MIRROR_DAC_COUNT;
        if (i < 8) {
            volatile unsigned char *p = MIRROR_DAC_TRACE + i + i + i;
            *p++ = ch;
            *p++ = slot;
            *p = dac_off[slot];
            MIRROR_DAC_COUNT = i + 1;
        }
    }
    MIRROR_DAC_OWNER0 = dac_owner[0];
    MIRROR_DAC_OWNER1 = dac_owner[1];
    return slot;
}

#pragma code-name (pop)

/* --- pitch --- */

/* Sixteen signed samples are sufficient because pitch itself resolves to
 * 1/16 semitone and the 59.9 Hz engine supplies at least eight updates even
 * at the fastest setting.  Starting at zero avoids a pitch jump on key-on. */
#pragma rodata-name (push, "HICODE3")
static const signed char vib_sine[16] = {
     0,  6, 11, 15, 16, 15, 11,  6,
     0, -6,-11,-15,-16,-15,-11, -6,
};
#pragma rodata-name (pop)

#pragma code-name (push, "HICODE3")
static signed char vibrato_value(struct voice *v)
{
    signed char s = vib_sine[v->vib_phase >> 4];
    unsigned char mag = (s < 0) ? (unsigned char)-s : (unsigned char)s;

    /* Round the magnitude before restoring the sign.  This keeps shallow
     * vibrato centred; a signed right shift makes the negative half one
     * pitch unit deeper than the positive half. */
    mag = (unsigned char)((((unsigned)mag * v->vib_depth) + 8) >> 4);
    return (s < 0) ? -(signed char)mag : (signed char)mag;
}

static unsigned char transpose_note(unsigned char note, signed char tsp)
{
    unsigned char d;

    if (tsp < 0) {
        d = (unsigned char)-tsp;
        return (note > d) ? note - d : NOTE_MIN;
    }
    d = (unsigned char)tsp;
    return (d <= NOTE_MAX - note) ? note + d : NOTE_MAX;
}
#pragma code-name (pop)

/* effective pitch in 1/16 semis -> program sh_bkup (+ctl if clock moved) */
static void pitch_update(unsigned char ch)
{
    struct voice *v = &voices[ch];
    int p;
    unsigned char n, frac, b0, b1, clk, ctl;
    signed char vib = 0;

    if (v->type == IT_KIT || v->dac_slot < NDAC || v->env_phase == 0)
        return;
    if (v->vib_depth)
        vib = vibrato_value(v);
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

/* Program a 9-bit tap value (D11 user layout) into the feedback shadow and
 * remember it as the sweep base. Full reprogram (retime) reseeds — same path
 * as an N override. Used by trigger, N, and the G sweep. */
static void set_taps(struct voice *v, unsigned taps9)
{
    unsigned char lo = (unsigned char)taps9;
    v->tap_cur = taps9;
    v->sh_feedback = (lo & 0x3F) | ((lo & 0x80) >> 1)
                     | (((taps9 >> 8) & 1) << 7);
    if (lo & 0x40)
        v->sh_ctl |= AUD_TAP7;
    else
        v->sh_ctl &= ~AUD_TAP7;
    v->retime = 1;
    v->dirty = 1;
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
    case CMD_G:                     /* Glide: signed per-tick tap sweep */
        v->tap_rate = (signed char)param;
        break;
    case CMD_B:                     /* set the WAV wavetable live (0-7) */
        if (v->type == IT_WAV && v->dac_slot < NDAC && param < 8
            && v->base_note >= 1
            && v->base_note <= 96) {
            wave_start(v->dac_slot, param);
            wave_rate(v->dac_slot, wave_clock[v->base_note - 1],
                      wave_bkup[v->base_note - 1],
                      wave_step[v->base_note - 1]);
        }
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
    case CMD_N:
        /* absolute taps override (D11): bits 0-5 = taps 0-5, 6 = tap 7,
         * 7 = tap 10 (8-bit, so no tap 11); also seeds the G sweep base */
        set_taps(v, param);
        break;
    case CMD_R:
        v->rt_rate = param & 0x0F;
        v->rt_fade = param >> 4;
        v->rt_cnt = 0;
        break;
    case CMD_S:
        if (v->dac_slot < NDAC) {
            dac_rate_set(v->dac_slot, param);
            MIRROR_DAC_S_SLOT = v->dac_slot;
            MIRROR_DAC_S_RATE = param;
        }
        break;
    case CMD_E:
        v->e_atk = env_rate[param >> 4];
        v->e_dcy = env_rate[param & 0x0F];
        break;
    case CMD_T:
        if (param) {
            unsigned char t = (unsigned char)(899u / param);
            unsigned char i;
            if (t < 1) t = 1;
            if (t > 15) t = 15;
            sd.grooves[eng_groove][0] = t;
            sd.grooves[eng_groove][1] = t;
            for (i = 2; i < 16; ++i)
                sd.grooves[eng_groove][i] = 0;
        }
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
    unsigned char slot;

    /* Instrument transpose is resolved once at key-on, before a KIT note is
     * mapped to a pad and before either WAV pitch path is selected. */
    note = transpose_note(note, in->tsp);

    if (in->type == IT_KIT) {
        /* Every track owns its physical channel's DAC.  Two timer-fed
         * streams may run at once; the third trigger steals the oldest. */
        slot = dac_acquire(ch);
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
        CHAN[ch]->dac = 0;
        v->type = IT_KIT;
        v->inum = inum;
        v->base_note = note;
        v->env_phase = 0;
        v->env_level = 0;
        v->kill_in = EMPTY;
        v->rt_rate = v->rt_fade = v->rt_cnt = 0;
        v->sh_pan = in->pan;
        (&MIKEY.attena)[ch] = in->pan;
        v->dirty = 0;
        dac_rate_set(slot, 127);         /* S on this row may override it */
        pool_trigger(slot, in->wave < 8 ? in->wave : 0,
                     ((note - 1) % 12) & 7);
        return;
    }

    if (v->dac_slot < NDAC)
        dac_release(ch);
    v->base_note = note;
    v->inum = inum;
    v->type = in->type;
    v->env_peak = in->vol;
    v->e_atk = env_rate[in->env >> 4];
    v->e_dcy = env_rate[in->env & 0x0F];
    v->hold_left = in->hold & 0x0F;
    if (in->env & 0xF0) {
        v->env_phase = 1;
        v->env_level = 0;
    } else {
        v->env_phase = 2;
        v->env_level = in->vol;
    }
    /* One-shot effect values reset on every note.  Vibrato phase is the
     * exception: it free-runs across retriggers so short notes do not keep
     * sampling the same sharp half-cycle.  stop_nolock resets it at the
     * transport boundary. */
    v->bend = 0;
    /* Patch SWP follows the sibling trackers' period-direction convention:
     * positive values fall, negative values rise.  Row/table P keeps its
     * established direct signed-pitch convention and overrides this rate. */
    v->bend_rate = (in->type <= IT_NOISE) ? -in->swp : 0;
    v->slide_off = 0;
    v->slide_rate = 0;
    if (in->type <= IT_NOISE) {
        v->vib_speed = in->vib >> 4;
        v->vib_depth = in->vib & 0x0F;
        v->trm_speed = in->trm >> 4;
        v->trm_depth = in->trm & 0x0F;
    } else
        v->vib_speed = v->vib_depth = v->trm_speed = v->trm_depth = 0;
    v->trm_phase = 0;
    v->tap_rate = 0;                    /* G sweep off until a G command */
    v->chord[0] = 0;
    v->chord_len = 1;
    v->chord_pos = 0;
    v->kill_in = EMPTY;
    v->rt_rate = 0;
    v->table = (in->table < NTABLES) ? in->table : EMPTY;
    v->tpos = 0;
    v->tbl_tsp = 0;

    if (in->type == IT_WAV && in->wave < 8) {
        /* Table-WAV shares the same symmetric two-DAC budget as KIT.
         * Its envelope gates note length; samples remain full amplitude. */
        slot = dac_acquire(ch);
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
        CHAN[ch]->dac = 0;
        wave_start(slot, in->wave);
        wave_rate(slot, wave_clock[note - 1], wave_bkup[note - 1],
                  wave_step[note - 1]);
        v->sh_pan = in->pan;
        (&MIKEY.attena)[ch] = in->pan;
        v->dirty = 0;
        return;
    }
    if (in->type == IT_WAV) {
        v->sh_feedback = 0x80;          /* tap-11 triangle carrier */
        v->sh_ctl = AUD_INTEGRATE;
        v->sh_shiftlo = 0;
        v->sh_shifthi = 0;
    } else {
        /* raw tap mask (D11): user bits 0-5 = taps 0-5 -> FEEDBACK 0-5;
         * user bit 6 = tap 7 -> control bit 7; user bit 7 = tap 10 ->
         * FEEDBACK 6; user bit 8 = tap 11 -> FEEDBACK 7.
         * taps 0 = frozen shifter = silence — never useful, and old saves
         * (pre-D11 timbre byte) land here: fall back to the square. */
        unsigned char lo = in->taps_lo;
        if (!lo && !(in->taps_hi & 1))
            lo = TAPS_SQUARE;
        v->sh_feedback = (lo & 0x3F) | ((lo & 0x80) >> 1)
                         | ((in->taps_hi & 0x01) << 7);
        v->sh_ctl = (lo & 0x40) ? AUD_TAP7 : 0;
        v->tap_cur = lo | ((in->taps_hi & 0x01) << 8);  /* G sweep base */
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

/* TONE/NOISE tremolo is a one-directional dip inside the AHD envelope.
 * A 6-bit triangle and the 0-F depth produce up to 116 Lynx volume steps;
 * depth F therefore reaches near-silence without ever exceeding the peak. */
#pragma code-name (push, "HICODE2")
static unsigned char tone_level(struct voice *v)
{
    unsigned char level = v->env_level;

    if (v->trm_depth) {
        unsigned char tri = v->trm_phase & 0x1F;
        unsigned char dip;
        if (v->trm_phase & 0x20)
            tri = 0x1F - tri;
        dip = (unsigned char)(((unsigned)tri * v->trm_depth) >> 2);
        level = (level > dip) ? level - dip : 0;
    }
    return level;
}
#pragma code-name (pop)

static void flush(unsigned char ch)
{
    volatile struct _mikey_audio *h = CHAN[ch];
    struct voice *v = &voices[ch];

    if (v->dac_slot < NDAC) {
        v->dirty = 0;
        return;
    }
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
        v->fb_dirty = 0;                /* full reprogram writes feedback */
        h->control = 0;                 /* full reprogram (trigger/clock) */
        h->feedback = v->sh_feedback;
        h->dac = 0;
        h->shiftlo = v->sh_shiftlo;     /* LFSR seed, bits 7-0 */
        h->other = v->sh_shifthi;       /* seed bits 11-8 in bits 7-4 */
        h->count = v->sh_bkup;
        h->reload = v->sh_bkup;
        h->volume = (eng_mute & (1 << ch)) ? 0 : tone_level(v);
        (&MIKEY.attena)[ch] = v->sh_pan;
        h->control = v->sh_ctl;
        return;
    }
    /* running update: never restart the phase. G sweep writes FEEDBACK
     * live (taps change without touching the shift register = no reseed) */
    if (v->fb_dirty) {
        v->fb_dirty = 0;
        h->feedback = v->sh_feedback;
    }
    h->reload = v->sh_bkup;
    h->volume = (eng_mute & (1 << ch)) ? 0 : tone_level(v);
}

/* editor mute/solo: apply immediately, even mid-note */
#pragma code-name (push, "HICODE2")
void __fastcall__ engine_set_mute(unsigned char mask)
{
    unsigned char ch, slot;
    eng_mute = mask;
    for (ch = 0; ch < NCH; ++ch)
        CHAN[ch]->volume = ((mask & (1 << ch))
                            || voices[ch].dac_slot < NDAC)
                           ? 0 : tone_level(&voices[ch]);
    for (slot = 0; slot < NDAC; ++slot)
        dac_muted[slot] = (dac_owner[slot] < NCH
                           && (mask & (1 << dac_owner[slot]))) ? 1 : 0;
}
#pragma code-name (pop)

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
    if (eng_mode == MODE_LIVE && live_q[ch] != EMPTY) {
        unsigned char q = live_q[ch];            /* phrase boundary: launch */
        live_q[ch] = EMPTY;
        if (q == 0xFE || q >= NCHAINS || sd.chains[q][0].phrase == EMPTY) {
            w->active = 0;                       /* queued stop */
            voices[ch].env_phase = 0;
            voices[ch].env_level = 0;
            voices[ch].dirty = 1;
            return;
        }
        w->chain = q;
        w->cpos = 0;
        w->phrase = sd.chains[q][0].phrase;
        w->tsp = sd.chains[q][0].tsp;
        return;
    }
    ++w->cpos;
    if (w->cpos < PHRASE_ROWS && sd.chains[w->chain][w->cpos].phrase != EMPTY) {
        w->phrase = sd.chains[w->chain][w->cpos].phrase;
        w->tsp = sd.chains[w->chain][w->cpos].tsp;
        return;
    }
    if (eng_mode == MODE_CHAIN || eng_mode == MODE_LIVE) {
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
    if (w->prow == PHRASE_ROWS - 1)             /* last row: this pass is
                                                   done — count it (I/J) */
        ++play_cnt[w->phrase];
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
    if (s->cmd == CMD_I && n
        && !(s->param & (1 << (play_cnt[w->phrase] & 7))))
        n = 0;                                  /* not this pass */
    if (s->cmd == CMD_J && n
        && (s->param & (1 << (play_cnt[w->phrase] & 3)))) {
        unsigned char x = s->param >> 4;
        int jn = (int)n + ((x < 8) ? (int)x : (int)x - 16);
        if (jn >= NOTE_MIN && jn <= NOTE_MAX)
            n = (unsigned char)jn;
    }
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
        v->slide_off = prev_pitch - (int)v->base_note * 16;
        v->slide_rate = s->param ? s->param : 1;
    } else if (s->cmd == CMD_H) {
        w->prow = PHRASE_ROWS - 1;              /* phrase ends after this row */
    } else if (s->cmd && s->cmd != CMD_D && s->cmd != CMD_Z
               && s->cmd != CMD_I && s->cmd != CMD_J)
        exec_cmd(ch, s->cmd, s->param);
}

static void stop_nolock(void)
{
    unsigned char ch, slot;
    if (eng_mode)
        sync_tx(SYNC_OP_STOP);
    eng_mode = MODE_STOP;
    pcm_stop();
    for (slot = 0; slot < NDAC; ++slot) {
        pool_cancel(slot);
        dac_owner[slot] = EMPTY;
    }
    MIRROR_DAC_OWNER0 = MIRROR_DAC_OWNER1 = EMPTY;
    live_q[0] = live_q[1] = live_q[2] = live_q[3] = EMPTY;
    for (ch = 0; ch < NCH; ++ch) {
        voices[ch].env_phase = 0;
        voices[ch].env_level = 0;
        voices[ch].dac_slot = EMPTY;
        voices[ch].dirty = 0;
        voices[ch].dly_in = EMPTY;
        voices[ch].vib_phase = 0;       /* deterministic transport-start LFO */
        eng_walk[ch].active = 0;
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
    }
}

static void play_common(void)
{
    memset(play_cnt, 0, sizeof(play_cnt));  /* I/J counts reset (ported) */
    eng_tick = 0;
    eng_gpos = 0;
    eng_groove = 0;
    row_ticks = sd.grooves[0][0] ? sd.grooves[0][0] : 6;
    MIRROR_DAC_COUNT = 0;
    MIRROR_DAC_S_SLOT = EMPTY;
    MIRROR_DAC_S_RATE = 0;
    MIRROR_TRIG0 = MIRROR_TRIG1 = 0;
    prng ^= frames;                     /* Z roll: seed from play-start time */
    prng |= 1;                          /* never the LFSR zero state */
}

/* the engine tick runs in the VBlank IRQ: every main-thread mutation of
 * engine state sits inside a brief IRQ-masked window */
void engine_stop(void)
{
    __asm__("sei");
    stop_nolock();
    __asm__("cli");
}

void engine_play_song(unsigned char row)
{
    unsigned char ch;
    __asm__("sei");
    stop_nolock();
    for (ch = 0; ch < NCH; ++ch)
        walk_load_song(ch, row);
    play_common();
    eng_mode = MODE_SONG;
    sync_tx(SYNC_OP_START);
    __asm__("cli");
}

void engine_play_chain(unsigned char track, unsigned char chain)
{
    struct walk *w = &eng_walk[track];
    __asm__("sei");
    stop_nolock();
    if (sd.chains[chain][0].phrase == EMPTY) {
        __asm__("cli");
        return;
    }
    w->chain = chain;
    w->cpos = 0;
    w->phrase = sd.chains[chain][0].phrase;
    w->tsp = sd.chains[chain][0].tsp;
    w->prow = 0;
    w->song_row = 0;
    w->active = 1;
    play_common();
    eng_mode = MODE_CHAIN;
    __asm__("cli");
}

void engine_play_phrase(unsigned char track, unsigned char phrase)
{
    struct walk *w = &eng_walk[track];
    __asm__("sei");
    stop_nolock();
    w->chain = 0;
    w->cpos = 0;
    w->phrase = phrase;
    w->tsp = 0;
    w->prow = 0;
    w->song_row = 0;
    w->active = 1;
    play_common();
    eng_mode = MODE_PHRASE;
    __asm__("cli");
}

/* LIVE: queue a chain on a track; launches at its next phrase boundary.
 * From stopped, the first queue starts the engine on that track. */
void __fastcall__ engine_live_queue(unsigned char track, unsigned char chain)
{
    struct walk *w = &eng_walk[track];

    __asm__("sei");
    if (eng_mode != MODE_LIVE) {
        stop_nolock();
        play_common();
        eng_mode = MODE_LIVE;
    }
    if (!w->active && !eng_walk[0].active && !eng_walk[1].active
        && !eng_walk[2].active && !eng_walk[3].active
        && chain != 0xFE && chain < NCHAINS
        && sd.chains[chain][0].phrase != EMPTY) {
        w->chain = chain;                /* first launch defines the grid */
        w->cpos = 0;
        w->phrase = sd.chains[chain][0].phrase;
        w->tsp = sd.chains[chain][0].tsp;
        w->prow = 0;
        w->active = 1;
        live_bar = 0;
        live_q[track] = EMPTY;
    } else
        live_q[track] = chain;
    __asm__("cli");
}

#pragma code-name (push, "HICODE2")
void engine_audition(unsigned char note, unsigned char inum)
{
    __asm__("sei");
    trigger(0, note, inum);
    flush(0);
    __asm__("cli");
}

/* prelisten a phrase row's command too, so editing C/V/P/N... is audible */
void __fastcall__ engine_audition_cmd(unsigned char cmd, unsigned char param)
{
    if (cmd && cmd != CMD_D && cmd != CMD_Z && cmd != CMD_H && cmd != CMD_L) {
        __asm__("sei");
        exec_cmd(0, cmd, param);
        __asm__("cli");
    }
}
#pragma code-name (pop)

/* HIRAM segments are NOT cleared by cc65's zerobss — call once at boot */
#pragma code-name (push, "HICODE2")
void engine_init(void)
{
    unsigned char slot;

    memset(voices, 0, sizeof(voices));
    memset(eng_walk, 0, sizeof(eng_walk));
    for (slot = 0; slot < NDAC; ++slot) {
        dac_owner[slot] = EMPTY;
        dac_off[slot] = slot << 3;
        dac_rate[slot] = 127;
    }
    stop_nolock();
}
#pragma code-name (pop)

void engine_tick(void)
{
    unsigned char ch;
    /* snapshot + clear the IRQ DAC peaks for KIT/WAV meters (this frame) */
    unsigned char dac_lvl0 = dac_peak[0], dac_lvl1 = dac_peak[1];
    dac_peak[0] = 0;
    dac_peak[1] = 0;

    /* sync slave: rows are granted by received ROW bytes, not the groove */
    if (eng_playing && sync_mode == SYNC_IN && eng_tick && sync_row_pending) {
        --sync_row_pending;
        for (ch = 0; ch < NCH; ++ch)
            walk_advance(ch);
        eng_tick = 0;
    }

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
        /* KIT has no volume envelope, so retrigger must not be gated by
         * env_phase.  Table-WAV and hardware voices retain the old gate. */
        if (v->rt_rate && (v->env_phase || v->dac_slot < NDAC)
            && ++v->rt_cnt >= v->rt_rate) {
            v->rt_cnt = 0;
            if (v->type == IT_KIT && v->dac_slot < NDAC) {
                unsigned char kb = sd.instrs[v->inum < NINSTR
                                             ? v->inum : 0].wave;
                pool_trigger(v->dac_slot, kb < 8 ? kb : 0,
                             ((v->base_note - 1) % 12) & 7);
            } else {
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
                                         ? v->inum : 0].hold & 0x0F;
                v->dirty = 1;
            }
        }
        if (v->env_phase) {
            table_step(ch);
            if (v->tap_rate) {                   /* G: live tap sweep (D11) */
                unsigned char lo;
                v->tap_cur = (v->tap_cur + v->tap_rate) & 0x1FF;
                lo = (unsigned char)v->tap_cur;
                v->sh_feedback = (lo & 0x3F) | ((lo & 0x80) >> 1)
                                 | (((v->tap_cur >> 8) & 1) << 7);
                v->fb_dirty = 1;                 /* write feedback, keep the shifter */
                v->dirty = 1;
            }
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
                /* 8-bit phase: 2..32/256 cycle per tick = ~0.5..7.5 Hz. */
                v->vib_phase += (v->vib_speed + 1) << 1;
            if (v->trm_depth) {
                v->trm_phase += v->trm_speed + 1;
                v->dirty = 1;                    /* volume changes every tick */
            }
            if (v->chord_len > 1
                && ++v->chord_pos >= v->chord_len)
                v->chord_pos = 0;
            pitch_update(ch);
            envelope(ch);
        }
        /* K also applies to envelope-less KIT samples. */
        if (v->kill_in != EMPTY
            && (v->env_phase || v->dac_slot < NDAC)) {
            if (v->kill_in == 0) {
                v->env_phase = 0;
                v->env_level = 0;
                v->kill_in = EMPTY;
                if (v->dac_slot < NDAC)
                    dac_release(ch);
                v->dirty = 1;
            } else
                --v->kill_in;
        }
        if (v->type == IT_WAV && v->dac_slot < NDAC
            && v->env_phase == 0) {
            dac_release(ch);
        }
        flush(ch);
        /* meter source: TONE/NOISE use the envelope (it scales VOLUME);
         * KIT/WAV play the DAC full-amplitude, so use the real DAC peak */
        {
            unsigned char lvl;
            if (v->dac_slot == 0)
                lvl = dac_lvl0;
            else if (v->dac_slot == 1)
                lvl = dac_lvl1;
            else
                lvl = tone_level(v);
            eng_level[ch] = (eng_mute & (1 << ch)) ? 0 : lvl;
        }
    }

    if (!eng_playing)
        return;

    if (sync_mode == SYNC_IN) {         /* slave: hold until the next clock */
        eng_tick = 1;
        return;
    }

    if (++eng_tick >= row_ticks) {
        eng_tick = 0;
        if (++eng_gpos >= 16 || sd.grooves[eng_groove][eng_gpos] == 0)
            eng_gpos = 0;
        for (ch = 0; ch < NCH; ++ch)
            walk_advance(ch);
        if (eng_mode == MODE_LIVE && ++live_bar >= PHRASE_ROWS) {
            live_bar = 0;
            for (ch = 0; ch < NCH; ++ch) {   /* idle tracks launch on the bar */
                struct walk *w = &eng_walk[ch];
                unsigned char q = live_q[ch];
                if (!w->active && q != EMPTY) {
                    live_q[ch] = EMPTY;
                    if (q != 0xFE && q < NCHAINS
                        && sd.chains[q][0].phrase != EMPTY) {
                        w->chain = q;
                        w->cpos = 0;
                        w->phrase = sd.chains[q][0].phrase;
                        w->tsp = sd.chains[q][0].tsp;
                        w->prow = 0;
                        w->active = 1;
                    }
                }
            }
        }
        sync_tx(SYNC_OP_ROW);
    }
}
