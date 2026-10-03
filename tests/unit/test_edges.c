/* test_edges - edge terms within the chips' AR fields (mga.ar_bits).
 *
 * The G100 and G200 hold AR0, AR2, AR4, AR5 and AR6 in 18 bits, signed, the
 * G400 in 22 (AR1: 24), and the trapezoid walk keeps its error terms there
 * (docs/loop-c-results.md). Checks that the setup's edges fit and draw what
 * the unlimited ones would:
 *   - exact edges, divided down to fit: the same column on every row;
 *   - whole triangles, refrast at the chip's width against the ideal
 *     (32 bits, no limit): exact edges identical; Voodoo edges identical
 *     unless one fell back to the centre rule (counted);
 *   - triangles wider or taller than 8191 pixels (split in four): only
 *     pixels within reach of the edges differ, none inside goes missing. */
#include "unit.h"
#include "refrast.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include "mga/fp.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
int mga_fifo_free;
void fifo_reserve(int n) { (void)n; }
void fifo_reset(void) {}
uint32_t sys_time_us(void) { static uint32_t t; return t += 10; }
void sys_delay_us(uint32_t us) { (void)us; }

static uint32_t seed = 0x2545F491u;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int32_t rndi(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }

/* AR writes outside the family's fields. */
static long ar_overflow;
static void hook(uint32_t off, uint32_t v)
{
    int32_t sv = (int32_t)v;
    int bits;
    if (off < MGAREG_AR0 || off > MGAREG_AR6)
        return;
    bits = (off == MGAREG_AR1 || off == MGAREG_AR3) ? 24 : mga.ar_bits;
    if (bits && (sv < -(1 << (bits - 1)) || sv > (1 << (bits - 1)) - 1))
        ar_overflow++;
}

/* The engine's walk for n rows, in 64 bits or wrapping at the field width. */
static int32_t wrap(int64_t v, int bits)
{
    uint32_t m, u;
    if (!bits || bits >= 32)
        return (int32_t)v;
    m = (1u << bits) - 1;
    u = (uint32_t)v & m;
    return (int32_t)((u & (1u << (bits - 1))) ? (u | ~m) : u);
}
static void walk(const mga_edge *e, int n, int bits, int32_t *xs)
{
    int64_t err = wrap(e->ar_err, bits), step = wrap(e->ar_step, bits), dec = wrap(e->ar_dec, bits);
    int32_t x = e->x;
    int k, guard;
    for (k = 0; k < n; k++) {
        xs[k] = x;
        for (guard = 0; err < 0 && step && guard < 300000; guard++) {   /* the engine stops on AR0 = 0 */
            err = wrap(err + step, bits);
            x += e->neg ? -1 : 1;
        }
        err = wrap(err + dec, bits);
    }
}

static void exact_edges(int bits, int n_edges)
{
    static int32_t want[2048], got[2048];
    int i, bad = 0, unfit = 0;
    for (i = 0; i < n_edges; i++) {
        int32_t span = rnd() & 1 ? 16 * 8191 : 16 * 600;
        int32_t Xa = rndi(-span / 2, span / 2), Ya = rndi(-span / 2, span / 2);
        int32_t Xb = Xa + rndi(-span / 2, span / 2), Yb = Ya + rndi(1, span / 2);
        int32_t y0 = (Ya + 7) >> 4, y1 = (Yb + 7) >> 4, ys, n;
        mga_edge ideal, fit;
        if (Yb - Ya > 16 * 8191 || Xb - Xa > 16 * 8191 || Xa - Xb > 16 * 8191 || y1 <= y0)
            continue;
        ys = rndi(y0, y1 - 1);
        n = y1 - ys < 512 ? y1 - ys : 512;
        mga.ar_bits = 0;
        setup_edge(Xa, Ya, Xb, Yb, ys, &ideal);
        mga.ar_bits = (uint8_t)bits;
        setup_edge(Xa, Ya, Xb, Yb, ys, &fit);
        if (fit.ar_step >= 1 << (bits - 1) || fit.ar_dec < -(1 << (bits - 1)))
            unfit++;
        walk(&ideal, n, 0, want);
        walk(&fit, n, bits, got);
        if (memcmp(want, got, (size_t)n * sizeof want[0]))
            bad++;
    }
    CHECK_EQ(unfit, 0);
    CHECK_EQ(bad, 0);
    printf("test_edges: %d-bit fields: %d exact edges up to 8191 px, %d unfit, %d walk differently\n", bits, n_edges,
           unfit, bad);
}

/* One triangle into a fresh refrast at the given width; the HAL's limit set
 * for the same family (0: none). Returns the 1024x1024 16-bit image. */
static uint16_t img_a[1024 * 1024], img_b[1024 * 1024];
static void render(uint16_t *img, int rr_bits, int hal_bits, const mga_svtx *v, uint32_t flags)
{
    mga_target t;
    mga_tri_ctx ctx;
    refrast_init();
    refrast_ar_bits = rr_bits;
    refrast_write_hook = hook;
    mga.ar_bits = (uint8_t)hal_bits;
    engine_init(1024, 16);
    memset(&t, 0, sizeof t);
    t.color_off = 0;
    t.pitch_px = 1024;
    t.bpp = 16;
    t.zbits = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, 1024, 1024);
    engine_fill(0, 0, 1024, 1024, 0);
    setup_invalidate();
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = flags;
    ctx.clip_y0 = 0;
    ctx.clip_y1 = 1024;
    setup_triangle(&v[0], &v[1], &v[2], &ctx);
    refrast_write_hook = NULL;
    memcpy(img, rr->vram, 1024 * 1024 * 2);
}

static void tri_vertices(mga_svtx *v, int32_t lo, int32_t hi)
{
    int i;
    memset(v, 0, 3 * sizeof *v);
    for (i = 0; i < 3; i++) {
        v[i].X16 = rndi(lo, hi);
        v[i].Y16 = rndi(lo, hi);
        v[i].r = 255; v[i].g = 255; v[i].a = 255; v[i].fog = 255;
    }
}

static void triangles(mga_family fam, uint32_t flags, int n, int32_t lo, int32_t hi)
{
    int i, differ = 0, fallbacks = 0;
    long over = 0, px_diff = 0, px = 0;
    mga.family = fam;
    mga_chip_caps(&mga);
    {
        int bits = mga.ar_bits;
        for (i = 0; i < n; i++) {
            mga_svtx v[3];
            uint32_t fb0;
            int j;
            tri_vertices(v, lo, hi);
            render(img_b, 32, 0, v, flags);                    /* the ideal */
            fb0 = setup_stats.vfallback;
            ar_overflow = 0;
            render(img_a, bits, bits, v, flags);               /* the chip */
            over += ar_overflow;
            if (setup_stats.vfallback != fb0)
                fallbacks++;
            if (memcmp(img_a, img_b, sizeof img_a)) {
                if (setup_stats.vfallback == fb0)
                    differ++;
                for (j = 0; j < 1024 * 1024; j++)
                    px_diff += img_a[j] != img_b[j];
            }
            for (j = 0; j < 1024 * 1024; j++)
                px += img_b[j] != 0;
        }
        CHECK_EQ(over, 0);
        CHECK_EQ(differ, 0);
        printf("test_edges: %s %s: %d triangles in [%d, %d] px, %d with a centre-rule edge, %ld of %ld pixels "
               "differ (all from those), %ld AR values too wide\n", fam == MGA_FAMILY_G400 ? "g400" : "g200",
               flags & MGA_S_VOODOO_EDGES ? "voodoo" : "exact ", n, lo / 16, hi / 16, fallbacks, px_diff, px, over);
    }
}

/* Distance from pixel centre (px, py) to the segment a-b, in pixels. */
static double seg_dist(double px, double py, const mga_svtx *a, const mga_svtx *b)
{
    double ax = a->X16 / 16.0, ay = a->Y16 / 16.0, bx = b->X16 / 16.0, by = b->Y16 / 16.0;
    double dx = bx - ax, dy = by - ay, t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return hypot(px - (ax + t * dx), py - (ay + t * dy));
}

static void splits(int n)
{
    int i;
    long far_diff = 0, over = 0, near_diff = 0;
    uint32_t s0 = setup_stats.splits;
    mga.family = MGA_FAMILY_G200;
    mga_chip_caps(&mga);
    for (i = 0; i < n; i++) {
        mga_svtx v[3];
        int x, y;
        /* Spans 8192-20000 pixels, crossing the 1024x1024 window. */
        tri_vertices(v, -16 * 10000, 16 * 10000);
        v[0].X16 = rndi(-16 * 200, 16 * 1200);
        v[0].Y16 = rndi(-16 * 200, 16 * 1200);
        render(img_b, 32, 0, v, 0);
        ar_overflow = 0;
        render(img_a, 18, 18, v, 0);
        over += ar_overflow;
        for (y = 0; y < 1024; y++)
            for (x = 0; x < 1024; x++)
                if (img_a[y * 1024 + x] != img_b[y * 1024 + x]) {
                    double d0 = seg_dist(x + 0.5, y + 0.5, &v[0], &v[1]), d1 = seg_dist(x + 0.5, y + 0.5, &v[1], &v[2]),
                           d2 = seg_dist(x + 0.5, y + 0.5, &v[2], &v[0]);
                    if (d0 < 1.0 || d1 < 1.0 || d2 < 1.0)
                        near_diff++;
                    else
                        far_diff++;
                }
    }
    CHECK_EQ(over, 0);
    CHECK_EQ(far_diff, 0);
    CHECK(setup_stats.splits > s0);
    printf("test_edges: %d triangles over 8191 px on the G200: %u splits, %ld pixels differ within 1 px of an edge, "
           "%ld elsewhere\n", n, setup_stats.splits - s0, near_diff, far_diff);
}

int unit_main(void)
{
    FPU_ENTER();
    exact_edges(18, 20000);
    exact_edges(22, 5000);
    triangles(MGA_FAMILY_G200, 0, 60, -16 * 1500, 16 * 2500);
    triangles(MGA_FAMILY_G200, MGA_S_VOODOO_EDGES, 60, -16 * 200, 16 * 1200);
    triangles(MGA_FAMILY_G400, 0, 30, -16 * 1500, 16 * 2500);
    triangles(MGA_FAMILY_G400, MGA_S_VOODOO_EDGES, 30, -16 * 200, 16 * 1200);
    splits(12);
    FPU_LEAVE();
    return 0;
}
