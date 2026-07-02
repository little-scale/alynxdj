/* Packed EEPROM save (DESIGN.md D4/D10/§12).
 *
 * The flat song block (struct songdata) RLE-packs into a RAM buffer and
 * then into the 93C86 word by word: 4 header words (magic 'ALDJ', packed
 * length, version+checksum) + payload. Capacity 2040 payload bytes; the
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

#define SAVE_VER  1

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
    return ST_OK;
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
