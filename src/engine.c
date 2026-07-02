/* Sequencer engine — per-tick pipeline ported from SMSGGDJ engine.asm:
 *
 *   groove -> row advance -> trigger -> AHD envelope -> shadow -> flush
 *
 * C-first per DESIGN.md D2; the tick currently runs from the main loop on
 * VBlank frame change (not inside the IRQ) — same rate, render-bounded
 * jitter. It moves into the IRQ when the driver goes asm.
 *
 * M3 scope: voice 0 only, one phrase looping, one groove. The data shapes
 * (channel array, 4-byte steps, tick pipeline order) are the ported,
 * load-bearing part.
 */
#include <lynx.h>

#include "../build/notes.h"

#define AUD_ENABLE_COUNT  0x10
#define AUD_ENABLE_RELOAD 0x08

#define NCH 4

#define PHRASE_ROWS 16

/* phrase step, ported layout: note, instr, cmd, param */
struct step {
    unsigned char note;     /* 0 = empty, 1..96 = C-1..B-8 */
    unsigned char instr;
    unsigned char cmd;
    unsigned char param;
};

struct voice {
    /* current note */
    unsigned char note;
    /* AHD envelope: phase 0=off 1=attack 2=hold 3=decay */
    unsigned char env_phase;
    unsigned char env_level;    /* 0..$7F current */
    unsigned char env_peak;     /* attack target */
    unsigned char hold_left;    /* hold ticks remaining */
    /* Mikey shadow (flush writes only deltas) */
    unsigned char sh_vol, sh_feedback, sh_bkup, sh_ctl;
    unsigned char dirty;
};

/* M3 hardcoded instrument */
#define ENV_ATTACK  64          /* level += per tick until peak */
#define ENV_HOLD    2           /* ticks at peak */
#define ENV_DECAY   32          /* level -= per tick */

static struct voice voices[NCH];

struct step phrase[PHRASE_ROWS];        /* demo phrase, RAM (editable later) */

static const unsigned char groove[2] = {6, 6};  /* ticks per row, LSDJ default */

unsigned char eng_playing;
unsigned char eng_row;                  /* current phrase row */
static unsigned char eng_tick;          /* tick within row */
static unsigned char eng_gpos;          /* groove index */

static volatile struct _mikey_audio *const CHAN[NCH] = {
    &MIKEY.channel_a, &MIKEY.channel_b, &MIKEY.channel_c, &MIKEY.channel_d,
};

static void trigger(unsigned char ch, unsigned char note)
{
    struct voice *v = &voices[ch];
    v->note = note;
    v->env_phase = 1;
    v->env_level = 0;
    v->env_peak = 0x7F;
    v->hold_left = ENV_HOLD;
    v->sh_feedback = 0x01;                       /* square (M2 recipe) */
    v->sh_bkup = note_bkup[note - 1];
    v->sh_ctl = AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD | note_clock[note - 1];
    v->dirty = 1;
}

static void envelope(unsigned char ch)
{
    struct voice *v = &voices[ch];
    unsigned char l = v->env_level;

    switch (v->env_phase) {
    case 1:                                      /* attack */
        if (l >= v->env_peak - ENV_ATTACK) {
            l = v->env_peak;
            v->env_phase = 2;
        } else
            l += ENV_ATTACK;
        break;
    case 2:                                      /* hold */
        if (v->hold_left == 0)
            v->env_phase = 3;
        else
            --v->hold_left;
        return;                                  /* level unchanged */
    case 3:                                      /* decay */
        if (l <= ENV_DECAY) {
            l = 0;
            v->env_phase = 0;
        } else
            l -= ENV_DECAY;
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
    h->control = 0;                              /* stop while retiming */
    h->feedback = v->sh_feedback;
    h->dac = 0;
    h->shiftlo = 0;
    h->other = 0;
    h->count = v->sh_bkup;
    h->reload = v->sh_bkup;
    h->volume = v->env_level;
    h->control = v->sh_ctl;
}

/* Full reprogram only on trigger; volume-only updates must not stop the
 * oscillator (envelope would otherwise glitch the phase every tick). */
static void flush_vol(unsigned char ch)
{
    volatile struct _mikey_audio *h = CHAN[ch];
    struct voice *v = &voices[ch];

    if (!v->dirty)
        return;
    v->dirty = 0;
    h->volume = (v->env_phase == 0) ? 0 : v->env_level;
    if (v->env_phase == 0)
        h->control = 0;
}

void engine_stop(void)
{
    unsigned char ch;
    eng_playing = 0;
    for (ch = 0; ch < NCH; ++ch) {
        voices[ch].env_phase = 0;
        voices[ch].env_level = 0;
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
    }
}

void engine_play(void)
{
    eng_row = 0;
    eng_tick = 0;
    eng_gpos = 0;
    eng_playing = 1;
}

void engine_tick(void)
{
    unsigned char ch;
    unsigned char triggered = 0;

    if (!eng_playing)
        return;

    if (eng_tick == 0) {                         /* row start */
        struct step *s = &phrase[eng_row];
        if (s->note) {
            trigger(0, s->note);
            triggered = 1;
        }
    }

    for (ch = 0; ch < NCH; ++ch) {
        if (!(triggered && ch == 0))
            envelope(ch);
        if (triggered && ch == 0)
            flush(ch);
        else
            flush_vol(ch);
    }

    if (++eng_tick >= groove[eng_gpos]) {        /* row advance */
        eng_tick = 0;
        if (++eng_gpos >= sizeof(groove))
            eng_gpos = 0;
        if (++eng_row >= PHRASE_ROWS)
            eng_row = 0;
    }
}
