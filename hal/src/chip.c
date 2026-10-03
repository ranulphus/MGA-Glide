/* chip.c - per-chip capabilities and VRAM sizing. */
#include "mga/hal.h"
#include "mga/sys.h"

void mga_chip_caps(mga_chip *c)
{
    c->min_tex_dim = 8;
    switch (c->family) {
    case MGA_FAMILY_G100:
        c->fifo_depth = 64; c->fifo_mask = 0x7F;
        c->max_mip_levels = 1; c->max_tex_size = 2048;
        c->ar_bits = 18;
        c->has_ydstorg = 1; c->zorg_ydst_relative = 1;
        c->fog_textrap_only = 1; c->alphasel_tex_always = 1;
        break;
    case MGA_FAMILY_G200:
    case MGA_FAMILY_G200E:
        c->fifo_depth = 64; c->fifo_mask = 0x7F;
        c->max_mip_levels = 5; c->max_tex_size = 2048;
        c->ar_bits = 18;
        c->has_dwgsync = 1; c->has_dstorg = 1; c->has_ydstorg = 1;
        c->has_texctl2 = 1; c->has_alpha_blend = 1; c->has_alpha_test = 1;
        c->has_tlut = 1; c->has_specular = 1; c->has_decalblend = 1;
        break;
    case MGA_FAMILY_G400:
        c->fifo_depth = 16; c->fifo_mask = 0x1F;
        c->max_mip_levels = 11; c->max_tex_size = 2048;
        c->ar_bits = 22;
        c->has_dwgsync = 1; c->has_dstorg = 1;
        c->has_texctl2 = 1; c->has_alpha_blend = 1; c->has_alpha_test = 1;
        c->has_tlut = 1; c->has_specular = 1; c->has_decalblend = 1;
        c->g400_clip_quirk = 1;
        c->has_dual_tex = 1;
        break;
    default:
        break;
    }
}

/* Size VRAM by writing a signature at each megabyte and looking for
 * aliasing. Must run before anything useful is in VRAM. */
uint32_t mga_probe_vram(void)
{
    volatile uint32_t *fb = (volatile uint32_t *)mga_fb;
    uint32_t mb, max = mga.fb_size >> 20;
    fb[0] = 0x4D474130u;             /* "MGA0" */
    for (mb = 1; mb < max; mb++) {
        volatile uint32_t *p = (volatile uint32_t *)(mga_fb + (mb << 20));
        p[0] = 0x4D470000u | mb;
        if (fb[0] != 0x4D474130u || p[0] != (0x4D470000u | mb))
            break;
    }
    return mb << 20;
}
