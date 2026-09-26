/* combine.h - reduce Glide's colour/alpha/texture combine state to what the
 * Matrox can do (PRD §7.3). */
#ifndef MG_COMBINE_H
#define MG_COMBINE_H

enum {
    CS_ITERATED,        /* per-vertex r,g,b */
    CS_CONSTANT,        /* constant colour */
    CS_ITER_ALPHA,      /* per-vertex a replicated to r,g,b */
    CS_CONST_ALPHA,     /* constant alpha replicated */
    CS_ZERO,
    CS_ONE
};

typedef struct {
    int textured;       /* draw with the texture (TEXTURE_TRAP) */
    int modulate;       /* texel * colour source; otherwise decal */
    int color_src;      /* CS_*: untextured colour, or the modulation factor */
    int tex_white;      /* texture combine yields 1 (use the white texture) */
    int approx;         /* the result is an approximation (logged once per state) */
} mg_plan;

void mg_combine_plan(mg_plan *p);

enum { AS_ONE, AS_ITERATED, AS_CONSTANT, AS_TEXTURE, AS_MODULATED_ITER, AS_MODULATED_CONST };

typedef struct {
    int alpha_src;      /* AS_*: where the fragment's alpha comes from */
    int stipple;        /* G100: approximate the blend with the stipple */
    int invert;         /* stipple with 255 - alpha */
    int fixed_alpha;    /* >= 0: stipple with this constant coverage instead */
    int native;         /* chip blends: ALPHACTRL factors below */
    uint32_t factors;   /* ALPHACTRL src | dst << 4 */
    int color_off;      /* draw depth only (blend leaves colour unchanged) */
    int skip;           /* nothing to draw */
    int atest;          /* alpha test to apply (GR_CMP_*), GR_CMP_ALWAYS = none */
    int approx;
} mg_aplan;

void mg_alpha_plan(mg_aplan *a);

#endif
