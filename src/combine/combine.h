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

#endif
