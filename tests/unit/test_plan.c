/* The mode planner (hal/src/vbe.c) against the mode lists the genuine
 * Matrox BIOSes report (docs/emulated-g200.md, docs/emulated-g400.md):
 * every Glide resolution plus the sizes DOS-GL offers. */
#include "unit.h"
#include "mga/hal.h"
#include "mga/sys.h"
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
int sys_rm_int(uint8_t intno, sys_rmregs *r) { (void)intno; (void)r; return -1; }
void *sys_dos_alloc(uint32_t bytes, uint16_t *seg) { (void)bytes; (void)seg; return NULL; }
void *sys_real_ptr(uint16_t seg, uint16_t off) { (void)seg; (void)off; return NULL; }

static int nmodes;
static mga_vbe_mode modes[16];

static void add(int w, int h, int bpp, int g)
{
    mga_vbe_mode *m = &modes[nmodes++];
    memset(m, 0, sizeof *m);
    m->mode = (uint16_t)(0x110 + nmodes);
    m->width = (uint16_t)w; m->height = (uint16_t)h; m->bpp = (uint16_t)bpp;
    m->red_size = 5; m->green_size = (uint8_t)g; m->blue_size = 5;
    m->pitch_bytes = (uint16_t)(w * bpp / 8);
}

/* G200 900-33, G400 897-21, G450 935-20: 555 and 565 at 640x480..1280x1024,
 * and 32 bpp; the G100's BIOS also has 1600x1200. */
static void genuine(int with_1600)
{
    static const int sz[][2] = { { 640, 480 }, { 800, 600 }, { 1024, 768 }, { 1280, 1024 }, { 1600, 1200 } };
    int i, n = with_1600 ? 5 : 4;
    nmodes = 0;
    for (i = 0; i < n; i++) {
        add(sz[i][0], sz[i][1], 16, 5);
        add(sz[i][0], sz[i][1], 16, 6);
        if (i < 3)
            add(sz[i][0], sz[i][1], 32, 8);
    }
}

static mga_mode_plan plan(int w, int h, unsigned flags)
{
    mga_mode_plan p;
    memset(&p, 0, sizeof p);
    p.fit = -1;
    if (mga_plan_from_list(modes, nmodes, w, h, 16, flags, &p) != 0)
        p.fit = -1;
    return p;
}

static void expect(int w, int h, unsigned flags, int fit, int dispw, int disph, int dx, int dy, int dw, int dh)
{
    mga_mode_plan p = plan(w, h, flags);
    CHECK_EQ(p.fit, fit);
    if (fit < 0 || p.fit != fit)
        return;
    CHECK_EQ(p.disp.width, dispw);
    CHECK_EQ(p.disp.height, disph);
    CHECK_EQ(p.disp.green_size, 6);                 /* 16 bpp is RGB565 */
    CHECK_EQ(p.dx, dx); CHECK_EQ(p.dy, dy); CHECK_EQ(p.dw, dw); CHECK_EQ(p.dh, dh);
    CHECK_EQ(p.w, w); CHECK_EQ(p.h, h);
    CHECK_EQ(p.zoom, fit == MGA_FIT_ZOOM ? 2 : 1);
}

int unit_main(void)
{
    genuine(0);
    /* Native. */
    expect(640, 480, 0, MGA_FIT_NATIVE, 640, 480, 0, 0, 640, 480);
    expect(800, 600, 0, MGA_FIT_NATIVE, 800, 600, 0, 0, 800, 600);
    expect(1024, 768, 0, MGA_FIT_NATIVE, 1024, 768, 0, 0, 1024, 768);
    expect(1280, 1024, 0, MGA_FIT_NATIVE, 1280, 1024, 0, 0, 1280, 1024);
    /* The half modes: scaled 2x, or zoomed when asked. */
    expect(320, 240, 0, MGA_FIT_INTEGER, 640, 480, 0, 0, 640, 480);
    expect(400, 300, 0, MGA_FIT_INTEGER, 800, 600, 0, 0, 800, 600);
    expect(512, 384, 0, MGA_FIT_INTEGER, 1024, 768, 0, 0, 1024, 768);
    expect(640, 512, 0, MGA_FIT_INTEGER, 1280, 1024, 0, 0, 1280, 1024);
    expect(320, 240, MGA_PLAN_ZOOM, MGA_FIT_ZOOM, 640, 480, 0, 0, 640, 480);
    expect(400, 300, MGA_PLAN_ZOOM, MGA_FIT_ZOOM, 800, 600, 0, 0, 800, 600);
    expect(512, 384, MGA_PLAN_ZOOM, MGA_FIT_ZOOM, 1024, 768, 0, 0, 1024, 768);
    expect(640, 512, MGA_PLAN_ZOOM, MGA_FIT_ZOOM, 1280, 1024, 0, 0, 1280, 1024);
    expect(640, 480, MGA_PLAN_ZOOM, MGA_FIT_NATIVE, 640, 480, 0, 0, 640, 480);   /* native first */
    /* CRT-era sizes fill 640x480 (4:3), also with zoom asked (no 640x400 mode). */
    expect(320, 200, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(320, 200, MGA_PLAN_ZOOM, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(640, 400, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(640, 350, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(640, 200, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(400, 256, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    expect(512, 256, 0, MGA_FIT_FILL, 640, 480, 0, 0, 640, 480);
    /* Anything else: evenly scaled and centred. */
    expect(960, 720, 0, MGA_FIT_ASPECT, 1024, 768, 0, 0, 1024, 768);
    expect(856, 480, 0, MGA_FIT_ASPECT, 1024, 768, 0, 96, 1024, 574);
    /* 1600x1200 only where the BIOS lists it. */
    expect(1600, 1200, 0, -1, 0, 0, 0, 0, 0, 0);
    /* Test switch: the scaled path at 1x; the old top-left placement. */
    expect(640, 480, MGA_PLAN_FORCE, MGA_FIT_INTEGER, 640, 480, 0, 0, 640, 480);
    expect(512, 384, MGA_PLAN_TOPLEFT, MGA_FIT_TOPLEFT, 640, 480, 0, 0, 512, 384);
    expect(320, 240, MGA_PLAN_TOPLEFT | MGA_PLAN_ZOOM, MGA_FIT_ZOOM, 640, 480, 0, 0, 640, 480);
    CHECK_EQ(plan(0, 480, 0).fit, -1);

    genuine(1);
    expect(1600, 1200, 0, MGA_FIT_NATIVE, 1600, 1200, 0, 0, 1600, 1200);
    expect(800, 600, MGA_PLAN_ZOOM, MGA_FIT_NATIVE, 800, 600, 0, 0, 800, 600);

    /* 32 bpp takes the 8-bit-per-channel modes only. */
    {
        mga_mode_plan p;
        CHECK_EQ(mga_plan_from_list(modes, nmodes, 800, 600, 32, 0, &p), 0);
        CHECK_EQ(p.fit, MGA_FIT_NATIVE);
        CHECK_EQ(p.disp.bpp, 32);
        CHECK_EQ(mga_plan_from_list(modes, nmodes, 1280, 1024, 32, 0, &p), -1);
    }

    CHECK_EQ(mga_pow2_pitch(320), 512);
    CHECK_EQ(mga_pow2_pitch(400), 512);
    CHECK_EQ(mga_pow2_pitch(512), 512);
    CHECK_EQ(mga_pow2_pitch(640), 1024);
    CHECK_EQ(mga_pow2_pitch(856), 1024);
    CHECK_EQ(mga_pow2_pitch(8), 32);
    CHECK_STR(mga_fit_name(MGA_FIT_ZOOM), "zoom");
    CHECK_STR(mga_fit_name(MGA_FIT_ASPECT), "aspect");
    return 0;
}
