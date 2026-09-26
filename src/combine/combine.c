/* combine.c - see combine.h. */
#include "glide/mg.h"
#include "combine/combine.h"
#include "tex/texmgr.h"

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
