/* edgecmp - compare mgarig edgecal's frames from a chip with refrast.
 *
 *   edgecmp FAMILY DIR      (FAMILY: g100, g200, g200e, g400)
 *
 * For each edgecal.h triangle, DIR/edgecal-NN-off.raw (drawn with the AR
 * limit off: the values before the fix) is compared with refrast at the
 * family's AR width (does the model predict the chip?), and
 * DIR/edgecal-NN-fix.raw (the HAL as it is) with refrast at 32 bits given
 * the same values (does the chip draw what the fixed values say? They fit,
 * so the width no longer matters; how the fixed Voodoo edges differ from the
 * Voodoo's is tests/unit/test_edges.c's). Exit 0 when every frame matches
 * pixel for pixel. --write-model writes the model's frames into DIR instead
 * (a check of this tool). Host build: the same setup code and arithmetic as
 * mgarig's (x86-64). */
#include "edgecal.h"
#include "refrast.h"
#include "mga/hal.h"
#include "mga/mmio.h"
#include "mga/regs_mga.h"
#include "mga/setup.h"
#include "mga/sys.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

mga_chip mga;
volatile uint8_t *mga_mmio, *mga_fb;
int mga_fifo_free;
void fifo_reserve(int n) { (void)n; }
void fifo_reset(void) {}
uint32_t sys_time_us(void) { static uint32_t t; return t += 10; }
void sys_delay_us(uint32_t us) { (void)us; }

static uint16_t chip[EDGECAL_W * EDGECAL_H], model[EDGECAL_W * EDGECAL_H];

static void render(const edgecal_tri *t, int rr_bits, int hal_bits)
{
    mga_target tg;
    mga_svtx v[3];
    mga_tri_ctx ctx;
    int i, y;
    refrast_init();
    refrast_ar_bits = rr_bits;
    mga.ar_bits = (uint8_t)hal_bits;
    engine_init(EDGECAL_W, 16);
    memset(&tg, 0, sizeof tg);
    tg.pitch_px = EDGECAL_W;
    tg.bpp = 16;
    tg.zbits = 16;
    engine_set_target(&tg);
    engine_set_clip(0, 0, EDGECAL_W, EDGECAL_H);
    engine_fill(0, 0, EDGECAL_W, EDGECAL_H, 0x0000);
    memset(v, 0, sizeof v);
    for (i = 0; i < 3; i++) {
        v[i].r = 255; v[i].g = 255; v[i].a = 255; v[i].fog = 255;
        v[i].X16 = t->X16[i]; v[i].Y16 = t->Y16[i];
    }
    memset(&ctx, 0, sizeof ctx);
    ctx.dwgctl = DWG_OPCOD_TRAP | DWG_ATYPE_I | DWG_ZMODE_NOZCMP | DWG_BOP_COPY;
    ctx.flags = t->flags;
    ctx.clip_y0 = 0;
    ctx.clip_y1 = EDGECAL_H;
    setup_invalidate();
    setup_triangle(&v[0], &v[1], &v[2], &ctx);
    for (y = 0; y < EDGECAL_H; y++)
        memcpy(&model[y * EDGECAL_W], rr->vram + (size_t)y * EDGECAL_W * 2, EDGECAL_W * 2);
}

static int write_model;

static long compare(const char *dir, int k, const char *which)
{
    char path[512];
    FILE *f;
    long n = 0;
    int i;
    snprintf(path, sizeof path, "%s/edgecal-%02d-%s.raw", dir, k, which);
    if (write_model) {
        f = fopen(path, "wb");
        if (!f || fwrite(model, sizeof model, 1, f) != 1)
            exit(2);
        fclose(f);
        return 0;
    }
    f = fopen(path, "rb");
    if (!f || fread(chip, sizeof chip, 1, f) != 1) {
        fprintf(stderr, "edgecmp: cannot read %s\n", path);
        exit(2);
    }
    fclose(f);
    for (i = 0; i < EDGECAL_W * EDGECAL_H; i++)
        n += chip[i] != model[i];
    return n;
}

int main(int argc, char **argv)
{
    int k, bad = 0, bits;
    if (argc != 3 && !(argc == 4 && !strcmp(argv[3], "--write-model"))) {
        fprintf(stderr, "usage: edgecmp g100|g200|g200e|g400 DIR [--write-model]\n");
        return 2;
    }
    write_model = argc == 4;
    memset(&mga, 0, sizeof mga);
    mga.family = !strcmp(argv[1], "g400") ? MGA_FAMILY_G400 : !strcmp(argv[1], "g100") ? MGA_FAMILY_G100 :
                 !strcmp(argv[1], "g200e") ? MGA_FAMILY_G200E : MGA_FAMILY_G200;
    mga_chip_caps(&mga);
    bits = mga.ar_bits;
    printf("edgecmp: %s, AR fields %d bits\n", argv[1], bits);
    for (k = 0; k < EDGECAL_N; k++) {
        long off, fix;
        render(&edgecal_tris[k], bits, 0);          /* the old values on the modelled chip */
        off = compare(argv[2], k, "off");
        render(&edgecal_tris[k], 32, bits);         /* the values the HAL writes now */
        fix = compare(argv[2], k, "fix");
        printf("edgecmp %-16s limit off vs %d-bit model: %7ld pixels differ; fixed vs its values at 32 bits: %7ld\n",
               edgecal_tris[k].name, bits, off, fix);
        bad += off != 0 || fix != 0;
    }
    printf("edgecmp: %d of %d triangles differ\n", bad, EDGECAL_N);
    return bad ? 1 : 0;
}
