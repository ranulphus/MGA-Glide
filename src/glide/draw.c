/* draw.c - vertex conversion, culling and the drawing entry points. */
#include "glide/mg.h"
#include "mga/setup.h"
#include "mga/regs_mga.h"
#include "mga/mmio.h"
#include "combine/combine.h"
#include "tex/texmgr.h"
#include "mga/fp.h"

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

static void vtx(mga_svtx *o, const GrVertex *v, int wmode, int csrc)
{
    uint32_t c;
    o->X16 = snap16(v->x);
    o->Y16 = snap16(v->y);
    if (mg.st.origin == GR_ORIGIN_LOWER_LEFT)
        o->Y16 = mg.height * 16 - o->Y16;
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
    default:
        o->r = v->r < 0 ? 0 : (v->r > 255 ? 255 : v->r);
        o->g = v->g < 0 ? 0 : (v->g > 255 ? 255 : v->g);
        o->b = v->b < 0 ? 0 : (v->b > 255 ? 255 : v->b);
        break;
    }
    o->a = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
    o->fog = 255;
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
 *  - in wrap mode, shift each axis by whole texture periods so every vertex
 *    has a non-negative coordinate (identical result, no negative-coordinate
 *    handling needed in hardware);
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
            if (wrap && mn < 0)
                shift += mga_floor(-mn) + 1.0;
            *p += (float)(shift * v[i]->q);
        }
    }
}

static void emit_texture_state(const tex_level_hw *hw, int modulate)
{
    /* takey=1, tamask=0: texel alpha never keys (G100 spec: opaque). */
    uint32_t texctl = (uint32_t)hw->hwfmt | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT((uint32_t)hw->pitch & 0x7FF) |
                      TEXCTL_TAKEY;
    uint32_t filt = bilinear_on() ? TEXFILTER_BILIN : TEXFILTER_NRST;
    if (tmu0.clamp_s == GR_TEXTURECLAMP_CLAMP) texctl |= TEXCTL_CLAMPU;
    if (tmu0.clamp_t == GR_TEXTURECLAMP_CLAMP) texctl |= TEXCTL_CLAMPV;
    if (modulate) texctl |= TEXCTL_TMODULATE;
    fifo_reserve(5);
    MGA_WR32(MGAREG_TEXORG, hw->org);
    MGA_WR32(MGAREG_TEXCTL, texctl);
    MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(filt) | TEXFILTER_MAG(filt));
    MGA_WR32(MGAREG_TEXTRANS, 0x0000FFFFu);
    /* Blending off: alpha from the (unused) diffuse plane, no stipple. */
    MGA_WR32(MGAREG_ALPHACTRL, ALPHACTRL_G100_FIXED | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
}

/* Draw a textured triangle. The G100 samples one level per trapezoid, so
 * when the Voodoo would change level across the triangle it is drawn in
 * horizontal bands, each at the level for its centre (PRD §7.4). */
#define BAND_ROWS 8

static void draw_band(const mga_svtx *a0, const mga_svtx *b0, const mga_svtx *c0, mga_tri_ctx *ctx,
                      const mg_plan *plan, GrLOD_t lod, int row0, int row1)
{
    mga_svtx sa = *a0, sb = *b0, sc = *c0;
    tex_level_hw hw;
    double sig_s, sig_t;
    if (plan->tex_white || tex_bind_level(lod, &hw) < 0) {
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
    emit_texture_state(&hw, plan->modulate);
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
                          mga_tri_ctx *ctx, const mg_plan *plan)
{
    lod_planes L;
    int32_t ymin = sa->Y16, ymax = sa->Y16;
    int row, r0, r1, run_start;
    double cx;
    GrLOD_t run_lod;
    if (tmu0.mipmap == GR_MIPMAP_DISABLE || tmu0.small == tmu0.large || plan->tex_white) {
        draw_band(sa, sb, sc, ctx, plan, tmu0.large, 0, mg.mode.height);
        return;
    }
    lod_setup(&L, a, b, c);
    if (sb->Y16 < ymin) ymin = sb->Y16;
    if (sc->Y16 < ymin) ymin = sc->Y16;
    if (sb->Y16 > ymax) ymax = sb->Y16;
    if (sc->Y16 > ymax) ymax = sc->Y16;
    r0 = (ymin + 7) >> 4;
    r1 = (ymax + 7) >> 4;
    if (r1 <= r0)
        return;
    cx = (a->x + b->x + c->x) / 3.0;
    run_start = r0;
    run_lod = (GrLOD_t)-1;
    for (row = r0; row < r1; row += BAND_ROWS) {
        int mid = row + BAND_ROWS / 2;
        double gy;
        GrLOD_t l;
        if (mid >= r1) mid = (row + r1) / 2;
        gy = mg.st.origin == GR_ORIGIN_LOWER_LEFT ? mg.height - (mid + 0.5) : mid + 0.5;
        l = level_for(lod_at(&L, cx, gy));
        if (run_lod == (GrLOD_t)-1)
            run_lod = l;
        else if (l != run_lod) {
            draw_band(sa, sb, sc, ctx, plan, run_lod, run_start, row);
            run_start = row;
            run_lod = l;
        }
    }
    draw_band(sa, sb, sc, ctx, plan, run_lod, run_start, r1);
}

void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    mga_svtx sa, sb, sc;
    mga_tri_ctx ctx;
    mg_plan plan;
    int wmode = mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int64_t area;
    if (!mg.open)
        return;
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
    mg_combine_plan(&plan);
    if (plan.approx)
        mg_note_approx();
    vtx(&sa, a, wmode, plan.color_src);
    vtx(&sb, b, wmode, plan.color_src);
    vtx(&sc, c, wmode, plan.color_src);
    mg_validate();
    ctx.flags = MGA_S_VOODOO_EDGES;
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
    if (!plan.textured) {
        ctx.dwgctl |= DWG_OPCOD_TRAP;
        if (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA)
            ctx.flags |= MGA_S_COLOR;
        setup_triangle(&sa, &sb, &sc, &ctx);
    } else {
        ctx.dwgctl |= DWG_OPCOD_TEXTURE_TRAP;
        ctx.flags |= MGA_S_TEX;
        if (plan.modulate && (plan.color_src == CS_ITERATED || plan.color_src == CS_ITER_ALPHA))
            ctx.flags |= MGA_S_COLOR;
        draw_textured(a, b, c, &sa, &sb, &sc, &ctx, &plan);
    }
    mg_stats.tris++;
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
