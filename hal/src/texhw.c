/* texhw.c - sampler registers and coordinate conditioning for TEXTURE_TRAP. */
#include "mga/tex.h"
#include "mga/hal.h"
#include "mga/fp.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"

void tex_emit(const mga_texstate *t)
{
    static const uint32_t orgreg[4] = { MGAREG_TEXORG1, MGAREG_TEXORG2, MGAREG_TEXORG3, MGAREG_TEXORG4 };
    uint32_t texctl = t->hwfmt | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT((uint32_t)t->pitch & 0x7FF);
    uint32_t filt = t->bilinear ? TEXFILTER_BILIN : TEXFILTER_NRST;
    int k;
    /* takey=1, tamask=0: texel alpha never keys; tamask=1, takey=0: alpha 0
     * is transparent. */
    texctl |= t->key_alpha0 ? (TEXCTL_STRANS | TEXCTL_TAMASK) : TEXCTL_TAKEY;
    if (t->clamp_u) texctl |= TEXCTL_CLAMPU;
    if (t->clamp_v) texctl |= TEXCTL_CLAMPV;
    if (t->modulate) texctl |= TEXCTL_TMODULATE;
    fifo_reserve(9);
    MGA_WR32(MGAREG_TEXORG, t->org);
    MGA_WR32(MGAREG_TEXCTL, texctl);
    if (mga.has_texctl2)
        MGA_WR32(MGAREG_TEXCTL2, t->texctl2);
    if (t->mip_n > 1) {
        /* G200 window: levels 1..n-1 at TEXORG1..; the chip picks the level
         * per pixel. */
        for (k = 1; k < t->mip_n; k++)
            MGA_WR32(orgreg[k - 1], t->mip_org[k]);
        MGA_WR32(MGAREG_TEXFILTER,
                 TEXFILTER_MIN(t->trilinear ? (t->bilinear ? TEXFILTER_MM8S : TEXFILTER_MM4S)
                                            : (t->bilinear ? TEXFILTER_MM2S : TEXFILTER_MM1S)) |
                 TEXFILTER_MAG(filt) | TEXFILTER_FILTERALPHA | TEXFILTER_FTHRES(0x10) |
                 TEXFILTER_MAPNB((uint32_t)(t->mip_n - 1)));
    } else
        MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(filt) | TEXFILTER_MAG(filt));
    MGA_WR32(MGAREG_TEXTRANS, 0x0000FFFFu);
}

void tex_adjust_coords(mga_svtx *a, mga_svtx *b, mga_svtx *c, const mga_texstate *t)
{
    mga_svtx *v[3];
    int i, axis;
    v[0] = a; v[1] = b; v[2] = c;
    for (axis = 0; axis < 2; axis++) {
        int wrap = axis ? !t->clamp_v : !t->clamp_u;
        double mn = 1e30, half = 0.5 / (double)(1 << (axis ? t->h_log2 : t->w_log2));
        for (i = 0; i < 3; i++) {
            double q = v[i]->q > 0 ? v[i]->q : 1e-9;
            double u = (axis ? v[i]->t : v[i]->s) / q;
            if (u < mn) mn = u;
        }
        if (t->bilinear)
            mn -= half;
        for (i = 0; i < 3; i++) {
            float *p = axis ? &v[i]->t : &v[i]->s;
            double shift = 0;
            if (t->bilinear)
                shift -= half;
            if (wrap)
                shift -= mga_floor(mn);     /* minimum into [0, 1): whole periods only */
            *p += (float)(shift * v[i]->q);
        }
    }
}
