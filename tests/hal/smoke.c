/* smoke.c - HAL smoke test for any toolchain (DJGPP for DOS-GL, Open
 * Watcom for MGA-Glide): find and map the card, set 640x480x16, fill the
 * screen and one Gouraud triangle through the setup code, read two pixels
 * back, report over COM1 with HX- lines, restore text mode. Uses nothing
 * but the HAL, so it is the first program to build when the HAL moves to
 * its own repository. */
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/serial.h"
#include "mga/setup.h"
#include <stdio.h>
#include <string.h>

static void say(const char *fmt, unsigned a, unsigned b)
{
    char line[128];
    snprintf(line, sizeof line, fmt, a, b);
    serial_puts(line);
    serial_puts("\r\n");
}

int main(void)
{
    mga_vbe_mode m;
    mga_target t;
    mga_svtx v[3];
    mga_tri_ctx ctx;
    int pitch = 0, i, ok = 1;
    volatile uint16_t *fb;
    serial_init(SERIAL_COM1, 115200);
    serial_puts("HX-START smoke\r\n");
    if (mga_find(&mga) != 0 || mga_map(&mga) != 0) {
        serial_puts("HX-TEST find FAIL no Matrox card\r\nHX-DONE 2\r\n");
        return 2;
    }
    say("HX-TEST find PASS device=%04x family=%u", mga.device_id, (unsigned)mga.family);
    if (vbe_find_mode(640, 480, 16, &m) != 0 || vbe_set_mode(&m, 1024, &pitch) != 0) {
        serial_puts("HX-TEST mode FAIL\r\nHX-DONE 2\r\n");
        return 2;
    }
    engine_init(pitch, 16);
    memset(&t, 0, sizeof t);
    t.pitch_px = pitch; t.bpp = 16; t.zbits = 16;
    engine_set_target(&t);
    engine_set_clip(0, 0, 640, 480);
    engine_fill(0, 0, 640, 480, 0x001F);                     /* blue */
    memset(v, 0, sizeof v);
    for (i = 0; i < 3; i++) { v[i].r = 255; v[i].g = 255; v[i].b = 0; v[i].a = 255; v[i].fog = 255; }
    v[0].X16 = 100 * 16; v[0].Y16 = 100 * 16;
    v[1].X16 = 300 * 16; v[1].Y16 = 120 * 16;
    v[2].X16 = 150 * 16; v[2].Y16 = 300 * 16;
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.clip_y0 = 0; ctx.clip_y1 = 480;
    setup_triangle(&v[0], &v[1], &v[2], &ctx);
    ok = engine_sync(200000) == 0;
    fb = (volatile uint16_t *)mga_fb;
    say("HX-STAT pixels outside=%04x inside=%04x", fb[10 * pitch + 10], fb[150 * pitch + 170]);
    ok = ok && fb[10 * pitch + 10] == 0x001F && fb[150 * pitch + 170] == 0xFFE0;
    vbe_set_text_mode();
    say(ok ? "HX-TEST draw PASS resets=%u%.0u" : "HX-TEST draw FAIL resets=%u%.0u", engine_resets, 0);
    serial_puts(ok ? "HX-DONE 0\r\n" : "HX-DONE 1\r\n");
    return ok ? 0 : 1;
}
