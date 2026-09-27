/* tex.h - texture state for TEXTURE_TRAP (shared by MGA-Glide and DOS-GL).
 *
 * Texture coordinates reach setup_triangle as s, t normalised to the bound
 * level (1.0 = one texture width) and multiplied by q = 1/w; the setup
 * scales them and writes TEXWIDTH/TEXHEIGHT. tex_emit() programs the rest
 * of the sampler for one level or a mip window. */
#ifndef MGA_TEX_H
#define MGA_TEX_H
#include "mga/setup.h"

typedef struct {
    uint32_t org;               /* VRAM offset of the first level */
    uint32_t mip_org[5];        /* the window's levels; mip_org[0] == org */
    int      mip_n;             /* levels in the window (G200+); 0 or 1 = one level */
    int      w_log2, h_log2;    /* hardware size of the first level (>= 3) */
    int      pitch;             /* texels */
    uint32_t hwfmt;             /* TEXCTL_TW16, TEXCTL_TW15, TEXCTL_TW12, ... */
    int      clamp_u, clamp_v;  /* clamp instead of wrap */
    int      modulate;          /* texel times iterated colour (TEXCTL.tmodulate) */
    int      bilinear;          /* linear magnification and minification */
    int      trilinear;         /* mip window: blend two levels (G200+) */
    int      key_alpha0;        /* texels with alpha 0 are transparent (STRANS|TAMASK) */
    uint32_t texctl2;           /* G200+: TEXCTL2 (specular, decal blend) */
} mga_texstate;

/* Program TEXORG (and TEXORG1..4 for a window), TEXCTL, TEXCTL2, TEXFILTER
 * and TEXTRANS. ALPHACTRL stays with the caller. */
void tex_emit(const mga_texstate *t);

/* Keep the engine on well-behaved inputs: in wrap mode shift each axis by
 * whole texture periods so the smallest coordinate lies in [0, 1); with
 * bilinear filtering move the sample point by half a texel so the filter
 * centres on texels. */
void tex_adjust_coords(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t);

#endif
