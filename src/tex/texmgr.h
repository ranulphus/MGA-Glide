/* texmgr.h - TMU address virtualisation, residency and the per-draw
 * texture binding (PRD §7.5). */
#ifndef MG_TEXMGR_H
#define MG_TEXMGR_H
#include "glide/glide2.h"
#include "mga/types.h"
#include "tex/texconv.h"

typedef struct {
    uint32_t org;               /* VRAM byte offset of the level */
    int      w_log2, h_log2;    /* hardware dimensions (>= 8) */
    int      hwfmt;             /* HW_TW16 / HW_TW15 / HW_TW12 */
    int      pitch;             /* texels */
    int      logical_maxdim;    /* max(w, h) of the level as the game sees it */
} tex_level_hw;

typedef struct {
    /* TMU0 source state. */
    int         rec;            /* record index, -1 = none/unknown */
    GrLOD_t     large, small;
    GrAspectRatio_t aspect;
    GrTextureFormat_t fmt;
    FxU32       evenOdd;
    /* Sampling state. */
    GrTextureFilterMode_t minf, magf;
    GrTextureClampMode_t  clamp_s, clamp_t;
    GrMipMapMode_t        mipmap;
    FxBool                lod_blend;
    float                 lod_bias;
    /* Texture combine (TMU0). */
    GrCombineFunction_t   rgb_func, alpha_func;
    GrCombineFactor_t     rgb_factor, alpha_factor;
    FxBool                rgb_invert, alpha_invert;
    int                   combine_set;   /* grTexCombine* called since WinOpen */
    tex_tables            tables;
    uint32_t              pal_gen, ncc_gen;
} tex_unit;

extern tex_unit tmu0;

void tex_reset(void);                              /* at grSstWinOpen */
void tex_heap_init(uint32_t start, uint32_t end);
/* Ensure level 'lod' of the current source is resident; fills *hw.
 * Returns 0, or -1 when there is no usable texture (draw with white). */
int  tex_bind_level(GrLOD_t lod, const tex_variant *var, tex_level_hw *hw);
/* Alpha at a texel is binary (0/255) for the bound texture's level? Used
 * to decide whether an alpha test can use the hardware key directly. */
int  tex_level_alpha_class(void);
int  tex_white(tex_level_hw *hw);
void tex_frame(void);                              /* LRU clock */

#endif
