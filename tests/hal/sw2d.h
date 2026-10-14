/* sw2d.h - TWODTEST's software model of the HAL's 2D primitives: what each
 * should leave in VRAM, drawn in a copy of it in system memory. Written
 * from the primitives' definitions in hal.h, not from the engine's
 * behaviour, so the two can disagree. */
#ifndef SW2D_H
#define SW2D_H
#include <stdint.h>

typedef struct {
    uint8_t *mem;               /* the surface's first pixel, in the copy */
    int pitch_px, bpp;          /* bpp 8, 16 or 32 */
    int cx0, cy0, cx1, cy1;     /* the clip, inclusive-exclusive */
} sw_surf;

uint32_t sw_get(const sw_surf *s, int x, int y);
void sw_put(const sw_surf *s, int x, int y, uint32_t v);          /* clipped */
void sw_fill(const sw_surf *s, int x, int y, int w, int h, uint32_t v);
/* The source rectangle as it was before the copy, into the destination
   (which may be the same surface, overlapping). */
void sw_copy(const sw_surf *d, int dx, int dy, const sw_surf *src, int sx, int sy, int w, int h);
/* One-bit rows (stride bytes apart, the leftmost pixel in bit 7 of the first
   byte): a set bit draws fg, a clear one bg, unless transparent. */
void sw_mono(const sw_surf *d, int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg,
             uint32_t bg, int transparent);
/* Pixels in the surface's format, rows stride bytes apart. */
void sw_image(const sw_surf *d, int x, int y, int w, int h, const void *px, int stride);

#endif
