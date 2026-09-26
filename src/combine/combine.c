/* combine.c - see combine.h. */
#include "glide/mg.h"
#include "combine/combine.h"
#include "tex/texmgr.h"
#include "mga/regs_mga.h"

/* Colour source for a Glide "local" selector. */
static int local_src(GrCombineLocal_t l, int alpha)
{
    if (l == GR_COMBINE_LOCAL_CONSTANT)
        return alpha ? CS_CONST_ALPHA : CS_CONSTANT;
    return alpha ? CS_ITER_ALPHA : CS_ITERATED;
}

static int other_src(GrCombineOther_t o)
{
    return o == GR_COMBINE_OTHER_CONSTANT ? CS_CONSTANT : CS_ITERATED;
}

void mg_combine_plan(mg_plan *p)
{
    const mg_state *s = &mg.st;
    int tex_zero = 0;
    p->textured = 0;
    p->modulate = 0;
    p->color_src = CS_ITERATED;
    p->tex_white = 0;
    p->approx = 0;

    /* What TMU0 hands to the colour unit. */
    switch (tmu0.rgb_func) {
    case GR_COMBINE_FUNCTION_LOCAL:
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        break;
    case GR_COMBINE_FUNCTION_ZERO:
        if (tmu0.rgb_invert) p->tex_white = 1; else tex_zero = 1;
        break;
    default:
        p->approx = 1;            /* two-TMU forms: TMU1 is absent, use the local texel */
        break;
    }

    switch (s->cc_func) {
    case GR_COMBINE_FUNCTION_ZERO:
        p->color_src = s->cc_invert ? CS_ONE : CS_ZERO;
        return;
    case GR_COMBINE_FUNCTION_LOCAL:
        p->color_src = local_src(s->cc_local, 0);
        break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        p->color_src = local_src(s->cc_local, 1);
        break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        if (s->cc_func != GR_COMBINE_FUNCTION_SCALE_OTHER)
            p->approx = 1;       /* additive / blend forms need G200 specular */
        if (s->cc_other == GR_COMBINE_OTHER_TEXTURE) {
            if (tex_zero) { p->color_src = CS_ZERO; return; }
            p->textured = 1;
            switch (s->cc_factor) {
            case GR_COMBINE_FACTOR_ONE:
                break;                                     /* decal */
            case GR_COMBINE_FACTOR_LOCAL:
                p->modulate = 1;
                p->color_src = local_src(s->cc_local, 0);
                break;
            case GR_COMBINE_FACTOR_LOCAL_ALPHA:
                p->modulate = 1;
                p->color_src = local_src(s->cc_local, 1);
                break;
            case GR_COMBINE_FACTOR_ZERO:
                p->textured = 0;
                p->color_src = CS_ZERO;
                break;
            default:
                p->approx = 1;                             /* texture-alpha factors: decal */
                break;
            }
        } else {
            p->color_src = other_src(s->cc_other);
            if (s->cc_factor != GR_COMBINE_FACTOR_ONE)
                p->approx = 1;
        }
        break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        p->approx = 1;
        p->color_src = local_src(s->cc_local, 0);
        break;
    default:
        p->approx = 1;
        break;
    }
    if (s->cc_invert)
        p->approx = 1;
}

/* Log each distinct approximated combine state once. */
void mg_note_approx(void)
{
    static uint32_t seen[64];
    static int nseen;
    const mg_state *s = &mg.st;
    uint32_t h = ((uint32_t)s->cc_func << 24) ^ ((uint32_t)s->cc_factor << 16) ^ ((uint32_t)s->cc_local << 12) ^
                 ((uint32_t)s->cc_other << 8) ^ ((uint32_t)s->cc_invert << 7) ^ ((uint32_t)tmu0.rgb_func << 3) ^
                 (uint32_t)tmu0.rgb_invert;
    int i;
    for (i = 0; i < nseen; i++)
        if (seen[i] == h)
            return;
    if (nseen < 64)
        seen[nseen++] = h;
    mg_line("MGA-DEGRADE cc=%d,%d,%d,%d,%d tc=%d,%d,%d approx", s->cc_func, s->cc_factor, s->cc_local,
            s->cc_other, s->cc_invert, tmu0.rgb_func, tmu0.rgb_factor, tmu0.rgb_invert);
}

/* Feature census: log each distinct draw-state combination once, so the
 * features a game actually uses can be read off a run's serial log. */
void mg_census(void)
{
    static uint32_t seen[256];
    static int nseen;
    const mg_state *s = &mg.st;
    uint32_t v[12], h = 2166136261u;
    int i;
    v[0] = (uint32_t)s->cc_func | ((uint32_t)s->cc_factor << 8) | ((uint32_t)s->cc_local << 16) | ((uint32_t)s->cc_other << 24);
    v[1] = (uint32_t)s->ac_func | ((uint32_t)s->ac_factor << 8) | ((uint32_t)s->ac_local << 16) | ((uint32_t)s->ac_other << 24);
    v[2] = (uint32_t)s->blend_src | ((uint32_t)s->blend_dst << 8) | ((uint32_t)s->blend_asrc << 16) | ((uint32_t)s->blend_adst << 24);
    v[3] = (uint32_t)s->alpha_test_func | ((uint32_t)s->alpha_test_ref << 8);
    v[4] = (uint32_t)s->chroma_mode;
    v[5] = (uint32_t)s->fog_mode;
    v[6] = (uint32_t)s->depth_mode | ((uint32_t)s->depth_func << 8) | ((uint32_t)s->depth_mask << 16);
    v[7] = (uint32_t)tmu0.rgb_func | ((uint32_t)tmu0.rgb_factor << 8) | ((uint32_t)tmu0.alpha_func << 16) | ((uint32_t)tmu0.alpha_factor << 24);
    v[8] = (uint32_t)tmu0.fmt | ((uint32_t)tmu0.minf << 8) | ((uint32_t)tmu0.magf << 12) | ((uint32_t)tmu0.mipmap << 16);
    v[9] = (uint32_t)s->cc_invert | ((uint32_t)s->ac_invert << 1) | ((uint32_t)tmu0.rgb_invert << 2) | ((uint32_t)tmu0.alpha_invert << 3);
    v[10] = (uint32_t)s->cull;
    v[11] = s->stw_hint;
    for (i = 0; i < 12; i++)
        h = (h ^ v[i]) * 16777619u;
    for (i = 0; i < nseen; i++)
        if (seen[i] == h)
            return;
    if (nseen < 256)
        seen[nseen++] = h;
    mg_line("MGL-CENSUS cc=%d,%d,%d,%d,%d ac=%d,%d,%d,%d,%d blend=%d,%d,%d,%d atest=%d,%d chroma=%d fog=%d "
            "depth=%d,%d,%d tc=%d,%d,%d,%d fmt=%d filt=%d,%d mip=%d cull=%d hint=%x",
            s->cc_func, s->cc_factor, s->cc_local, s->cc_other, s->cc_invert,
            s->ac_func, s->ac_factor, s->ac_local, s->ac_other, s->ac_invert,
            s->blend_src, s->blend_dst, s->blend_asrc, s->blend_adst, s->alpha_test_func, s->alpha_test_ref,
            s->chroma_mode, s->fog_mode, s->depth_mode, s->depth_func, s->depth_mask,
            tmu0.rgb_func, tmu0.rgb_factor, tmu0.alpha_func, tmu0.alpha_factor, tmu0.fmt, tmu0.minf, tmu0.magf,
            tmu0.mipmap, s->cull, s->stw_hint);
}

/* Alpha source from grAlphaCombine. */
static int alpha_source(int *approx)
{
    const mg_state *s = &mg.st;
    switch (s->ac_func) {
    case GR_COMBINE_FUNCTION_ZERO:
        return s->ac_invert ? AS_ONE : AS_CONSTANT;     /* constant with value 0 handled by caller */
    case GR_COMBINE_FUNCTION_LOCAL:
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA:
        return s->ac_local == GR_COMBINE_LOCAL_CONSTANT ? AS_CONSTANT : AS_ITERATED;
    case GR_COMBINE_FUNCTION_SCALE_OTHER:
        if (s->ac_other == GR_COMBINE_OTHER_TEXTURE) {
            if (s->ac_factor == GR_COMBINE_FACTOR_ONE)
                return AS_TEXTURE;
            if (s->ac_factor == GR_COMBINE_FACTOR_LOCAL || s->ac_factor == GR_COMBINE_FACTOR_LOCAL_ALPHA)
                return s->ac_local == GR_COMBINE_LOCAL_CONSTANT ? AS_MODULATED_CONST : AS_MODULATED_ITER;
            *approx = 1;
            return AS_TEXTURE;
        }
        if (s->ac_factor != GR_COMBINE_FACTOR_ONE)
            *approx = 1;
        return s->ac_other == GR_COMBINE_OTHER_CONSTANT ? AS_CONSTANT : AS_ITERATED;
    default:
        *approx = 1;
        return AS_ITERATED;
    }
}

static int mga_src_factor(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return BLEND_ZERO;
    case GR_BLEND_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GR_BLEND_DST_COLOR: return BLEND_DST_COLOR;
    case GR_BLEND_DST_ALPHA: return BLEND_ONE;              /* no destination alpha: reads as 1 */
    case GR_BLEND_ONE: return BLEND_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_DST_COLOR: return BLEND_ONE_MINUS_DST_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return BLEND_ZERO;
    case GR_BLEND_ALPHA_SATURATE: return BLEND_SRC_ALPHA_SATURATE;
    default: return BLEND_ONE;
    }
}

static int mga_dst_factor(GrAlphaBlendFnc_t f)
{
    switch (f) {
    case GR_BLEND_ZERO: return BLEND_ZERO;
    case GR_BLEND_SRC_ALPHA: return BLEND_SRC_ALPHA;
    case GR_BLEND_SRC_COLOR: return BLEND_SRC_COLOR;
    case GR_BLEND_DST_ALPHA: return BLEND_ONE;
    case GR_BLEND_ONE: return BLEND_ONE;
    case GR_BLEND_ONE_MINUS_SRC_ALPHA: return BLEND_ONE_MINUS_SRC_ALPHA;
    case GR_BLEND_ONE_MINUS_SRC_COLOR: return BLEND_ONE_MINUS_SRC_COLOR;
    case GR_BLEND_ONE_MINUS_DST_ALPHA: return BLEND_ZERO;
    default: return BLEND_ZERO;
    }
}

void mg_alpha_plan(mg_aplan *a)
{
    const mg_state *s = &mg.st;
    GrAlphaBlendFnc_t sf = s->blend_src, df = s->blend_dst;
    a->approx = 0;
    a->alpha_src = alpha_source(&a->approx);
    a->stipple = 0;
    a->invert = 0;
    a->fixed_alpha = -1;
    a->native = 0;
    a->factors = 0;
    a->color_off = 0;
    a->skip = 0;
    a->atest = s->alpha_test_func;
    if (a->atest == GR_CMP_NEVER) {
        a->skip = 1;
        return;
    }
    if (sf == GR_BLEND_ONE && df == GR_BLEND_ZERO)
        return;                                            /* opaque */
    if (sf == GR_BLEND_ZERO && df == GR_BLEND_ONE) {
        a->color_off = 1;                                  /* depth-only pass */
        return;
    }
    if (mga.has_alpha_blend) {
        a->native = 1;
        a->factors = (uint32_t)mga_src_factor(sf) | ((uint32_t)mga_dst_factor(df) << 4);
        return;
    }
    /* G100: only the stipple (PRD D8). */
    if (sf == GR_BLEND_SRC_ALPHA && df == GR_BLEND_ONE_MINUS_SRC_ALPHA) {
        a->stipple = 1;
    } else if (sf == GR_BLEND_ONE_MINUS_SRC_ALPHA && df == GR_BLEND_SRC_ALPHA) {
        a->stipple = 1;
        a->invert = 1;
        a->approx = 1;
    } else if (df == GR_BLEND_ONE && (sf == GR_BLEND_ONE || sf == GR_BLEND_SRC_ALPHA)) {
        /* Additive: no destination read on G100. */
        a->approx = 1;
        if (mg_config.g100_additive == 1)
            a->skip = 1;
        else {
            a->stipple = 1;
            if (sf == GR_BLEND_ONE)
                a->fixed_alpha = 128;
        }
    } else if (df == GR_BLEND_SRC_COLOR || sf == GR_BLEND_DST_COLOR || df == GR_BLEND_ONE_MINUS_SRC_COLOR ||
               sf == GR_BLEND_ONE_MINUS_DST_COLOR) {
        a->skip = 1;                                       /* multiplicative: impossible on G100 */
        a->approx = 1;
    } else {
        a->stipple = 1;
        a->fixed_alpha = 128;
        a->approx = 1;
    }
}
