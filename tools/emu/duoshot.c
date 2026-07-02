/* Two bridged Handy instances for ComLynx testing.
 *
 *   duoshot <coreA> <coreB> <rom> <outA> <outB> <scriptA> <scriptB> [frames]
 *
 * coreA/coreB must be two separate FILE COPIES of the patched core (dlopen
 * dedups identical paths; copies get independent globals). Their ComLynx
 * UARTs are cross-wired via the handy_comlynx_* exports: every byte one
 * unit transmits is queued into the other's receiver — a two-Lynx cable.
 * Scripts are "maskHex@frames,..." per unit; audio lands in <out>.wav,
 * the last frame in <out>.ppm, 64K RAM in <out>.ram.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <dlfcn.h>
#include "libretro.h"

typedef struct {
    void *dl;
    void (*set_environment)(retro_environment_t);
    void (*set_video_refresh)(retro_video_refresh_t);
    void (*set_input_poll)(retro_input_poll_t);
    void (*set_input_state)(retro_input_state_t);
    void (*set_audio_sample)(retro_audio_sample_t);
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*init)(void);
    bool (*load_game)(const struct retro_game_info *);
    void (*run)(void);
    void (*unload_game)(void);
    void (*deinit)(void);
    void *(*get_memory_data)(unsigned);
    size_t (*get_memory_size)(unsigned);
    void (*comlynx_cable)(int);
    void (*comlynx_rx)(int);
    void (*comlynx_set_tx)(void (*)(int, unsigned), unsigned);

    unsigned fmt, w, h;
    uint8_t  fb[1024 * 512 * 4];
    int16_t *aud;
    size_t   aud_n, aud_max;
    unsigned buttons;
    const char *script;
} unit_t;

static unit_t U[2];

static void log_cb(enum retro_log_level level, const char *fmt, ...)
{
    (void)level; (void)fmt;
}

#define DEF_CBS(n) \
static bool env_cb_##n(unsigned cmd, void *data) { \
    switch (cmd) { \
    case RETRO_ENVIRONMENT_GET_LOG_INTERFACE: \
        ((struct retro_log_callback *)data)->log = log_cb; return true; \
    case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: \
        U[n].fmt = *(const enum retro_pixel_format *)data; return true; \
    case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY: \
    case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY: \
        *(const char **)data = "/tmp/duo" #n; return true; \
    case RETRO_ENVIRONMENT_GET_CAN_DUPE: \
        *(bool *)data = true; return true; \
    default: return false; } } \
static void video_cb_##n(const void *d, unsigned w, unsigned h, size_t pitch) { \
    unsigned y; \
    if (!d) return; \
    U[n].w = w; U[n].h = h; \
    for (y = 0; y < h; y++) \
        memcpy(U[n].fb + y * w * 2, (const uint8_t *)d + y * pitch, w * 2); } \
static void poll_cb_##n(void) {} \
static int16_t input_cb_##n(unsigned port, unsigned dev, unsigned idx, unsigned id) { \
    (void)idx; \
    if (port == 0 && (dev & 0xFF) == RETRO_DEVICE_JOYPAD) { \
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return (int16_t)U[n].buttons; \
        if (id < 16) return (U[n].buttons >> id) & 1; } \
    return 0; } \
static void aud_cb_##n(int16_t l, int16_t r) { \
    if (U[n].aud_n + 2 <= U[n].aud_max) { \
        U[n].aud[U[n].aud_n++] = l; U[n].aud[U[n].aud_n++] = r; } } \
static size_t audb_cb_##n(const int16_t *d, size_t f) { \
    size_t i; \
    for (i = 0; i < f; i++) aud_cb_##n(d[2*i], d[2*i+1]); \
    return f; } \
static void tx_cb_##n(int data, unsigned ref) { \
    (void)ref; \
    U[n ^ 1].comlynx_rx(data); }

DEF_CBS(0)
DEF_CBS(1)

#define SYM(u, field, name) do { \
    *(void **)(&(u)->field) = dlsym((u)->dl, name); \
    if (!(u)->field) { fprintf(stderr, "missing %s\n", name); exit(2); } } while (0)

static void unit_load(unit_t *u, const char *core)
{
    u->dl = dlopen(core, RTLD_NOW | RTLD_LOCAL);
    if (!u->dl) { fprintf(stderr, "dlopen %s: %s\n", core, dlerror()); exit(2); }
    SYM(u, set_environment, "retro_set_environment");
    SYM(u, set_video_refresh, "retro_set_video_refresh");
    SYM(u, set_input_poll, "retro_set_input_poll");
    SYM(u, set_input_state, "retro_set_input_state");
    SYM(u, set_audio_sample, "retro_set_audio_sample");
    SYM(u, set_audio_sample_batch, "retro_set_audio_sample_batch");
    SYM(u, init, "retro_init");
    SYM(u, load_game, "retro_load_game");
    SYM(u, run, "retro_run");
    SYM(u, unload_game, "retro_unload_game");
    SYM(u, deinit, "retro_deinit");
    SYM(u, get_memory_data, "retro_get_memory_data");
    SYM(u, get_memory_size, "retro_get_memory_size");
    SYM(u, comlynx_cable, "handy_comlynx_cable");
    SYM(u, comlynx_rx, "handy_comlynx_rx");
    SYM(u, comlynx_set_tx, "handy_comlynx_set_tx");
    u->aud_max = 48000u * 2 * 60;
    u->aud = malloc(u->aud_max * sizeof(int16_t));
}

/* step "mask@frames,..." — returns the button mask for frame f */
static unsigned script_mask(const char *s, int f)
{
    int acc = 0;
    while (s && *s) {
        char *at;
        unsigned m = (unsigned)strtoul(s, &at, 16);
        int n = (*at == '@') ? atoi(at + 1) : 1;
        if (f < acc + n)
            return m;
        acc += n;
        s = strchr(s, ',');
        if (s) s++;
    }
    return 0;
}

static void write_outputs(unit_t *u, const char *base)
{
    char path[1024];
    FILE *o;
    unsigned i;

    snprintf(path, sizeof path, "%s.wav", base);
    o = fopen(path, "wb");
    if (o) {
        uint32_t db = (uint32_t)(u->aud_n * 2), rate = 48000;
        uint32_t br = rate * 4, ch = 36 + db, s1 = 16;
        uint16_t f = 1, nch = 2, bps = 16, al = 4;
        fwrite("RIFF", 1, 4, o); fwrite(&ch, 4, 1, o); fwrite("WAVE", 1, 4, o);
        fwrite("fmt ", 1, 4, o); fwrite(&s1, 4, 1, o); fwrite(&f, 2, 1, o);
        fwrite(&nch, 2, 1, o); fwrite(&rate, 4, 1, o); fwrite(&br, 4, 1, o);
        fwrite(&al, 2, 1, o); fwrite(&bps, 2, 1, o);
        fwrite("data", 1, 4, o); fwrite(&db, 4, 1, o);
        fwrite(u->aud, 2, u->aud_n, o);
        fclose(o);
    }
    snprintf(path, sizeof path, "%s.ppm", base);
    o = fopen(path, "wb");
    if (o && u->w) {
        fprintf(o, "P6\n%u %u\n255\n", u->w, u->h);
        for (i = 0; i < u->w * u->h; i++) {
            uint16_t p = ((uint16_t *)u->fb)[i];
            fputc(((p >> 11) & 0x1F) << 3, o);
            fputc(((p >> 5) & 0x3F) << 2, o);
            fputc((p & 0x1F) << 3, o);
        }
        fclose(o);
    }
    snprintf(path, sizeof path, "%s.ram", base);
    o = fopen(path, "wb");
    if (o) {
        void *ram = u->get_memory_data(RETRO_MEMORY_SYSTEM_RAM);
        size_t sz = u->get_memory_size(RETRO_MEMORY_SYSTEM_RAM);
        if (ram && sz)
            fwrite(ram, 1, sz, o);
        fclose(o);
    }
}

int main(int argc, char **argv)
{
    struct retro_game_info gi = {0};
    long sz;
    void *rom;
    FILE *f;
    int frames, i, n;

    if (argc < 8) {
        fprintf(stderr, "usage: duoshot coreA coreB rom outA outB scriptA scriptB [frames]\n");
        return 1;
    }
    frames = argc > 8 ? atoi(argv[8]) : 600;

    f = fopen(argv[3], "rb");
    if (!f) { fprintf(stderr, "rom?\n"); return 3; }
    fseek(f, 0, SEEK_END); sz = ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(sz);
    if (fread(rom, 1, sz, f) != (size_t)sz) return 3;
    fclose(f);
    gi.path = argv[3]; gi.data = rom; gi.size = sz;

    unit_load(&U[0], argv[1]);
    unit_load(&U[1], argv[2]);
    U[0].script = argv[6];
    U[1].script = argv[7];

    U[0].set_environment(env_cb_0);
    U[0].set_video_refresh(video_cb_0);
    U[0].set_input_poll(poll_cb_0);
    U[0].set_input_state(input_cb_0);
    U[0].set_audio_sample(aud_cb_0);
    U[0].set_audio_sample_batch(audb_cb_0);
    U[1].set_environment(env_cb_1);
    U[1].set_video_refresh(video_cb_1);
    U[1].set_input_poll(poll_cb_1);
    U[1].set_input_state(input_cb_1);
    U[1].set_audio_sample(aud_cb_1);
    U[1].set_audio_sample_batch(audb_cb_1);

    U[0].init();
    U[1].init();
    if (!U[0].load_game(&gi) || !U[1].load_game(&gi)) {
        fprintf(stderr, "load_game failed\n");
        return 4;
    }
    U[0].comlynx_cable(1);
    U[1].comlynx_cable(1);
    U[0].comlynx_set_tx(tx_cb_0, 0);
    U[1].comlynx_set_tx(tx_cb_1, 1);

    for (i = 0; i < frames; i++) {
        for (n = 0; n < 2; n++) {
            U[n].buttons = script_mask(U[n].script, i);
            U[n].run();
        }
    }

    write_outputs(&U[0], argv[4]);
    write_outputs(&U[1], argv[5]);
    U[0].unload_game(); U[0].deinit();
    U[1].unload_game(); U[1].deinit();
    fprintf(stderr, "duoshot: %d frames, audio A=%zu B=%zu samples\n",
            frames, U[0].aud_n / 2, U[1].aud_n / 2);
    return 0;
}
