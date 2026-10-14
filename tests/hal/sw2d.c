/* sw2d.c - TWODTEST's software model of the HAL's 2D primitives (sw2d.h). */
#include "sw2d.h"
#include <stdlib.h>
#include <string.h>

uint32_t sw_get(const sw_surf *s, int x, int y)
{
    const uint8_t *p = s->mem + ((long)y * s->pitch_px + x) * (s->bpp / 8);
    if (s->bpp == 8)
        return p[0];
    if (s->bpp == 16)
        return (uint32_t)p[0] | (uint32_t)p[1] << 8;
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static int inside(const sw_surf *s, int x, int y)
{
    return x >= s->cx0 && x < s->cx1 && y >= s->cy0 && y < s->cy1;
}

void sw_put(const sw_surf *s, int x, int y, uint32_t v)
{
    uint8_t *p;
    int i;
    if (!inside(s, x, y))
        return;
    p = s->mem + ((long)y * s->pitch_px + x) * (s->bpp / 8);
    for (i = 0; i < s->bpp / 8; i++)
        p[i] = (uint8_t)(v >> (8 * i));
}

void sw_fill(const sw_surf *s, int x, int y, int w, int h, uint32_t v)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            sw_put(s, x + i, y + j, v);
}

void sw_copy(const sw_surf *d, int dx, int dy, const sw_surf *src, int sx, int sy, int w, int h)
{
    uint32_t *keep;
    int i, j;
    if (w <= 0 || h <= 0 || !(keep = malloc((size_t)w * (size_t)h * sizeof *keep)))
        return;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            keep[j * w + i] = sw_get(src, sx + i, sy + j);
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++)
            sw_put(d, dx + i, dy + j, keep[j * w + i]);
    free(keep);
}

void sw_mono(const sw_surf *d, int x, int y, int w, int h, const uint8_t *bits, int stride, uint32_t fg,
             uint32_t bg, int transparent)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            if (bits[j * stride + i / 8] & (0x80 >> (i & 7)))
                sw_put(d, x + i, y + j, fg);
            else if (!transparent)
                sw_put(d, x + i, y + j, bg);
        }
}

void sw_image(const sw_surf *d, int x, int y, int w, int h, const void *px, int stride)
{
    const uint8_t *row;
    int i, j, k, n = d->bpp / 8;
    for (j = 0; j < h; j++) {
        row = (const uint8_t *)px + (long)j * stride;
        for (i = 0; i < w; i++) {
            uint32_t v = 0;
            for (k = 0; k < n; k++)
                v |= (uint32_t)row[i * n + k] << (8 * k);
            sw_put(d, x + i, y + j, v);
        }
    }
}
