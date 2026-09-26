/* draw.c - vertex conversion, culling and the drawing entry points. */
#include "glide/mg.h"
#include "mga/setup.h"
#include "mga/regs_mga.h"
#include "mga/fp.h"

static int32_t snap16(float v)
{
    return (int32_t)mga_floor((double)v * 16.0 + 0.5);
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

static void vtx(mga_svtx *o, const GrVertex *v, int wmode, uint32_t cc)
{
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
    if (cc) {
        uint32_t c = mg_color_to_argb(mg.st.constant_color);
        o->r = (float)((c >> 16) & 0xFF); o->g = (float)((c >> 8) & 0xFF); o->b = (float)(c & 0xFF);
    } else {
        o->r = v->r < 0 ? 0 : (v->r > 255 ? 255 : v->r);
        o->g = v->g < 0 ? 0 : (v->g > 255 ? 255 : v->g);
        o->b = v->b < 0 ? 0 : (v->b > 255 ? 255 : v->b);
    }
    o->a = v->a < 0 ? 0 : (v->a > 255 ? 255 : v->a);
    o->fog = 255;
    o->s = o->t = 0;
    o->q = v->oow;
}

/* Which colour source the current combine state reduces to (untextured
 * forms; textured forms are handled by the texture combine path). */
enum { SRC_ITERATED, SRC_CONSTANT, SRC_ZERO };

static int color_source(void)
{
    const mg_state *s = &mg.st;
    switch (s->cc_func) {
    case GR_COMBINE_FUNCTION_ZERO:
        return SRC_ZERO;
    case GR_COMBINE_FUNCTION_LOCAL:
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        return s->cc_local == GR_COMBINE_LOCAL_CONSTANT ? SRC_CONSTANT : SRC_ITERATED;
    default:
        if (s->cc_other == GR_COMBINE_OTHER_CONSTANT)
            return SRC_CONSTANT;
        return SRC_ITERATED;
    }
}

void mg_draw_tri(const GrVertex *a, const GrVertex *b, const GrVertex *c)
{
    mga_svtx sa, sb, sc;
    mga_tri_ctx ctx;
    int wmode = mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER || mg.st.depth_mode == GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS;
    int depth = mg.st.depth_mode != GR_DEPTHBUFFER_DISABLE && mg.has_aux;
    int src = color_source();
    int64_t area;
    if (!mg.open)
        return;
    if (depth && mg.st.depth_func == GR_CMP_NEVER)
        return;
    vtx(&sa, a, wmode, src == SRC_CONSTANT);
    vtx(&sb, b, wmode, src == SRC_CONSTANT);
    vtx(&sc, c, wmode, src == SRC_CONSTANT);
    if (src == SRC_ZERO) {
        sa.r = sa.g = sa.b = sb.r = sb.g = sb.b = sc.r = sc.g = sc.b = 0;
    }
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
    mg_validate();
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_BOP_COPY;
    ctx.flags = (src == SRC_ITERATED) ? MGA_S_COLOR : 0;
    if (depth) {
        ctx.dwgctl |= (mg.st.depth_mask ? DWG_ATYPE_ZI : DWG_ATYPE_I) |
                      zmode_for(mg.st.depth_func, wmode);
        ctx.flags |= MGA_S_Z | (mg.zbits == 32 ? MGA_S_Z32 : 0);
    } else {
        ctx.dwgctl |= DWG_ATYPE_I | DWG_ZMODE_NOZCMP;
    }
    ctx.clip_y0 = 0;
    ctx.clip_y1 = mg.mode.height;
    ctx.tex_k = 0;
    setup_triangle(&sa, &sb, &sc, &ctx);
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
