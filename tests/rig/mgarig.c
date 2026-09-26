/* mgarig - Loop C stages on the cuda6 G200eR2 (PRD §4.4, plan §7).
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

#define FB_PHYS   0xD2000000u
#define MMIO_PHYS 0xDCFFC000u
#define WORK      (4u << 20)        /* off-screen work area: safe even with 8 MB of VRAM */

static int map(void)
{
    mga.family = MGA_FAMILY_G200E;
    mga.device_id = 0x0534;
    mga.fb_phys = FB_PHYS;
    mga.fb_size = 16u << 20;
    mga.mmio_phys = MMIO_PHYS;
    mga_chip_caps(&mga);
    mga_mmio = (volatile uint8_t *)sys_map_phys(MMIO_PHYS, 0x4000);
    if (!mga_mmio) {
        printf("map mmio FAILED (root? RIG_BDF?)\n");
        return -1;
    }
    if (!getenv("RIG_NOFB")) {
        mga_fb = (volatile uint8_t *)sys_map_phys(FB_PHYS, mga.fb_size);
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
        { 0x1E10, "FIFOSTATUS" }, { 0x1E14, "STATUS" }, { 0x1C04, "MACCESS" }, { 0x1C1C, "ZORG" },
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
    printf("unknown stage %s\n", stage);
    return 2;
}
