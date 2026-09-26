/* probe.c - HAL bring-up: identify the card, map it, list VBE modes, set
 * 640x480x16, and exercise engine fills (colour and depth). */
#include "hx.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include <stdio.h>
#include <string.h>

uint32_t mga_probe_vram(void);

static void mode_cb(const mga_vbe_mode *m, void *ctx)
{
    MGA_UNUSED(ctx);
    hx_log("HX-STAT vbe mode=%03x %dx%dx%d pitch=%d lfb=%08lx rgb=%d%d%d",
           m->mode, m->width, m->height, m->bpp, m->pitch_bytes, (unsigned long)m->lfb_phys,
           m->red_size, m->green_size, m->blue_size);
}

int main(int argc, char **argv)
{
    mga_vbe_mode mode;
    mga_target t;
    int pitch = 0, ok, x, y, bad = 0;
    volatile uint16_t *fb16;

    hx_init(argc, argv, "probe");
    ok = mga_find(&mga) == 0;
    hx_test("pci", ok, "%s id=%04x rev=%02x at %d:%d.%d subsys=%08lx option=%08lx",
            ok ? mga.name : "none", mga.device_id, mga.revision, mga.bus, mga.dev, mga.fn,
            (unsigned long)mga.subsys, (unsigned long)mga.option);
    if (!ok)
        hx_done(HX_INIT_FAILED);
    hx_log("HX-STAT bars fb=%08lx mmio=%08lx iload=%08lx family=%s emulated=%d",
           (unsigned long)mga.fb_phys, (unsigned long)mga.mmio_phys, (unsigned long)mga.iload_phys,
           mga_family_name(mga.family), mga_detect_emulator());
    ok = mga_map(&mga) == 0;
    hx_test("map", ok, "mmio=%p fb=%p", (void *)mga_mmio, (void *)mga_fb);
    if (!ok)
        hx_done(HX_INIT_FAILED);
    hx_log("HX-STAT status=%08lx fifostatus=%08lx", (unsigned long)MGA_RD32(MGAREG_STATUS),
           (unsigned long)MGA_RD32(MGAREG_FIFOSTATUS));
    hx_log("HX-STAT vbe version=%04x", vbe_version());
    vbe_enumerate(mode_cb, NULL);
    ok = vbe_find_mode(640, 480, 16, &mode) == 0;
    hx_test("vbe-find", ok, "mode=%03x", mode.mode);
    if (!ok)
        hx_done(HX_INIT_FAILED);
    ok = vbe_set_mode(&mode, 1024, &pitch) == 0;
    hx_test("vbe-set", ok, "pitch=%d px lfb=%08lx", pitch, (unsigned long)mode.lfb_phys);
    mga.vram_bytes = mga_probe_vram();
    hx_log("HX-STAT vram=%lu", (unsigned long)mga.vram_bytes);

    engine_init(pitch, 16);
    memset(&t, 0, sizeof t);
    t.color_off = 0; t.z_off = (uint32_t)pitch * 2 * 480 * 2; t.pitch_px = pitch; t.bpp = 16; t.zbits = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, 640, 480);
    engine_fill(0, 0, 640, 480, 0x0000);
    engine_fill(40, 40, 200, 100, 0xF800);        /* red */
    engine_fill(260, 40, 200, 100, 0x07E0);       /* green */
    engine_fill(480, 40, 120, 100, 0x001F);       /* blue */
    engine_set_clip(100, 200, 540, 420);
    engine_fill(0, 180, 640, 300, 0xFFE0);        /* yellow, clipped */
    engine_set_clip(0, 0, 640, 480);
    engine_fill_depth(0, 0, 640, 480, 0x1234);
    ok = engine_sync(500000) == 0;
    hx_test("sync", ok, "");

    fb16 = (volatile uint16_t *)mga_fb;
    bad += fb16[60 * pitch + 100] != 0xF800;
    bad += fb16[60 * pitch + 300] != 0x07E0;
    bad += fb16[60 * pitch + 500] != 0x001F;
    bad += fb16[300 * pitch + 50] != 0x0000;      /* outside clip */
    bad += fb16[300 * pitch + 300] != 0xFFE0;     /* inside clip */
    hx_test("fill", bad == 0, "c1=%04x c2=%04x c3=%04x out=%04x in=%04x",
            fb16[60 * pitch + 100], fb16[60 * pitch + 300], fb16[60 * pitch + 500],
            fb16[300 * pitch + 50], fb16[300 * pitch + 300]);
    for (bad = 0, y = 0; y < 480; y += 37)
        for (x = 0; x < 640; x += 41)
            bad += ((volatile uint16_t *)(mga_fb + t.z_off))[y * pitch + x] != 0x1234;
    hx_test("depth-fill", bad == 0, "mismatches=%d z(0,0)=%04x", bad,
            ((volatile uint16_t *)(mga_fb + t.z_off))[0]);
    hx_snap_screen("probe");
    vbe_set_text_mode();
    hx_done(0);
    return 0;
}
