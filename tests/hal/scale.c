/* scale.c - the mode planner, zoom and engine_present on a real (or
 * emulated) card, for any toolchain. For each size it plans a mode, sets
 * it, draws a known picture (CPU pattern, engine fills, a Gouraud
 * triangle), reads the picture back as the truth, shows it (natively,
 * zoomed or scaled by engine_present into the hidden display buffer),
 * compares the display buffer with a CPU model of the scaling, flips, and
 * compares the unit tester's screenshot of the monitor image with the
 * display buffer (doubled for zoom).
 *
 *   SCALE [--zoom] [--bilinear] [--force] [--sizes 320x240,...]
 *
 * A size the card's BIOS cannot show at all (1600x1200 on most) is
 * skipped, not failed. HX-STAT lines give the present time per size, which
 * only means something on a real card. */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NDEFAULT 10
static const int default_sizes[NDEFAULT][2] = {
    { 320, 200 }, { 320, 240 }, { 400, 300 }, { 512, 384 }, { 640, 480 },
    { 640, 512 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 },
};

static unsigned plan_flags;
static int filter = MGA_PRESENT_NEAREST;

static uint16_t pat(int x, int y)
{
    return (uint16_t)((((x * 7 + y * 13) & 0x1F) << 11) | (((x ^ y) & 0x3F) << 5) | ((x + 2 * y) & 0x1F));
}

static uint32_t align4k(uint32_t v) { return (v + 4095u) & ~4095u; }

static volatile uint16_t *px16(uint32_t off) { return (volatile uint16_t *)(mga_fb + off); }

/* The bilinear model: the first and last pixel centres on the first and
 * last texel centres, weights to 1/256. */
static uint16_t bilinear(const uint16_t *src, int w, int h, int i, int j, int dw, int dh)
{
    double u = dw > 1 ? (double)i * (w - 1) / (dw - 1) : 0, v = dh > 1 ? (double)j * (h - 1) / (dh - 1) : 0;
    int x0 = (int)u, y0 = (int)v, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0, c, out = 0;
    double fx = u - x0, fy = v - y0;
    static const int shift[3] = { 11, 5, 0 }, mask[3] = { 31, 63, 31 };
    for (c = 0; c < 3; c++) {
        double a = (src[y0 * w + x0] >> shift[c]) & mask[c], b = (src[y0 * w + x1] >> shift[c]) & mask[c];
        double d = (src[y1 * w + x0] >> shift[c]) & mask[c], e = (src[y1 * w + x1] >> shift[c]) & mask[c];
        double r = (a * (1 - fx) + b * fx) * (1 - fy) + (d * (1 - fx) + e * fx) * fy;
        out |= ((int)(r + 0.5) & mask[c]) << shift[c];
    }
    return (uint16_t)out;
}

static int close565(uint16_t a, uint16_t b, int tol)
{
    int dr = ((a >> 11) & 31) - ((b >> 11) & 31), dg = ((a >> 5) & 63) - ((b >> 5) & 63), db = (a & 31) - (b & 31);
    return abs(dr) <= tol && abs(dg) <= 2 * tol && abs(db) <= tol;
}

/* The expected display pixel at (X, Y) of the display mode. */
static uint16_t model(const mga_mode_plan *p, const uint16_t *src, int X, int Y)
{
    int i = X - p->dx, j = Y - p->dy;
    if (p->fit == MGA_FIT_ZOOM)
        return src[(Y / 2) * p->w + X / 2];
    if (i < 0 || j < 0 || i >= p->dw || j >= p->dh)
        return 0;
    if (p->fit == MGA_FIT_NATIVE || p->fit == MGA_FIT_TOPLEFT)
        return src[j * p->w + i];
    if (filter == MGA_PRESENT_BILINEAR)
        return bilinear(src, p->w, p->h, i, j, p->dw, p->dh);
    /* floor((pixel centre) x source/destination + 1/64), as engine_present samples */
    return src[(int)((((long)j * 2 + 1) * p->h * 32 + p->dh) / (64L * p->dh)) * p->w +
               (int)((((long)i * 2 + 1) * p->w * 32 + p->dw) / (64L * p->dw))];
}

static void wait_frames(int n)
{
    long spin;
    while (n-- > 0) {
        for (spin = 0; spin < 2000000L && engine_in_vblank(); spin++)
            ;
        for (spin = 0; spin < 2000000L && !engine_in_vblank(); spin++)
            ;
    }
}

static void one(int w, int h)
{
    mga_mode_plan p;
    mga_target t;
    mga_surface rs, ds;
    mga_svtx v[3];
    mga_tri_ctx ctx;
    char name[32];
    int dpitch = 0, W, H, direct, x, y, i, bad = 0, shot_bad = 0, tol = filter == MGA_PRESENT_BILINEAR ? 2 : 0;
    uint32_t disp[2], draw_off, t0, present_us = 0;
    uint16_t *src;
    uint8_t *bgrx = NULL;
    uint16_t sw = 0, sh = 0;
    snprintf(name, sizeof name, "%dx%d", w, h);
    if (vbe_plan_mode(w, h, 16, plan_flags, &p) != 0) {
        hx_log("HX-STAT scale %s skip no BIOS mode can show it", name);
        return;
    }
    W = p.disp.width; H = p.disp.height;
    if (vbe_set_mode(&p.disp, 0, &dpitch) != 0) {
        hx_test(name, 0, "mode set %dx%d failed", W, H);
        return;
    }
    vbe_set_zoom(p.zoom);
    {
        uint8_t ramp[256];                          /* 16 bpp goes through the DAC's LUT: make it identity */
        for (i = 0; i < 256; i++)
            ramp[i] = (uint8_t)i;
        dac_set_ramp(ramp);
    }
    engine_init(dpitch, 16);
    engine_set_maccess_flags(MACCESS_NODITHER);
    disp[0] = 0;
    disp[1] = align4k((uint32_t)dpitch * H * 2);
    direct = p.fit == MGA_FIT_NATIVE || p.fit == MGA_FIT_ZOOM || p.fit == MGA_FIT_TOPLEFT;
    rs.off = align4k(disp[1] + (uint32_t)dpitch * H * 2);
    rs.w = w; rs.h = h; rs.pitch_px = mga_pow2_pitch(w);
    ds.off = disp[1]; ds.w = W; ds.h = H; ds.pitch_px = dpitch;
    if (!direct && !mga_present_ok(&rs)) {
        vbe_set_zoom(1);
        hx_test(name, 0, "render surface of pitch %d cannot be presented", rs.pitch_px);
        return;
    }
    if (!direct && rs.off + (uint32_t)rs.pitch_px * h * 2 > mga.vram_bytes) {
        hx_log("HX-STAT scale %s skip %lu KB of VRAM is not enough for %s in %dx%d", name,
               (unsigned long)(mga.vram_bytes >> 10), mga_fit_name(p.fit), W, H);
        return;
    }
    /* Both display buffers black. */
    memset(&t, 0, sizeof t);
    t.pitch_px = dpitch; t.bpp = 16; t.zbits = 16;
    for (i = 0; i < 2; i++) {
        t.color_off = disp[i];
        engine_set_target(&t);
        engine_set_clip(0, 0, W, H);
        engine_fill(0, 0, W, H, 0);
    }
    engine_sync(200000);
    /* The picture: CPU pattern, then engine fills and a triangle on top. */
    draw_off = direct ? disp[1] : rs.off;
    t.color_off = draw_off;
    t.pitch_px = direct ? dpitch : rs.pitch_px;
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            px16(draw_off)[y * t.pitch_px + x] = pat(x, y);
    engine_set_target(&t);
    engine_set_clip(0, 0, w, h);
    engine_fill(w / 4, h / 4, w / 4, h / 4, 0xF81F);
    engine_fill(0, h - 1, w, 1, 0xFFFF);            /* last row and column: edge sampling */
    engine_fill(w - 1, 0, 1, h, 0x07E0);
    memset(v, 0, sizeof v);
    v[0].X16 = (w / 2) * 16; v[0].Y16 = (h / 8) * 16;
    v[1].X16 = (w - w / 8) * 16; v[1].Y16 = (h / 2) * 16;
    v[2].X16 = (w / 2) * 16; v[2].Y16 = (h - h / 8) * 16;
    for (i = 0; i < 3; i++) { v[i].a = 255; v[i].fog = 255; }
    v[0].r = 255; v[1].g = 255; v[2].b = 255;
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = MGA_S_COLOR;
    ctx.clip_y0 = 0; ctx.clip_y1 = h;
    setup_triangle(&v[0], &v[1], &v[2], &ctx);
    engine_sync(200000);
    src = (uint16_t *)malloc((size_t)w * h * 2);
    bgrx = (uint8_t *)malloc((size_t)W * 4);
    if (!src || !bgrx) {
        free(src); free(bgrx);
        vbe_set_zoom(1);
        hx_test(name, 0, "out of memory");
        return;
    }
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            src[y * w + x] = px16(draw_off)[y * t.pitch_px + x];
    /* Show it. */
    if (!direct) {
        t0 = sys_time_us();
        engine_present(&rs, &ds, p.dx, p.dy, p.dw, p.dh, filter);
        engine_sync(200000);
        present_us = sys_time_us() - t0;
    }
    /* The display buffer against the model (zoom: the buffer holds the
     * picture at 1:1; the doubling happens on the way to the monitor). */
    for (y = 0; y < (p.fit == MGA_FIT_ZOOM ? h : H); y++)
        for (x = 0; x < (p.fit == MGA_FIT_ZOOM ? w : W); x++) {
            uint16_t got = px16(disp[1])[y * dpitch + x];
            uint16_t want = p.fit == MGA_FIT_ZOOM ? src[y * w + x] : model(&p, src, x, y);
            if (!close565(got, want, tol) && bad++ < 4)
                hx_log("HX-STAT scale %s buffer (%d,%d) got %04x want %04x", name, x, y, got, want);
        }
    /* The monitor image. */
    vbe_set_display_start(disp[1], dpitch * 2, 16);
    wait_frames(3);
    if (getenv("SCALE_SNAP")) {                     /* a picture of each size, and the CRTC, for a person */
        char n8[9], line[160];
        int r, k = 0;
        snprintf(n8, sizeof n8, "S%dX%d", w, h);
        hx_snap_screen(n8);
        for (r = 0; r < 0x19; r++) {
            MGA_WR8(MGAREG_CRTC_INDEX, (uint8_t)r);
            k += snprintf(line + k, sizeof line - (size_t)k, "%02x", MGA_RD8(MGAREG_CRTC_DATA));
        }
        hx_log("HX-STAT scale %s crtc %s", name, line);
        for (r = 0, k = 0; r < 6; r++) {
            MGA_WR8(MGAREG_CRTCEXT_INDEX, (uint8_t)r);
            k += snprintf(line + k, sizeof line - (size_t)k, "%02x", MGA_RD8(MGAREG_CRTCEXT_DATA));
        }
        hx_log("HX-STAT scale %s crtcext %s", name, line);
    }
    if (hx_ut_present() && hx_ut_capture(&sw, &sh) == 0) {
        if (sw != W || sh != H)
            shot_bad = -1;
        else
            for (y = 0; y < H; y++) {
                if (hx_ut_read(0, y, W, 1, bgrx) != 0) { shot_bad = -2; break; }
                for (x = 0; x < W; x++) {
                    uint16_t got = (uint16_t)(((bgrx[x * 4 + 2] >> 3) << 11) | ((bgrx[x * 4 + 1] >> 2) << 5) |
                                              (bgrx[x * 4 + 0] >> 3));
                    uint16_t want = p.fit == MGA_FIT_ZOOM ? src[(y / 2) * w + x / 2]
                                                          : px16(disp[1])[y * dpitch + x];
                    if (got != want && shot_bad++ < 4)
                        hx_log("HX-STAT scale %s screen (%d,%d) got %04x want %04x", name, x, y, got, want);
                }
            }
    }
    hx_log("HX-STAT scale %s fit=%s display=%dx%d pitch=%d rect=%d,%d,%dx%d present_us=%lu buffer_bad=%d screen_bad=%d",
           name, mga_fit_name(p.fit), W, H, dpitch, p.dx, p.dy, p.dw, p.dh, (unsigned long)present_us, bad, shot_bad);
    hx_test(name, bad == 0 && shot_bad == 0, "%s in %dx%d: %d buffer and %d screen mismatches",
            mga_fit_name(p.fit), W, H, bad, shot_bad);
    vbe_set_zoom(1);
    free(src); free(bgrx);
}

int main(int argc, char **argv)
{
    int i, n = 0, sizes[16][2];
    const char *list = NULL;
    for (i = 1; i < argc; i++) {
        int take = 1;
        if (!strcmp(argv[i], "--zoom")) plan_flags |= MGA_PLAN_ZOOM;
        else if (!strcmp(argv[i], "--force")) plan_flags |= MGA_PLAN_FORCE;
        else if (!strcmp(argv[i], "--bilinear")) filter = MGA_PRESENT_BILINEAR;
        else if (!strcmp(argv[i], "--sizes") && i + 1 < argc) { list = argv[i + 1]; take = 2; }
        else take = 0;
        if (take) {
            memmove(&argv[i], &argv[i + take], (size_t)(argc - i - take + 1) * sizeof *argv);
            argc -= take; i--;
        }
    }
    hx_init(argc, argv, "scale");
    while (list && *list && n < 16) {
        int w = 0, h = 0;
        if (sscanf(list, "%dx%d", &w, &h) == 2) { sizes[n][0] = w; sizes[n][1] = h; n++; }
        list = strchr(list, ',');
        if (list) list++;
    }
    if (!n) {
        memcpy(sizes, default_sizes, sizeof default_sizes);
        n = NDEFAULT;
    }
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        hx_test("find", 0, "no Matrox card");
        hx_done(HX_INIT_FAILED);
    }
    {
        mga_vbe_mode m;                             /* probing writes VRAM: only in a graphics mode */
        int pitch;
        if (vbe_find_mode(640, 480, 16, &m) == 0 && vbe_set_mode(&m, 0, &pitch) == 0)
            mga.vram_bytes = mga_probe_vram();
    }
    hx_log("HX-STAT scale card=%04x vram=%lu flags=%u filter=%s sizes=%d", mga.device_id,
           (unsigned long)mga.vram_bytes, plan_flags, filter == MGA_PRESENT_BILINEAR ? "bilinear" : "nearest", n);
    for (i = 0; i < n; i++)
        one(sizes[i][0], sizes[i][1]);
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
