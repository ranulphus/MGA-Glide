/* draw.c - vertex conversion, culling and the drawing entry points. */
#include "glide/mg.h"
#include "mga/setup.h"
#include "mga/regs_mga.h"
#include "mga/mmio.h"
#include "combine/combine.h"
#include "tex/texmgr.h"
#include "mga/fp.h"
#include <string.h>

/* The Voodoo converts float vertices to 12.4 fixed point by truncation
 * toward zero; games that follow 3dfx's advice pre-snap to 1/16 anyway. */
static int32_t snap16(float v)
{
    return (int32_t)(v * 16.0f);
}

static uint32_t zmode_for(GrCmpFnc_t f, int reversed)
{
    switch (f) {
    case GR_CMP_LESS: return reversed ? DWG_ZMODE_ZGT : DWG_ZMODE_ZLT;
    case GR_CMP_EQUAL: return DWG_ZMODE_ZE;
    case GR_CMP_LEQUAL: return reversed ? DWG_ZMODE_ZGTE : DWG_ZMODE_ZLTE;
    case GR_CMP_GREATER: return reversed ? DWG_ZMODE_ZLT : DWG_ZMODE_ZGT;
    case GR_CMP_NOTEQUAL: return DWG_ZMODE_ZNE;
    case GR_CMP_GEQUAL: return reversed ? DWG_ZMODE_ZLTE : DWG_ZMODE_ZGTE;
    default: return DWG_ZMODE_NOZCMP;
    }
}

/* Depth value written by grBufferClear for the current mode. */
uint32_t mg_depth_clear_value(FxU16 depth)
{
    if (mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS)
        return mg_wdepth_from_code(depth, mg.zbits);
    return mg.zbits == 32 ? ((uint32_t)depth << 16) | depth : depth;
}

static uint32_t argb_const(void) { return mg_color_to_argb(mg.st.constant_color); }

static void vtx(mga_svtx *o, const GrVertex *v, int wmode, int csrc, int asrc, int spec)
{
    uint32_t c;
    o->X16 = snap16(v->x);
    o->Y16 = snap16(v->y);
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
        o->Y16 = mg.height * 16 - o->Y16;
    if (mg.scaled) {
        /* After the Voodoo's truncation to 1/16 pixel, so coverage scales
         * the same way everywhere. */
        o->X16 = (int32_t)mga_floor(o->X16 * mg.sx);
        o->Y16 = (int32_t)mga_floor(o->Y16 * mg.sy);
    }
    if (wmode)
        o->z = mg_wdepth_from_oow(v->oow, mg.zbits);
    else {
        double z = v->ooz + mg.st.depth_bias;
        if (z < 0) z = 0;
        if (z > 65535.0) z = 65535.0;
        o->z = mg.zbits == 32 ? z * 65536.0 : z;
    }
    switch (csrc) {
    case CS_CONSTANT:
        c = argb_const();
        o->r = (float)((c >> 16) & 0xFF); o->g = (float)((c >> 8) & 0xFF); o->b = (float)(c & 0xFF);
        break;
    case CS_CONST_ALPHA:
        o->r = o->g = o->b = (float)(argb_const() >> 24);
        break;
    case CS_ITER_ALPHA:
        o->r = o->g = o->b = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
        break;
    case CS_ZERO:
        o->r = o->g = o->b = 0;
        break;
    case CS_ONE:
        o->r = o->g = o->b = 255;
        break;
    case CS_SOFT:
    case CS_SOFT_FACTOR: {
        float rgb[3];
        mg_combine_vertex(v, csrc, rgb);
        o->r = rgb[0]; o->g = rgb[1]; o->b = rgb[2];
        break; }
    default:
        o->r = v->r < 0 ? 0 : (v->r > 255 ? 255 : v->r);
        o->g = v->g < 0 ? 0 : (v->g > 255 ? 255 : v->g);
        o->b = v->b < 0 ? 0 : (v->b > 255 ? 255 : v->b);
        break;
    }
    if (asrc == AS_SOFT || asrc == AS_MODULATED_SOFT)
        o->a = mg_combine_vertex_alpha(v, asrc);
    else
        o->a = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
    o->fog = mg.fog_on ? (float)(255.0 - mg_fog_vertex(v)) : 255.0f;
    if (spec) {
        float srgb[3];
        mg_combine_vertex_spec(v, spec, srgb);
        o->sr = srgb[0]; o->sg = srgb[1]; o->sb = srgb[2];
    } else
        o->sr = o->sg = o->sb = 0;
    o->q = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v->tmuvtx[0].oow : v->oow;
    o->s = v->tmuvtx[0].sow;          /* scaled to the bound level below */
    o->t = v->tmuvtx[0].tow;
}

/* log2 without libm: exponent by scaling, mantissa by the atanh series. */
static double log2d(double v)
{
    double z, z2, ln;
    int e = 0;
    if (v <= 0)
        return -1e9;
    while (v >= 2.0) { v *= 0.5; e++; }
    while (v < 1.0) { v *= 2.0; e--; }
    z = (v - 1.0) / (v + 1.0);
    z2 = z * z;
    ln = 2.0 * z * (1.0 + z2 * (1.0 / 3 + z2 * (1.0 / 5 + z2 * (1.0 / 7 + z2 * (1.0 / 9)))));
    return e + ln * 1.4426950408889634;
}

/* Voodoo LOD model: lambda = log2(max(|grad u|_x, |grad u|_y)) + bias,
 * with u, v the perspective-correct texel coordinates at the largest
 * level; the level is floor(lambda), clamped to the chain. */
typedef struct { double s0, sx, sy, t0, tx, ty, q0, qx, qy, x0, y0; int valid; } lod_planes;

static void lod_setup(lod_planes *L, const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    const GrVertex *v[3];
    double k = (256 >> tmu0.large) / 256.0, x[3], y[3], S[3], T[3], Q[3], area;
    int i;
    v[0] = a; v[1] = b; v[2] = c;
    for (i = 0; i < 3; i++) {
        x[i] = v[i]->x; y[i] = v[i]->y;
        Q[i] = (mg.st.stw_hint & GR_STWHINT_W_DIFF_TMU0) ? v[i]->tmuvtx[0].oow : v[i]->oow;
        S[i] = v[i]->tmuvtx[0].sow * k;
        T[i] = v[i]->tmuvtx[0].tow * k;
    }
    area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
    L->valid = area != 0;
    if (!L->valid)
        return;
#define PL(A, o) do { \
        L->o##x = ((A[1] - A[0]) * (y[2] - y[0]) - (A[2] - A[0]) * (y[1] - y[0])) / area; \
        L->o##y = ((x[1] - x[0]) * (A[2] - A[0]) - (x[2] - x[0]) * (A[1] - A[0])) / area; \
        L->o##0 = A[0]; } while (0)
    PL(S, s); PL(T, t); PL(Q, q);
#undef PL
    L->x0 = x[0]; L->y0 = y[0];
}

static int lod_at(const lod_planes *L, double px, double py)
{
    double dx = px - L->x0, dy = py - L->y0;
    double S = L->s0 + L->sx * dx + L->sy * dy, T = L->t0 + L->tx * dx + L->ty * dy;
    double Q = L->q0 + L->qx * dx + L->qy * dy, u, v, gx, gy, g;
    int lam;
    if (!L->valid || Q <= 1e-12)
        return 0;
    u = S / Q; v = T / Q;
    gx = ((L->sx - u * L->qx) * (L->sx - u * L->qx) + (L->tx - v * L->qx) * (L->tx - v * L->qx)) / (Q * Q);
    gy = ((L->sy - u * L->qy) * (L->sy - u * L->qy) + (L->ty - v * L->qy) * (L->ty - v * L->qy)) / (Q * Q);
    g = gx > gy ? gx : gy;
    if (g <= 0)
        return -64 * 4;
    /* quarter levels: floor(4 * log2(sqrt g)) = floor(2 * log2 g) */
    lam = (int)mga_floor(2.0 * log2d(g));
    if (mg.scaled)
        lam -= (int)mga_floor(2.0 * log2d(mg.sx * mg.sy) + 0.5);   /* more pixels per texel: finer levels */
    return lam + (int)(tmu0.lod_bias * 4.0f + (tmu0.lod_bias >= 0 ? 0.5f : -0.5f));
}

static GrLOD_t level_for(int quarter_lod)
{
    int l = quarter_lod >= 0 ? quarter_lod / 4 : -((-quarter_lod + 3) / 4);   /* floor */
    if (tmu0.mipmap == GR_MIPMAP_DISABLE)
        return tmu0.large;
    l += tmu0.large;
    if (l < tmu0.large) l = tmu0.large;
    if (l > tmu0.small) l = tmu0.small;
    return (GrLOD_t)l;
}

static int bilinear_on(void)
{
    return tmu0.minf == GR_TEXTUREFILTER_BILINEAR || tmu0.magf == GR_TEXTUREFILTER_BILINEAR ||
           mg_config.forced_bilinear;
}

/* Coordinates here are normalised (1.0 = one texture width) and multiplied
 * by q. Two adjustments keep the engine on well-behaved inputs:
 *  - in wrap mode, shift each axis by whole texture periods so the smallest
 *    coordinate lies in [0, 1) (identical result; no negative coordinates,
 *    and no large ones that would force the prescale down);
 *  - with bilinear filtering, move the sample point by half a texel so the
 *    filter centres on texels as the Voodoo's does. */
static void tex_adjust(mga_svtx *a, mga_svtx *b, mga_svtx *c, const tex_level_hw *hw)
{
    mga_svtx *v[3];
    int i, axis;
    v[0] = a; v[1] = b; v[2] = c;
    for (axis = 0; axis < 2; axis++) {
        int wrap = axis ? tmu0.clamp_t == GR_TEXTURECLAMP_WRAP : tmu0.clamp_s == GR_TEXTURECLAMP_WRAP;
        double mn = 1e30, half = 0.5 / (double)(1 << (axis ? hw->h_log2 : hw->w_log2));
        for (i = 0; i < 3; i++) {
            double q = v[i]->q > 0 ? v[i]->q : 1e-9;
            double u = (axis ? v[i]->t : v[i]->s) / q;
            if (u < mn) mn = u;
        }
        if (bilinear_on())
            mn -= half;
        for (i = 0; i < 3; i++) {
            float *p = axis ? &v[i]->t : &v[i]->s;
            double shift = 0;
            if (bilinear_on())
                shift -= half;
            if (wrap)
                shift -= mga_floor(mn);     /* minimum into [0, 1): whole periods only */
            *p += (float)(shift * v[i]->q);
        }
    }
}

typedef struct {
    tex_variant var;          /* keyed texture variant (alpha test / chroma key) */
    uint32_t    alphactrl;    /* ALPHACTRL for the draw */
    int         akey0;        /* key texels whose alpha field is 0 (formats that keep alpha) */
    int         vertex_alpha; /* G200: blending or alpha test reads the iterated alpha */
    int         mip_n;        /* G200: levels in the hardware mip window (0/1 = single level) */
    uint32_t    texctl2;      /* G200: TEXCTL2 (specular enable) */
} draw_opts;

static void emit_texture_state(const tex_level_hw *hw, int modulate, const draw_opts *o,
                               const uint32_t *mip_org, int mip_n)
{
    /* takey=1, tamask=0: texel alpha never keys (G100 spec: opaque). A
     * keyed variant uses tamask=1, takey=0: alpha 0 is transparent. */
    uint32_t texctl = (uint32_t)hw->hwfmt | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT((uint32_t)hw->pitch & 0x7FF);
    uint32_t filt = bilinear_on() ? TEXFILTER_BILIN : TEXFILTER_NRST;
    if (o->var.kind || (o->akey0 && (hw->hwfmt == HW_TW12 || hw->hwfmt == HW_TW15)))
        texctl |= TEXCTL_STRANS | TEXCTL_TAMASK;
    else
        texctl |= TEXCTL_TAKEY;
    if (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP) texctl |= TEXCTL_CLAMPU;
    if (tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP) texctl |= TEXCTL_CLAMPV;
    if (modulate) texctl |= TEXCTL_TMODULATE;
    fifo_reserve(10);
    MGA_WR32(MGAREG_TEXORG, hw->org);
    MGA_WR32(MGAREG_TEXCTL, texctl);
    if (mga.has_texctl2)
        MGA_WR32(MGAREG_TEXCTL2, o->texctl2);
    if (mip_n > 1) {
        /* G200 window: levels 1..n-1 at TEXORG1..; the chip picks the level
         * per pixel (nearest level; texel filter as the game chose). */
        static const uint32_t orgreg[4] = { MGAREG_TEXORG1, MGAREG_TEXORG2, MGAREG_TEXORG3, MGAREG_TEXORG4 };
        int k;
        for (k = 1; k < mip_n; k++)
            MGA_WR32(orgreg[k - 1], mip_org[k]);
        MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(mg_config.trilinear ? (filt == TEXFILTER_BILIN ? TEXFILTER_MM8S : TEXFILTER_MM4S)
                                                                     : (filt == TEXFILTER_BILIN ? TEXFILTER_MM2S : TEXFILTER_MM1S)) |
                                   TEXFILTER_MAG(filt) | TEXFILTER_FILTERALPHA | TEXFILTER_FTHRES(0x10) |
                                   TEXFILTER_MAPNB(mip_n - 1));
    } else
        MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(filt) | TEXFILTER_MAG(filt));
    MGA_WR32(MGAREG_TEXTRANS, 0x0000FFFFu);
    MGA_WR32(MGAREG_ALPHACTRL, o->alphactrl);
}

/* Draw a textured triangle. The G100 samples one level per trapezoid, so
 * when the Voodoo would change level across the triangle it is drawn in
 * horizontal bands, each at the level for its centre (PRD §7.4). */
#define BAND_ROWS 8

/* Table fog over a band of rows: a linear fit of the table around the
 * band centre, through 1/w (which is linear in screen space). The fit is
 * pulled towards the centre value where it would leave 0..255 anywhere in
 * the band, because the fog iterator does not saturate. */
typedef struct { int on; double X[3], Y[3], O[3]; } fog_fit;

#define FOG_SPREAD 24           /* table-fog spread (of 255) that triggers banding */

static int clip_rows(const fog_fit *F, double y0, double y1, double *px, double *py)
{
    double ix[8], iy[8], ox[8], oy[8];
    int n = 3, m, i, pass;
    for (i = 0; i < 3; i++) { ix[i] = F->X[i]; iy[i] = F->Y[i]; }
    for (pass = 0; pass < 2; pass++) {
        m = 0;
        for (i = 0; i < n; i++) {
            int j = (i + 1) % n;
            double a = pass ? y1 - iy[i] : iy[i] - y0, b = pass ? y1 - iy[j] : iy[j] - y0;
            if (a >= 0) { ox[m] = ix[i]; oy[m] = iy[i]; m++; }
            if ((a >= 0) != (b >= 0) && m < 8) {
                double t = a / (a - b);
                ox[m] = ix[i] + t * (ix[j] - ix[i]); oy[m] = iy[i] + t * (iy[j] - iy[i]); m++;
            }
        }
        for (i = 0; i < m; i++) { ix[i] = ox[i]; iy[i] = oy[i]; }
        n = m;
    }
    for (i = 0; i < n; i++) { px[i] = ix[i]; py[i] = iy[i]; }
    return n;
}

static void fog_band(const fog_fit *F, int row0, int row1, mga_svtx *sa, mga_svtx *sb, mga_svtx *sc)
{
    double area, ox, oy, xc, yc, oc, fc, d, k = 1.0, px[8], py[8];
    mga_svtx *v[3];
    int i, n;
    v[0] = sa; v[1] = sb; v[2] = sc;
    area = (F->X[1] - F->X[0]) * (F->Y[2] - F->Y[0]) - (F->X[2] - F->X[0]) * (F->Y[1] - F->Y[0]);
    if (area == 0)
        return;
    ox = ((F->O[1] - F->O[0]) * (F->Y[2] - F->Y[0]) - (F->O[2] - F->O[0]) * (F->Y[1] - F->Y[0])) / area;
    oy = ((F->X[1] - F->X[0]) * (F->O[2] - F->O[0]) - (F->X[2] - F->X[0]) * (F->O[1] - F->O[0])) / area;
    n = clip_rows(F, row0, row1, px, py);
    if (n < 3)
        return;
    xc = yc = 0;
    for (i = 0; i < n; i++) { xc += px[i]; yc += py[i]; }
    xc /= n; yc /= n;
    oc = F->O[0] + ox * (xc - F->X[0]) + oy * (yc - F->Y[0]);
    if (oc < 1e-9) oc = 1e-9;
    fc = mg_fog_table_at(oc);
    d = (mg_fog_table_at(oc * 1.03) - mg_fog_table_at(oc / 1.03)) / (oc * 1.03 - oc / 1.03);
    for (i = 0; i < n; i++) {
        double fv = fc + d * (ox * (px[i] - xc) + oy * (py[i] - yc));
        if (fv > 255.0 && fv - fc > 0) { double kk = (255.0 - fc) / (fv - fc); if (kk < k) k = kk; }
        if (fv < 0.0 && fv - fc < 0) { double kk = (0.0 - fc) / (fv - fc); if (kk < k) k = kk; }
    }
    for (i = 0; i < 3; i++)
        v[i]->fog = (float)(255.0 - (fc + k * d * (F->O[i] - oc)));
}

static void draw_band(const mga_svtx *a0, const mga_svtx *b0, const mga_svtx *c0, mga_tri_ctx *ctx,
                      const mg_plan *plan, const draw_opts *o, GrLOD_t lod, int row0, int row1,
                      const fog_fit *F)
{
    mga_svtx sa = *a0, sb = *b0, sc = *c0;
    tex_level_hw hw;
    double sig_s, sig_t;
    if (F && F->on)
        fog_band(F, row0, row1, &sa, &sb, &sc);
    if (plan->tex_white || tex_bind_level(lod, o->var.kind ? &o->var : NULL, &hw) < 0) {
        tex_white(&hw);
        sig_s = sig_t = 0;
    } else {
        sig_s = (double)hw.logical_maxdim / (256.0 * (1 << hw.w_log2));
        sig_t = (double)hw.logical_maxdim / (256.0 * (1 << hw.h_log2));
    }
    if (!tmu0.combine_set)
        sig_s = sig_t = 0;               /* retail Glide: no s,t until a texture combine is set */
    sa.s *= (float)sig_s; sa.t *= (float)sig_t;
    sb.s *= (float)sig_s; sb.t *= (float)sig_t;
    sc.s *= (float)sig_s; sc.t *= (float)sig_t;
    tex_adjust(&sa, &sb, &sc, &hw);
    {
        uint32_t mip_org[5];
        int mip_n = 0;
        if (o->mip_n > 1 && !plan->tex_white) {
            /* Bind the following levels while each is 8x8 or more (their
             * pitch then halves, as the G200 expects) and converts to the
             * same hardware format. */
            tex_level_hw hk;
            mip_org[0] = hw.org;
            for (mip_n = 1; mip_n < o->mip_n; mip_n++) {
                if (lod + mip_n > tmu0.small ||
                    tex_bind_level((GrLOD_t)(lod + mip_n), o->var.kind ? &o->var : NULL, &hk) < 0 ||
                    hk.hwfmt != hw.hwfmt || hk.w_log2 != hw.w_log2 - mip_n || hk.h_log2 != hw.h_log2 - mip_n ||
                    hk.w_log2 < 3 || hk.h_log2 < 3)
                    break;
                mip_org[mip_n] = hk.org;
            }
        }
        emit_texture_state(&hw, plan->modulate, o, mip_org, mip_n);
    }
    ctx->tex_tw = hw.w_log2;
    ctx->tex_th = hw.h_log2;
    ctx->clip_y0 = row0 < 0 ? 0 : row0;
    ctx->clip_y1 = row1 > mg.mode.height ? mg.mode.height : row1;
    if (mg_log_level() >= MG_LOG_TRACE)
        mg_log(MG_LOG_TRACE, "band lod=%d rows=%d..%d org=%x tw=%d th=%d fmt=%d mod=%d", lod, ctx->clip_y0,
               ctx->clip_y1, hw.org, hw.w_log2, hw.h_log2, hw.hwfmt, plan->modulate);
    setup_triangle(&sa, &sb, &sc, ctx);
}

static void draw_textured(const GrVertex *a, const GrVertex *b, const GrVertex *c,
                          const mga_svtx *sa, const mga_svtx *sb, const mga_svtx *sc,
                          mga_tri_ctx *ctx, const mg_plan *plan, const draw_opts *o)
{
    lod_planes L;
    fog_fit F;
    int32_t ymin = sa->Y16, ymax = sa->Y16;
    int row, r0, r1, run_start, need_lod;
    double cx;
    GrLOD_t run_lod;
    need_lod = !(tmu0.mipmap == GR_MIPMAP_DISABLE || tmu0.small == tmu0.large || plan->tex_white);
    if (sb->Y16 < ymin) ymin = sb->Y16;
    if (sc->Y16 < ymin) ymin = sc->Y16;
    if (sb->Y16 > ymax) ymax = sb->Y16;
    if (sc->Y16 > ymax) ymax = sc->Y16;
    r0 = (ymin + 7) >> 4;
    r1 = (ymax + 7) >> 4;
    if (r1 <= r0)
        return;
    F.on = 0;
    if (mg.fog_on && mg_fog_source() == GR_FOG_WITH_TABLE && r1 - r0 > BAND_ROWS) {
        double fmin = sa->fog, fmax = sa->fog;
        if (sb->fog < fmin) fmin = sb->fog;
        if (sc->fog < fmin) fmin = sc->fog;
        if (sb->fog > fmax) fmax = sb->fog;
        if (sc->fog > fmax) fmax = sc->fog;
        if (fmax - fmin > FOG_SPREAD) {
            F.on = 1;
            F.X[0] = sa->X16 / 16.0; F.Y[0] = sa->Y16 / 16.0; F.O[0] = a->oow;
            F.X[1] = sb->X16 / 16.0; F.Y[1] = sb->Y16 / 16.0; F.O[1] = b->oow;
            F.X[2] = sc->X16 / 16.0; F.Y[2] = sc->Y16 / 16.0; F.O[2] = c->oow;
        }
    }
    if (!need_lod && !F.on) {
        draw_band(sa, sb, sc, ctx, plan, o, tmu0.large, 0, mg.mode.height, NULL);
        return;
    }
    if (need_lod)
        lod_setup(&L, a, b, c);
    if (need_lod && mga.max_mip_levels > 1 && tmu0.lod_bias == 0.0f && !F.on) {
        /* G200: one draw with a hardware mip window starting at the finest
         * level any part of the triangle needs. (No LOD bias register, so a
         * biased chain keeps the banded path.) */
        draw_opts mo = *o;
        GrLOD_t l0 = level_for(lod_at(&L, a->x, a->y)), l;
        double cxm = (a->x + b->x + c->x) / 3.0, cym = (a->y + b->y + c->y) / 3.0;
        l = level_for(lod_at(&L, b->x, b->y)); if (l < l0) l0 = l;
        l = level_for(lod_at(&L, c->x, c->y)); if (l < l0) l0 = l;
        l = level_for(lod_at(&L, cxm, cym)); if (l < l0) l0 = l;
        mo.mip_n = mga.max_mip_levels > 5 ? 5 : mga.max_mip_levels;
        draw_band(sa, sb, sc, ctx, plan, &mo, l0, 0, mg.mode.height, NULL);
        return;
    }
    cx = (a->x + b->x + c->x) / 3.0;
    run_start = r0;
    run_lod = (GrLOD_t)-1;
    for (row = r0; row < r1; row += BAND_ROWS) {
        int mid = row + BAND_ROWS / 2, end = row + BAND_ROWS > r1 ? r1 : row + BAND_ROWS;
        double gy;
        GrLOD_t l = tmu0.large;
        if (mid >= r1) mid = (row + r1) / 2;
        gy = mg.st.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - (mid + 0.5) : mid + 0.5;
        if (need_lod)
            l = level_for(lod_at(&L, cx, gy));
        if (F.on) {
            /* Every band gets its own fog fit (and its own level). */
            draw_band(sa, sb, sc, ctx, plan, o, l, row, end, &F);
            continue;
        }
        if (run_lod == (GrLOD_t)-1)
            run_lod = l;
        else if (l != run_lod) {
            draw_band(sa, sb, sc, ctx, plan, o, run_lod, run_start, row, NULL);
            run_start = row;
            run_lod = l;
        }
    }
    if (!F.on)
        draw_band(sa, sb, sc, ctx, plan, o, run_lod, run_start, r1, NULL);
}

static int atest_pass(int func, int a, int ref)
{
    switch (func) {
    case GR_CMP_NEVER: return 0;
    case GR_CMP_LESS: return a < ref;
    case GR_CMP_EQUAL: return a == ref;
    case GR_CMP_LEQUAL: return a <= ref;
    case GR_CMP_GREATER: return a > ref;
    case GR_CMP_NOTEQUAL: return a != ref;
    case GR_CMP_GEQUAL: return a >= ref;
    default: return 1;
    }
}

/* Apply the alpha plan: alpha test, chroma key and blending. Returns 0
 * when the triangle draws nothing. */
/* Alpha below which stipple coverage rounds to zero (half of 1/16). */
#define STIPPLE_MIN_ALPHA 8

static int alpha_setup(mg_plan *plan, const mg_aplan *ap, draw_opts *o, mga_svtx *sa, mga_svtx *sb, mga_svtx *sc)
{
    const mg_state *s = &mg.st;
    uint32_t at_bits = 0;
    mga_svtx *v[3];
    int i, tex_alpha = ap->alpha_src == AS_TEXTURE || ap->alpha_src == AS_MODULATED_ITER ||
                       ap->alpha_src == AS_MODULATED_CONST || ap->alpha_src == AS_MODULATED_SOFT;
    int const_a = (int)(mg_color_to_argb(s->constant_color) >> 24);
    int binary = tmu0.fmt == GR_TEXFMT_ARGB_1555;
    v[0] = sa; v[1] = sb; v[2] = sc;
    /* Per-vertex alpha for the diffuse plane. */
    for (i = 0; i < 3; i++) {
        if (ap->alpha_src == AS_CONSTANT || ap->alpha_src == AS_MODULATED_CONST)
            v[i]->a = (float)const_a;
        else if (ap->alpha_src == AS_ONE || ap->alpha_src == AS_TEXTURE)
            v[i]->a = 255;
        if (ap->fixed_alpha >= 0)
            v[i]->a = (float)ap->fixed_alpha;
        if (ap->invert)
            v[i]->a = 255 - v[i]->a;
    }
    /* Alpha test: native on the G200 family, otherwise keyed texture
     * variants (texture alpha) or a per-triangle decision (vertex alpha). */
    if (ap->atest != GR_CMP_ALWAYS && mga.has_alpha_test) {
        static const uint8_t atmode[8] = { 0, 4, 2, 5, 6, 3, 7, 0 };   /* by GR_CMP_* */
        at_bits = ALPHACTRL_ATEN | ALPHACTRL_ATMODE(atmode[ap->atest & 7]) | ALPHACTRL_ATREF(s->alpha_test_ref);
    } else if (ap->atest != GR_CMP_ALWAYS) {
        if (tex_alpha && plan->textured) {
            o->var.kind |= TV_ATEST;
            o->var.afunc = ap->atest;
            o->var.aref = s->alpha_test_ref;
        } else {
            int pass = 0;
            for (i = 0; i < 3; i++)
                pass += atest_pass(ap->atest, (int)v[i]->a, s->alpha_test_ref);
            if (!pass)
                return 0;
        }
    }
    /* Chroma key: exact texel matches become transparent. */
    if (s->chroma_mode == GR_CHROMAKEY_ENABLE) {
        uint32_t key = mg_color_to_argb(s->chroma_value) & 0xFFFFFF;
        if (plan->textured && !plan->tex_white) {
            o->var.kind |= TV_CHROMA;
            o->var.chroma = key;
        } else if (!plan->textured) {
            /* Untextured: keyed out only when the whole triangle is the key colour. */
            int hit = 0;
            for (i = 0; i < 3; i++) {
                uint32_t c = ((uint32_t)(v[i]->r + 0.5f) << 16) | ((uint32_t)(v[i]->g + 0.5f) << 8) |
                             (uint32_t)(v[i]->b + 0.5f);
                hit += c == key;
            }
            if (hit == 3)
                return 0;
        }
    }
    /* Blending. */
    if (mga.has_alpha_blend) {
        uint32_t f = ap->native ? ap->factors : (ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO));
        int sel = !(tex_alpha && plan->textured) ? ALPHASEL_DIFFUSE
                : ap->alpha_src == AS_TEXTURE ? ALPHASEL_TEXTURE : ALPHASEL_MODULATED;
        o->alphactrl = f | at_bits | ALPHACTRL_ALPHASEL(sel);
        /* Any alpha but the texel's own comes from the alpha plane. */
        o->vertex_alpha = (ap->native || at_bits) && sel != ALPHASEL_TEXTURE;
        return 1;
    }
    if (ap->stipple) {
        if (ap->alpha_src == AS_TEXTURE && binary && plan->textured && ap->fixed_alpha < 0) {
            /* 1-bit alpha: blending with alpha 0 or 1 is exactly a key. */
            o->var.kind |= TV_ATEST;
            o->var.afunc = ap->invert ? GR_CMP_LESS : GR_CMP_GEQUAL;
            o->var.aref = 128;
            return 1;
        }
        /* The stipple threshold matrix includes 0, so any alpha draws at
         * least one pixel in 16, where a blend with alpha ~0 draws nothing.
         * Drop what would round to zero coverage: whole triangles when the
         * alpha comes from the vertices, texels when it comes from the
         * texture (hardware alpha key, which keeps the texel alpha). */
        {
            int src_vtx = ap->fixed_alpha >= 0 || ap->alpha_src != AS_TEXTURE;
            int src_tex = ap->fixed_alpha < 0 && tex_alpha && plan->textured && !ap->invert;
            if (src_vtx && ap->alpha_src != AS_TEXTURE) {
                int max_a = 0;
                for (i = 0; i < 3; i++)
                    if ((int)v[i]->a > max_a)
                        max_a = (int)v[i]->a;
                if (max_a < STIPPLE_MIN_ALPHA)
                    return 0;
            }
            if (src_tex)
                o->akey0 = 1;   /* hardware key on alpha 0; no conversion, alpha kept */
        }
        if (!plan->textured) {
            /* G100 applies alpha only in textured trapezoids. */
            plan->textured = 1;
            plan->tex_white = 1;
            plan->modulate = 1;
        }
        o->alphactrl = ALPHACTRL_G100_FIXED | ALPHACTRL_ASTIPPLE |
                       ALPHACTRL_ALPHASEL(ap->alpha_src == AS_TEXTURE && ap->fixed_alpha < 0 ? ALPHASEL_TEXTURE :
                                          (ap->alpha_src == AS_MODULATED_ITER || ap->alpha_src == AS_MODULATED_CONST ||
                                           ap->alpha_src == AS_MODULATED_SOFT) &&
                                          ap->fixed_alpha < 0 ? ALPHASEL_MODULATED : ALPHASEL_DIFFUSE);
    }
    return 1;
}

/* ---- Subdivision -----------------------------------------------------------
 * A combine that multiplies two iterated terms is not linear across the
 * triangle, so per-vertex evaluation drifts in the interior. Large such
 * triangles are split into SUBDIV x SUBDIV pieces (error falls with the
 * square of the size). Attributes are interpolated linearly in screen
 * space, as the hardware iterates them; points on an original edge are
 * computed from the edge's endpoints in a canonical order, so a
 * neighbour sharing the edge gets bit-identical vertices. */
#define SUBDIV 4
#define SUBDIV_MIN_AREA 256.0f          /* pixels^2 */

static int subdividing;

static void vlerp(GrVertex *o, const GrVertex *p, const GrVertex *q, float t)
{
    const float *fp = (const float *)p, *fq = (const float *)q;
    float *fo = (float *)o;
    unsigned i;
    for (i = 0; i < sizeof(GrVertex) / sizeof(float); i++)
        fo[i] = fp[i] + (fq[i] - fp[i]) * t;
}

static void edge_point(GrVertex *o, const GrVertex *p, const GrVertex *q, int k, int n)
{
    if (p->x < q->x || (p->x == q->x && p->y < q->y))
        vlerp(o, p, q, (float)k / n);
    else
        vlerp(o, q, p, (float)(n - k) / n);
}

static void subdivide(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    static GrVertex P[(SUBDIV + 1) * (SUBDIV + 1)];
    int i, j;
#define PT(i, j) P[(j) * (SUBDIV + 1) + (i)]
    for (j = 0; j <= SUBDIV; j++)
        for (i = 0; i + j <= SUBDIV; i++) {
            GrVertex *o = &PT(i, j);
            if (j == 0) edge_point(o, a, b, i, SUBDIV);
            else if (i == 0) edge_point(o, a, c, j, SUBDIV);
            else if (i + j == SUBDIV) edge_point(o, b, c, j, SUBDIV);
            else {
                GrVertex ab, ac;
                vlerp(&ab, a, b, (float)i / SUBDIV);
                vlerp(&ac, a, c, (float)j / SUBDIV);
                {   /* o = a + (b - a) i/n + (c - a) j/n */
                    const float *fa = (const float *)a, *fb = (const float *)&ab, *fc = (const float *)&ac;
                    float *fo = (float *)o;
                    unsigned k;
                    for (k = 0; k < sizeof(GrVertex) / sizeof(float); k++)
                        fo[k] = fb[k] + fc[k] - fa[k];
                }
            }
        }
    for (j = 0; j < SUBDIV; j++)
        for (i = 0; i + j < SUBDIV; i++) {
            mg_draw_tri(&PT(i, j), &PT(i + 1, j), &PT(i, j + 1));
            if (i + j < SUBDIV - 1)
                mg_draw_tri(&PT(i + 1, j), &PT(i + 1, j + 1), &PT(i, j + 1));
        }
#undef PT
}

void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    mga_svtx sa, sb, sc;
    mga_tri_ctx ctx;
    mg_plan plan;
    mg_aplan ap;
    draw_opts opts;
    int wmode = mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int64_t area;
    if (!mg.open)
        return;
    mg_stats.tri_processed++;
    if (depth && mg.st.depth_func == GR_CMP_NEVER)
        return;
    /* Culling uses the game's orientation (before any origin flip). */
    if (mg.st.cull != GR_CULL_DISABLE) {
        int64_t ax = snap16(a->x), ay = snap16(a->y), bx = snap16(b->x), by = snap16(b->y);
        int64_t cx = snap16(c->x), cy = snap16(c->y);
        area = (bx - ax) * (cy - ay) - (cx - ax) * (by - ay);
        if (area == 0)
            return;
        if (mg.st.cull == GR_CULL_NEGATIVE && area < 0)
            return;
        if (mg.st.cull == GR_CULL_POSITIVE && area > 0)
            return;
    }
    if (!mg.st.color_mask_rgb && !(depth && mg.st.depth_mask))
        return;
    if (mg.st.delta0) {
        /* DELTA0: every vertex carries the grConstantColorValue4 colour. */
        static GrVertex d[3];
        uint32_t k = mg.st.delta0_argb;
        int n;
        d[0] = *a; d[1] = *b; d[2] = *c;
        for (n = 0; n < 3; n++) {
            d[n].r = (float)((k >> 16) & 0xFF); d[n].g = (float)((k >> 8) & 0xFF); d[n].b = (float)(k & 0xFF);
        }
        a = &d[0]; b = &d[1]; c = &d[2];
    }
    {
        /* Plans depend only on state: recompute them when it changed. */
        static mg_plan c_plan;
        static mg_aplan c_ap;
        static uint32_t c_gen;
        if (c_gen != mg_state_gen) {
            mg_combine_plan(&c_plan);
            mg_alpha_plan(&c_ap);
            c_gen = mg_state_gen;
        }
        plan = c_plan;
        ap = c_ap;
    }
    if ((plan.quadratic || ap.quadratic) && !subdividing) {
        float ar = ((b->x - a->x) * (c->y - a->y) - (c->x - a->x) * (b->y - a->y)) * 0.5f;
        if (ar < 0) ar = -ar;
        if (ar > SUBDIV_MIN_AREA) {
            subdividing = 1;
            subdivide(a, b, c);
            subdividing = 0;
            return;
        }
    }
    if (plan.approx || ap.approx)
        mg_note_approx();
    if (mg_config.census)
        mg_census();
    if (ap.skip || (ap.color_off && !depth))
        return;
    /* Fog: the G100 fogs only in textured trapezoids (white texture for
     * untextured draws); MULT2 keeps only the fog term (f * fog colour),
     * so the colour into the fog unit is forced to zero. */
    mg.fog_on = mg_fog_source() != GR_FOG_DISABLE;
    if (mg.fog_on) {
        if (mg.st.fog_mode & GR_FOG_MULT2) {
            plan.color_src = CS_ZERO;
            plan.modulate = 1;
        }
        if (!plan.textured) {
            plan.textured = 1;
            plan.tex_white = 1;
            plan.modulate = 1;
        }
    }
    vtx(&sa, a, wmode, plan.color_src, ap.alpha_src, plan.spec);
    vtx(&sb, b, wmode, plan.color_src, ap.alpha_src, plan.spec);
    vtx(&sc, c, wmode, plan.color_src, ap.alpha_src, plan.spec);
    memset(&opts, 0, sizeof opts);
    opts.alphactrl = mga.has_alpha_blend ? ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                           ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE)
                                         : ALPHACTRL_G100_FIXED | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE);
    if (!alpha_setup(&plan, &ap, &opts, &sa, &sb, &sc))
        return;
    mg_validate();
    ctx.flags = MGA_S_VOODOO_EDGES;
    if (mg.fog_on) {
        mg_fog_emit_color();
        ctx.flags |= MGA_S_FOG;
    }
    ctx.dwgctl = DWG_BOP_COPY;
    ctx.tex_tw = ctx.tex_th = 3;
    if (depth) {
        ctx.dwgctl |= (mg.st.depth_mask ? DWG_ATYPE_ZI : DWG_ATYPE_I) | zmode_for(mg.st.depth_func, wmode);
        ctx.flags |= MGA_S_Z | (mg.zbits == 32 ? MGA_S_Z32 : 0);
    } else {
        ctx.dwgctl |= DWG_ATYPE_I | DWG_ZMODE_NOZCMP;
    }
    ctx.clip_y0 = 0;
    ctx.clip_y1 = mg.mode.height;
    if (opts.vertex_alpha)
        ctx.flags |= MGA_S_ALPHA;
    if (plan.spec) {
        ctx.flags |= MGA_S_SPEC;
        opts.texctl2 |= TEXCTL2_SPECEN;
    }
    if (!plan.textured) {
        ctx.dwgctl |= DWG_OPCOD_TRAP;
        if (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA || plan.color_src == CS_SOFT)
            ctx.flags |= MGA_S_COLOR;
        if (mga.has_alpha_blend) {
            /* The G200 blends and alpha-tests plain trapezoids too. */
            fifo_reserve(1);
            MGA_WR32(MGAREG_ALPHACTRL, opts.alphactrl);
        }
        setup_triangle(&sa, &sb, &sc, &ctx);
    } else {
        ctx.dwgctl |= DWG_OPCOD_TEXTURE_TRAP;
        ctx.flags |= MGA_S_TEX;
        if (plan.modulate && (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA ||
                              plan.color_src == CS_SOFT || plan.color_src == CS_SOFT_FACTOR))
            ctx.flags |= MGA_S_COLOR;
        if (ap.stipple && ap.alpha_src != AS_TEXTURE)
            ctx.flags |= MGA_S_ALPHA;
        draw_textured(a, b, c, &sa, &sb, &sc, &ctx, &plan, &opts);
    }
    mg_stats.tris++;
    mg_stats.tri_drawn++;
}

GR_ENTRY(void, grDrawTriangle, (const GrVertex *a, const GrVertex *b, const GrVertex *c))
{
    FPU_ENTER();
    mg_draw_tri(a, b, c);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    int i;
    FPU_ENTER();
    for (i = 2; i < nverts; i++)
        mg_draw_tri(&vlist[0], &vlist[i - 1], &vlist[i]);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    int i;
    FPU_ENTER();
    for (i = 2; i < nverts; i++)
        mg_draw_tri(&vlist[ilist[0]], &vlist[ilist[i - 1]], &vlist[ilist[i]]);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawPlanarPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    grDrawPolygonVertexList(nverts, vlist);
}

GR_ENTRY(void, grDrawPlanarPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    grDrawPolygon(nverts, ilist, vlist);
}

/* Points and lines as small quads (PRD §7). */
static void quad(const GrVertex *p, float x0, float y0, float x1, float y1)
{
    GrVertex q[4];
    int i;
    for (i = 0; i < 4; i++) q[i] = *p;
    q[0].x = x0; q[0].y = y0; q[1].x = x1; q[1].y = y0;
    q[2].x = x1; q[2].y = y1; q[3].x = x0; q[3].y = y1;
    mg_draw_tri(&q[0], &q[1], &q[2]);
    mg_draw_tri(&q[0], &q[2], &q[3]);
}

GR_ENTRY(void, grDrawPoint, (const GrVertex *pt))
{
    float x, y;
    FPU_ENTER();
    x = (float)mga_floor(pt->x);
    y = (float)mga_floor(pt->y);
    quad(pt, x, y, x + 1.0f, y + 1.0f);
    FPU_LEAVE();
}

GR_ENTRY(void, grDrawLine, (const GrVertex *v1, const GrVertex *v2))
{
    GrVertex q[4];
    float dx, dy;
    int i;
    FPU_ENTER();
    dx = v2->x - v1->x; dy = v2->y - v1->y;
    q[0] = *v1; q[1] = *v2; q[2] = *v2; q[3] = *v1;
    if ((dx < 0 ? -dx : dx) >= (dy < 0 ? -dy : dy)) {
        q[0].y -= 0.5f; q[1].y -= 0.5f; q[2].y += 0.5f; q[3].y += 0.5f;
    } else {
        q[0].x -= 0.5f; q[1].x -= 0.5f; q[2].x += 0.5f; q[3].x += 0.5f;
    }
    for (i = 0; i < 1; i++) {
        mg_draw_tri(&q[0], &q[1], &q[2]);
        mg_draw_tri(&q[0], &q[2], &q[3]);
    }
    FPU_LEAVE();
}

/* Antialiased primitives degrade to their plain forms (logged once). */
GR_ENTRY(void, grAADrawPoint, (const GrVertex *pt)) { mg_stub_hit(MGA_API_grAADrawPoint); grDrawPoint(pt); }
GR_ENTRY(void, grAADrawLine, (const GrVertex *v1, const GrVertex *v2)) { mg_stub_hit(MGA_API_grAADrawLine); grDrawLine(v1, v2); }
GR_ENTRY(void, grAADrawTriangle, (const GrVertex *a, const GrVertex *b, const GrVertex *c,
                                  FxBool ab_antialias, FxBool bc_antialias, FxBool ca_antialias))
{
    MGA_UNUSED(ab_antialias); MGA_UNUSED(bc_antialias); MGA_UNUSED(ca_antialias);
    mg_stub_hit(MGA_API_grAADrawTriangle);
    grDrawTriangle(a, b, c);
}
GR_ENTRY(void, grAADrawPolygon, (int nverts, const int ilist[], const GrVertex vlist[]))
{
    mg_stub_hit(MGA_API_grAADrawPolygon);
    grDrawPolygon(nverts, ilist, vlist);
}
GR_ENTRY(void, grAADrawPolygonVertexList, (int nverts, const GrVertex vlist[]))
{
    mg_stub_hit(MGA_API_grAADrawPolygonVertexList);
    grDrawPolygonVertexList(nverts, vlist);
}
