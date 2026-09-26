/* trap.c - triangle setup for the MGA trapezoid engine.
 *
 * Coverage: a pixel (x, y) is drawn when its centre (x+0.5, y+0.5) lies
 * inside the triangle, with the top and left edges inclusive (the top-left
 * rule). Rows and edge positions are computed exactly in integers from
 * 1/16-pixel vertices; the engine's edge DDA
 *     while (AR1 < 0) { AR1 += AR0; x += dir; }  AR1 += AR2;
 * then reproduces the same columns row after row. */
#include "mga/setup.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/fp.h"

mga_setup_stats setup_stats;

static int64_t floordiv64(int64_t a, int64_t b)
{
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0)))
        q--;
    return q;
}

/* First covered row for an edge starting at Y (1/16 px): ceil((Y-8)/16). */
static int32_t first_row(int32_t Y) { return (int32_t)floordiv64((int64_t)Y - 8 + 15, 16); }

/* An edge whose first covered column on row k (k = 0 at the trapezoid's
 * first row) is x_k = floor((N0 + S*k) / D), D > 0. The engine walks it
 * with: while (AR1 < 0) { AR1 += AR0; x += dir; } AR1 += AR2. */
static void edge_from_line(int64_t N0, int64_t S, int64_t D, mga_edge *e)
{
    int64_t x0 = floordiv64(N0, D);
    int64_t r0 = N0 - x0 * D;                      /* 0 <= r0 < D */
    e->x = (int32_t)x0;
    e->neg = S < 0;
    e->ar_step = (int32_t)D;
    e->ar_dec = (int32_t)(S < 0 ? S : -S);
    e->ar_err = (int32_t)((S < 0 ? r0 : (D - 1 - r0)) + e->ar_dec);
}

/* Exact mode: column covered when its centre is inside (top-left rule). */
void setup_edge(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e)
{
    int64_t dY = (int64_t)Yb - Ya, dX = (int64_t)Xb - Xa;
    int64_t P = (int64_t)Xa * dY + ((int64_t)16 * ys + 8 - Ya) * dX;    /* X(ys) * dY */
    /* ceil(X/16 - 1/2) = floor((P - 8dY + 16dY - 1) / 16dY) */
    edge_from_line(P - 8 * dY + 16 * dY - 1, 16 * dX, 16 * dY, e);
}

/* Voodoo mode: the Voodoo evaluates each edge in 16.16 fixed point with a
 * truncated slope and takes columns [floor(xl + 7/16), floor(xr + 7/16)),
 * sampling at x + 9/16. Matching it keeps shared edges identical to the
 * reference frames. */
void setup_edge_voodoo(int32_t Xa, int32_t Ya, int32_t Xb, int32_t Yb, int32_t ys, mga_edge *e)
{
    int64_t d = (((int64_t)Xb - Xa) << 16) / ((int64_t)Yb - Ya);      /* truncates toward zero */
    int64_t c0 = (int64_t)16 * ys + 8 - Ya;
    int64_t prod = d * c0;
    int64_t N0 = ((int64_t)Xa << 12) + (prod >= 0 ? prod >> 4 : -((-prod + 15) >> 4)) + 0x7000;
    edge_from_line(N0, d, 65536, e);
}

/* Plane A(x, y) = A0 + dx*(x - x0) + dy*(y - y0) in pixel units. */
typedef struct { double a0, dx, dy; } plane;

static void make_plane(plane *p, const mga_svtx *v[3], double a0, double a1, double a2, double inv)
{
    double x0 = v[0]->X16 / 16.0, y0 = v[0]->Y16 / 16.0;
    double x1 = v[1]->X16 / 16.0 - x0, y1 = v[1]->Y16 / 16.0 - y0;
    double x2 = v[2]->X16 / 16.0 - x0, y2 = v[2]->Y16 / 16.0 - y0;
    double d1 = a1 - a0, d2 = a2 - a0;
    p->a0 = a0;
    p->dx = (d1 * y2 - d2 * y1) * inv;
    p->dy = (x1 * d2 - x2 * d1) * inv;
}

static double eval(const plane *p, const mga_svtx *v0, double px, double py)
{
    return p->a0 + p->dx * (px - v0->X16 / 16.0) + p->dy * (py - v0->Y16 / 16.0);
}

static int32_t fx(double v, double scale)
{
    double r = mga_floor(v * scale + 0.5);
    if (r > 2147483647.0) r = 2147483647.0;
    if (r < -2147483648.0) r = -2147483648.0;
    return (int32_t)r;
}

/* Start value for colour-like 9.15 channels: truncating engine, so bias by
 * half an LSB of the 8-bit result is not applied (matches Voodoo floor). */
static uint32_t col_start(double v)
{
    double r = mga_floor(v * 32768.0);
    if (r < 0) r = 0;
    if (r > 255.999 * 32768.0) r = 255.999 * 32768.0;
    return (uint32_t)r;
}

static uint32_t z16_start(double z)
{
    double r = mga_floor(z * 32768.0);
    if (r < 0) r = 0;
    if (r > 65535.999 * 32768.0) r = 65535.999 * 32768.0;
    return (uint32_t)r;
}

static uint64_t z32_start(double z)
{
    double r = mga_floor(z * 32768.0);
    if (r < 0) r = 0;
    if (r > 4294967295.999 * 32768.0) r = 4294967295.999 * 32768.0;
    return (uint64_t)r;
}

static int64_t z32_inc(double d)
{
    return (int64_t)mga_floor(d * 32768.0 + 0.5);
}

void setup_triangle(const mga_svtx *a, const mga_svtx *b, const mga_svtx *c, const mga_tri_ctx *ctx)
{
    const mga_svtx *v[3], *t;
    int64_t area2, cross;
    int mid_right;
    double inv;
    plane pz, pr, pg, pb, pa, pf, ps, pt, pq, psr, psg, psb;
    double K = 1.0;
    int k = 0;
    int32_t y_top, y_mid, y_bot, part;
    uint32_t flags = ctx->flags;

    area2 = (int64_t)(b->X16 - a->X16) * (c->Y16 - a->Y16) - (int64_t)(c->X16 - a->X16) * (b->Y16 - a->Y16);
    if (area2 == 0) {
        setup_stats.culled_empty++;
        return;
    }
    /* Sort top to bottom (ties by x) so v0 is top, v2 bottom. */
    v[0] = a; v[1] = b; v[2] = c;
#define LESS(p, q) ((p)->Y16 < (q)->Y16 || ((p)->Y16 == (q)->Y16 && (p)->X16 < (q)->X16))
    if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
    if (LESS(v[2], v[1])) { t = v[1]; v[1] = v[2]; v[2] = t; }
    if (LESS(v[1], v[0])) { t = v[0]; v[0] = v[1]; v[1] = t; }
#undef LESS
    cross = (int64_t)(v[2]->X16 - v[0]->X16) * (v[1]->Y16 - v[0]->Y16) -
            (int64_t)(v[2]->Y16 - v[0]->Y16) * (v[1]->X16 - v[0]->X16);
    if (cross == 0) {
        setup_stats.culled_empty++;
        return;
    }
    mid_right = cross < 0;
    setup_stats.tris++;

    /* Plane gradients in pixel units (area of the sorted triangle). */
    {
        double x1 = (v[1]->X16 - v[0]->X16) / 16.0, y1 = (v[1]->Y16 - v[0]->Y16) / 16.0;
        double x2 = (v[2]->X16 - v[0]->X16) / 16.0, y2 = (v[2]->Y16 - v[0]->Y16) / 16.0;
        inv = 1.0 / (x1 * y2 - x2 * y1);
    }
    if (flags & MGA_S_Z)
        make_plane(&pz, v, v[0]->z, v[1]->z, v[2]->z, inv);
    if (flags & MGA_S_COLOR) {
        make_plane(&pr, v, v[0]->r, v[1]->r, v[2]->r, inv);
        make_plane(&pg, v, v[0]->g, v[1]->g, v[2]->g, inv);
        make_plane(&pb, v, v[0]->b, v[1]->b, v[2]->b, inv);
    }
    if (flags & MGA_S_ALPHA)
        make_plane(&pa, v, v[0]->a, v[1]->a, v[2]->a, inv);
    if (flags & MGA_S_FOG)
        make_plane(&pf, v, v[0]->fog, v[1]->fog, v[2]->fog, inv);
    if (flags & MGA_S_SPEC) {
        make_plane(&psr, v, v[0]->sr, v[1]->sr, v[2]->sr, inv);
        make_plane(&psg, v, v[0]->sg, v[1]->sg, v[2]->sg, inv);
        make_plane(&psb, v, v[0]->sb, v[1]->sb, v[2]->sb, inv);
    }
    if (flags & MGA_S_TEX) {
        /* Prescale K = 2^k keeps the most bits through the engine's
         * truncation of s/w before the divide by q (TMR6 12.20 < 2048,
         * TMR8 16.16 < 32768). */
        double ms = 0, mq = 0;
        int i;
        for (i = 0; i < 3; i++) {
            double as = v[i]->s < 0 ? -v[i]->s : v[i]->s, at = v[i]->t < 0 ? -v[i]->t : v[i]->t;
            if (as > ms) ms = as;
            if (at > ms) ms = at;
            if (v[i]->q > mq) mq = v[i]->q;
        }
        k = 15;
        while (k > -8 && ((ms * (double)(1 << (k + 8)) / 256.0) >= 2047.0 || (mq * (double)(1 << (k + 8)) / 256.0) >= 32767.0))
            k--;
        K = (double)(1 << (k + 8)) / 256.0;
        make_plane(&ps, v, v[0]->s * K, v[1]->s * K, v[2]->s * K, inv);
        make_plane(&pt, v, v[0]->t * K, v[1]->t * K, v[2]->t * K, inv);
        make_plane(&pq, v, v[0]->q * K, v[1]->q * K, v[2]->q * K, inv);
    }

    /* Per-triangle increments. */
    fifo_reserve(1);
    MGA_WR32(MGAREG_DWGCTL, ctx->dwgctl);
    if (flags & MGA_S_Z) {
        if (flags & MGA_S_Z32) {
            int64_t zx = z32_inc(pz.dx), zy = z32_inc(pz.dy);
            fifo_reserve(4);
            MGA_WR32(MGAREG_DR2_Z32LSB, (uint32_t)zx);
            MGA_WR32(MGAREG_DR2_Z32MSB, (uint32_t)((uint64_t)zx >> 32) & 0xFFFF);
            MGA_WR32(MGAREG_DR3_Z32LSB, (uint32_t)zy);
            MGA_WR32(MGAREG_DR3_Z32MSB, (uint32_t)((uint64_t)zy >> 32) & 0xFFFF);
        } else {
            fifo_reserve(2);
            MGA_WR32(MGAREG_DR2, (uint32_t)fx(pz.dx, 32768.0));
            MGA_WR32(MGAREG_DR3, (uint32_t)fx(pz.dy, 32768.0));
        }
    }
    fifo_reserve(6);
    if (flags & MGA_S_COLOR) {
        MGA_WR32(MGAREG_DR6, (uint32_t)fx(pr.dx, 32768.0));
        MGA_WR32(MGAREG_DR7, (uint32_t)fx(pr.dy, 32768.0));
        MGA_WR32(MGAREG_DR10, (uint32_t)fx(pg.dx, 32768.0));
        MGA_WR32(MGAREG_DR11, (uint32_t)fx(pg.dy, 32768.0));
        MGA_WR32(MGAREG_DR14, (uint32_t)fx(pb.dx, 32768.0));
        MGA_WR32(MGAREG_DR15, (uint32_t)fx(pb.dy, 32768.0));
    } else {
        MGA_WR32(MGAREG_DR6, 0); MGA_WR32(MGAREG_DR7, 0);
        MGA_WR32(MGAREG_DR10, 0); MGA_WR32(MGAREG_DR11, 0);
        MGA_WR32(MGAREG_DR14, 0); MGA_WR32(MGAREG_DR15, 0);
    }
    if (flags & MGA_S_ALPHA) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_ALPHAXINC, (uint32_t)fx(pa.dx, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_ALPHAYINC, (uint32_t)fx(pa.dy, 32768.0) & 0xFFFFFF);
    }
    if (flags & MGA_S_FOG) {
        fifo_reserve(2);
        MGA_WR32(MGAREG_FOGXINC, (uint32_t)fx(pf.dx, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_FOGYINC, (uint32_t)fx(pf.dy, 32768.0) & 0xFFFFFF);
    }
    if (flags & MGA_S_SPEC) {
        fifo_reserve(6);
        MGA_WR32(MGAREG_SPECRXINC, (uint32_t)fx(psr.dx, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_SPECRYINC, (uint32_t)fx(psr.dy, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_SPECGXINC, (uint32_t)fx(psg.dx, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_SPECGYINC, (uint32_t)fx(psg.dy, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_SPECBXINC, (uint32_t)fx(psb.dx, 32768.0) & 0xFFFFFF);
        MGA_WR32(MGAREG_SPECBYINC, (uint32_t)fx(psb.dy, 32768.0) & 0xFFFFFF);
    }

    if (flags & MGA_S_TEX) {
        int tw = ctx->tex_tw, th = ctx->tex_th;
        fifo_reserve(8);
        MGA_WR32(MGAREG_TEXWIDTH, TEXWH(tw, 8 - tw - k, (1u << tw) - 1));
        MGA_WR32(MGAREG_TEXHEIGHT, TEXWH(th, 8 - th - k, (1u << th) - 1));
        MGA_WR32(MGAREG_TMR(0), (uint32_t)fx(ps.dx, 1048576.0));
        MGA_WR32(MGAREG_TMR(1), (uint32_t)fx(ps.dy, 1048576.0));
        MGA_WR32(MGAREG_TMR(2), (uint32_t)fx(pt.dx, 1048576.0));
        MGA_WR32(MGAREG_TMR(3), (uint32_t)fx(pt.dy, 1048576.0));
        MGA_WR32(MGAREG_TMR(4), (uint32_t)fx(pq.dx, 65536.0));
        MGA_WR32(MGAREG_TMR(5), (uint32_t)fx(pq.dy, 65536.0));
    }

    y_top = first_row(v[0]->Y16);
    y_mid = first_row(v[1]->Y16);
    y_bot = first_row(v[2]->Y16);

    for (part = 0; part < 2; part++) {
        int32_t ys = part ? y_mid : y_top, ye = part ? y_bot : y_mid;
        const mga_svtx *sa = part ? v[1] : v[0], *sb = part ? v[2] : v[1];
        mga_edge el, er, lng, sht;
        double px, py;
        if (ys < ctx->clip_y0) ys = ctx->clip_y0;
        if (ye > ctx->clip_y1) ye = ctx->clip_y1;
        if (ye <= ys)
            continue;
        if (flags & MGA_S_VOODOO_EDGES) {
            setup_edge_voodoo(v[0]->X16, v[0]->Y16, v[2]->X16, v[2]->Y16, ys, &lng);
            setup_edge_voodoo(sa->X16, sa->Y16, sb->X16, sb->Y16, ys, &sht);
        } else {
            setup_edge(v[0]->X16, v[0]->Y16, v[2]->X16, v[2]->Y16, ys, &lng);
            setup_edge(sa->X16, sa->Y16, sb->X16, sb->Y16, ys, &sht);
        }
        if (mid_right) { el = lng; er = sht; } else { el = sht; er = lng; }
        setup_stats.traps++;
        px = el.x + 0.5;
        py = ys + 0.5;
        fifo_reserve(8);
        MGA_WR32(MGAREG_AR0, (uint32_t)el.ar_step);
        MGA_WR32(MGAREG_AR1, (uint32_t)el.ar_err);
        MGA_WR32(MGAREG_AR2, (uint32_t)el.ar_dec);
        MGA_WR32(MGAREG_AR4, (uint32_t)er.ar_err);
        MGA_WR32(MGAREG_AR5, (uint32_t)er.ar_dec);
        MGA_WR32(MGAREG_AR6, (uint32_t)er.ar_step);
        MGA_WR32(MGAREG_SGN, (el.neg ? SGN_SDXL : 0) | (er.neg ? SGN_SDXR : 0));
        MGA_WR32(MGAREG_FXBNDRY, ((uint32_t)(er.x & 0xFFFF) << 16) | (uint32_t)(el.x & 0xFFFF));
        if (flags & MGA_S_Z) {
            double z = eval(&pz, v[0], px, py);
            if (flags & MGA_S_Z32) {
                uint64_t zs = z32_start(z);
                fifo_reserve(2);
                MGA_WR32(MGAREG_DR0_Z32LSB, (uint32_t)zs);
                MGA_WR32(MGAREG_DR0_Z32MSB, (uint32_t)(zs >> 32) & 0xFFFF);
            } else {
                fifo_reserve(1);
                MGA_WR32(MGAREG_DR0, z16_start(z));
            }
        }
        fifo_reserve(3);
        if (flags & MGA_S_COLOR) {
            MGA_WR32(MGAREG_DR4, col_start(eval(&pr, v[0], px, py)));
            MGA_WR32(MGAREG_DR8, col_start(eval(&pg, v[0], px, py)));
            MGA_WR32(MGAREG_DR12, col_start(eval(&pb, v[0], px, py)));
        } else {
            MGA_WR32(MGAREG_DR4, col_start(v[0]->r));
            MGA_WR32(MGAREG_DR8, col_start(v[0]->g));
            MGA_WR32(MGAREG_DR12, col_start(v[0]->b));
        }
        if (flags & MGA_S_ALPHA) {
            fifo_reserve(1);
            MGA_WR32(MGAREG_ALPHASTART, col_start(eval(&pa, v[0], px, py)));
        }
        if (flags & MGA_S_FOG) {
            fifo_reserve(1);
            MGA_WR32(MGAREG_FOGSTART, col_start(eval(&pf, v[0], px, py)));
        }
        if (flags & MGA_S_SPEC) {
            fifo_reserve(3);
            MGA_WR32(MGAREG_SPECRSTART, col_start(eval(&psr, v[0], px, py)));
            MGA_WR32(MGAREG_SPECGSTART, col_start(eval(&psg, v[0], px, py)));
            MGA_WR32(MGAREG_SPECBSTART, col_start(eval(&psb, v[0], px, py)));
        }
        if (flags & MGA_S_TEX) {
            fifo_reserve(3);
            MGA_WR32(MGAREG_TMR(6), (uint32_t)fx(eval(&ps, v[0], px, py), 1048576.0));
            MGA_WR32(MGAREG_TMR(7), (uint32_t)fx(eval(&pt, v[0], px, py), 1048576.0));
            MGA_WR32(MGAREG_TMR(8), (uint32_t)fx(eval(&pq, v[0], px, py), 65536.0));
        }
        fifo_reserve(1);
        MGA_WR32(MGAREG_YDSTLEN + MGAREG_EXEC, ((uint32_t)(ys & 0xFFFF) << 16) | (uint32_t)(ye - ys));
    }
}
