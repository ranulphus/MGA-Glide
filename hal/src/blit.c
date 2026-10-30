/* blit.c - the engine's BITBLTs (docs/matrox-2d.md): rectangles copied
 * from VRAM to the target, and one-bit pixels in VRAM expanded onto it.
 *
 * A BITBLT reads its source at AR3, in pixels (in bits for one-bit data),
 * and ends each line where it reaches AR0. The specifications give AR0
 * 18 bits, signed, on the G100 and G200 (22 on the G400), and say nothing
 * of an end address beyond them. On the G200 and G400 the source is
 * SRCORG + AR3, so the HAL points SRCORG near the rows it copies and keeps
 * AR0 small, taking tall rectangles a band of rows at a time. The G100 has
 * no SRCORG: its source addresses are absolute, and whether lines beyond
 * 2^17 pixels end where they should is a silicon question (X.org relies on
 * it). DWGCTL is written before the AR registers (see engine.c). */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"

static uint32_t ar_room(void)                   /* what AR0 can hold */
{
    return mga.family == MGA_FAMILY_G400 ? (1u << 21) - 1 : (1u << 17) - 1;
}

void engine_copy(int sx, int sy, int dx, int dy, int w, int h)
{
    engine_copy_from(engine_target->color_off, engine_target->pitch_px, sx, sy, dx, dy, w, h);
}

void engine_copy_from(uint32_t src_off, int src_pitch_px, int sx, int sy, int dx, int dy, int w, int h)
{
    const mga_target *t = engine_target;
    uint32_t bpp_b = (uint32_t)t->bpp / 8, sp = (uint32_t)src_pitch_px, W = (uint32_t)w - 1;
    int same = src_off == t->color_off && src_pitch_px == t->pitch_px;
    int up = same && dy > sy, left = same && dy == sy && dx > sx;   /* overlap: read before it is written */
    int band, done, n;
    if (w <= 0 || h <= 0 || src_pitch_px <= 0)
        return;
    engine_warp_order();
    fifo_reserve(4);
    setup_forget_dwgctl();
    MGA_WR32(MGAREG_DWGCTL, DWG_OPCOD_BITBLT | DWG_ATYPE_RSTR | DWG_SHFTZERO | DWG_BOP_COPY | DWG_BLTMOD_BFCOL);
    MGA_WR32(MGAREG_SGN, (left ? SGN_SCANLEFT : 0u) | (up ? SGN_SDY : 0u));
    MGA_WR32(MGAREG_AR5, up ? (uint32_t)-(int32_t)sp : sp);
    MGA_WR32(MGAREG_FXBNDRY, ((uint32_t)(dx + w - 1) << 16) | ((uint32_t)dx & 0xFFFFu));
    if (!mga.has_dstorg) {                      /* G100: absolute addresses, one go */
        uint32_t row = src_off / bpp_b + (uint32_t)(sy + (up ? h - 1 : 0)) * sp + (uint32_t)sx;
        fifo_reserve(3);
        MGA_WR32(MGAREG_AR0, left ? row : row + W);
        MGA_WR32(MGAREG_AR3, left ? row + W : row);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)(dy + (up ? h - 1 : 0)) << 16) | (uint32_t)h);
        return;
    }
    band = (int)((ar_room() - 63 / bpp_b - W) / sp) + 1;    /* rows whose addresses fit AR0 */
    for (done = 0; done < h; done += n) {
        int top;                                /* the band's first row, from the rectangle's top */
        uint32_t at, first;
        n = h - done < band ? h - done : band;
        top = up ? h - done - n : done;
        at = src_off + ((uint32_t)(sy + top) * sp + (uint32_t)sx) * bpp_b;
        first = (at & 63) / bpp_b + (up ? (uint32_t)(n - 1) * sp : 0);  /* the first line's left end */
        fifo_reserve(4);
        MGA_WR32(MGAREG_SRCORG, at & ~63u);
        MGA_WR32(MGAREG_AR0, left ? first : first + W);
        MGA_WR32(MGAREG_AR3, left ? first + W : first);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)(dy + top + (up ? n - 1 : 0)) << 16) | (uint32_t)n);
    }
}

/* One-bit rows in VRAM: the G200 and G400 read them with the first pixel in
 * bit 7 of each byte (BMONOWF), as GLOS's glyphs are; the G100's BITBLT
 * takes only bit 0 first (BMONOLEF), so its copies are stored mirrored. */
void engine_mono_store(uint32_t off, const uint8_t *bits, int stride, int w, int h)
{
    int nb = (w + 7) / 8, i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < nb; i++) {
            uint8_t b = bits[(long)j * stride + i];
            if (!mga.has_dstorg) {
                b = (uint8_t)(((b & 0xF0) >> 4) | ((b & 0x0F) << 4));
                b = (uint8_t)(((b & 0xCC) >> 2) | ((b & 0x33) << 2));
                b = (uint8_t)(((b & 0xAA) >> 1) | ((b & 0x55) << 1));
            }
            mga_fb[off + (uint32_t)j * (uint32_t)stride + (uint32_t)i] = b;
        }
}

void engine_mono_vram(uint32_t off, int stride, int x, int y, int w, int h, uint32_t fg, uint32_t bg,
                      unsigned flags)
{
    uint32_t sp = (uint32_t)stride * 8, W = (uint32_t)w - 1;
    int band, done, n;
    if (w <= 0 || h <= 0 || stride <= 0)
        return;
    engine_warp_order();
    fifo_reserve(6);
    setup_forget_dwgctl();
    MGA_WR32(MGAREG_DWGCTL, DWG_OPCOD_BITBLT | DWG_ATYPE_RSTR | DWG_SGNZERO | DWG_SHFTZERO | DWG_BOP_COPY |
                            (mga.has_dstorg ? DWG_BLTMOD_BMONOWF : DWG_BLTMOD_BMONOLEF) |
                            (flags & MGA_MONO_TRANSPARENT ? DWG_TRANSC : 0u));
    MGA_WR32(MGAREG_FCOL, engine_colour(fg));
    MGA_WR32(MGAREG_BCOL, engine_colour(bg));
    MGA_WR32(MGAREG_AR5, sp);
    MGA_WR32(MGAREG_FXBNDRY, ((uint32_t)(x + w - 1) << 16) | ((uint32_t)x & 0xFFFFu));
    if (!mga.has_dstorg) {                      /* G100: an absolute bit address (spage: AR3 <26:24>) */
        fifo_reserve(3);
        MGA_WR32(MGAREG_AR0, off * 8 + W);
        MGA_WR32(MGAREG_AR3, off * 8);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)y << 16) | (uint32_t)h);
        return;
    }
    band = (int)((ar_room() - 63 * 8 - W) / sp) + 1;
    for (done = 0; done < h; done += n) {
        uint32_t at = off + (uint32_t)done * (uint32_t)stride, first = (at & 63) * 8;
        n = h - done < band ? h - done : band;
        fifo_reserve(4);
        MGA_WR32(MGAREG_SRCORG, at & ~63u);
        MGA_WR32(MGAREG_AR0, first + W);
        MGA_WR32(MGAREG_AR3, first);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)(y + done) << 16) | (uint32_t)n);
    }
}
