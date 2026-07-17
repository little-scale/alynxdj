/* Packed EEPROM save (DESIGN.md D4/D10/§12).
 *
 * The flat song block (struct songdata) RLE-packs into a RAM buffer and
 * then into the 93C86 word by word: 4 header words (magic 'ALDJ', packed
 * length, version+checksum) plus payload. Capacity is 2032 payload bytes; the
 * FILES screen shows the packed size against it (D10: refuse, never
 * truncate).
 *
 * RLE: token < $80 = literal, token+1 bytes follow verbatim;
 *      token >= $80 = run, next byte repeats (token-$80)+3 times (3..130).
 */
#include <string.h>

#include "tracker.h"

#define HDR_WORDS 4
#define CAP_BYTES SAVE_CAP_BYTES        /* see tracker.h (Handy load cap) */

#define SAVE_VER  5

/* nearest time-curve nibble for a v1 per-tick rate (0 stays special) */
#pragma code-name (push, "HICODE3")
static unsigned char rate_nib(unsigned char rate)
{
    unsigned char i, best = 1, bd = 255, d;
    if (!rate)
        return 0;
    for (i = 1; i < 16; ++i) {
        d = (env_rate[i] > rate) ? env_rate[i] - rate : rate - env_rate[i];
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

/* pack buffer in the free RAM band above the mirrors */
static unsigned char *const buf = (unsigned char *)0xC100;

static unsigned char cksum(unsigned len)
{
    unsigned char s = 0;
    unsigned i;
    for (i = 0; i < len; ++i)
        s += buf[i];
    return s;
}
#pragma code-name (pop)

/* RLE-pack sd into buf; 0 = does not fit */
unsigned save_pack(void)
{
    const unsigned char *src = (const unsigned char *)&sd;
    unsigned remain = sizeof(sd);
    unsigned out = 0;

    while (remain) {
        unsigned char run = 1;
        unsigned char v = *src;
        while (run < 130 && run < remain && src[run] == v)
            ++run;
        if (run >= 3) {
            if (out + 2 > CAP_BYTES)
                return 0;
            buf[out++] = 0x80 + (run - 3);
            buf[out++] = v;
            src += run;
            remain -= run;
        } else {
            /* literal: gather until the next run of >=3 (max 128) */
            unsigned char lit = 0;
            while (lit < 128 && lit < remain) {
                if (remain - lit >= 3 && src[lit] == src[lit + 1]
                    && src[lit] == src[lit + 2])
                    break;
                ++lit;
            }
            if (out + 1 + lit > CAP_BYTES)
                return 0;
            buf[out++] = lit - 1;
            memcpy(buf + out, src, lit);
            out += lit;
            src += lit;
            remain -= lit;
        }
    }
    return out;
}

static void unpack(unsigned len)
{
    unsigned char *dst = (unsigned char *)&sd;
    unsigned in = 0;

    while (in < len) {
        unsigned char t = buf[in++];
        if (t & 0x80) {
            unsigned char n = (t - 0x80) + 3;
            unsigned char v = buf[in++];
            memset(dst, v, n);
            dst += n;
        } else {
            memcpy(dst, buf + in, t + 1);
            dst += t + 1;
            in += t + 1;
        }
    }
}

unsigned char save_do(void)
{
    unsigned len = save_pack();
    unsigned i, w;

    if (!len)
        return ST_TOOBIG;
    ee_write(0, 'A' | ('L' << 8));
    ee_write(1, 'D' | ('J' << 8));
    ee_write(2, len);
    ee_write(3, (SAVE_VER << 8) | cksum(len));
    w = (len + 1) >> 1;
    for (i = 0; i < w; ++i)
        ee_write(HDR_WORDS + i, buf[i * 2] | (buf[i * 2 + 1] << 8));
    return ST_OK;
}

unsigned char save_load(void)
{
    unsigned len, i, w, v;

    if (ee_read(0) != ('A' | ('L' << 8)) || ee_read(1) != ('D' | ('J' << 8)))
        return ST_NODATA;
    len = ee_read(2);
    if (len == 0 || len > CAP_BYTES)
        return ST_NODATA;
    w = (len + 1) >> 1;
    for (i = 0; i < w; ++i) {
        v = ee_read(HDR_WORDS + i);
        buf[i * 2] = (unsigned char)v;
        buf[i * 2 + 1] = (unsigned char)(v >> 8);
    }
    if ((unsigned char)ee_read(3) != cksum(len))
        return ST_BADSUM;
    unpack(len);
    v = ee_read(3) >> 8;
    if (v < 2) {
        /* v1 records stored ATK/HOLD/DCY as per-tick RATE bytes at
         * offsets 2/3/4 — fold each rate onto the nearest env_rate[]
         * nibble (time semantics, v2) */
        unsigned char i;
        for (i = 0; i < NINSTR; ++i) {
            unsigned char *r = (unsigned char *)&sd.instrs[i];
            r[2] = (rate_nib(r[2]) << 4) | rate_nib(r[4]);
            r[3] = (r[3] > 15) ? 15 : r[3];
            r[4] = 0;
        }
    }
    if (v < 4) {
        /* v4 assigns the three formerly-reserved record bytes.  Old songs
         * normally contain zero here, but clear them explicitly so legacy
         * or third-party files cannot acquire accidental modulation. */
        unsigned char i;
        for (i = 0; i < NINSTR; ++i) {
            sd.instrs[i].swp = 0;
            sd.instrs[i].vib = 0;
            sd.instrs[i].trm = 0;
        }
    }
    if (v < 5) {
        /* v5 gives the final reserved instrument byte to TSP. */
        unsigned char i;
        for (i = 0; i < NINSTR; ++i)
            sd.instrs[i].tsp = 0;
    }
    return ST_OK;
}

/* --- machine config: the top 4 EEPROM cells (1020-1023), written on
 * OPTIONS edits, loaded at boot. Layout: 1020 = 'C'|'F'<<8,
 * 1021 = palette | prelisten<<8, 1022 = repeat | sync<<8, 1023 spare. */

void config_save(void)
{
    ee_write(1020, 'C' | ('F' << 8));
    ee_write(1021, opt_palette | ((unsigned)opt_prelisten << 8));
    ee_write(1022, opt_repeat | ((unsigned)sync_mode << 8));
}

void config_load(void)
{
    unsigned v;
    if (ee_read(1020) != ('C' | ('F' << 8)))
        return;
    v = ee_read(1021);
    opt_palette = (unsigned char)v % NPALETTES;
    opt_prelisten = (v >> 8) ? 1 : 0;
    v = ee_read(1022);
    opt_repeat = (unsigned char)v;
    if (opt_repeat < 4 || opt_repeat > 30)
        opt_repeat = 12;
    sync_mode = (v >> 8) % NSYNC;
}

/* 16-bit additive checksum of the song block (harness verification) */
unsigned save_sum(void)
{
    const unsigned char *p = (const unsigned char *)&sd;
    unsigned s = 0;
    unsigned i;
    for (i = 0; i < sizeof(sd); ++i)
        s += p[i];
    return s;
}
