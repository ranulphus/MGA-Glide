/* twodtest.c - TWODTEST: the HAL's 2D primitives (GLOS M7 step B;
 * docs/matrox-2d.md). At each depth asked for (8, 16 and 32 by default) it
 * draws with each primitive on the screen and on surfaces off it, draws the
 * same with its own software (sw2d.c) in a copy of VRAM, and compares the
 * two byte for byte. Then, at 16 bits a pixel, the hardware cursor and the
 * display start, read from the emulated monitor (86Box's unit tester).
 *
 *   TWODTEST [8] [16] [32] [ops=N]
 *
 * HX-TEST lines: fill, copy, copy-from, mono, mono-vram, image, mixed (each
 * with -BPP), cursor, cursor-edge, start, start-high. */
#include "hx.h"
#include "sw2d.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SW 640                  /* the screen as drawn on */
#define SH 480
#define PITCH 1024              /* every depth: the screen's pitch in pixels */

typedef struct {
    const char *name;
    uint32_t off;               /* in VRAM */
    int w, h, pitch_px;
    sw_surf s;                  /* its copy (clip set per draw) */
} surface;

static uint8_t *shadow;         /* VRAM [0, used) as it should be */
static uint32_t used;
static int bpp, bpp_b;
static surface scr, s1, s2;     /* the screen; two surfaces off it, of other pitches */
static uint32_t glyphs;         /* one-bit rows in VRAM (engine_mono_store) */
static mga_vram heap;
static unsigned seed = 1;
static int nops = 300;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return n ? (seed >> 8) % n : 0;
}

static int rint_(int lo, int hi) { return lo + (int)rnd((unsigned)(hi - lo + 1)); }

static uint32_t rpix(void)
{
    uint32_t v = (rnd(65536) << 16) ^ rnd(65536);
    return bpp == 32 ? v : bpp == 16 ? v & 0xFFFF : v & 0xFF;
}

static void target(surface *t, int cx0, int cy0, int cx1, int cy1)
{
    mga_target g;
    memset(&g, 0, sizeof g);
    g.color_off = t->off;
    g.pitch_px = t->pitch_px;
    g.bpp = bpp;
    g.zbits = 16;
    engine_set_target(&g);
    engine_set_clip(cx0, cy0, cx1, cy1);
    t->s.cx0 = cx0, t->s.cy0 = cy0, t->s.cx1 = cx1, t->s.cy1 = cy1;
}

static void random_clip(surface *t)
{
    if (rnd(3) == 0) {
        target(t, 0, 0, t->w, t->h);
    } else {
        int x0 = rint_(0, t->w - 2), y0 = rint_(0, t->h - 2);
        target(t, x0, y0, rint_(x0 + 1, t->w), rint_(y0 + 1, t->h));
    }
}

/* The surface in VRAM against its copy; 0 when they agree. */
static int differs(const surface *t, char *why)
{
    int y, x;
    uint32_t row = (uint32_t)t->pitch_px * (uint32_t)bpp_b;
    for (y = 0; y < t->h; y++) {
        const uint8_t *a = (const uint8_t *)mga_fb + t->off + y * row, *b = shadow + t->off + y * row;
        if (memcmp(a, b, row) == 0)
            continue;
        for (x = 0; x < t->pitch_px && !memcmp(a + x * bpp_b, b + x * bpp_b, (size_t)bpp_b); x++)
            ;
        {
            sw_surf v = t->s;
            uint32_t want = sw_get(&v, x, y);
            v.mem = (uint8_t *)mga_fb + t->off;
            sprintf(why, "%s at %d,%d: %0*lx, want %0*lx", t->name, x, y, bpp_b * 2, (unsigned long)sw_get(&v, x, y),
                    bpp_b * 2, (unsigned long)want);
        }
        return 1;
    }
    return 0;
}

static int check(const char *name)
{
    char why[160] = "";
    int bad;
    engine_sync(2000000);
    bad = differs(&scr, why) || differs(&s1, why) || differs(&s2, why);
    hx_test(name, !bad, "%s", bad ? why : "same");
    if (bad)                                    /* the next case starts from what the card has */
        memcpy(shadow, (const void *)mga_fb, used);
    return !bad;
}

static void name_of(char *out, const char *what)
{
    sprintf(out, "%s-%d", what, bpp);
}

/* ---- the primitives, on the card and in the copy */

static void do_fill(surface *t)
{
    int x = rint_(-20, t->w), y = rint_(-20, t->h), w = rint_(1, 200), h = rint_(1, 150);
    uint32_t v = rpix();
    engine_fill(x, y, w, h, v);
    sw_fill(&t->s, x, y, w, h, v);
}

static void do_copy(surface *t)
{
    int w = rint_(1, t->w / 2), h = rint_(1, t->h / 2);
    int sx = rint_(0, t->w - w), sy = rint_(0, t->h - h), dx, dy;
    if (rnd(2)) {                               /* overlapping, any direction */
        dx = sx + rint_(-w / 2, w / 2), dy = sy + rint_(-h / 2, h / 2);
    } else {
        dx = rint_(-10, t->w - 1), dy = rint_(-10, t->h - 1);
    }
    engine_copy(sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &t->s, sx, sy, w, h);
}

static void do_copy_from(surface *t, surface *from)
{
    int w = rint_(1, from->w), h = rint_(1, from->h);
    int sx = rint_(0, from->w - w), sy = rint_(0, from->h - h), dx = rint_(-10, t->w - 1), dy = rint_(-10, t->h - 1);
    engine_copy_from(from->off, from->pitch_px, sx, sy, dx, dy, w, h);
    sw_copy(&t->s, dx, dy, &from->s, sx, sy, w, h);
}

static uint8_t bits[64 * 1024];

static void random_bits(int n)
{
    int i;
    for (i = 0; i < n; i++)
        bits[i] = (uint8_t)rnd(256);
}

static void do_mono(surface *t, int w, int h)
{
    static const int widths[] = { 1, 7, 8, 9, 16, 31, 32, 33, 63, 64, 65, 100, 200 };
    int stride, x, y;
    unsigned flags = rnd(2) ? MGA_MONO_TRANSPARENT : 0;
    uint32_t fg = rpix(), bg = rpix();
    if (!w)
        w = widths[rnd(sizeof widths / sizeof widths[0])], h = rint_(1, 40);
    stride = (w + 7) / 8 + (int)rnd(3);
    random_bits(stride * h);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    engine_mono(x, y, w, h, bits, stride, fg, bg, flags);
    sw_mono(&t->s, x, y, w, h, bits, stride, fg, bg, flags & MGA_MONO_TRANSPARENT);
}

static void do_mono_vram(surface *t, int w, int h)
{
    int stride, x, y;
    uint32_t fg = rpix(), bg = rpix(), at;
    unsigned flags = rnd(2) ? MGA_MONO_TRANSPARENT : 0;
    if (!w)
        w = rint_(1, 120), h = rint_(1, 40);
    stride = (w + 7) / 8 + (int)rnd(4);
    at = glyphs + rnd(64);                      /* (any byte) */
    random_bits(stride * h);
    engine_sync(2000000);                       /* (the area may be in use by the last expansion) */
    engine_mono_store(at, bits, stride, w, h);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    engine_mono_vram(at, stride, x, y, w, h, fg, bg, flags);
    sw_mono(&t->s, x, y, w, h, bits, stride, fg, bg, flags & MGA_MONO_TRANSPARENT);
}

static void do_image(surface *t, int w, int h)
{
    static uint32_t row[4096 / 4 + 1];
    uint8_t *px;
    int x, y, j, i, dw;
    if (!w)
        w = rint_(1, 150), h = rint_(1, 60);
    px = malloc((size_t)w * (size_t)h * (size_t)bpp_b);
    for (i = 0; i < w * h * bpp_b; i++)
        px[i] = (uint8_t)rnd(256);
    x = rint_(-20, t->w - 1), y = rint_(-20, t->h - 1);
    dw = engine_image_begin(x, y, w, h);
    for (j = 0; j < h; j++) {
        memset(row, 0, (size_t)dw * 4);
        memcpy(row, px + (long)j * w * bpp_b, (size_t)(w * bpp_b));
        engine_iload_row(row);
    }
    engine_iload_end();
    sw_image(&t->s, x, y, w, h, px, w * bpp_b);
    free(px);
}

/* ---- the cases, at one depth */

static void depth_cases(void)
{
    char n[32];
    int i, k;
    surface *ts[3] = { &scr, &s1, &s2 };

    name_of(n, "fill");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_fill(t);
    }
    check(n);

    name_of(n, "copy");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_copy(t);
    }
    target(&scr, 0, 0, SW, SH);                 /* the whole screen a line up, a line down, a column each way */
    engine_copy(0, 1, 0, 0, SW, SH - 1);
    sw_copy(&scr.s, 0, 0, &scr.s, 0, 1, SW, SH - 1);
    engine_copy(0, 0, 0, 1, SW, SH - 1);
    sw_copy(&scr.s, 0, 1, &scr.s, 0, 0, SW, SH - 1);
    engine_copy(1, 0, 0, 0, SW - 1, SH);
    sw_copy(&scr.s, 0, 0, &scr.s, 1, 0, SW - 1, SH);
    engine_copy(0, 0, 1, 0, SW - 1, SH);
    sw_copy(&scr.s, 1, 0, &scr.s, 0, 0, SW - 1, SH);
    check(n);

    name_of(n, "copy-from");
    for (i = 0; i < 30; i++) {
        surface *t = ts[i % 3], *f = ts[(i + 1 + (int)rnd(2)) % 3];
        random_clip(t);
        do_copy_from(t, f);
    }
    check(n);

    name_of(n, "mono");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_mono(t, 0, 0);
    }
    target(&scr, 0, 0, SW, SH);
    do_mono(&scr, 600, 300);                    /* (in parts: AR0's 2^17) */
    check(n);

    name_of(n, "mono-vram");
    for (i = 0; i < 40; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_mono_vram(t, 0, 0);
    }
    target(&scr, 0, 0, SW, SH);
    do_mono_vram(&scr, 500, 400);
    check(n);

    name_of(n, "image");
    for (i = 0; i < 30; i++) {
        surface *t = ts[i % 3];
        random_clip(t);
        do_image(t, 0, 0);
    }
    target(&scr, 0, 0, SW, SH);
    do_image(&scr, SW, 200);
    check(n);

    name_of(n, "mixed");
    for (i = 0; i < nops; i++) {
        surface *t = ts[rnd(3)];
        random_clip(t);
        k = (int)rnd(7);
        if (k == 0)
            do_fill(t);
        else if (k == 1)
            do_copy(t);
        else if (k == 2)
            do_copy_from(t, ts[rnd(3)]);
        else if (k == 3)
            do_mono(t, 0, 0);
        else if (k == 4)
            do_mono_vram(t, 0, 0);
        else if (k == 5)
            do_image(t, 0, 0);
        else
            engine_resync();                    /* (nothing the HAL draws may change) */
    }
    check(n);
}

/* ---- the cursor and the display start, from the emulated monitor */

static uint8_t *grab(int x, int y, int w, int h)
{
    uint8_t *p = malloc((size_t)w * (size_t)h * 4);
    uint16_t cw, ch;
    engine_vsync_wait(200000);
    engine_vsync_wait(200000);                  /* (a whole frame since the change) */
    if (!p || hx_ut_capture(&cw, &ch) < 0 || hx_ut_read(x, y, w, h, p) < 0) {
        free(p);
        return NULL;
    }
    return p;
}

static uint32_t at_(const uint8_t *p, int w, int x, int y)
{
    const uint8_t *q = p + ((long)y * w + x) * 4;
    return (uint32_t)q[2] << 16 | (uint32_t)q[1] << 8 | q[0];
}

static uint8_t and_mask[512], xor_mask[512];

/* A cursor row as seen and as meant: 0 and 1 the colours, . the screen,
   x the screen inverted, ? none of those. */
static void cursor_row(const uint8_t *under, const uint8_t *seen, int w, int y, int x0, int cx, int iy, uint32_t c0,
                       uint32_t c1)
{
    char got[65], want[65];
    int ix;
    for (ix = 0; ix < 64; ix++) {
        int x = cx + ix - x0, a = (and_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
        int o = (xor_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
        uint32_t u = x >= 0 && x < w ? at_(under, w, x, y) : 0, g = x >= 0 && x < w ? at_(seen, w, x, y) : 0;
        want[ix] = !a ? (o ? '1' : '0') : o ? 'x' : '.';
        got[ix] = x < 0 || x >= w ? ' ' : g == c0 ? '0' : g == c1 ? '1' : g == u ? '.' : g == (u ^ 0xFFFFFF) ? 'x' : '?';
    }
    got[64] = want[64] = 0;
    hx_log("HX-STAT cursor row %d seen %s", iy, got);
    hx_log("HX-STAT cursor row %d want %s", iy, want);
}

/* What the cursor shows over `under`, its image's top left at (cx, cy) of
   the grabbed area (x0, y0 on the screen); 0 when all agree. */
static int cursor_differs(const uint8_t *under, const uint8_t *seen, int x0, int y0, int w, int h, int cx, int cy,
                          uint32_t c0, uint32_t c1, char *why)
{
    int x, y;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++) {
            int ix = x + x0 - cx, iy = y + y0 - cy;
            uint32_t want = at_(under, w, x, y), got = at_(seen, w, x, y);
            if (ix >= 0 && ix < 64 && iy >= 0 && iy < 64) {
                int a = (and_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1, o = (xor_mask[iy * 8 + ix / 8] >> (7 - ix % 8)) & 1;
                want = !a ? (o ? c1 : c0) : o ? want ^ 0xFFFFFF : want;
            }
            if (got != want) {
                sprintf(why, "at %d,%d (image %d,%d): %06lx, want %06lx", x + x0, y + y0, ix, iy, (unsigned long)got,
                        (unsigned long)want);
                if (iy >= 0 && iy < 64)
                    cursor_row(under, seen, w, y, x0, cx, iy, c0, c1);
                return 1;
            }
        }
    return 0;
}

static void cursor_cases(void)
{
    const uint32_t c0 = 0x2040C0, c1 = 0xF0B030;        /* (red and blue told apart) */
    uint32_t off;
    uint8_t *under, *seen;
    char why[160] = "";
    int i, bad;

    for (i = 0; i < 512; i++) {                 /* each of the four kinds, in bands and specks */
        int y = i / 8, x8 = i % 8;
        and_mask[i] = (uint8_t)(y < 16 ? 0x00 : y < 32 ? 0xFF : (0xF0 >> (x8 & 1)) ^ (uint8_t)(y * 7));
        xor_mask[i] = (uint8_t)(y < 8 ? 0x00 : y < 16 ? 0xFF : y < 24 ? 0x00 : y < 32 ? 0xFF : (uint8_t)(x8 * 37 + y));
    }
    if (vram_alloc(&heap, MGA_CURSOR_BYTES, 1024, MGA_VRAM_TOP, 9, &off) != 0 ||
        (off >= (16u << 20) && vram_alloc(&heap, MGA_CURSOR_BYTES, 1024, 0, 9, &off) != 0) ||
        cursor_load(off, and_mask, xor_mask) != 0) {
        hx_test("cursor", 0, "no place for the image");
        return;
    }
    hx_stat("cursor at=%08lx", (unsigned long)off);
    cursor_colours(c0, c1);
    cursor_show(0);
    under = grab(80, 40, 96, 96);
    cursor_move(100, 50);
    cursor_show(1);
    seen = grab(80, 40, 96, 96);
    bad = !under || !seen || cursor_differs(under, seen, 80, 40, 96, 96, 100, 50, c0, c1, why);
    hx_test("cursor", !bad, "%s", !under || !seen ? "no capture" : bad ? why : "image, colours and place");
    free(under);
    free(seen);

    cursor_show(0);                             /* partly off the top left corner */
    under = grab(0, 0, 64, 64);
    cursor_move(-10, -20);
    cursor_show(1);
    seen = grab(0, 0, 64, 64);
    bad = !under || !seen || cursor_differs(under, seen, 0, 0, 64, 64, -10, -20, c0, c1, why);
    hx_test("cursor-edge", !bad, "%s", !under || !seen ? "no capture" : bad ? why : "clipped at the top left");
    free(under);
    free(seen);
    cursor_show(0);
}

static void start_case(const char *name, uint32_t at)
{
    uint8_t *before, *after;
    uint32_t row = (uint32_t)PITCH * (uint32_t)bpp_b;
    int bad;
    before = grab(0, 100, SW, 64);              /* rows 100-163, which `at` holds a copy of */
    if (at != 100 * row)
        memcpy((uint8_t *)mga_fb + at, (const uint8_t *)mga_fb + 100 * row, 64 * row);
    crtc_set_start(at);
    after = grab(0, 0, SW, 64);
    bad = !before || !after || memcmp(before, after, (size_t)SW * 64 * 4) != 0;
    hx_test(name, !bad, "start=%08lx%s", (unsigned long)at, !before || !after ? " no capture" : bad ? " other pixels" : "");
    free(before);
    free(after);
    crtc_set_start(0);
}

/* ---- set up */

static void place(surface *t, const char *name, int w, int h, int pitch_px)
{
    t->name = name;
    t->w = w, t->h = h, t->pitch_px = pitch_px;
    if (vram_alloc(&heap, (uint32_t)pitch_px * (uint32_t)h * (uint32_t)bpp_b, 64, 0, 2, &t->off) != 0) {
        hx_test("vram", 0, "no room for %s", name);
        hx_done(HX_FAIL);
    }
}

static int depth(int b, int with_display)
{
    mga_vbe_mode mode;
    int pitch = 0, i;
    char n[32];
    bpp = b, bpp_b = b / 8;
    sprintf(n, "mode-%d", b);
    if (vbe_find_mode(SW, SH, b, &mode) != 0 || vbe_set_mode(&mode, PITCH, &pitch) != 0 || pitch != PITCH) {
        hx_test(n, 0, "640x480x%d at pitch %d: pitch %d", b, PITCH, pitch);
        return 0;
    }
    if (!mga.vram_bytes) {
        mga.vram_bytes = mga_probe_vram();
        hx_stat("vram=%lu", (unsigned long)mga.vram_bytes);
    }
    engine_init(pitch, b);
    vram_init(&heap, 0, mga.vram_bytes);
    scr.name = "screen", scr.off = 0, scr.w = SW, scr.h = SH, scr.pitch_px = PITCH;
    vram_reserve(&heap, 0, (uint32_t)PITCH * SH * (uint32_t)bpp_b, 1);
    place(&s1, "s1", 300, 200, 320);
    place(&s2, "s2", 200, 150, 224);
    if (vram_alloc(&heap, 64 * 1024, 64, 0, 3, &glyphs) != 0) {
        hx_test("vram", 0, "no room for glyphs");
        return 0;
    }
    used = glyphs + 64 * 1024;
    free(shadow);
    if (!(shadow = malloc(used))) {
        hx_test(n, 0, "no memory for a copy of %lu bytes", (unsigned long)used);
        return 0;
    }
    for (i = 0; i < (int)used; i++)             /* both start the same, not blank */
        shadow[i] = (uint8_t)rnd(256);
    engine_sync(2000000);
    memcpy((void *)mga_fb, shadow, used);
    scr.s.mem = shadow + scr.off, s1.s.mem = shadow + s1.off, s2.s.mem = shadow + s2.off;
    scr.s.pitch_px = scr.pitch_px, s1.s.pitch_px = s1.pitch_px, s2.s.pitch_px = s2.pitch_px;
    scr.s.bpp = s1.s.bpp = s2.s.bpp = b;
    hx_test(n, 1, "pitch=%d s1=%08lx s2=%08lx glyphs=%08lx", pitch, (unsigned long)s1.off, (unsigned long)s2.off,
            (unsigned long)glyphs);
    depth_cases();
    if (with_display && hx_ut_present()) {
        cursor_cases();
        start_case("start", 100u * PITCH * (uint32_t)bpp_b);
        if (mga.vram_bytes > (8u << 20))
            start_case("start-high", (8u << 20) + 4096);
    }
    return 1;
}

int main(int argc, char **argv)
{
    int depths[3], nd = 0, i, ok;
    hx_init(1, argv, "twodtest");               /* (the arguments are TWODTEST's own) */
    for (i = 1; i < argc; i++) {
        if (!strncmp(argv[i], "ops=", 4))
            nops = atoi(argv[i] + 4);
        else if (atoi(argv[i]) == 8 || atoi(argv[i]) == 16 || atoi(argv[i]) == 32)
            depths[nd < 3 ? nd++ : 2] = atoi(argv[i]);
    }
    if (!nd)
        depths[0] = 8, depths[1] = 16, depths[2] = 32, nd = 3;
    ok = mga_find(&mga) == 0 && mga_map(&mga) == 0;
    hx_test("card", ok, "%s", ok ? mga.name : "none");
    if (!ok)
        hx_done(HX_INIT_FAILED);
    mga.vram_bytes = 0;                         /* (sized after the first mode set) */
    for (i = 0; i < nd; i++)
        depth(depths[i], depths[i] == 16);
    free(shadow);
    vbe_set_text_mode();
    hx_done(hx_failures() ? HX_FAIL : HX_PASS);
    return 0;
}
