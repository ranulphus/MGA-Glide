/* mgarig - Loop C stages on a Matrox card under Linux (PRD §4.4, plan §7):
 * the cuda6 G200eR2 by default, any other card with RIG_BDF=<domain:bus:dev.fn>.
 *
 *   mgarig regs    read-only: identity and register values (no writes)
 *   mgarig sync    write DWGSYNC and read it back
 *   mgarig trap    one flat TRAP fill into off-screen VRAM, read back
 *   mgarig tex     one TEXTURE_TRAP from an off-screen texture, read back
 *
 * Every write goes to the MGA drawing registers or to VRAM from 4 MB up,
 * far from the VGA text console at the start of VRAM; nothing is shown on
 * screen. Output is plain text for out/rig/. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WORK      (4u << 20)        /* off-screen work area: safe even with 8 MB of VRAM */

/* Identity and BARs come from the card's PCI configuration space (sysfs,
 * through the Linux port), so the rig runs on any Matrox card at RIG_BDF. */
static int map(void)
{
    if (mga_find(&mga)) {
        printf("no Matrox card at RIG_BDF (default 0000:0a:00.0; root?)\n");
        return -1;
    }
    mga.fb_size = 16u << 20;
    printf("card %s id=%04x rev=%02x family=%s fb=%08lx mmio=%08lx iload=%08lx\n", mga.name, mga.device_id,
           mga.revision, mga_family_name(mga.family), (unsigned long)mga.fb_phys, (unsigned long)mga.mmio_phys,
           (unsigned long)mga.iload_phys);
    mga_mmio = (volatile uint8_t *)sys_map_phys(mga.mmio_phys, 0x4000);
    if (!mga_mmio) {
        printf("map mmio FAILED (root? RIG_BDF?)\n");
        return -1;
    }
    if (!getenv("RIG_NOFB")) {
        mga_fb = (volatile uint8_t *)sys_map_phys(mga.fb_phys, mga.fb_size);
        if (!mga_fb) {
            printf("map fb FAILED\n");
            return -1;
        }
    }
    return 0;
}

static void regs(void)
{
    static const struct { uint32_t off; const char *name; } r[] = {
        { 0x1E10, "FIFOSTATUS" }, { 0x1E14, "STATUS" }, { 0x1C04, "MACCESS" }, { 0x1C1C, "PLNWT" }, { 0x1C0C, "ZORG" },
        { 0x1C8C, "PITCH" }, { 0x1C94, "YDSTORG" }, { 0x1C98, "YTOP" }, { 0x1C9C, "YBOT" },
        { 0x1C80, "CXBNDRY" }, { 0x2C30, "TEXCTL" }, { 0x2C3C, "TEXCTL2" }, { 0x2C4C, "DWGSYNC" },
        { 0x2C58, "TEXFILTER" }, { 0x2C7C, "ALPHACTRL" }, { 0x2CB4, "SRCORG" }, { 0x2CB8, "DSTORG" },
        { 0x1E54, "OPMODE" }, { 0x1E20, "VCOUNT" },
    };
    unsigned i;
    for (i = 0; i < sizeof r / sizeof r[0]; i++)
        printf("reg %-10s %04x = %08x\n", r[i].name, r[i].off, MGA_RD32(r[i].off));
}

static int idle(const char *what)
{
    uint32_t t0 = sys_time_us();
    while (MGA_RD32(MGAREG_STATUS) & STATUS_DWGENGSTS) {
        if (sys_time_us() - t0 > 200000) {
            printf("%s: engine still busy after 200 ms: STATUS=%08x FIFOSTATUS=%08x\n", what,
                   MGA_RD32(MGAREG_STATUS), MGA_RD32(MGAREG_FIFOSTATUS));
            return -1;
        }
    }
    return 0;
}

static void setup_target(void)
{
    mga_target t;
    engine_init(64, 16);
    memset(&t, 0, sizeof t);
    t.color_off = WORK;
    t.pitch_px = 64;
    t.bpp = 16;
    t.zbits = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, 64, 64);
}

static int check(const char *what, uint16_t want_in, uint16_t want_out, int xin, int yin)
{
    volatile uint16_t *p = (volatile uint16_t *)(mga_fb + WORK);
    uint16_t in = p[yin * 64 + xin], out = p[63 * 64 + 63];
    printf("%s: inside=%04x (want %04x) corner=%04x (want %04x) -> %s\n", what, in, want_in, out, want_out,
           in == want_in && out == want_out ? "PASS" : "FAIL");
    return in == want_in && out == want_out;
}

/* ---- Experiments: what the emulated G200 guesses (docs/emulated-g200.md) ----
 * All in a 32-bit target (exact 8-bit values), 64x64 pixels at WORK,
 * textures from WORK + 64 KB. */

#define TEXBASE (WORK + 0x10000u)

static void target32(void)
{
    mga_target t;
    engine_init(64, 32);
    memset(&t, 0, sizeof t);
    t.color_off = WORK;
    t.pitch_px = 64;
    t.bpp = 32;
    t.zbits = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, 64, 64);
}

static uint32_t px32(int x, int y) { return ((volatile uint32_t *)(mga_fb + WORK))[y * 64 + x] & 0xFFFFFFu; }

/* A screen-aligned quad (two triangles) with texture coordinates in
 * texture widths (s0..s1, t0..t1), constant colour and alpha. */
static void quad(int x0, int y0, int x1, int y1, float s0, float t0, float s1, float t1, uint32_t dwgctl,
                 uint32_t flags, int tw, int th, float r, float g, float b, float a)
{
    mga_svtx v[4];
    mga_tri_ctx ctx;
    int i;
    memset(v, 0, sizeof v);
    for (i = 0; i < 4; i++) { v[i].r = r; v[i].g = g; v[i].b = b; v[i].a = a; v[i].fog = 255; v[i].q = 1.0f; }
    v[0].X16 = x0 * 16; v[0].Y16 = y0 * 16; v[0].s = s0; v[0].t = t0;
    v[1].X16 = x1 * 16; v[1].Y16 = y0 * 16; v[1].s = s1; v[1].t = t0;
    v[2].X16 = x1 * 16; v[2].Y16 = y1 * 16; v[2].s = s1; v[2].t = t1;
    v[3].X16 = x0 * 16; v[3].Y16 = y1 * 16; v[3].s = s0; v[3].t = t1;
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = dwgctl;
    ctx.flags = flags;
    ctx.tex_tw = tw; ctx.tex_th = th;
    ctx.clip_y0 = 0; ctx.clip_y1 = 64;
    setup_triangle(&v[0], &v[1], &v[2], &ctx);
    setup_triangle(&v[0], &v[2], &v[3], &ctx);
}

static void tex_regs(uint32_t org, uint32_t fmt, int pitch, uint32_t extra, uint32_t filter)
{
    fifo_reserve(3);
    MGA_WR32(MGAREG_TEXORG, org);
    MGA_WR32(MGAREG_TEXCTL, fmt | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT((uint32_t)pitch) | extra);
    MGA_WR32(MGAREG_TEXFILTER, filter);
}

static void alphactrl(uint32_t v)
{
    fifo_reserve(1);
    MGA_WR32(MGAREG_ALPHACTRL, v);
}

#define TT (DWG_OPCOD_TEXTURE_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY)
#define TR (DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY)
#define OPAQUE (ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO))
#define NEAREST (TEXFILTER_MIN(TEXFILTER_NRST) | TEXFILTER_MAG(TEXFILTER_NRST))

static int experiments(void)
{
    volatile uint16_t *t16 = (volatile uint16_t *)(mga_fb + TEXBASE);
    int i, x, y;

    /* E1: TW12 expansion. Texel 0x8888: colour and alpha nibbles 8.
     * Decal into 32 bpp shows the colour expansion; alpha test EQUAL
     * against 0x88 and 0x80 shows the alpha expansion. */
    for (i = 0; i < 64; i++) t16[i] = 0x8888;
    target32();
    engine_fill(0, 0, 64, 64, 0x000000);
    idle("e1");
    tex_regs(TEXBASE, TEXCTL_TW12, 8, TEXCTL_TAKEY, NEAREST);
    alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_TEXTURE));
    quad(0, 0, 8, 8, 0, 0, 1, 1, TT, MGA_S_TEX, 3, 3, 255, 255, 255, 255);
    alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_TEXTURE) | ALPHACTRL_ATEN | ALPHACTRL_ATMODE(2) | ALPHACTRL_ATREF(0x88));
    quad(10, 0, 18, 8, 0, 0, 1, 1, TT, MGA_S_TEX, 3, 3, 255, 255, 255, 255);
    alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_TEXTURE) | ALPHACTRL_ATEN | ALPHACTRL_ATMODE(2) | ALPHACTRL_ATREF(0x80));
    quad(20, 0, 28, 8, 0, 0, 1, 1, TT, MGA_S_TEX, 3, 3, 255, 255, 255, 255);
    idle("e1");
    printf("E1 tw12 texel 8888 -> colour %06x; alpha==0x88 %s; alpha==0x80 %s\n", px32(4, 4),
           px32(14, 4) ? "drawn" : "not drawn", px32(24, 4) ? "drawn" : "not drawn");

    /* E2: TW15 alpha bit 0, alpha test GREATER 0x80 (drawn only if the
     * texel alpha counts as 255). Texel 0x7C00 (alpha 0, red) and 0xFC00. */
    for (i = 0; i < 64; i++) t16[i] = (uint16_t)((i & 1) ? 0xFC00 : 0x7C00);
    target32();
    engine_fill(0, 0, 64, 64, 0x000000);
    idle("e2");
    tex_regs(TEXBASE, TEXCTL_TW15, 8, TEXCTL_TAKEY, NEAREST);
    alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_TEXTURE) | ALPHACTRL_ATEN | ALPHACTRL_ATMODE(6) | ALPHACTRL_ATREF(0x80));
    quad(0, 0, 8, 8, 0, 0, 1, 1, TT, MGA_S_TEX, 3, 3, 255, 255, 255, 255);
    idle("e2");
    printf("E2 tw15 alpha>0x80: texel a=0 %s, texel a=1 %s\n", px32(0, 0) ? "drawn" : "not drawn",
           px32(1, 0) ? "drawn" : "not drawn");

    /* E3: blending arithmetic. Destination 0x404040; source colour 0xC0 with
     * alpha a (diffuse), SRC_ALPHA / ONE_MINUS_SRC_ALPHA, in TRAP and in
     * TEXTURE_TRAP (white texture, modulated). */
    for (i = 0; i < 64; i++) t16[i] = 0xFFFF;
    {
        static const int alphas[] = { 0, 1, 64, 128, 192, 254, 255 };
        for (i = 0; i < 7; i++) {
            target32();
            engine_fill(0, 0, 64, 64, 0x404040);
            idle("e3");
            alphactrl(ALPHACTRL_SRC(BLEND_SRC_ALPHA) | ALPHACTRL_DST(BLEND_ONE_MINUS_SRC_ALPHA) |
                      ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
            quad(0, 0, 8, 8, 0, 0, 1, 1, TR, MGA_S_ALPHA, 3, 3, 192, 192, 192, (float)alphas[i]);
            tex_regs(TEXBASE, TEXCTL_TW16, 8, TEXCTL_TAKEY | TEXCTL_TMODULATE, NEAREST);
            quad(10, 0, 18, 8, 0, 0, 1, 1, TT, MGA_S_TEX | MGA_S_ALPHA | MGA_S_COLOR, 3, 3, 192, 192, 192,
                 (float)alphas[i]);
            idle("e3");
            printf("E3 blend a=%3d: TRAP %06x TEXTURE_TRAP %06x\n", alphas[i], px32(4, 4), px32(14, 4));
        }
    }

    /* E4: the stipple matrix. alpha 0x80 (then 0x40) with astipple in
     * TEXTURE_TRAP over black: which of a 4x4 block are drawn. */
    {
        static const int alphas[] = { 16, 64, 128, 192 };
        for (i = 0; i < 4; i++) {
            target32();
            engine_fill(0, 0, 64, 64, 0x000000);
            idle("e4");
            tex_regs(TEXBASE, TEXCTL_TW16, 8, TEXCTL_TAKEY, NEAREST);
            alphactrl(ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) | ALPHACTRL_ASTIPPLE |
                      ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
            quad(0, 0, 16, 16, 0, 0, 1, 1, TT, MGA_S_TEX | MGA_S_ALPHA, 3, 3, 255, 255, 255, (float)alphas[i]);
            idle("e4");
            printf("E4 stipple a=%3d:", alphas[i]);
            for (y = 0; y < 4; y++) {
                printf(" ");
                for (x = 0; x < 4; x++)
                    printf("%c", px32(x, y) ? '#' : '.');
            }
            printf("\n");
        }
    }

    /* E5: mip level choice. Levels 64,32,16,8 as solid colours (TW16)
     * at TEXORG..TEXORG3; nearest-level point mode (mm1s); a 64-texel
     * texture over n pixels for several n; the colour tells the level. */
    {
        static const uint16_t lc[4] = { 0xF800, 0x07E0, 0x001F, 0xFFE0 };
        static const int widths[] = { 64, 48, 40, 32, 24, 20, 16, 12, 8 };
        uint32_t org[4];
        int l, n, w = 64;
        uint32_t off = TEXBASE;
        for (l = 0; l < 4; l++, w >>= 1) {
            volatile uint16_t *p = (volatile uint16_t *)(mga_fb + off);
            org[l] = off;
            for (i = 0; i < w * w; i++) p[i] = lc[l];
            off += (uint32_t)(w * w * 2 + 255) & ~255u;
        }
        for (n = 0; n < 9; n++) {
            uint32_t c;
            target32();
            engine_fill(0, 0, 64, 64, 0x000000);
            idle("e5");
            fifo_reserve(6);
            MGA_WR32(MGAREG_TEXORG, org[0]);
            MGA_WR32(MGAREG_TEXORG1, org[1]);
            MGA_WR32(MGAREG_TEXORG2, org[2]);
            MGA_WR32(MGAREG_TEXORG3, org[3]);
            MGA_WR32(MGAREG_TEXCTL, TEXCTL_TW16 | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT(64) | TEXCTL_TAKEY);
            MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(TEXFILTER_MM1S) | TEXFILTER_MAG(TEXFILTER_NRST) |
                                       TEXFILTER_FTHRES(0x10) | TEXFILTER_MAPNB(3));
            alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
            quad(0, 0, widths[n], widths[n], 0, 0, 1, 1, TT, MGA_S_TEX, 6, 6, 255, 255, 255, 255);
            idle("e5");
            c = px32(widths[n] / 2, widths[n] / 2);
            for (l = 0; l < 4; l++) {
                uint32_t r = (uint32_t)((lc[l] >> 11) << 3), g = (uint32_t)(((lc[l] >> 5) & 63) << 2), b = (uint32_t)((lc[l] & 31) << 3);
                if (((c >> 16) & 0xF8) == (r & 0xF8) && ((c >> 8) & 0xFC) == (g & 0xFC) && (c & 0xF8) == (b & 0xF8))
                    break;
            }
            printf("E5 mip 64 texels over %2d px (ratio %.2f): colour %06x level %d\n", widths[n],
                   64.0 / widths[n], c, l);
        }
    }

    /* E5b: does TEXFILTER.fthres move the level choice? */
    {
        static const int widths[] = { 48, 40, 24, 20 };
        static const uint32_t th[] = { 0x00, 0x08, 0x10, 0x20, 0x40, 0x80, 0xFF };
        static const uint16_t lc[4] = { 0xF800, 0x07E0, 0x001F, 0xFFE0 };
        uint32_t org[4], off = TEXBASE;
        int l, n, k, w = 64;
        for (l = 0; l < 4; l++, w >>= 1) {
            org[l] = off;
            off += (uint32_t)(w * w * 2 + 255) & ~255u;
        }
        for (k = 0; k < 7; k++) {
            printf("E5b fthres=%02x:", th[k]);
            for (n = 0; n < 4; n++) {
                uint32_t c;
                target32();
                engine_fill(0, 0, 64, 64, 0x000000);
                idle("e5b");
                fifo_reserve(6);
                MGA_WR32(MGAREG_TEXORG, org[0]);
                MGA_WR32(MGAREG_TEXORG1, org[1]);
                MGA_WR32(MGAREG_TEXORG2, org[2]);
                MGA_WR32(MGAREG_TEXORG3, org[3]);
                MGA_WR32(MGAREG_TEXCTL, TEXCTL_TW16 | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT(64) | TEXCTL_TAKEY);
                MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(TEXFILTER_MM1S) | TEXFILTER_MAG(TEXFILTER_NRST) |
                                           TEXFILTER_FTHRES(th[k]) | TEXFILTER_MAPNB(3));
                alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
                quad(0, 0, widths[n], widths[n], 0, 0, 1, 1, TT, MGA_S_TEX, 6, 6, 255, 255, 255, 255);
                idle("e5b");
                c = px32(widths[n] / 2, widths[n] / 2);
                for (l = 0; l < 4; l++) {
                    uint32_t r = (uint32_t)((lc[l] >> 11) << 3), g = (uint32_t)(((lc[l] >> 5) & 63) << 2);
                    if (((c >> 16) & 0xF8) == (r & 0xF8) && ((c >> 8) & 0xFC) == (g & 0xFC))
                        break;
                }
                printf(" ratio %.2f->L%d", 64.0 / widths[n], l);
            }
            printf("\n");
        }
    }

    /* E6: texel key under bilinear magnification. 8x1 TW15 texture: texels
     * 0-3 alpha 0 (keyed, colour green), 4-7 opaque red; key on alpha 0
     * (STRANS | TAMASK); 8 texels over 64 pixels, bilinear. Row 0 shows
     * where drawing starts and the colours near the boundary. */
    for (i = 0; i < 8; i++) t16[i] = (uint16_t)(i < 4 ? 0x03E0 : 0xFC00);
    for (i = 8; i < 64; i++) t16[i] = t16[i & 7];
    target32();
    engine_fill(0, 0, 64, 64, 0x000000);
    idle("e6");
    tex_regs(TEXBASE, TEXCTL_TW15, 8, TEXCTL_STRANS | TEXCTL_TAMASK | TEXCTL_CLAMPU | TEXCTL_CLAMPV,
             TEXFILTER_MIN(TEXFILTER_BILIN) | TEXFILTER_MAG(TEXFILTER_BILIN));
    alphactrl(OPAQUE | ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
    quad(0, 0, 64, 8, 0, 0, 1, 1, TT, MGA_S_TEX, 3, 3, 255, 255, 255, 255);
    idle("e6");
    printf("E6 key+bilinear row:");
    for (x = 20; x < 44; x++)
        printf(" %02d:%06x", x, px32(x, 4));
    printf("\n");
    return 0;
}

int main(int argc, char **argv)
{
    const char *stage = argc > 1 ? argv[1] : "regs";
    if (map())
        return 2;
    printf("# mgarig %s\n", stage);
    if (!strcmp(stage, "regs")) {
        regs();
        return 0;
    }
    if (!strcmp(stage, "sync")) {
        MGA_WR32(MGAREG_DWGSYNC, 0x5A5A1234u);
        printf("DWGSYNC wrote 5a5a1234 read %08x\n", MGA_RD32(MGAREG_DWGSYNC));
        return 0;
    }
    if (!strcmp(stage, "trap")) {
        setup_target();
        engine_fill(0, 0, 64, 64, 0x001F);                     /* blue background */
        if (idle("fill"))
            return 1;
        {
            mga_svtx v[3];
            mga_tri_ctx ctx;
            int i;
            memset(v, 0, sizeof v);
            for (i = 0; i < 3; i++) { v[i].r = 255; v[i].g = 255; v[i].b = 0; v[i].a = 255; v[i].fog = 255; }
            v[0].X16 = 4 * 16;  v[0].Y16 = 4 * 16;
            v[1].X16 = 60 * 16; v[1].Y16 = 8 * 16;
            v[2].X16 = 8 * 16;  v[2].Y16 = 56 * 16;
            memset(&ctx, 0, sizeof ctx);
            ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
            ctx.clip_y0 = 0; ctx.clip_y1 = 64;
            setup_triangle(&v[0], &v[1], &v[2], &ctx);
        }
        if (idle("trap"))
            return 1;
        return check("trap", 0xFFE0, 0x001F, 15, 15) ? 0 : 1;
    }
    if (!strcmp(stage, "tex")) {
        volatile uint16_t *tex = (volatile uint16_t *)(mga_fb + WORK + 0x10000);
        int x, y;
        for (y = 0; y < 8; y++)
            for (x = 0; x < 8; x++)
                tex[y * 8 + x] = 0xF800;                        /* red 8x8 texture */
        setup_target();
        engine_fill(0, 0, 64, 64, 0x001F);
        if (idle("fill"))
            return 1;
        {
            mga_svtx v[3];
            mga_tri_ctx ctx;
            int i;
            fifo_reserve(4);
            MGA_WR32(MGAREG_TEXORG, WORK + 0x10000);
            MGA_WR32(MGAREG_TEXCTL, TEXCTL_TW16 | TEXCTL_TPITCHLIN | TEXCTL_TPITCHEXT(8) | TEXCTL_TAKEY);
            MGA_WR32(MGAREG_TEXFILTER, TEXFILTER_MIN(TEXFILTER_NRST) | TEXFILTER_MAG(TEXFILTER_NRST));
            MGA_WR32(MGAREG_ALPHACTRL, ALPHACTRL_SRC(BLEND_ONE) | ALPHACTRL_DST(BLEND_ZERO) |
                                       ALPHACTRL_ALPHASEL(ALPHASEL_DIFFUSE));
            memset(v, 0, sizeof v);
            for (i = 0; i < 3; i++) { v[i].r = v[i].g = v[i].b = v[i].a = 255; v[i].fog = 255; v[i].q = 1.0f; }
            v[0].X16 = 4 * 16;  v[0].Y16 = 4 * 16;  v[0].s = 0;   v[0].t = 0;
            v[1].X16 = 60 * 16; v[1].Y16 = 8 * 16;  v[1].s = 1.0f; v[1].t = 0;
            v[2].X16 = 8 * 16;  v[2].Y16 = 56 * 16; v[2].s = 0;   v[2].t = 1.0f;
            memset(&ctx, 0, sizeof ctx);
            ctx.dwgctl = DWG_OPCOD_TEXTURE_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
            ctx.flags = MGA_S_TEX;
            ctx.tex_tw = ctx.tex_th = 3;
            ctx.clip_y0 = 0; ctx.clip_y1 = 64;
            setup_triangle(&v[0], &v[1], &v[2], &ctx);
        }
        if (idle("texture_trap"))
            return 1;
        return check("texture_trap", 0xF800, 0x001F, 15, 15) ? 0 : 1;
    }
    if (!strcmp(stage, "exp"))
        return experiments();
    printf("unknown stage %s\n", stage);
    return 2;
}
