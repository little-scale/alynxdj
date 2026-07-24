/* Cart-streamed HELP screen.
 *
 * The renderer itself is cold-loaded over the stopped PCM rings at $D004.
 * Its editable text stays in cart blocks 251-253 and only one <=38-byte row
 * is copied into the scratch tail of the ring area at a time. This keeps the
 * working song and the already-full resident/cold code regions untouched.
 */
#include "tracker.h"
#include "../build/help.h"

#define HELP_DATA_BLOCK 251
#define HELP_PACK       ((unsigned char *)0xD3B0)
#define HELP_LINE       ((char *)0xD3D8)

#pragma code-name (push, "HELPCODE")
#pragma rodata-name (push, "HELPCODE")

static void help_title(void)
{
    HELP_LINE[0] = 'H';
    HELP_LINE[1] = 'E';
    HELP_LINE[2] = 'L';
    HELP_LINE[3] = 'P';
    HELP_LINE[4] = ' ';
    HELP_LINE[5] = 0;
    draw_text(1, 0, HELP_LINE, PEN_ACCENT, PEN_BG);
    HELP_LINE[0] = '1' + help_page;
    HELP_LINE[1] = '/';
    HELP_LINE[2] = '0' + HELP_PAGES;
    HELP_LINE[3] = 0;
    draw_text(6, 0, HELP_LINE, PEN_TEXT, PEN_BG);
}

void help_draw(void)
{
    unsigned char meta[2];
    unsigned char row, count, tag, len, i, out, ch;
    unsigned off;

    if (help_page >= HELP_PAGES)
        help_page = 0;
    draw_x_offset = 0;
    clear_grid();
    help_title();
    cart_seek(HELP_DATA_BLOCK, 6 + (unsigned)help_page * 2);
    cart_read(meta, 2);
    off = (unsigned)meta[0] | ((unsigned)meta[1] << 8);
    cart_seek(HELP_DATA_BLOCK + (unsigned char)(off >> 10), off & 0x03FF);
    cart_read(&count, 1);

    for (row = 0; row < count; ++row) {
        cart_read(&tag, 1);
        len = tag & 0x3F;
        if (len)
            cart_read(HELP_PACK, len);
        out = 0;
        for (i = 0; i < len; ++i) {
            ch = HELP_PACK[i];
            if (ch < 0x20) {
                while (ch--)
                    HELP_LINE[out++] = ' ';
            } else
                HELP_LINE[out++] = ch;
        }
        HELP_LINE[out] = 0;
        draw_text(1, row + 1, HELP_LINE,
                  (tag & 0x80) ? PEN_BG : PEN_TEXT,
                  (tag & 0x80) ? PEN_ACCENT : PEN_BG);
    }
}

void __fastcall__ help_turn(unsigned char dir)
{
    if (dir == 0 || dir == 2)
        help_page = help_page ? help_page - 1 : HELP_PAGES - 1;
    else
        help_page = (help_page + 1 == HELP_PAGES) ? 0 : help_page + 1;
    help_draw();
}

#pragma rodata-name (pop)
#pragma code-name (pop)
