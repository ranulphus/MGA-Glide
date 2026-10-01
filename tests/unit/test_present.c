/* engine_present (hal/src/present.c) on the host model: the registers it
 * leaves for the texture, and the engine state it restores. The scaled
 * pixels themselves are checked in 86Box by tests/hal/scale.c (the host
 * model has no texture unit). */
#include "unit.h"
#include "refrast.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
int mga_fifo_free;                  /* stays 0: every reservation reaches the stub */
void fifo_reserve(int n) { (void)n; }
void fifo_reset(void) {}
uint32_t sys_time_us(void) { static uint32_t t; return t += 10; }
void sys_delay_us(uint32_t us) { (void)us; }

#define REGV(r) (rr->reg[(r) >> 2])

static void chip(int g200)
{
    memset(&mga, 0, sizeof mga);
    mga.has_dstorg = g200;
    mga.has_ydstorg = !g200;
    mga.has_alpha_blend = g200;
    mga.has_texctl2 = g200;
    mga.fifo_depth = 64;
    mga.fifo_mask = 0x7F;
}

static void run(int g200, int filter)
{
    mga_target t;
    mga_surface src = { 0x400000, 320, 240, 512 }, dst = { 0x200000, 640, 480, 1024 };
    uint32_t pitch, org, maccess, cx, ytop, ybot;
    mga_setup_stats before;
    chip(g200);
    refrast_init();
    engine_init(1024, 16);
    memset(&t, 0, sizeof t);
    t.color_off = 0x100000; t.z_off = 0x300000; t.pitch_px = 512; t.bpp = 16; t.zbits = 16;
    engine_set_target(&t);
    engine_set_maccess_flags(MACCESS_FOGEN);
    engine_set_clip(0, 0, 320, 240);
    pitch = REGV(MGAREG_PITCH);
    org = g200 ? REGV(MGAREG_DSTORG) : REGV(MGAREG_YDSTORG);
    maccess = REGV(MGAREG_MACCESS);
    cx = REGV(MGAREG_CXBNDRY); ytop = REGV(MGAREG_YTOP); ybot = REGV(MGAREG_YBOT);
    CHECK_EQ(pitch, 512);
    before = setup_stats;
    rr->unsupported = 0;

    engine_present(&src, &dst, 0, 0, 640, 480, filter);

    /* The texture: the render surface, TW16, clamped, the chosen filter. */
    CHECK_EQ(REGV(MGAREG_TEXORG), 0x400000);
    CHECK_EQ(REGV(MGAREG_TEXCTL) & 0xF, TEXCTL_TW16);
    CHECK((REGV(MGAREG_TEXCTL) & (TEXCTL_CLAMPU | TEXCTL_CLAMPV)) == (TEXCTL_CLAMPU | TEXCTL_CLAMPV));
    CHECK((REGV(MGAREG_TEXCTL) & TEXCTL_TMODULATE) == 0);
    CHECK_EQ(REGV(MGAREG_TEXFILTER) & 0xFF,
             filter == MGA_PRESENT_BILINEAR ? (TEXFILTER_BILIN | TEXFILTER_BILIN << 4) : 0);
    CHECK_EQ(REGV(MGAREG_TEXWIDTH) & 0x1FF, 9);      /* log2 of the 512-texel pitch */
    CHECK_EQ(REGV(MGAREG_TEXHEIGHT) & 0x1FF, 8);     /* 240 rows: a 256-row texture */
    CHECK_EQ(REGV(MGAREG_ALPHACTRL) & 0xFF, g200 ? (BLEND_ONE | BLEND_ZERO << 4) : 0x54);
    CHECK_EQ(REGV(MGAREG_PLNWT), 0xFFFFFFFFu);
    CHECK_EQ(REGV(MGAREG_DWGCTL) & 0xF, DWG_OPCOD_TEXTURE_TRAP);
    CHECK(rr->unsupported >= 2);                    /* textured trapezoids reached the engine */
    /* Restored: the caller's pitch, origin, MACCESS (with its flags) and clip. */
    CHECK_EQ(REGV(MGAREG_PITCH), pitch);
    CHECK_EQ(g200 ? REGV(MGAREG_DSTORG) : REGV(MGAREG_YDSTORG), org);
    CHECK_EQ(REGV(MGAREG_MACCESS), maccess);
    CHECK((REGV(MGAREG_MACCESS) & MACCESS_FOGEN) != 0);
    CHECK_EQ(REGV(MGAREG_CXBNDRY), cx);
    CHECK_EQ(REGV(MGAREG_YTOP), ytop);
    CHECK_EQ(REGV(MGAREG_YBOT), ybot);
    CHECK_EQ(engine_target->color_off, 0x100000);
    CHECK_EQ(engine_target->pitch_px, 512);
    CHECK(memcmp(&before, &setup_stats, sizeof before) == 0);
}

int unit_main(void)
{
    mga_surface ok = { 0x400000, 320, 240, 512 };
    run(1, MGA_PRESENT_NEAREST);
    run(1, MGA_PRESENT_BILINEAR);
    run(0, MGA_PRESENT_NEAREST);                    /* G100: YDSTORG, fixed ALPHACTRL */
    CHECK(mga_present_ok(&ok));
    ok.pitch_px = 400; CHECK(!mga_present_ok(&ok));  /* not a power of two */
    ok.pitch_px = 4096; CHECK(!mga_present_ok(&ok)); /* beyond the texture unit */
    ok.pitch_px = 512; ok.off = 0x400020; CHECK(!mga_present_ok(&ok));
    ok.off = 0x400000; ok.w = 600; CHECK(!mga_present_ok(&ok));
    return 0;
}
