/* Sequencer engine — per-tick pipeline ported from SMSGGDJ engine.asm:
 *
 *   groove -> row advance -> trigger -> AHD envelope -> shadow -> flush
 *
 * M5: full SONG -> CHAIN -> PHRASE hierarchy. Each of the 4 tracks walks the
 * song independently (its song row advances when its chain ends), so tracks
 * can run different chain lengths — the ported per-channel walker model.
 * Row *timing* is global (one groove clocks all tracks).
 *
 * C-first per DESIGN.md D2; the tick runs from the main loop on VBlank
 * frame change. It moves into the IRQ when the driver goes asm.
 */
#include <lynx.h>

#include "tracker.h"
#include "../build/notes.h"

#define AUD_ENABLE_COUNT  0x10
#define AUD_ENABLE_RELOAD 0x08
#define AUD_INTEGRATE     0x20

/* the flat song block (DESIGN.md D4: contiguous, save-image order; the
 * EEPROM packer serializes exactly this struct at M10) */
struct songdata sd;

struct walk eng_walk[NCH];

struct voice {
    unsigned char note;
    unsigned char env_phase;    /* 0=off 1=attack 2=hold 3=decay */
    unsigned char env_level;
    unsigned char env_peak;
    unsigned char hold_left;
    unsigned char e_atk, e_dcy; /* latched from the instrument at trigger */
    unsigned char sh_vol, sh_feedback, sh_bkup, sh_ctl, sh_pan;
    unsigned char dirty;
};

static struct voice voices[NCH];

/* LFSR tap presets (feedback register values) — the timbre banks.
 * Preset 0 of the tone bank is the proven square; the rest are a spread of
 * short-loop (buzzy, pitched) to long-loop (noisy) tap sets. Curated on
 * real hardware at the Q4 pass — Handy's shifter is the reference until
 * then. Taps: bits 0-5 = LFSR taps 0-5, bit 6 = tap 10, bit 7 = tap 11. */
static const unsigned char timbre_tone[8] = {
    0x01, 0x03, 0x07, 0x0F, 0x1F, 0x3F, 0x43, 0xC1,
};
static const unsigned char timbre_noise[8] = {
    0x81, 0x83, 0x87, 0x8F, 0x9F, 0xBF, 0xC3, 0xFF,
};

static const unsigned char groove[2] = {6, 6};

unsigned char eng_mode;
#define eng_playing (eng_mode)

static unsigned char eng_tick;
static unsigned char eng_gpos;

static volatile struct _mikey_audio *const CHAN[NCH] = {
    &MIKEY.channel_a, &MIKEY.channel_b, &MIKEY.channel_c, &MIKEY.channel_d,
};

static void trigger(unsigned char ch, unsigned char note, unsigned char inum)
{
    struct voice *v = &voices[ch];
    struct instr *in = &sd.instrs[inum < NINSTR ? inum : 0];

    if (in->type == IT_KIT) {
        /* PCM: note semitone picks the kit slot; plays on channel D's DAC
         * regardless of track (mono sample bus, SMSGGDJ T3 policy). The
         * engine leaves channel D's registers alone while it runs. */
        pcm_play(((note - 1) % 12) & 7);
        v->env_phase = 0;
        v->env_level = 0;
        v->dirty = 0;
        return;
    }
    v->note = note;
    v->env_peak = in->vol;
    v->e_atk = in->atk;
    v->e_dcy = in->dcy;
    v->hold_left = in->hold;
    if (in->atk) {
        v->env_phase = 1;
        v->env_level = 0;
    } else {
        v->env_phase = 2;                        /* instant attack */
        v->env_level = in->vol;
    }
    if (in->type == IT_WAV) {
        /* Hardware triangle: integrate mode + feedback from tap 11 only.
         * The inverted-parity feedback cycles the all-zero shifter through
         * 12 ones / 12 zeros, so OUTPUT ramps +/-VOLUME for 12 shifts each
         * way — a triangle at shiftrate/24. That is 12x the square's
         * divider: look the note up +43 semitones (2 cents off 12x).
         * Peak = 12 * step: keep instrument vol <= 10 or the 8-bit
         * accumulator wraps. */
        unsigned char n = note + 43;    /* extended table entries 96..138 */
        v->sh_feedback = 0x80;
        v->sh_bkup = note_bkup[n - 1];
        v->sh_ctl = AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD | AUD_INTEGRATE
                    | note_clock[n - 1];
    } else {
        v->sh_feedback = (in->type == IT_NOISE)
                         ? timbre_noise[in->timbre & 7]
                         : timbre_tone[in->timbre & 7];
        v->sh_bkup = note_bkup[note - 1];
        v->sh_ctl = AUD_ENABLE_COUNT | AUD_ENABLE_RELOAD
                    | note_clock[note - 1];
    }
    v->sh_pan = in->pan;
    v->dirty = 1;
}

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
            return;                              /* sustain until next note */
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
    h->control = 0;                              /* stop while retiming */
    h->feedback = v->sh_feedback;
    h->dac = 0;
    h->shiftlo = 0;
    h->other = 0;
    h->count = v->sh_bkup;
    h->reload = v->sh_bkup;
    h->volume = v->env_level;
    (&MIKEY.attena)[ch] = v->sh_pan;             /* L/R nibbles (Lynx II) */
    h->control = v->sh_ctl;
}

/* Volume-only update: never restarts the oscillator (envelope ticks would
 * otherwise glitch the phase). */
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

/* --- walkers --- */

/* Load track ch's walker from song row `row` (first row at/after with a
 * chain; EMPTY row 0 after wrap = inactive). */
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
                break;              /* first hole ends the track (ported) */
        }
        row = 0;                    /* wrap once: loop the song */
    }
}

/* Advance track ch one phrase row; steps chain / song row as phrases end. */
static void walk_advance(unsigned char ch)
{
    struct walk *w = &eng_walk[ch];

    if (!w->active)
        return;
    if (++w->prow < PHRASE_ROWS)
        return;
    w->prow = 0;

    if (eng_mode == MODE_PHRASE)                 /* loop one phrase */
        return;

    ++w->cpos;
    if (w->cpos < PHRASE_ROWS && sd.chains[w->chain][w->cpos].phrase != EMPTY) {
        w->phrase = sd.chains[w->chain][w->cpos].phrase;
        w->tsp = sd.chains[w->chain][w->cpos].tsp;
        return;
    }
    /* chain ended */
    if (eng_mode == MODE_CHAIN) {                /* loop the chain */
        w->cpos = 0;
        w->phrase = sd.chains[w->chain][0].phrase;
        w->tsp = sd.chains[w->chain][0].tsp;
        return;
    }
    walk_load_song(ch, w->song_row + 1);         /* MODE_SONG */
}

/* returns 1 if a note was triggered on this row */
static unsigned char row_trigger(unsigned char ch)
{
    struct walk *w = &eng_walk[ch];
    struct step *s;
    unsigned char n;

    if (!w->active)
        return 0;
    s = &sd.phrases[w->phrase][w->prow];
    if (!s->note)
        return 0;
    n = s->note + w->tsp;
    if (n < NOTE_MIN || n > NOTE_MAX)
        n = s->note;
    trigger(ch, n, s->instr);
    return 1;
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
        eng_walk[ch].active = 0;
        CHAN[ch]->control = 0;
        CHAN[ch]->volume = 0;
    }
}

static void play_common(void)
{
    eng_tick = 0;
    eng_gpos = 0;
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

/* Editor prelisten on voice 0; envelope keeps ticking while stopped so the
 * note decays naturally. */
void engine_audition(unsigned char note, unsigned char inum)
{
    trigger(0, note, inum);
    flush(0);
    voices[0].dirty = 0;
}

void engine_tick(void)
{
    unsigned char ch;
    unsigned char trig = 0;

    if (eng_playing && eng_tick == 0)
        trig = 1;

    for (ch = 0; ch < NCH; ++ch) {
        if (trig && row_trigger(ch))
            flush(ch);
        else {
            envelope(ch);
            flush_vol(ch);
        }
    }

    if (!eng_playing)
        return;

    if (++eng_tick >= groove[eng_gpos]) {
        eng_tick = 0;
        if (++eng_gpos >= sizeof(groove))
            eng_gpos = 0;
        for (ch = 0; ch < NCH; ++ch)
            walk_advance(ch);
    }
}
